// The bundled font set (item 4, step 3): the compiled manifest agrees with
// assets/fonts/bundled-fonts.sha256; a face is read once, checked against its size and digest
// before it is handed out, sealed, and shared; every refusal names its cause and releases its
// pages; a face outlives the set while a borrower keeps it. With --write-sealed the program
// writes into a sealed face and must die by the platform's protection fault
// (cmake/expect_fault.cmake checks how it ended).

#include "../src/core/resources/fonts/bundled_font_set.hpp"
#include "gui_forms/metrics/metrics/metrics.hpp"
#include "../src/core/resources/fonts/bundled_font_families.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using gui_forms::BundledFontDiagnostics;
using gui_forms::BundledFontSource;
using gui_forms::detail::BundledFaceRecord;
using gui_forms::detail::BundledFontCandidate;
using gui_forms::detail::BundledFontLocation;
using gui_forms::detail::BundledFontSet;
using gui_forms::detail::FontFaceLoad;
using gui_forms::detail::FontSetError;
using gui_forms::detail::SealedFontFace;
using gui_forms::detail::SealedPageFunctions;

SealedPageFunctions platform_pages() {
#if defined(_WIN32)
    const SealedPageFunctions pages = gui_forms::host::windows_sealed_pages();
#else
    const SealedPageFunctions pages = gui_forms::host::posix_sealed_pages();
#endif
    return pages;
}

// Heap pages that count every call, and can be told to refuse. seal only records the call.
struct PageLedger final {
    std::size_t reserved{};
    std::size_t sealed{};
    std::size_t released{};
    bool refuse_reserve{};
    bool refuse_seal{};
};
PageLedger ledger{};

std::byte* counted_reserve(std::size_t bytes) noexcept {
    if (ledger.refuse_reserve) return nullptr;
    std::byte* pages = new (std::nothrow) std::byte[bytes]{};
    if (pages != nullptr) ++ledger.reserved;
    return pages;
}
bool counted_seal(std::byte* pages, std::size_t bytes) noexcept {
    static_cast<void>(pages);
    static_cast<void>(bytes);
    if (ledger.refuse_seal) return false;
    ++ledger.sealed;
    return true;
}
void counted_release(std::byte* pages, std::size_t bytes) noexcept {
    static_cast<void>(bytes);
    delete[] pages;
    ++ledger.released;
}
constexpr SealedPageFunctions counted_pages{
    .reserve = &counted_reserve, .seal = &counted_seal, .release = &counted_release};

bool require(bool condition, const char* message) {
    if (!condition) std::printf("bundled_font_set: %s\n", message);
    return condition;
}

bool fails_with(const FontFaceLoad& load, FontSetError expected) {
    const std::optional<FontSetError> error = load.error();
    const bool matches = !load.accepted() && load.face() == nullptr && error && *error == expected;
    return matches;
}

