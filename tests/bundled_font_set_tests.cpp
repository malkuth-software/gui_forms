// The bundled font set (item 4, step 3; font pack stage 4): the compiled manifest agrees with
// assets/fonts/bundled-fonts.sha256; a face is read once, checked against its size and digest
// before it is handed out, sealed, and shared; every refusal names its cause and releases its
// pages; a face outlives the set while a borrower keeps it. From the embedded pack, the first
// request decodes only the eager group, a lazy group is decoded once on its first face, and a
// group whose stored CRC-32 is altered, or a pack that does not open, is refused and reported.
// The lookup takes the pack unless a development build's directory holds the faces. With
// --write-sealed the program writes into a sealed face and must die by the platform's
// protection fault (cmake/expect_fault.cmake checks how it ended).

#include "../src/core/resources/fonts/bundled_font_set.hpp"
#include "gui_forms/metrics/metrics/metrics.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "stx_fontpack/loader.hpp"
#include "support/unique_temporary_directory.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <stxlib/optional.hpp>
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
using gui_forms::detail::BundledFontLookup;
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

// The compiled manifest is the 39 faces of bundled-fonts.sha256, and each record matches the
// file in assets/fonts.
bool compiled_manifest_matches() {
    const std::span<const BundledFaceRecord> manifest = gui_forms::detail::bundled_face_manifest();
    if (!require(manifest.size() == 39U, "the compiled manifest does not list 39 faces")) return false;
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
        if (!require(fails_with(set.face("Wingdings.ttf"), FontSetError::unknown_face),
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
        gui_forms::test_support::make_unique_temporary_directory("gui_forms_bundled_font_set_tests");
    std::error_code status;
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

// The face files of the registry's families that are not a role's (Selawik, Arimo, Tinos,
// Portsmouth Mono, Selawik Semibold): the faces a host registers as family faces, each file
// once although Selawik Semibold shares two of Selawik's.
std::vector<std::string_view> own_family_faces() {
    std::vector<std::string_view> faces{};
    for (const gui_forms::FontFamilyRecord& record : gui_forms::bundled_font_families) {
        if (record.role.present()) continue;
        for (const gui_forms::FontFamilyFace& face : record.faces) {
            const bool listed = std::find(faces.begin(), faces.end(), face.file_name) != faces.end();
            if (!listed) faces.push_back(face.file_name);
        }
    }
    return faces;
}

// The faces the pack carries in its eager group that no host registers yet: the Portsmouth
// faces outside the control role (font pack stage 4 packs all of them).
constexpr std::array<std::string_view, 8> pack_only_faces{{
    "PortsmouthRapids-Italic.ttf", "PortsmouthRapids-BoldItalic.ttf", "PortsmouthRapids-Light.ttf",
    "Portsmouth.ttf", "Portsmouth-Bold.ttf", "Portsmouth-Italic.ttf", "Portsmouth-BoldItalic.ttf",
    "Portsmouth-Light.ttf",
}};

// Every face a host registers is in the manifest, once, as a role or fallback face or as a
// face of a family with faces of its own; every other manifest face is a pack-only face; and
// exactly the seven primaries a window cannot do without are required.
bool face_uses_match_the_manifest() {
    const std::span<const BundledFaceRecord> manifest = gui_forms::detail::bundled_face_manifest();
    const std::span<const gui_forms::detail::BundledFaceUse> uses = gui_forms::detail::bundled_face_uses();
    const std::vector<std::string_view> family_faces = own_family_faces();
    if (!require(uses.size() + family_faces.size() + pack_only_faces.size() == manifest.size(),
                 "the face uses, family faces and pack-only faces differ from the manifest in length"))
        return false;
    for (const BundledFaceRecord& record : manifest) {
        std::size_t registered = 0;
        for (const gui_forms::detail::BundledFaceUse& use : uses) {
            if (use.file_name == record.file_name) ++registered;
        }
        for (const std::string_view family_face : family_faces) {
            if (family_face == record.file_name) ++registered;
        }
        for (const std::string_view pack_only : pack_only_faces) {
            if (pack_only == record.file_name) ++registered;
        }
        if (!require(registered == 1U, "a manifest face is not registered exactly once")) return false;
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

// The family registry (owner decisions of 2026-11-13): each family has its fixed number and
// at least one face; a role's family lists exactly the faces registered for that role, with
// their weights and slants; numbers are unique and within the bundled range.
bool registry_matches_the_face_uses() {
    const std::span<const gui_forms::detail::BundledFaceUse> uses = gui_forms::detail::bundled_face_uses();
    for (std::size_t index = 0U; index < gui_forms::bundled_font_families.size(); ++index) {
        const gui_forms::FontFamilyRecord& record = gui_forms::bundled_font_families[index];
        if (!require(record.id.value != 0U && record.id.value <= gui_forms::maximum_bundled_font_family &&
                         !record.faces.empty(),
                     "a family has no number in the bundled range or no face"))
            return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (!require(!(gui_forms::bundled_font_families[earlier].id == record.id), "two families share a number"))
                return false;
        }
        if (!require(gui_forms::font_family_record(record.id) == &record, "a number does not find its family"))
            return false;
        if (record.role.absent()) continue;
        std::size_t role_faces = 0U;
        for (const gui_forms::detail::BundledFaceUse& use : uses) {
            if (!use.role || *use.role != (*record.role.value())) continue;
            ++role_faces;
            bool listed = false;
            for (const gui_forms::FontFamilyFace& face : record.faces) {
                if (face.file_name == use.file_name && face.weight == use.weight && face.italic == use.italic) {
                    listed = true;
                }
            }
            if (!require(listed, "a role face is missing from its family")) return false;
        }
        if (!require(role_faces == record.faces.size(), "a role family lists a face the role does not register"))
            return false;
        if (!require(gui_forms::role_font_family((*record.role.value())) == record.id, "a role does not find its family"))
            return false;
    }
    const bool fixed = require(gui_forms::selawik_font_family.value == 1U &&
                                   gui_forms::portsmouth_rapids_font_family.value == 2U &&
                                   gui_forms::carlito_font_family.value == 3U &&
                                   gui_forms::cousine_font_family.value == 4U &&
                                   gui_forms::arimo_font_family.value == 5U &&
                                   gui_forms::tinos_font_family.value == 6U &&
                                   gui_forms::portsmouth_mono_font_family.value == 7U &&
                                   gui_forms::selawik_semibold_font_family.value == 8U,
                               "a fixed family number changed");
    return fixed;
}

// Family names: every family by its own name, without regard to ASCII case (the substitution
// table's Arimo, Tinos, Portsmouth Mono and Selawik Semibold among them since font pack stage
// 4); a font that is not bundled (a host font, or a substitution's request such as Arial) has
// no name here; an unlisted number is not valid; a substitution's face file finds its family.
bool family_names_resolve_bundled_faces_only() {
    const gui_forms::FontFamilyRecord* const selawik = gui_forms::font_family_named("selawik");
    if (!require(selawik != nullptr && (*selawik).role.absent() && (*selawik).id == gui_forms::selawik_font_family,
                 "Selawik is not a family with faces of its own"))
        return false;
    const gui_forms::FontFamilyRecord* const rapids = gui_forms::font_family_named("PORTSMOUTH RAPIDS");
    if (!require(rapids != nullptr && (*rapids).role == stxlib::Optional<gui_forms::FontRole>::of(gui_forms::FontRole::control) &&
                     (*rapids).id == gui_forms::portsmouth_rapids_font_family,
                 "Portsmouth Rapids is not the control role's family"))
        return false;
    const char* const unknown[] = {"Arial", "Segoe UI", "Times New Roman", "Consolas", "", "Selawik "};
    for (const char* const name : unknown) {
        if (!require(gui_forms::font_family_named(name) == nullptr, "a name outside the bundle resolved"))
            return false;
    }
    struct Named final {
        const char* name{};
        gui_forms::FontFamilyId id{};
    };
    const Named drawing_families[] = {
        {"arimo", gui_forms::arimo_font_family},
        {"TINOS", gui_forms::tinos_font_family},
        {"Portsmouth Mono", gui_forms::portsmouth_mono_font_family},
        {"selawik semibold", gui_forms::selawik_semibold_font_family},
    };
    for (const Named& entry : drawing_families) {
        const gui_forms::FontFamilyRecord* const record = gui_forms::font_family_named(entry.name);
        if (!require(record != nullptr && (*record).id == entry.id && (*record).role.absent(),
                     "a substitution table family is not a family with faces of its own"))
            return false;
    }
    const gui_forms::FontFamilyRecord* const semibold = gui_forms::font_family_with_face("Selawik-Semibold.ttf");
    if (!require(semibold == selawik, "a shared face file does not find the lower-numbered family")) return false;
    const gui_forms::FontFamilyRecord* const arimo = gui_forms::font_family_with_face("Arimo-Regular.ttf");
    if (!require(arimo != nullptr && (*arimo).id == gui_forms::arimo_font_family, "Arimo's face has no family"))
        return false;
    if (!require(gui_forms::font_family_with_face("Wingdings.ttf") == nullptr, "an unbundled face has a family"))
        return false;
    for (const gui_forms::GenericFontFamily& generic : gui_forms::generic_font_families) {
        if (!require(gui_forms::valid_font_family(generic.family), "a generic draws an unknown family")) return false;
    }
    const gui_forms::GenericFontFamily* const sans = gui_forms::generic_font_family("Sans-Serif");
    const gui_forms::GenericFontFamily* const mono = gui_forms::generic_font_family("ui-monospace");
    const gui_forms::GenericFontFamily* const serif = gui_forms::generic_font_family("serif");
    if (!require(sans != nullptr && (*sans).family == gui_forms::carlito_font_family && mono != nullptr &&
                     (*mono).family == gui_forms::cousine_font_family && serif != nullptr &&
                     (*serif).family == gui_forms::tinos_font_family &&
                     gui_forms::generic_font_family("Selawik") == nullptr,
                 "the generic mapping is not ours"))
        return false;
    const bool passed = require(!gui_forms::valid_font_family(gui_forms::FontFamilyId{200U}) &&
                                    gui_forms::valid_font_family(gui_forms::FontFamilyId{}),
                                "an unlisted family number is valid, or zero is not");
    return passed;
}

// Faces within a family by nearest weight and slant, never synthesized (stage F decision 7):
// the slant first, then the weight distance, an equal distance to the lighter face at 500 or
// less and to the heavier above.
bool nearest_faces_are_chosen() {
    const gui_forms::FontFamilyRecord& selawik = *gui_forms::font_family_record(gui_forms::selawik_font_family);
    const gui_forms::FontFamilyRecord& carlito = *gui_forms::font_family_record(gui_forms::carlito_font_family);
    const gui_forms::FontFamilyRecord& mono =
        *gui_forms::font_family_record(gui_forms::portsmouth_mono_font_family);
    const gui_forms::FontFamilyRecord& semibold =
        *gui_forms::font_family_record(gui_forms::selawik_semibold_font_family);
    struct Case final {
        const gui_forms::FontFamilyRecord* family{};
        std::uint16_t weight{};
        bool italic{};
        std::string_view expected{};
    };
    const Case cases[] = {
        {&selawik, 400U, false, "Selawik-Regular.ttf"},
        {&selawik, 600U, false, "Selawik-Semibold.ttf"},
        {&selawik, 650U, false, "Selawik-Bold.ttf"},
        {&selawik, 500U, false, "Selawik-Regular.ttf"},
        {&selawik, 900U, false, "Selawik-Bold.ttf"},
        {&selawik, 100U, false, "Selawik-Regular.ttf"},
        {&selawik, 700U, true, "Selawik-Bold.ttf"},
        {&carlito, 400U, true, "Carlito-Italic.ttf"},
        {&carlito, 700U, true, "Carlito-BoldItalic.ttf"},
        {&carlito, 550U, false, "Carlito-Bold.ttf"},
        {&carlito, 449U, false, "Carlito-Regular.ttf"},
        {&mono, 700U, true, "PortsmouthMono.ttf"},
        {&semibold, 400U, false, "Selawik-Semibold.ttf"},
        {&semibold, 700U, false, "Selawik-Bold.ttf"},
    };
    for (const Case& entry : cases) {
        const std::size_t index = gui_forms::nearest_font_family_face(*entry.family, entry.weight, entry.italic);
        if (!require((*entry.family).faces[index].file_name == entry.expected, "the nearest face is not chosen"))
            return false;
    }
    return true;
}

// Family stacks: a list registers once and keeps its number; a refused list is zero; a font's
// order is its family, then the stack's families not already listed.
bool family_stacks_register_and_order() {
    const std::array<gui_forms::FontFamilyId, 2> pair{gui_forms::selawik_font_family, gui_forms::cousine_font_family};
    const gui_forms::FontFamilyStackId first = gui_forms::register_font_family_stack(pair);
    const gui_forms::FontFamilyStackId again = gui_forms::register_font_family_stack(pair);
    if (!require(first.value != 0U && first == again, "a stack does not keep its number")) return false;
    const gui_forms::FontFamilyStack read = gui_forms::font_family_stack(first);
    if (!require(read.count == 2U && read.families[0] == gui_forms::selawik_font_family &&
                     read.families[1] == gui_forms::cousine_font_family,
                 "a stack does not read back its families"))
        return false;
    const std::array<gui_forms::FontFamilyId, 2> repeated{gui_forms::selawik_font_family,
                                                          gui_forms::selawik_font_family};
    const std::array<gui_forms::FontFamilyId, 1> unknown{gui_forms::FontFamilyId{200U}};
    const std::array<gui_forms::FontFamilyId, 5> long_list{gui_forms::selawik_font_family,
                                                           gui_forms::portsmouth_rapids_font_family,
                                                           gui_forms::carlito_font_family,
                                                           gui_forms::cousine_font_family, gui_forms::FontFamilyId{}};
    if (!require(gui_forms::register_font_family_stack(repeated).value == 0U &&
                     gui_forms::register_font_family_stack(unknown).value == 0U &&
                     gui_forms::register_font_family_stack(long_list).value == 0U &&
                     gui_forms::register_font_family_stack({}).value == 0U,
                 "a refused stack has a number"))
        return false;
    if (!require(gui_forms::font_family_stack(gui_forms::FontFamilyStackId{60000U}).count == 0U &&
                     !gui_forms::valid_font_family_stack(gui_forms::FontFamilyStackId{60000U}) &&
                     gui_forms::valid_font_family_stack(first),
                 "an unnumbered stack reads as one"))
        return false;
    gui_forms::FontSpec font{};
    font.family = gui_forms::cousine_font_family;
    font.stack = first;
    const gui_forms::FontFamilyOrder order = gui_forms::font_family_order(font);
    const bool passed = require(order.count == 2U && order.families[0] == gui_forms::cousine_font_family &&
                                    order.families[1] == gui_forms::selawik_font_family,
                                "the order is not the family, then the stack without repeats");
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

// The faces of the pack's eager group, as font-pack.manifest places them: every Portsmouth
// face and Carlito.
bool eager_face(std::string_view file_name) {
    const bool portsmouth = file_name.substr(0U, 10U) == "Portsmouth";
    const bool carlito = file_name.substr(0U, 8U) == "Carlito-";
    return portsmouth || carlito;
}

// From the embedded pack: nothing is decoded before a request; the first request decodes the
// eager group alone and answers every eager face from it; a lazy group (CJK) is decoded once,
// on its first face; the bytes are the files' bytes; every sealed page is released.
bool pack_faces_decode_by_group() {
    const std::span<const std::byte> pack = gui_forms::detail::bundled_font_pack();
    std::size_t eager_faces{};
    for (const BundledFaceRecord& record : gui_forms::detail::bundled_face_manifest()) {
        if (eager_face(record.file_name)) ++eager_faces;
    }
    bool passed = require(eager_faces == 15U, "not fifteen eager faces in the manifest");
    ledger = PageLedger{};
    {
        BundledFontSet set(pack, counted_pages);
        passed = passed && require(set.decoded_group_count() == 0U && set.loaded_face_count() == 0U,
                                   "a pack set decoded before its first request");
        const FontFaceLoad control = set.face("PortsmouthRapids.ttf");
        passed = passed && require(control.accepted(), "the control face was refused from the pack");
        passed = passed && require(set.decoded_group_count() == 1U && set.loaded_face_count() == eager_faces,
                                   "the first request decoded more or less than the eager group");
        if (control.accepted()) {
            const std::vector<std::byte> expected =
                file_bytes(std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / "PortsmouthRapids.ttf");
            const std::span<const std::byte> bytes = (*(*control.face())).bytes();
            passed = passed && require(bytes.size() == expected.size() &&
                                           std::equal(bytes.begin(), bytes.end(), expected.begin()),
                                       "a decoded face differs from its file");
        }
        passed = passed && require(set.face("Carlito-BoldItalic.ttf").accepted() &&
                                       set.face("PortsmouthMono.ttf").accepted() && set.decoded_group_count() == 1U,
                                   "an eager face was not answered from the eager decode");
        const FontFaceLoad cjk = set.face("NotoSansCJKjp-Subset.otf");
        if (cjk.accepted()) {
            const FontFaceLoad again = set.face("NotoSansCJKjp-Subset.otf");
            passed = passed && require(set.decoded_group_count() == 2U &&
                                           (*again.face()).get() == (*cjk.face()).get(),
                                       "the CJK group was not decoded exactly once");
            const std::uintmax_t size = std::filesystem::file_size(
                std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / "NotoSansCJKjp-Subset.otf");
            passed = passed && require((*(*cjk.face())).bytes().size() == size, "the CJK face has the wrong size");
        } else {
            // A build without the fallback faces (GUI_FORMS_INSTALL_FALLBACK_FONTS=OFF).
            passed = passed && require(fails_with(cjk, FontSetError::unreadable) && set.decoded_group_count() == 1U,
                                       "a face the pack leaves out was not unreadable");
        }
        passed = passed && require(fails_with(set.face("Wingdings.ttf"), FontSetError::unknown_face),
                                   "a face outside the manifest was not unknown");
        passed = passed && require(ledger.sealed == set.loaded_face_count() && ledger.released == 0U,
                                   "a decoded face was not sealed once");
    }
    passed = passed && require(ledger.released == ledger.reserved, "the pack set leaked pages");
    return passed;
}

// The byte offset in `pack` of the stored CRC-32 of the font called `file_name`, from the
// container layout (stx_fontpack/src/detail/format.hpp): the font's name, then its size as a
// varint, then the CRC-32. Zero when the pack does not open or lacks the font.
std::size_t stored_crc_offset(std::span<const std::byte> pack, std::string_view file_name) {
    const std::unique_ptr<stx_fontpack::PackIndex> index = std::make_unique<stx_fontpack::PackIndex>();
    if (!stx_fontpack::pack_open(pack, *index)) return 0U;
    for (std::uint32_t font = 0U; font < (*index).font_count; ++font) {
        const stx_fontpack::PackFont& packed = (*index).fonts[font];
        if (std::string_view(packed.name, packed.name_length) != file_name) continue;
        const std::uint8_t* at = reinterpret_cast<const std::uint8_t*>(packed.name) + packed.name_length;
        while ((*at & 0x80U) != 0U) ++at;
        ++at;
        const std::size_t offset = static_cast<std::size_t>(at - reinterpret_cast<const std::uint8_t*>(pack.data()));
        return offset;
    }
    return 0U;
}

// A pack whose stored CRC-32 for Cousine-Regular.ttf is altered: the eager faces still load;
// the monospace group is refused once, every face of it is pack_refused, and the refusal is
// counted and names the group, the cause and the faces. A pack that does not open refuses
// every face and is reported too.
bool refused_pack_is_reported() {
    const std::span<const std::byte> embedded = gui_forms::detail::bundled_font_pack();
    std::vector<std::byte> damaged(embedded.begin(), embedded.end());
    const std::size_t offset = stored_crc_offset(damaged, "Cousine-Regular.ttf");
    bool passed = require(offset != 0U && offset < damaged.size(), "the pack's Cousine-Regular CRC was not found");
    if (!passed) return false;
    damaged[offset] = damaged[offset] ^ std::byte{0xff};
    const BundledFontDiagnostics before = gui_forms::bundled_font_diagnostics();
    ledger = PageLedger{};
    {
        BundledFontSet set(damaged, counted_pages);
        passed = passed && require(set.face("PortsmouthRapids.ttf").accepted(),
                                   "a damaged lazy group refused the eager faces");
        passed = passed && require(fails_with(set.face("Cousine-Regular.ttf"), FontSetError::pack_refused),
                                   "a face whose CRC-32 is altered was not refused");
        const std::size_t decoded = set.decoded_group_count();
        passed = passed && require(fails_with(set.face("Cousine-Bold.ttf"), FontSetError::pack_refused) &&
                                       set.decoded_group_count() == decoded,
                                   "a refused group was decoded again or spared a face");
    }
    passed = passed && require(ledger.released == ledger.reserved, "a refused group leaked pages");
    const BundledFontDiagnostics after = gui_forms::bundled_font_diagnostics();
    const std::string refusal = gui_forms::bundled_font_pack_refusal();
    passed = passed && require(after.pack_refusals == before.pack_refusals + 1U, "the refusal was not counted");
    passed = passed && require(refusal.find("monospace") != std::string::npos &&
                                   refusal.find("bad check") != std::string::npos &&
                                   refusal.find("Cousine-Regular.ttf") != std::string::npos,
                               "the refusal does not name the group, the cause and the faces");

    const std::span<const std::byte> truncated = embedded.first(16U);
    {
        BundledFontSet set(truncated, counted_pages);
        passed = passed && require(fails_with(set.face("PortsmouthRapids.ttf"), FontSetError::pack_refused) &&
                                       set.decoded_group_count() == 0U,
                                   "a pack that does not open did not refuse its faces");
    }
    passed = passed && require(gui_forms::bundled_font_diagnostics().pack_refusals == before.pack_refusals + 2U &&
                                   gui_forms::bundled_font_pack_refusal().find("does not open") != std::string::npos,
                               "a pack that does not open was not reported");
    return passed;
}

// A shipped build ignores the hosts' directories and takes the embedded pack. A development
// build offers them in order (GUI_FORMS_FONT_DIR, then the checkout's assets/fonts; an empty
// one is skipped): the first holding every required face wins; with none, the pack serves and
// the notice names every directory tried. Only a pack that does not open leaves no source: the lookup is counted,
// the refusal says so and names the directories, and the windows' metrics report it; a later
// success clears the refusal.
bool font_directory_lookup() {
    const std::filesystem::path root =
        gui_forms::test_support::make_unique_temporary_directory("gui_forms_bundled_font_lookup_tests");
    std::error_code status;
    const std::filesystem::path partial = root / "partial";
    const std::filesystem::path complete = root / "complete";
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
    candidates[1].source = BundledFontSource::source_tree;
    candidates[1].directory = partial;
    candidates[2].source = BundledFontSource::source_tree;
    candidates[2].directory = complete;
    BundledFontLookup lookup{};
    lookup.candidates = candidates;
    lookup.pack = gui_forms::detail::bundled_font_pack();

    // Shipped: the directories are not honoured, whatever they hold.
    lookup.directories_honoured = false;
    const BundledFontLocation shipped = gui_forms::detail::locate_bundled_fonts(lookup);
    passed = passed && require(shipped.source == BundledFontSource::embedded_pack && shipped.directory.empty(),
                               "a shipped lookup did not take the embedded pack");
    std::uint32_t fallback_total{};
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (!use.role && !use.required) ++fallback_total;
    }
    passed = passed && require(fallback_total == 9U, "the library does not know the nine fallback faces");
    const BundledFontDiagnostics after_shipped = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_shipped.fallback_faces_expected == fallback_total &&
                                   after_shipped.fallback_faces_found <= fallback_total &&
                                   (after_shipped.fallback_faces_found == fallback_total) ==
                                       gui_forms::bundled_font_notice().empty(),
                               "the pack's fallback faces were not counted, or the notice disagrees");
    // The hosts' own lookup follows the build.
    const BundledFontLocation hosts = gui_forms::detail::locate_bundled_fonts(std::span<const BundledFontCandidate>(candidates));
    const BundledFontSource expected_source = gui_forms::detail::development_font_directories()
                                                  ? BundledFontSource::source_tree
                                                  : BundledFontSource::embedded_pack;
    passed = passed && require(hosts.source == expected_source, "the hosts' lookup does not follow the build");

    // Development: the first directory with every face wins.
    lookup.directories_honoured = true;
    const std::uint64_t lookups_before = gui_forms::bundled_font_diagnostics().lookups;
    const BundledFontLocation found = gui_forms::detail::locate_bundled_fonts(lookup);
    passed = passed && require(found.source == BundledFontSource::source_tree && found.directory == complete,
                               "the complete source-tree directory was not found");
    const BundledFontDiagnostics after_found = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_found.lookups == lookups_before + 1U &&
                                   after_found.lookups_without_source == before.lookups_without_source &&
                                   after_found.last_source == BundledFontSource::source_tree,
                               "a successful lookup was not counted as found");
    passed = passed && require(gui_forms::bundled_font_refusal().empty(), "a successful lookup left a refusal");
    // The fallback faces are optional: a directory without them is accepted, counted and
    // noted, never refused.
    passed = passed && require(after_found.fallback_faces_expected == fallback_total &&
                                   after_found.fallback_faces_found == 0U,
                               "a directory without the fallback faces was not counted as lacking them");
    const std::string notice = gui_forms::bundled_font_notice();
    passed = passed && require(notice.find("0 of 9") != std::string::npos &&
                                   notice.find("NotoEmoji-Regular.ttf") != std::string::npos &&
                                   notice.find("fallback_fonts") != std::string::npos,
                               "the notice does not name the absent fallback faces and the component");

    // No directory holds the faces: the pack serves, and the notice names the directories.
    candidates[2].directory = root / "nowhere";
    const BundledFontLocation fell_back = gui_forms::detail::locate_bundled_fonts(lookup);
    const std::string development_notice = gui_forms::bundled_font_notice();
    passed = passed && require(fell_back.source == BundledFontSource::embedded_pack &&
                                   development_notice.find("source_tree (") != std::string::npos &&
                                   development_notice.find("partial") != std::string::npos &&
                                   development_notice.find("nowhere") != std::string::npos &&
                                   development_notice.find("embedded font pack") != std::string::npos,
                               "a development lookup without a directory did not fall back to the pack, noted");

    // Nor does the pack open: no source, counted and refused.
    lookup.pack = gui_forms::detail::bundled_font_pack().first(16U);
    const BundledFontLocation missing = gui_forms::detail::locate_bundled_fonts(lookup);
    passed = passed && require(missing.source == BundledFontSource::none && missing.directory.empty(),
                               "a lookup without the faces named a source");
    const BundledFontDiagnostics after_missing = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_missing.lookups_without_source == before.lookups_without_source + 1U &&
                                   after_missing.last_source == BundledFontSource::none,
                               "a lookup without the faces was not counted");
    const std::string refusal = gui_forms::bundled_font_refusal();
    passed = passed && require(refusal.find("does not open") != std::string::npos &&
                                   refusal.find("source_tree (") != std::string::npos &&
                                   refusal.find("partial") != std::string::npos &&
                                   refusal.find("nowhere") != std::string::npos &&
                                   refusal.find("environment") == std::string::npos &&
                                   refusal.find("GUI_FORMS_FONT_DIR") != std::string::npos,
                               "the refusal does not name the pack, the directories tried and the remedy");
    gui_forms::Metrics metrics;
    passed = passed && require(metrics.snapshot().bundled_font_lookups_without_source ==
                                   after_missing.lookups_without_source,
                               "the metrics do not report the lookups without a source");
    passed = passed && require(metrics.snapshot().to_json().find("\"bundled_font_lookups_without_source\":") !=
                                   std::string::npos,
                               "the metrics JSON leaves out the lookups without a source");
    passed = passed && require(gui_forms::bundled_font_diagnostics().fallback_faces_found == 0U &&
                                   gui_forms::bundled_font_notice().empty(),
                               "a lookup without a source reported fallback faces or kept the notice");

    // With the fallback faces in the directory, every face is counted and nothing is noted.
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (!use.role && !use.required) write_file(complete / use.file_name, "face");
    }
    candidates[2].directory = complete;
    const BundledFontLocation again = gui_forms::detail::locate_bundled_fonts(lookup);
    passed = passed && require(again.source == BundledFontSource::source_tree &&
                                   gui_forms::bundled_font_refusal().empty(),
                               "a later successful lookup kept the old refusal");
    const BundledFontDiagnostics after_again = gui_forms::bundled_font_diagnostics();
    passed = passed && require(after_again.fallback_faces_found == fallback_total &&
                                   after_again.fallback_faces_expected == fallback_total &&
                                   gui_forms::bundled_font_notice().empty(),
                               "a directory with every fallback face was not counted as complete");
    passed = passed && require(gui_forms::bundled_font_source_name(BundledFontSource::source_tree) ==
                                       "source_tree" &&
                                   gui_forms::bundled_font_source_name(BundledFontSource::embedded_pack) ==
                                       "embedded_pack",
                               "a source is misnamed");
    std::filesystem::remove_all(root, status);
    return passed;
}

