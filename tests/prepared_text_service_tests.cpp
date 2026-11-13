#include "prepared_text_test_support.hpp"
#include "../src/core/text/prepared/prepared_storage.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>
#include <iostream>
#include <limits>

namespace {
using namespace prepared_test;

void test_identity_exhaustion() {
    constexpr std::uint64_t terminal = std::numeric_limits<std::uint64_t>::max();
    std::atomic<std::uint64_t> local{terminal - 1U};
    std::uint64_t output = 42U;
    PreparedTextStatus status = detail::acquire_prepared_identity(local, output);
    require(status == PreparedTextStatus::success && output == terminal - 1U,
        "last permitted identity is issued exactly once");
    output = 42U;
    status = detail::acquire_prepared_identity(local, output);
    require(status == PreparedTextStatus::generation_exhausted && output == 42U && local.load() == terminal,
        "exhausted identity source neither wraps nor changes output");
    std::atomic<std::uint64_t> zero{0U};
    status = detail::acquire_prepared_identity(zero, output);
    require(status == PreparedTextStatus::generation_exhausted && output == 42U,
        "zero cannot be issued as an identity");
}

void test_session_claim(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    EncodedFontLease bank = make_bank(service, bytes);
    const std::shared_ptr<const detail::PreparedFontBank>& fonts = detail::PreparedTextAccess::fonts(bank);
    const std::shared_ptr<detail::PreparedLedger> ledger = (*fonts).ledger;
    detail::PreparedSessionClaim claim{};
    PreparedTextStatus status = claim.acquire({});
    require(status == PreparedTextStatus::invalid_input, "null claim refuses");
    {
        detail::PreparedSessionClaim setup{};
        status = setup.acquire(ledger);
        require(status == PreparedTextStatus::success, "private setup claims ledger");
        status = setup.acquire(ledger);
        require(status == PreparedTextStatus::busy, "repeated claim preserves ownership");
        status = claim.acquire(ledger);
        require(status == PreparedTextStatus::busy, "another owner cannot share workspace session");
        std::unique_ptr<PreparedTextSession> refused{};
        status = service.open_session(bank, nullptr, refused);
        require(status == PreparedTextStatus::busy && !refused, "private claim blocks A2 without publishing session");
        // No worker was created in this setup scope; automatic cleanup releases
        // its claim just as an unsuccessful start must do.
    }
    std::unique_ptr<PreparedTextSession> session{};
    status = service.open_session(bank, nullptr, session);
    require(status == PreparedTextStatus::success, "A2 opens after abandoned setup retires");
    status = claim.acquire(ledger);
    require(status == PreparedTextStatus::busy, "live A2 blocks private workspace");
    const PreparedTextKey key = make_key(service, bank, "retained");
    PreparedTextLayout retained{};
    prepare(service, *session, key, "retained", retained);
    (*session).begin_close();
    status = claim.acquire(ledger);
    require(status == PreparedTextStatus::busy, "close alone cannot release workspace ownership");
    (*session).join_and_release();
    status = claim.acquire(ledger);
    require(status == PreparedTextStatus::success, "confirmed join releases workspace ownership");
    const PreparedTextBudgetSnapshot usage = service.budget_snapshot();
    require(usage.payload_generations == 1U && !retained.empty(), "workspace retirement does not release retained geometry");
    std::unique_ptr<PreparedTextSession> replacement{};
    status = service.open_session(bank, nullptr, replacement);
    require(status == PreparedTextStatus::busy && !replacement, "joined A2 weak registry cannot bypass private claim");
    claim.release();
    claim.release();
    status = service.open_session(bank, nullptr, replacement);
    require(status == PreparedTextStatus::success, "idempotent private release permits next A2");
    service.begin_close();
    status = claim.acquire(ledger);
    require(status == PreparedTextStatus::closing, "closed ledger cannot acquire workspace claim");
}

void test_generations(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    EncodedFontLease bank = make_bank(service, bytes);
    Wake wake{};
    std::unique_ptr<PreparedTextSession> session{};
    PreparedTextStatus status = service.open_session(bank, &wake, session);
    require(status == PreparedTextStatus::success, "session opens");
    const PreparedTextKey key = make_key(service, bank, "hello");
    std::array<PreparedTextLayout, 3> layouts{};
    for (std::size_t index = 0; index < layouts.size(); ++index) prepare(service, *session, key, "hello", layouts[index]);
    PreparedTextBudgetSnapshot budget = service.budget_snapshot();
    require(budget.payload_generations == 3 && budget.payload_reserved_bytes == 24U * 1024U * 1024U,
        "all old generations remain charged");
    // A retained command holds the typed immutable payload after its wrapper dies.
    std::shared_ptr<const detail::PreparedTextStorage> retained = detail::PreparedTextAccess::layout(layouts[0]);
    layouts[0] = PreparedTextLayout{};
    LayoutAuthority authority{};
    status = (*session).desire(key, authority);
    require(status == PreparedTextStatus::success, "fourth desire");
    PrepareInput input{};
    status = make_input(service, key, "hello", input);
    require(status == PreparedTextStatus::success, "fourth input admitted separately");
    status = (*session).submit(authority, input);
    require(status == PreparedTextStatus::busy && !input.empty(), "fourth payload denied and input preserved");
    retained.reset();
    status = (*session).submit(authority, input);
    require(status == PreparedTextStatus::success && input.empty(), "actual retirement permits admission");
    const PreparedTextSessionSnapshot ready = wait_ready(*session);
    require(ready.completion == PreparedTextStatus::success, "fourth completes");
    status = (*session).adopt_ready(authority, layouts[0]);
    require(status == PreparedTextStatus::success, "fourth adopts");
    (*session).join_and_release();
    std::unique_ptr<PreparedTextSession> reopened{};
    status = service.open_session(bank, nullptr, reopened);
    require(status == PreparedTextStatus::success, "new session after join");
    status = (*reopened).desire(key, authority);
    require(status == PreparedTextStatus::success, "new session desire");
    status = make_input(service, key, "hello", input);
    require(status == PreparedTextStatus::success, "new session input");
    status = (*reopened).submit(authority, input);
    require(status == PreparedTextStatus::busy && !input.empty(), "session reopening cannot evade payload ledger");
    require(wake.count.load(std::memory_order_acquire) == 4, "one wake per observed completion");
}

void test_authority(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    EncodedFontLease bank = make_bank(service, bytes);
    Wake wake{};
    std::unique_ptr<PreparedTextSession> session{};
    PreparedTextStatus status = service.open_session(bank, &wake, session);
    require(status == PreparedTextStatus::success, "session opens");
    const PreparedTextKey key = make_key(service, bank, "hello");
    PreparedTextLayout old{};
    prepare(service, *session, key, "hello", old);
    const LayoutAuthority old_authority = old.authority();
    LayoutAuthority next{};
    status = (*session).desire(key, next);
    require(status == PreparedTextStatus::success && next.epoch != old_authority.epoch, "same key advances epoch");
    PrepareInput input{};
    status = make_input(service, key, "hello", input);
    require(status == PreparedTextStatus::success, "input created");
    status = (*session).submit(old_authority, input);
    require(status == PreparedTextStatus::stale && !input.empty(), "old authority cannot submit");
    PreparedTextKey invalid = key;
    invalid.scale = 0;
    LayoutAuthority unchanged = next;
    status = (*session).desire(invalid, unchanged);
    require(status == PreparedTextStatus::unsupported_profile && same_layout_authority(unchanged, next), "invalid desire preserves authority");
    status = (*session).submit(next, input);
    require(status == PreparedTextStatus::success && input.empty(), "valid authority preserved internally");
    const PreparedTextSessionSnapshot ready = wait_ready(*session);
    require(ready.completion == PreparedTextStatus::success, "completion");
    (*session).cancel();
    status = (*session).adopt_ready(next, old);
    require(status == PreparedTextStatus::stale && same_layout_authority(old.authority(), old_authority), "cancelled adoption preserves output");
    status = (*session).desire(key, unchanged);
    require(status == PreparedTextStatus::success, "same key after cancel");
    status = (*session).adopt_ready(unchanged, old);
    require(status == PreparedTextStatus::stale, "same key cannot revive cancelled result");
    PreparedTextBudgetSnapshot budget = service.budget_snapshot();
    require(budget.payload_generations == 2, "cancel is not retirement");
    status = (*session).discard_ready();
    require(status == PreparedTextStatus::success, "discard ready");
    budget = service.budget_snapshot();
    require(budget.payload_generations == 1, "discard retires completed owner");
    status = make_input(service, key, "hello", input);
    require(status == PreparedTextStatus::success, "close input");
    status = (*session).submit(unchanged, input);
    require(status == PreparedTextStatus::success, "close job admitted");
    (*session).begin_close();
    const unsigned revoked_wakes = wake.count.load(std::memory_order_acquire);
    (*session).join_and_release();
    require(wake.count.load(std::memory_order_acquire) == revoked_wakes, "close revokes subsequent posts");
    const PreparedTextSessionSnapshot closed = (*session).inspect_ready();
    require(closed.closing && closed.joined && closed.slot == PreparedTextSlot::empty, "joined slot cleared");
    budget = service.budget_snapshot();
    require(budget.payload_generations == 1, "join retains only caller old layout");
}

void test_validation_and_fonts(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    EncodedFontLease first = make_bank(service, bytes);
    EncodedFontLease shared = first;
    EncodedFontLease second = make_bank(service, bytes);
    const std::array<PreparedFontSource, 1> sources{{{.encoded = bytes, .role = FontRole::content}}};
    EncodedFontLease denied{};
    PreparedTextStatus status = service.create_font_bank(sources, denied);
    require(status == PreparedTextStatus::busy && denied.empty(), "third font bank denied");
    first = EncodedFontLease{};
    status = service.create_font_bank(sources, denied);
    require(status == PreparedTextStatus::busy, "shared font lease extends charge");
    shared = EncodedFontLease{};
    status = service.create_font_bank(sources, denied);
    require(status == PreparedTextStatus::success, "font lease retirement permits bank");
    const PreparedTextKey key = make_key(service, second, "hello");
    PrepareInput input{};
    status = make_input(service, key, "hello", input, {false, true});
    require(status == PreparedTextStatus::context_required && input.empty(), "requires paragraph proof");
    status = make_input(service, key, "hello", input);
    require(status == PreparedTextStatus::success, "valid input");
    const PreparedTextKey tab = make_key(service, second, "a\tb");
    status = make_input(service, tab, "a\tb", input);
    require(status == PreparedTextStatus::unsupported_profile && !input.empty(), "unsupported checked before busy preserves old input");
    service.begin_close();
    PrepareInput closed{};
    status = make_input(service, key, "hello", closed);
    require(status == PreparedTextStatus::closing && closed.empty(), "closed service refuses input");
    const PreparedTextBudgetSnapshot budget = service.budget_snapshot();
    require(budget.input_owners == 1 && budget.font_banks == 2, "close does not uncharge external owners");
}

void test_failed_slot(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    const std::array<std::byte, 8> invalid_font{};
    const std::array<PreparedFontSource, 1> sources{{{.encoded = invalid_font, .role = FontRole::content}}};
    EncodedFontLease bank{};
    PreparedTextStatus status = service.create_font_bank(sources, bank);
    require(status == PreparedTextStatus::incompatible_font && bank.empty(), "invalid native font refused before session");
    bank = make_bank(service, bytes);
    std::unique_ptr<PreparedTextSession> session{};
    status = service.open_session(bank, nullptr, session);
    require(status == PreparedTextStatus::success, "valid font session");
    const std::string_view missing = "\xf4\x8f\xbf\xbf";
    const PreparedTextKey key = make_key(service, bank, missing);
    LayoutAuthority authority{};
    status = (*session).desire(key, authority);
    require(status == PreparedTextStatus::success, "failed slot desire");
    PrepareInput input{};
    status = make_input(service, key, missing, input);
    require(status == PreparedTextStatus::success, "failed slot input");
    status = (*session).submit(authority, input);
    require(status == PreparedTextStatus::success, "failed slot submit");
    const PreparedTextSessionSnapshot ready = wait_ready(*session);
    require(ready.completion == PreparedTextStatus::missing_font_coverage, "missing coverage reported");
    const PreparedTextBudgetSnapshot before = service.budget_snapshot();
    require(before.payload_generations == 1, "failure retains reservation");
    PreparedTextLayout output{};
    status = (*session).adopt_ready(authority, output);
    require(status == PreparedTextStatus::missing_font_coverage && output.empty(), "failure cannot adopt");
    status = (*session).discard_ready();
    require(status == PreparedTextStatus::success, "failed slot discarded");
    const PreparedTextBudgetSnapshot after = service.budget_snapshot();
    require(after.payload_generations == 0, "failed slot retired");
}

void test_font_byte_budget(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    std::vector<std::byte> padded(PreparedTextLimits::font_face_bytes);
    std::copy(bytes.begin(), bytes.end(), padded.begin());
    const std::array<PreparedFontSource, 2> sources{{
        {.encoded = padded, .role = FontRole::content}, {.encoded = padded}}};
    EncodedFontLease full{};
    PreparedTextStatus status = service.create_font_bank(sources, full);
    require(status == PreparedTextStatus::success, "exact aggregate encoded font budget");
    const PreparedTextBudgetSnapshot charged = service.budget_snapshot();
    require(charged.font_banks == 1 && charged.font_bytes == PreparedTextLimits::font_bytes, "eight MiB actual aggregate charge");
    const std::array<PreparedFontSource, 1> small{{{.encoded = bytes, .role = FontRole::content}}};
    EncodedFontLease replacement{};
    status = service.create_font_bank(small, replacement);
    require(status == PreparedTextStatus::busy && replacement.empty(), "second bank cannot exceed aggregate bytes");
    full = EncodedFontLease{};
    status = service.create_font_bank(small, replacement);
    require(status == PreparedTextStatus::success, "retired bank releases actual bytes");
}

void test_mapping(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    EncodedFontLease bank = make_bank(service, bytes);
    PreparedTextKey key = make_key(service, bank, "[x]a");
    key.source.end = SourceByteOffset(102);
    key.page.permitted = key.source;
    const std::array<DocumentMapSpan, 2> mapping{{
        {.source = {SourceByteOffset(100), SourceByteOffset(101)}, .begin = DisplayByteOffset(7), .end = DisplayByteOffset(10), .kind = DocumentMapKind::atomic_token},
        {.source = {SourceByteOffset(101), SourceByteOffset(102)}, .begin = DisplayByteOffset(10), .end = DisplayByteOffset(11)}}};
    std::array<PreparedSourceEndpoint, 3> endpoints{{
        {SourceByteOffset(100), DisplayByteOffset(7)}, {SourceByteOffset(101), DisplayByteOffset(10)},
        {SourceByteOffset(102), DisplayByteOffset(11)}}};
    PrepareInput input{};
    PreparedTextStatus status = service.create_input(key, "[x]a", mapping, endpoints, {true, true}, input);
    require(status == PreparedTextStatus::success, "atomic and identity mapping admitted");
    endpoints[1].display = DisplayByteOffset(8);
    status = service.create_input(key, "[x]a", mapping, endpoints, {true, true}, input);
    require(status == PreparedTextStatus::invalid_input && !input.empty(), "interior atomic endpoint invalid before capacity check");
    const std::array<std::string_view, 6> separators{"a\nb", "a\rb", "a\vb", "a\xc2\x85" "b", "a\xe2\x80\xa8" "b", "a\xe2\x80\xa9" "b"};
    for (std::size_t index = 0; index < separators.size(); ++index) {
        key = make_key(service, bank, separators[index]);
        status = make_input(service, key, separators[index], input);
        require(status == PreparedTextStatus::unsupported_profile, "paragraph separator refused before busy");
    }
}

void test_cross_service_authority(const std::span<const std::byte> bytes) {
    PreparedTextService first_service{};
    PreparedTextService second_service{};
    EncodedFontLease first_bank = make_bank(first_service, bytes);
    EncodedFontLease second_bank = make_bank(second_service, bytes);
    std::unique_ptr<PreparedTextSession> first_session{};
    std::unique_ptr<PreparedTextSession> second_session{};
    PreparedTextStatus status = first_service.open_session(first_bank, nullptr, first_session);
    require(status == PreparedTextStatus::success, "first independent session");
    status = second_service.open_session(second_bank, nullptr, second_session);
    require(status == PreparedTextStatus::success, "second independent session");
    const PreparedTextKey first_key = make_key(first_service, first_bank, "same");
    const PreparedTextKey second_key = make_key(second_service, second_bank, "same");
    PreparedTextLayout first_layout{};
    PreparedTextLayout second_layout{};
    prepare(first_service, *first_session, first_key, "same", first_layout);
    prepare(second_service, *second_session, second_key, "same", second_layout);
    const LayoutAuthority first_authority = first_layout.authority();
    const LayoutAuthority second_authority = second_layout.authority();
    require(first_authority.epoch == second_authority.epoch && first_authority.session != second_authority.session,
        "session identity unique across independent services at equal epoch");
    GrayTextMask output{};
    status = rasterize_prepared_text(first_layout, first_authority, output);
    require(status == PreparedTextStatus::success, "valid initial mask");
    const std::uint8_t* pixels = output.pixels().data();
    status = rasterize_prepared_text(first_layout, second_authority, output);
    require(status == PreparedTextStatus::stale && output.pixels().data() == pixels, "foreign authority cannot rasterize another service layout");
    LayoutAuthority first_next{};
    LayoutAuthority second_next{};
    status = (*first_session).desire(first_key, first_next);
    require(status == PreparedTextStatus::success, "first replacement desire");
    status = (*second_session).desire(second_key, second_next);
    require(status == PreparedTextStatus::success && first_next.epoch == second_next.epoch, "equal replacement epochs");
    PrepareInput input{};
    status = make_input(first_service, first_key, "same", input);
    require(status == PreparedTextStatus::success, "replacement input");
    status = (*first_session).submit(first_next, input);
    require(status == PreparedTextStatus::success, "replacement submit");
    const PreparedTextSessionSnapshot ready = wait_ready(*first_session);
    require(ready.completion == PreparedTextStatus::success, "replacement ready");
    status = (*first_session).adopt_ready(second_next, first_layout);
    require(status == PreparedTextStatus::stale && same_layout_authority(first_layout.authority(), first_authority),
        "foreign authority adoption preserves old layout");
    const PreparedTextSessionSnapshot preserved = (*first_session).inspect_ready();
    require(preserved.slot == PreparedTextSlot::ready, "foreign authority preserves ready slot");
    status = (*first_session).adopt_ready(first_next, first_layout);
    require(status == PreparedTextStatus::success, "correct authority still adopts ready result");
}
// F2 (planning/GAPS.md): a font bank carries family faces, and prepared text whose key names
// the family shapes with them, so its advance is the family's, not the role's. A face that
// is a role's and a family's at once, and an unknown family, are refused.
void test_family_faces(const std::filesystem::path& directory, const std::span<const std::byte> carlito) {
    const std::filesystem::path path = directory / "Selawik-Regular.ttf";
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "family font opens");
    const std::streamsize length = input.tellg();
    require(length > 0 && length <= 4 * 1024 * 1024, "family font size");
    std::vector<std::byte> selawik(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(selawik.data()), length);
    require(static_cast<bool>(input), "family font read");