std::vector<std::byte> file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> text((std::istreambuf_iterator<char>(stream)),
                                 std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
        bytes[index] = static_cast<std::byte>(text[index]);
    return bytes;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// The compiled manifest is the 20 faces of bundled-fonts.sha256, and each record matches the
// file in assets/fonts.
bool compiled_manifest_matches() {
    const std::span<const BundledFaceRecord> manifest = gui_forms::detail::bundled_face_manifest();
    if (!require(manifest.size() == 20U, "the compiled manifest does not list 20 faces")) return false;
    std::ifstream listing(std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / "bundled-fonts.sha256");
    std::string line;
    std::size_t index = 0;
    while (std::getline(listing, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!require(index < manifest.size(), "the manifest file has more lines than the table"))
            return false;
        const BundledFaceRecord& record = manifest[index];
        const std::string expected =
            std::string(record.sha256_hex) + "  " + std::string(record.file_name);
        if (!require(line == expected, "a compiled record differs from its manifest line")) return false;
        std::error_code status;
        const std::uintmax_t size = std::filesystem::file_size(
            std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / std::filesystem::path(record.file_name),
            status);
        if (!require(!status && size == record.size_bytes, "a compiled size differs from the file"))
            return false;
        ++index;
    }
    const bool complete = require(index == manifest.size(), "the manifest file has fewer lines");
    return complete;
}

// Real faces in real sealed pages: read once, shared, byte-identical to the file, and alive
// after the set while a borrower keeps the owner.
bool bundled_faces_load_once() {
    std::shared_ptr<const SealedFontFace> kept{};
    {
        BundledFontSet set(GUI_FORMS_TEST_FONT_DIRECTORY, platform_pages());
        if (!require(set.loaded_face_count() == 0U, "faces loaded before any request")) return false;
        const FontFaceLoad first = set.face("Cousine-Regular.ttf");
        if (!require(first.accepted() && first.face() != nullptr && !first.error(),
                     "a bundled face was refused"))
            return false;
        const FontFaceLoad second = set.face("Cousine-Regular.ttf");
        if (!require(second.accepted() && (*second.face()).get() == (*first.face()).get(),
                     "a second request did not share the first face"))
            return false;
        if (!require(set.loaded_face_count() == 1U, "one request loaded more than one face")) return false;
        const std::vector<std::byte> expected =
            file_bytes(std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / "Cousine-Regular.ttf");
        const std::span<const std::byte> bytes = (*(*first.face())).bytes();
        if (!require(set.loaded_bytes() == bytes.size() && bytes.size() == expected.size() &&
                         std::equal(bytes.begin(), bytes.end(), expected.begin()),
                     "the sealed bytes differ from the file"))
            return false;
        if (!require(fails_with(set.face("Arimo-Regular.ttf"), FontSetError::unknown_face),
                     "a face outside the manifest was not refused as unknown"))
            return false;
        kept = *first.face();
    }
    const std::span<const std::byte> survivor = (*kept).bytes();
    const bool readable = require(survivor.size() > 4U && survivor[0] == std::byte{0} &&
                                      survivor[1] == std::byte{1},
                                  "a face kept past its set is not a TrueType file");
    return readable;
}

// A synthetic manifest over files written here: each refusal has its own cause, a refusal is
// kept, and every reserved page is released.
bool refusals_name_their_cause() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "gui_forms_bundled_font_set_tests";
    std::error_code status;
    std::filesystem::remove_all(directory, status);
    std::filesystem::create_directories(directory / "a-directory.ttf");
    write_file(directory / "good.ttf", "abc");
    write_file(directory / "altered.ttf", "abd");
    write_file(directory / "short.ttf", "ab");
    // SHA-256("abc").
    constexpr std::string_view abc =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    static constexpr BundledFaceRecord manifest[] = {
        {"good.ttf", 3U, abc},       {"altered.ttf", 3U, abc}, {"short.ttf", 3U, abc},
        {"missing.ttf", 3U, abc},    {"a-directory.ttf", 3U, abc},
    };
    bool passed = true;
    ledger = PageLedger{};
    {
        BundledFontSet set(directory, manifest, counted_pages);
        const FontFaceLoad good = set.face("good.ttf");
        passed = passed && require(good.accepted() && (*(*good.face())).bytes().size() == 3U &&
                                       (*(*good.face())).bytes()[2] == std::byte{'c'},
                                   "a matching file was refused");
        passed = passed && require(fails_with(set.face("altered.ttf"), FontSetError::digest_mismatch),
                                   "a file with the wrong digest was accepted");
        passed = passed && require(fails_with(set.face("short.ttf"), FontSetError::size_mismatch),
                                   "a file with the wrong size was accepted");
        passed = passed && require(fails_with(set.face("missing.ttf"), FontSetError::unreadable),
                                   "a missing file was not refused as unreadable");
        passed = passed && require(fails_with(set.face("a-directory.ttf"), FontSetError::unreadable),
                                   "a directory was not refused as unreadable");
        // A kept refusal: repairing the file does not change the answer within this set.
        write_file(directory / "altered.ttf", "abc");
        passed = passed && require(fails_with(set.face("altered.ttf"), FontSetError::digest_mismatch),
                                   "a refusal was not kept");
        // Reserved for good and altered only; size, missing and directory stop before.
        passed = passed && require(ledger.reserved == 2U && ledger.sealed == 1U && ledger.released == 1U,
                                   "a refusal reserved or kept pages");
        passed = passed && require(set.loaded_face_count() == 1U && set.loaded_bytes() == 3U,
                                   "the loaded counts include a refusal");
    }
    passed = passed && require(ledger.released == ledger.reserved, "the set leaked pages");

    // The page capability's own failures.
    ledger = PageLedger{};
    ledger.refuse_reserve = true;
    {
        BundledFontSet set(directory, manifest, counted_pages);
        passed = passed && require(fails_with(set.face("good.ttf"), FontSetError::page_failure),
                                   "a refused reservation was not a page failure");
    }
    ledger = PageLedger{};
    ledger.refuse_seal = true;
    {
        BundledFontSet set(directory, manifest, counted_pages);
        passed = passed && require(fails_with(set.face("good.ttf"), FontSetError::page_failure),
                                   "a refused seal was not a page failure");
    }
    passed = passed && require(ledger.reserved == 1U && ledger.released == 1U,
                               "pages that could not be sealed were not released");
    std::filesystem::remove_all(directory, status);
    return passed;
}