// The shared set for a location: the embedded pack's for embedded_pack, kept while held;
// none for no source.
bool shared_pack_set_is_shared() {
    BundledFontLocation embedded{};
    embedded.source = BundledFontSource::embedded_pack;
    const std::shared_ptr<BundledFontSet> first = gui_forms::detail::shared_bundled_font_set(embedded, counted_pages);
    const std::shared_ptr<BundledFontSet> second = gui_forms::detail::shared_bundled_font_set(embedded, counted_pages);
    const BundledFontLocation nowhere{};
    const bool passed = require(first && first == second &&
                                    !gui_forms::detail::shared_bundled_font_set(nowhere, counted_pages),
                                "the pack's shared set is not shared, or no source made a set");
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
    passed = registry_matches_the_face_uses() && passed;
    passed = family_names_resolve_bundled_faces_only() && passed;
    passed = nearest_faces_are_chosen() && passed;
    passed = family_stacks_register_and_order() && passed;
    passed = shared_set_is_shared() && passed;
    passed = pack_faces_decode_by_group() && passed;
    passed = refused_pack_is_reported() && passed;
    passed = shared_pack_set_is_shared() && passed;
    passed = font_directory_lookup() && passed;
    if (!passed) return 1;
    std::printf("bundled_font_set: manifest, loading, pack decoding, sharing, refusals, page release and font "
                "lookup passed\n");
    return 0;
}
