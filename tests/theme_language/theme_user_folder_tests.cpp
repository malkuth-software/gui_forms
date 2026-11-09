// User themes found in their folders (T3 user themes, notes:planning/
// stages/T3-theme-language/USER_THEMES.md sections 2, 5 and 7): the scan of
// the two `themes/` folders, the per-user theme winning a name; the files
// passed over with a log line (no header, a refused name, `built-in`, the
// limits); a refused theme kept with its diagnostics; the command line;
// the selection; and the export, which never overwrites and whose themes
// compile. Works in a folder of its own under the system's temporary one.
#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A 2 x 2 opaque RGBA PNG.
const std::string tiny_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x02\x00\x00"
                           "\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00\x12\x49\x44\x41\x54\x78\x9c\x63"
                           "\xf8\xcf\xc0\xf0\x1f\x84\x19\xa0\xf4\x7f\x00\x43\xce\x07\xf9\xfa\x81\x4a\xfd\x00\x00\x00"
                           "\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                           75U);

constexpr std::array<PartDeclaration, 1> face_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = face_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};

ThemeCompileOptions options() {
    ThemeCompileOptions made{};
    made.controls = controls;
    // These expectations were written onto windows-professional's recipes;
    // the default base is gui-forms (T4 phase 6).
    made.base = ThemeBase::windows_professional;
    return made;
}

constexpr std::string_view header = "@theme { version: \"2.0.0\"; author: \"A. User\"; }\n";

std::string text_of(const std::filesystem::path& path) {
    const std::u8string units = path.generic_u8string();
    std::string made(reinterpret_cast<const char*>(units.data()), units.size());
    return made;
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "a fixture file is written");
}

bool logged(const UserThemeScan& scan, std::string_view part) {
    for (const std::string& line : scan.log) {
        if (line.find(part) != std::string::npos) return true;
    }
    for (const std::string& line : scan.log) std::cerr << "  log: " << line << '\n';
    return false;
}

const UserThemeCandidate* candidate(const UserThemeScan& scan, std::string_view name) {
    for (const UserThemeCandidate& found : scan.themes) {
        if (found.name == name) return &found;
    }
    return nullptr;
}

bool listed(std::span<const std::string> paths, std::string_view ending) {
    for (const std::string& path : paths) {
        if (path.ends_with(ending)) return true;
    }
    return false;
}

// The program's package `harbour`: a sheet drawing its image `wood.png`.
ThemePackage harbour_package() {
    const std::string sheet = "button::part(face) { background-image: url(\"resource:wood.png\"); opacity: 0.8 }";
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"harbour\", \"version\": \"1.0.0\", \"engine\": "
                    "\">=0.3\", \"author\": \"The program\", \"sheet\": {\"path\": \"theme.css\", \"sha256\": \"" +
                    theme_language::package_sha256_hex(theme_language::package_sha256(sheet)) +
                    "\"}, \"resources\": [{\"name\": \"wood.png\", \"path\": \"wood.png\", \"sha256\": \"" +
                    theme_language::package_sha256_hex(theme_language::package_sha256(tiny_png)) + "\"}]}";
    made.files.push_back(ThemePackageFile{"theme.css", sheet});
    made.files.push_back(ThemePackageFile{"wood.png", tiny_png});
    return made;
}

void test_folder_path() {
    const std::string folder = per_user_theme_folder("harbour-editor");
    require(folder.empty() || folder.ends_with("harbour-editor/themes"), "the per-user folder is the program's");
}