    PreparedTextService service{};
    const std::array<PreparedFontSource, 1> both{{
        {.encoded = selawik, .role = FontRole::content, .family = selawik_font_family}}};
    EncodedFontLease refused{};
    PreparedTextStatus status = service.create_font_bank(both, refused);
    require(status == PreparedTextStatus::invalid_input && refused.empty(), "a face is a role's or a family's");
    const std::array<PreparedFontSource, 1> unknown{{{.encoded = selawik, .family = FontFamilyId{200U}}}};
    status = service.create_font_bank(unknown, refused);
    require(status == PreparedTextStatus::invalid_input && refused.empty(), "an unknown family is refused");

    const std::array<PreparedFontSource, 2> sources{{
        {.encoded = carlito, .role = FontRole::content},
        {.encoded = selawik, .family = selawik_font_family}}};
    EncodedFontLease bank{};
    status = service.create_font_bank(sources, bank);
    require(status == PreparedTextStatus::success, "a bank with a family face");
    std::unique_ptr<PreparedTextSession> session{};
    status = service.open_session(bank, nullptr, session);
    require(status == PreparedTextStatus::success, "family bank session");
    const std::string_view text = "Hamburgefonstiv";
    const PreparedTextKey role_key = make_key(service, bank, text);
    PreparedTextKey family_key = role_key;
    family_key.font.family = selawik_font_family;
    PreparedTextLayout role_layout{};
    prepare(service, *session, role_key, text, role_layout);
    const double role_advance = role_layout.metrics().advance_dip;
    PreparedTextLayout family_layout{};
    prepare(service, *session, family_key, text, family_layout);
    const double family_advance = family_layout.metrics().advance_dip;
    require(role_advance > 0.0 && family_advance > 0.0 && family_advance != role_advance,
            "text naming the family is shaped with the family's face");
}
} // namespace

int main(const int argc, char** const argv) {
    try {
        require(argc == 2, "font directory required");
        const std::vector<std::byte> bytes = read_font(std::filesystem::path(argv[1]));
        test_identity_exhaustion();
        test_session_claim(bytes);
        test_generations(bytes);
        test_authority(bytes);
        test_validation_and_fonts(bytes);
        test_failed_slot(bytes);
        test_font_byte_budget(bytes);
        test_mapping(bytes);
        test_cross_service_authority(bytes);
        test_family_faces(std::filesystem::path(argv[1]), bytes);
        std::cout << "prepared service checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