// Programmer errors at construction.
bool construction_is_validated() {
    static constexpr BundledFaceRecord malformed[] = {{"x.ttf", 3U, "not a digest"}};
    static constexpr BundledFaceRecord empty[] = {
        {"x.ttf", 0U, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}};
    static constexpr BundledFaceRecord huge[] = {
        {"x.ttf", gui_forms::detail::maximum_bundled_face_bytes + 1U,
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}};
    const std::span<const BundledFaceRecord> cases[] = {malformed, empty, huge};
    for (const std::span<const BundledFaceRecord> manifest : cases) {
        bool refused = false;
        try {
            const BundledFontSet set(".", manifest, counted_pages);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        if (!require(refused, "an invalid manifest record was accepted")) return false;
    }
    SealedPageFunctions missing = counted_pages;
    missing.seal = nullptr;
    bool refused = false;
    try {
        const BundledFontSet set(".", missing);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    const bool passed = require(refused, "a missing page function was accepted");
    return passed;
}

// Every face a host registers is in the manifest, once, as a role or fallback face or as a
// face of a numbered family a theme may name; every manifest face is one of them; and exactly
// the seven primaries a window cannot do without are required.
bool face_uses_match_the_manifest() {
    const std::span<const BundledFaceRecord> manifest = gui_forms::detail::bundled_face_manifest();
    const std::span<const gui_forms::detail::BundledFaceUse> uses = gui_forms::detail::bundled_face_uses();
    const std::span<const gui_forms::detail::BundledFamilyFace> family_faces(gui_forms::detail::bundled_family_faces);
    if (!require(uses.size() + family_faces.size() == manifest.size(),
                 "the face uses and family faces differ from the manifest in length"))
        return false;
    for (const BundledFaceRecord& record : manifest) {
        std::size_t registered = 0;
        for (const gui_forms::detail::BundledFaceUse& use : uses) {
            if (use.file_name == record.file_name) ++registered;
        }
        for (const gui_forms::detail::BundledFamilyFace& family_face : family_faces) {
            if (family_face.file_name == record.file_name) ++registered;
        }
        if (!require(registered == 1U, "a manifest face is not registered exactly once")) return false;
    }
    for (const gui_forms::detail::BundledFamilyFace& family_face : family_faces) {
        if (!require(family_face.family.value != 0U &&
                         gui_forms::detail::valid_bundled_family(family_face.family),
                     "a family face names no numbered family"))
            return false;
    }
    std::size_t required = 0;
    for (std::size_t index = 0; index < uses.size(); ++index) {
        std::size_t listed = 0;
        for (const BundledFaceRecord& record : manifest) {
            if (record.file_name == uses[index].file_name) ++listed;
        }
        if (!require(listed == 1U, "a registered face is not listed once in the manifest")) return false;
        if (uses[index].required) {
            if (!require(index == required && uses[index].role.has_value(),
                         "a required face is not a leading primary"))
                return false;
            ++required;
        }
    }
    const bool passed = require(required == 7U, "not seven required faces");
    return passed;
}

// Theme family names (font-family): a role's face name stands for the role, Selawik is a
// numbered family, names match without regard to ASCII case, and a font that is not bundled
// with every program (a host font, or a drawing-only face such as Arimo) has no name here.
bool family_names_resolve_bundled_faces_only() {
    const gui_forms::detail::BundledFamilyName* const selawik = gui_forms::detail::bundled_family_named("selawik");
    if (!require(selawik != nullptr && !(*selawik).role && (*selawik).family == gui_forms::detail::selawik_family,
                 "Selawik is not a numbered family"))
        return false;
    const gui_forms::detail::BundledFamilyName* const rapids =
        gui_forms::detail::bundled_family_named("PORTSMOUTH RAPIDS");
    if (!require(rapids != nullptr && (*rapids).role == gui_forms::FontRole::control && (*rapids).family.value == 0U,
                 "Portsmouth Rapids does not stand for the control role"))
        return false;
    const char* const unknown[] = {"Arial", "Segoe UI", "Arimo", "Tinos", "Selawik Semibold", "", "Selawik "};
    for (const char* const name : unknown) {
        if (!require(gui_forms::detail::bundled_family_named(name) == nullptr, "a name outside the bundle resolved"))
            return false;
    }
    const bool passed = require(!gui_forms::detail::valid_bundled_family(gui_forms::BundledFontFamily{200U}),
                                "an unlisted family number is valid");
    return passed;
}

// One set per process while anything holds it; it goes with its last holder.
bool shared_set_is_shared() {
    const std::filesystem::path fonts(GUI_FORMS_TEST_FONT_DIRECTORY);
    const std::shared_ptr<BundledFontSet> first =
        gui_forms::detail::shared_bundled_font_set(fonts, counted_pages);
    const std::shared_ptr<BundledFontSet> second =
        gui_forms::detail::shared_bundled_font_set(fonts, counted_pages);
    if (!require(first && first == second, "two holders got different sets")) return false;
    std::shared_ptr<BundledFontSet> elsewhere =
        gui_forms::detail::shared_bundled_font_set(fonts / "elsewhere", counted_pages);
    if (!require(elsewhere && elsewhere != first, "another directory shared a set")) return false;
    const std::weak_ptr<BundledFontSet> watched = elsewhere;
    {
        const std::shared_ptr<BundledFontSet> again =
            gui_forms::detail::shared_bundled_font_set(fonts / "elsewhere", counted_pages);
        if (!require(again == elsewhere, "the current shared set was not returned")) return false;
    }
    elsewhere.reset();
    const bool passed = require(watched.expired(), "the set outlived its last holder");
    return passed;
}

// A host offers its font directories in order (environment, executable, library module):
// the first holding every required face wins, whichever source it is, so a library loaded by
// another process finds the fonts beside itself. With none, the lookup is counted, the refusal
// names every directory tried, and the windows' metrics report it; a later success clears the
// refusal.
bool font_directory_lookup() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gui_forms_bundled_font_lookup_tests";
    std::error_code status;
    std::filesystem::remove_all(root, status);
    const std::filesystem::path partial = root / "executable" / "fonts";
    const std::filesystem::path complete = root / "library" / "fonts";
    std::filesystem::create_directories(partial);
    std::filesystem::create_directories(complete);
    std::size_t required{};
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (!use.required) continue;
        // Only names are checked here; the set checks sizes and digests when it reads.
        write_file(complete / use.file_name, "face");
        if (required == 0U) write_file(partial / use.file_name, "face");
        ++required;
    }
    bool passed = require(required > 1U, "fewer than two required faces to test a partial directory");
    passed = passed && require(gui_forms::detail::directory_holds_required_faces(complete),
                               "a directory with every required face was not accepted");
    passed = passed && require(!gui_forms::detail::directory_holds_required_faces(partial),
                               "a directory missing a required face was accepted");
    passed = passed && require(!gui_forms::detail::directory_holds_required_faces({}),
                               "the empty directory was accepted");

    const BundledFontDiagnostics before = gui_forms::bundled_font_diagnostics();
    std::array<BundledFontCandidate, 3> candidates{};
    candidates[0].source = BundledFontSource::environment;
    candidates[1].source = BundledFontSource::executable;
    candidates[1].directory = partial;
    candidates[2].source = BundledFontSource::library_module;
    candidates[2].directory = complete;
    const BundledFontLocation found = gui_forms::detail::locate_bundled_fonts(candidates);
    passed = passed && require(found.source == BundledFontSource::library_module && found.directory == complete,
                               "the fonts beside the library module were not found");
    const BundledFontDiagnostics after_found = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_found.lookups == before.lookups + 1U &&
                                   after_found.lookups_without_source == before.lookups_without_source &&
                                   after_found.last_source == BundledFontSource::library_module,
                               "a successful lookup was not counted as found");
    passed = passed && require(gui_forms::bundled_font_refusal().empty(), "a successful lookup left a refusal");
    // The fallback faces are an optional install component: a directory without them is
    // accepted, counted and noted, never refused.
    std::uint32_t fallback_total{};
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (!use.role && !use.required) ++fallback_total;
    }
    passed = passed && require(fallback_total == 7U, "the library does not know the seven fallback faces");
    passed = passed && require(after_found.fallback_faces_expected == fallback_total &&
                                   after_found.fallback_faces_found == 0U,
                               "a directory without the fallback faces was not counted as lacking them");
    const std::string notice = gui_forms::bundled_font_notice();
    passed = passed && require(notice.find("0 of 7") != std::string::npos &&
                                   notice.find("NotoEmoji-Regular.ttf") != std::string::npos &&
                                   notice.find("fallback_fonts") != std::string::npos,
                               "the notice does not name the absent fallback faces and the component");

    candidates[2].directory = root / "nowhere";
    const BundledFontLocation missing = gui_forms::detail::locate_bundled_fonts(candidates);
    passed = passed && require(missing.source == BundledFontSource::none && missing.directory.empty(),
                               "a lookup without the faces named a source");
    const BundledFontDiagnostics after_missing = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_missing.lookups == before.lookups + 2U &&
                                   after_missing.lookups_without_source == before.lookups_without_source + 1U &&
                                   after_missing.last_source == BundledFontSource::none,
                               "a lookup without the faces was not counted");
    const std::string refusal = gui_forms::bundled_font_refusal();
    passed = passed && require(refusal.find("executable (") != std::string::npos &&
                                   refusal.find("library_module (") != std::string::npos &&
                                   refusal.find("environment") == std::string::npos &&
                                   refusal.find("GUI_FORMS_FONT_DIR") != std::string::npos,
                               "the refusal does not name the directories tried and the remedy");
    gui_forms::Metrics metrics;
    passed = passed && require(metrics.snapshot().bundled_font_lookups_without_source ==
                                   after_missing.lookups_without_source,
                               "the metrics do not report the lookups without a source");
    passed = passed && require(metrics.snapshot().to_json().find("\"bundled_font_lookups_without_source\":") !=
                                   std::string::npos,
                               "the metrics JSON leaves out the lookups without a source");

    passed = passed && require(gui_forms::bundled_font_diagnostics().fallback_faces_found == 0U &&
                                   gui_forms::bundled_font_notice().empty(),
                               "a lookup without a directory reported fallback faces or kept the notice");

    // With the fallback component installed, every face is counted and nothing is noted.
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (!use.role && !use.required) write_file(complete / use.file_name, "face");
    }
    candidates[2].directory = complete;
    const BundledFontLocation again = gui_forms::detail::locate_bundled_fonts(candidates);
    passed = passed && require(again.source == BundledFontSource::library_module &&
                                   gui_forms::bundled_font_refusal().empty(),
                               "a later successful lookup kept the old refusal");
    const BundledFontDiagnostics after_again = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_again.fallback_faces_found == fallback_total &&
                                   after_again.fallback_faces_expected == fallback_total &&
                                   gui_forms::bundled_font_notice().empty(),
                               "a directory with every fallback face was not counted as complete");
    passed = passed && require(gui_forms::bundled_font_source_name(BundledFontSource::library_module) ==
                                   "library_module",
                               "the library module source is misnamed");
    std::filesystem::remove_all(root, status);
    return passed;
}

