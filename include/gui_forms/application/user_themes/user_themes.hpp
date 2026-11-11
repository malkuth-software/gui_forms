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

// -- The theme every program takes from its start (USER_THEMES.md 5, 8) -----
//
// Every GUI.Forms program, C++ or through the C ABI, takes a theme from the
// command line it was started with, with no code of its own:
//
//   --theme built-in        no user theme (the way back, never translated);
//   --theme <name>          the user theme of that name, from the folder
//                           beside the program or the per-user one;
//   --theme <path.css>      that sheet, read as a user theme.
//
// A value ending in `.css` (any case) or holding `/` or a backslash is a
// path; any other but `built-in` is a name. The environment variable
// GUI_FORMS_THEME_SHEET takes the same values, usually a sheet's path, for
// a program started without --theme. The command line wins over the environment, and both
// over the program's own choice (set_user_theme, such as its saved
// setting) for that run. The program's UserThemePolicy holds over all of
// them: refused, the request is ignored with a warning.
//
// The request is read when the application is made (Application::create,
// the C ABI's application.create, or the C ABI library's own resources)
// and settled when the first window is made with its resources, after the
// program has chosen its policy and theme. A request that cannot be met (no
// theme of that name, a file that does not read, a sheet that does not
// compile, a misformed command line) is never applied: its findings,
// `file:line:column: error code ...`, go to stderr and the report
// (ApplicationResources::theme_request_report), and the program runs on its
// own theme.
//
// The options stay in the process's arguments: the platform hands a
// program its argument vector before any library code runs, and GUI.Forms
// does not rewrite it. program_arguments gives the arguments without them,
// for a program that parses its own.

// The environment variable naming a theme sheet for a program started
// without --theme.
inline constexpr std::string_view theme_sheet_environment_variable = "GUI_FORMS_THEME_SHEET";

enum class ThemeRequestSource : std::uint8_t { none, command_line, environment };

enum class ThemeRequestKind : std::uint8_t {
    none,
    // `built-in`: no user theme.
    built_in,
    // A user theme by name.
    named,
    // A sheet by its path.
    sheet,
};

struct ThemeRequest final {
    ThemeRequestSource source{ThemeRequestSource::none};
    ThemeRequestKind kind{ThemeRequestKind::none};
    // The theme's name, or the sheet's path, as given.
    std::string value{};
    // Set when the command line's --theme cannot be read (no value, or
    // given twice); the request is then none, with source command_line.
    std::string error{};
};

// The request of `arguments` (after the program's own name) and of
// `environment_sheet`, the environment variable's value (empty where
// unset): the command line's --theme when given, else the environment's
// sheet.
[[nodiscard]] ThemeRequest read_theme_request(std::span<const std::string_view> arguments,
                                              std::string_view environment_sheet);

// The request the process was started with: read_theme_request over its
// own arguments and GUI_FORMS_THEME_SHEET.
[[nodiscard]] ThemeRequest process_theme_request();

// `arguments` without the user-theme options (`--theme <value>` and
// `--export-themes [folder]`), in order: what a program parses as its own.
// The views borrow `arguments`' text.
[[nodiscard]] std::vector<std::string_view> program_arguments(std::span<const std::string_view> arguments);

// The process's own arguments after its name, as UTF-8 (Windows: its
// command line split as the C runtime splits it; Linux:
// /proc/self/cmdline; macOS: the C runtime's argument vector). Empty where
// the platform gives none.
[[nodiscard]] std::vector<std::string> process_arguments();

// The folders a process reads user themes from: `themes` beside its
// executable, and the per-user folder of the program named by the
// executable's file name without its extension (per_user_theme_folder).
// Either is empty where the platform names none.
[[nodiscard]] UserThemeFolders process_theme_folders();

// What a request compiles against. Its library is shared, so a request
// held for the first window keeps what it needs alive.
struct ThemeRequestContext final {
    UserThemeFolders folders{};
    // The program's installed packages, which a user theme may `@import`.
    // Null: none.
    std::shared_ptr<const ThemeLibrary> library{};
    // The base a replacing theme is compiled onto (the program's).
    ThemeBase base{default_theme_base};
    // The control types a selector may name: the stock ones and the
    // program's own. Empty: the stock ones alone.
    std::vector<ControlAnatomy> controls{};
    // The program's published values.
    std::vector<std::string> published_values{};
    // The effects a theme's `effect` may name; it must outlive the request.
    // Null: only the names' form is checked.
    const EffectRegistry* effects{};
};

enum class ThemeRequestOutcome : std::uint8_t {
    // No request.
    none,
    // `built-in`: no user theme, the program's choice included.
    built_in,
    // The theme was compiled and selected.
    applied,
    // The program's policy refuses user themes; nothing changed.
    ignored_by_policy,
    // No user theme of that name was found; nothing changed.
    unknown_name,
    // The sheet, or the user theme of that name, does not compile; nothing
    // changed.
    refused,
    // The sheet's file does not read (missing, over 1 MiB, unreadable).
    unreadable,
    // The command line's --theme cannot be read; nothing changed.
    malformed,
};

// What a request did, kept by the resources it was settled on.
struct ThemeRequestReport final {
    ThemeRequest request{};
    ThemeRequestOutcome outcome{ThemeRequestOutcome::none};
    // The compile's findings: a refusal's diagnostics, or an applied
    // theme's warnings.
    std::vector<ThemeCompileDiagnostic> findings{};
    // The lines written to stderr, in order.
    std::vector<std::string> log{};
};

// A request held by resources until their first window
// (ApplicationResources::hold_theme_request).
struct DeferredThemeRequest final {
    ThemeRequest request{};
    ThemeRequestContext context{};
};

// Settles `request` on `resources` now, writing the report's log lines to
// stderr: under the refused policy, ignored with a warning; `built-in`
// selects no user theme; a name selects the theme of that name found in
// the context's folders; a sheet is read (at most 1 MiB) and compiled as a
// user theme whose header is implied (named `theme-sheet`), at the levels
// the policy needs, onto the context's base. Nothing is selected when the
// request cannot be met, so the program's own choice stands.
// std::logic_error once a window shares `resources` (as set_user_theme).
// UI thread.
[[nodiscard]] ThemeRequestReport apply_theme_request(ApplicationResources& resources, const ThemeRequest& request,
                                                     const ThemeRequestContext& context);

// Holds the process's request (process_theme_request) on `resources` with
// `context`, for their first window to settle; replaces a request held
// already. Nothing is held for a process started with none.
// std::logic_error once a window shares `resources`. UI thread.
void hold_process_theme_request(ApplicationResources& resources, ThemeRequestContext context);

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