void test_command_line() {
    const std::array<std::string_view, 4> named{"--size", "--theme", "dusk", "--automation"};
    const ThemeCommandLine dusk = read_theme_command_line(named);
    require(dusk.error.empty() && dusk.theme.kind == ThemeChoiceKind::named && dusk.theme.name == "dusk",
            "--theme <name>");
    const std::array<std::string_view, 2> built_in{"--theme", "built-in"};
    require(read_theme_command_line(built_in).theme.kind == ThemeChoiceKind::built_in, "--theme built-in");
    const std::array<std::string_view, 1> bare{"--theme"};
    require(!read_theme_command_line(bare).error.empty(), "--theme without a value is an error");
    const std::array<std::string_view, 4> twice{"--theme", "a", "--theme", "b"};
    require(!read_theme_command_line(twice).error.empty(), "--theme twice is an error");
    const std::array<std::string_view, 2> export_here{"--export-themes", "/somewhere"};
    const ThemeCommandLine exported = read_theme_command_line(export_here);
    require(exported.export_themes && exported.export_folder == "/somewhere", "--export-themes <folder>");
    const std::array<std::string_view, 2> export_default{"--export-themes", "--smoke-test"};
    const ThemeCommandLine plain = read_theme_command_line(export_default);
    require(plain.export_themes && plain.export_folder.empty() && plain.theme.kind == ThemeChoiceKind::unspecified,
            "--export-themes alone exports to the per-user folder");
}

void test_scan(const std::filesystem::path& root) {
    const std::filesystem::path beside = root / "beside";
    const std::filesystem::path user = root / "user";
    write_file(beside / "harbour.css", std::string(header) + "button { opacity: 0.7 }");
    write_file(beside / "plain.css", "button { opacity: 0.7 }");
    write_file(beside / "Bad_Name.css", std::string(header));
    write_file(beside / "built-in.css", std::string(header));
    write_file(beside / "dusk.css", std::string(header) + "button { opacity: }");
    write_file(user / "harbour.css",
               std::string(header) + "button::part(face) { background-image: url(\"resource:wood.png\") }");
    write_file(user / "harbour" / "wood.png", tiny_png);
    write_file(user / "harbour" / "icons" / "toolbar.save.png", tiny_png);
    for (std::size_t index = 0U; index <= maximum_user_theme_files; ++index) {
        write_file(user / "crowded" / ("i" + std::to_string(index) + ".png"), tiny_png);
    }
    write_file(user / "crowded.css", std::string(header));

    ApplicationResources resources{};
    const ImageLoadResult save = resources.load_png(std::as_bytes(std::span<const char>(tiny_png)));
    resources.register_icon("toolbar.save", save.image, IconOverride::overridable);
    const ThemeLibrary library{};
    const UserThemeFolders folders{.beside_program = text_of(beside), .per_user = text_of(user)};
    const UserThemeScan scan = scan_user_themes(folders, resources, library, options());

    require(scan.themes.size() == 2U, "two themes are found");
    const UserThemeCandidate* const harbour = candidate(scan, "harbour");
    require(harbour != nullptr && (*harbour).origin == UserThemeOrigin::per_user && (*harbour).theme != nullptr,
            "the per-user theme wins its name and compiles");
    require(harbour != nullptr && (*harbour).header.version == "2.0.0" && (*harbour).header.author == "A. User",
            "the header is read");
    require(harbour != nullptr && (*harbour).theme != nullptr && (*(*harbour).theme).icon_override("toolbar.save"),
            "the theme's icon is read from its folder");
    const UserThemeCandidate* const dusk = candidate(scan, "dusk");
    require(dusk != nullptr && (*dusk).theme == nullptr && !(*dusk).diagnostics.empty(),
            "a refused theme is kept with its diagnostics");
    require(logged(scan, "plain.css: not a theme"), "a file without a header is not a theme");
    require(logged(scan, "Bad_Name.css: skipped"), "a name breaking the rule is skipped");
    require(logged(scan, "built-in.css: skipped"), "built-in names no theme");
    require(logged(scan, "replaced by"), "the per-user theme replaces the one beside the program");
    require(logged(scan, "crowded: skipped"), "a theme folder over 256 files is skipped");

    require(select_user_theme(resources, scan, ThemeChoice{}) == ThemeSelection::unchanged, "no choice");
    const ThemeChoice unknown{.kind = ThemeChoiceKind::named, .name = "nothing"};
    require(select_user_theme(resources, scan, unknown) == ThemeSelection::unknown, "an unknown theme");
    const ThemeChoice refused{.kind = ThemeChoiceKind::named, .name = "dusk"};
    require(select_user_theme(resources, scan, refused) == ThemeSelection::refused &&
                resources.user_theme() == nullptr,
            "a refused theme is not selected");
    const ThemeChoice chosen{.kind = ThemeChoiceKind::named, .name = "harbour"};
    require(select_user_theme(resources, scan, chosen) == ThemeSelection::selected && harbour != nullptr &&
                resources.user_theme() == (*harbour).theme.get(),
            "a theme is selected by name");
    require(resources.icon("toolbar.save") != save.image, "the selected theme's icon is drawn");
    const ThemeChoice back{.kind = ThemeChoiceKind::built_in, .name = {}};
    require(select_user_theme(resources, scan, back) == ThemeSelection::built_in &&
                resources.user_theme() == nullptr,
            "built-in selects no user theme");

    ApplicationResources refusing{};
    refusing.set_user_theme_policy(UserThemePolicy::refused);
    require(scan_user_themes(folders, refusing, library, options()).themes.empty(),
            "a program refusing user themes reads none");
}