// Writes into a sealed face; reaching the end is the failure.
int write_into_sealed_face() {
    BundledFontSet set(GUI_FORMS_TEST_FONT_DIRECTORY, platform_pages());
    const FontFaceLoad load = set.face("NotoSansHebrew-Regular.ttf");
    if (!load.accepted()) {
        std::printf("bundled_font_set: the face to write into was refused\n");
        return 2;
    }
    const std::span<const std::byte> bytes = (*(*load.face())).bytes();
    // The pages were reserved writable and are not const objects, so the cast itself is
    // defined; the protection fault is what this mode tests.
    volatile std::byte* target = const_cast<std::byte*>(bytes.data());
    target[0] = std::byte{0x5a};
    std::printf("bundled_font_set: a write into a sealed face did not fault\n");
    return 1;
}

} // namespace

int main(int argument_count, char** arguments) {
    if (argument_count == 2 && std::string_view(arguments[1]) == "--write-sealed") {
        const int status = write_into_sealed_face();
        return status;
    }
    bool passed = compiled_manifest_matches();
    passed = bundled_faces_load_once() && passed;
    passed = refusals_name_their_cause() && passed;
    passed = construction_is_validated() && passed;
    passed = face_uses_match_the_manifest() && passed;
    passed = family_names_resolve_bundled_faces_only() && passed;
    passed = shared_set_is_shared() && passed;
    passed = font_directory_lookup() && passed;
    if (!passed) return 1;
    std::printf("bundled_font_set: manifest, loading, sharing, refusals, page release and font lookup passed\n");
    return 0;
}
