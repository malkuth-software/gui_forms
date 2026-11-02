#pragma once

#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// User themes found at launch (T3 user themes, notes:planning/stages/
// T3-theme-language/USER_THEMES.md sections 2, 5 and 7): the two `themes/`
// folders a program reads, the theme chosen on its command line, and the
// export of its own packages as a starting point. The engine reads no file
// system; this application layer reads the folders and hands their files
// to ApplicationResources::compile_user_theme. Paths are UTF-8 text.
//
// The settings entries (the list of themes, "Built-in" first, the saved
// choice and "Export themes...") belong to the program's settings page,
// which uses scan_user_themes, select_user_theme and export_user_themes.

namespace gui_forms {

// Where a scan reads: the folder beside the program (read-only, shipped
// with it) and the user's own; an empty path is a folder not read.
struct UserThemeFolders final {
    std::string beside_program{};
    std::string per_user{};
};

// The per-user `themes` folder of `program` (a name, as `harbour-editor`)
// under the platform's configuration folder: on macOS
// `~/Library/Application Support/<program>/themes`, on Windows
// `%APPDATA%\<program>\themes`, elsewhere `$XDG_CONFIG_HOME/<program>/themes`
// (`~/.config` without it). Empty where the environment names none.
[[nodiscard]] std::string per_user_theme_folder(std::string_view program);

// At most 64 theme files (`*.css`) are read from each folder; later ones,
// in name order, are skipped with a log line.
inline constexpr std::size_t maximum_theme_folder_files = 64U;
// The word `--theme` takes for no user theme; no user theme has the name.
inline constexpr std::string_view built_in_theme_name = "built-in";

enum class UserThemeOrigin : std::uint8_t { beside_program, per_user };

// One theme found: compiled, or refused with its diagnostics (a list shows
// it greyed out).
struct UserThemeCandidate final {
    std::string name{};
    UserThemeOrigin origin{UserThemeOrigin::beside_program};
    // The main file's path.
    std::string file{};
    UserThemeHeader header{};
    // A compiled theme's warnings, or a refused one's diagnostics.
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    // Null for a refused theme. Holds the theme's images in the resources'
    // registry while it lives.
    std::shared_ptr<const Theme> theme{};
};

// What a scan found: the themes by name (where both folders hold one, the
// per-user one), and one line for each file passed over (no theme header,
// a name the rule refuses, a limit, a folder that could not be read).
struct UserThemeScan final {
    std::vector<UserThemeCandidate> themes{};
    std::vector<std::string> log{};
};

// Reads both folders and compiles each theme found into `resources`
// (ApplicationResources::compile_user_theme, so its policy and icons)
// against the program's packages. Every theme is compiled, so a scan holds
// the images of all of them until it is released; select one and release
// the scan. Under the refused policy, nothing is read and the scan is
// empty. The resources' UI thread only.
[[nodiscard]] UserThemeScan scan_user_themes(const UserThemeFolders& folders, ApplicationResources& resources,
                                             const ThemeLibrary& program, const ThemeCompileOptions& options);

// The theme a command line chooses: none given, `--theme built-in`, or
// `--theme <name>`.
enum class ThemeChoiceKind : std::uint8_t { unspecified, built_in, named };

struct ThemeChoice final {
    ThemeChoiceKind kind{ThemeChoiceKind::unspecified};
    std::string name{};
};

// The user-theme options of a command line (the arguments after the
// program's own name): `--theme <name>` and `--export-themes [folder]`,
// the folder empty for the per-user one. Other arguments are the
// program's and are passed over. `error` is set, and the rest left as
// unspecified, when `--theme` has no value or is given twice.
struct ThemeCommandLine final {
    ThemeChoice theme{};
    bool export_themes{};
    std::string export_folder{};
    std::string error{};
};

[[nodiscard]] ThemeCommandLine read_theme_command_line(std::span<const std::string_view> arguments);

enum class ThemeSelection : std::uint8_t {
    // No choice: nothing changes.
    unchanged,
    // The built-in choice: no user theme.
    built_in,
    selected,
    // No theme of that name was found; nothing changes.
    unknown,
    // The theme of that name was refused; nothing changes.
    refused,
};

// Applies `choice` to `resources` (ApplicationResources::set_user_theme,
// so before the first window) from the themes `scan` found.
[[nodiscard]] ThemeSelection select_user_theme(ApplicationResources& resources, const UserThemeScan& scan,
                                               const ThemeChoice& choice);

// What an export did: the files written, those already there (left as they
// were) and those that could not be written, by path; and the package
// resources and icons that a user theme cannot hold.
struct ThemeExportReport final {
    std::vector<std::string> written{};
    std::vector<std::string> skipped{};
    std::vector<std::string> failed{};
    std::vector<std::string> omitted{};
};

// Writes every package of `program` into `folder` as a user theme
// (export_theme_packages: `<name>.css`, its images in `<name>/`), every
// overridable icon of `resources` that is a PNG as
// `<name>/icons/<identifier>.png` of each, and a `README.txt`. Never
// overwrites a file.
[[nodiscard]] ThemeExportReport export_user_themes(const ThemeLibrary& program, const ApplicationResources& resources,
                                                   std::string_view folder);

} // namespace gui_forms