void test_folder_limit(const std::filesystem::path& root) {
    const std::filesystem::path many = root / "many";
    for (std::size_t index = 0U; index <= maximum_theme_folder_files; ++index) {
        const std::string number = std::to_string(100U + index);
        write_file(many / ("t" + number + ".css"), std::string(header));
    }
    ApplicationResources resources{};
    const ThemeLibrary library{};
    const UserThemeFolders folders{.beside_program = text_of(many), .per_user = {}};
    const UserThemeScan scan = scan_user_themes(folders, resources, library, options());
    require(scan.themes.size() == maximum_theme_folder_files, "a folder gives at most 64 themes");
    require(logged(scan, "t164.css: skipped"), "the 65th theme file is skipped with a log line");
}

void test_export(const std::filesystem::path& root) {
    const std::filesystem::path folder = root / "export";
    ThemeLibrary library{};
    require(library.install(harbour_package()).empty(), "the program's package is installed");
    ApplicationResources resources{};
    const ImageLoadResult save = resources.load_png(std::as_bytes(std::span<const char>(tiny_png)));
    const ImageLoadResult shield = resources.load_png(std::as_bytes(std::span<const char>(tiny_png)));
    resources.register_icon("toolbar.save", save.image, IconOverride::overridable);
    resources.register_icon("security.shield", shield.image, IconOverride::fixed);

    const ThemeExportReport first = export_user_themes(library, resources, text_of(folder));
    require(first.failed.empty() && first.skipped.empty() && first.omitted.empty(), "the export writes everything");
    require(listed(first.written, "export/harbour.css") && listed(first.written, "export/harbour/wood.png") &&
                listed(first.written, "export/harbour/icons/toolbar.save.png") &&
                listed(first.written, "export/README.txt"),
            "the sheet, its image, the overridable icon and the README are written");
    require(!listed(first.written, "security.shield.png"), "a fixed icon is not exported");
    const ThemeExportReport second = export_user_themes(library, resources, text_of(folder));
    require(second.written.empty() && second.skipped.size() == first.written.size(), "the export never overwrites");

    // The exported theme compiles as a user theme over the program's.
    const ThemeLibrary none{};
    ApplicationResources reading{};
    const UserThemeFolders folders{.beside_program = {}, .per_user = text_of(folder)};
    const UserThemeScan scan = scan_user_themes(folders, reading, none, options());
    const UserThemeCandidate* const harbour = candidate(scan, "harbour");
    require(harbour != nullptr && (*harbour).theme != nullptr && (*harbour).header.author == "The program",
            "the exported theme compiles with its package's header");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("gui_forms_user_folder_tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error{};
    std::filesystem::create_directories(root, error);
    require(!error, "the test's folder is made");
    test_folder_path();
    test_command_line();
    test_scan(root);
    test_folder_limit(root);
    test_export(root);
    // The test's own folder, which it made above.
    std::filesystem::remove_all(root, error);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
