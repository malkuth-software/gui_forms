#pragma once

// Theme packages compiled into themes (stage T3, PLAN.md P10): the one
// public seam of the theme language. A package is a `theme.json` manifest
// and the files it lists, given as bytes; nothing is read from a file
// system or a network. Compiling a package gives a Theme, or a refusal with
// its diagnostics; the caller applies the theme through the atomic
// Window::set_theme or ApplicationResources::set_theme, so a refused theme
// never half-applies and the running one stays (B2, RC003).

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {

class EffectRegistry;

// One file of a theme package: its path inside the package, as the
// manifest lists it, and its bytes.
struct ThemePackageFile final {
    std::string path{};
    std::string bytes{};
};

// A theme package: its manifest's bytes and its files.
struct ThemePackage final {
    std::string manifest{};
    std::vector<ThemePackageFile> files{};
};

enum class ThemeDiagnosticSeverity : std::uint8_t {
    // The theme is refused.
    error,
    // One item is dropped; the rest applies.
    warning,
};

// One finding of an install or a compilation.
struct ThemeCompileDiagnostic final {
    // As `E601` or `W106`.
    std::string code{};
    // As `manifest_invalid`.
    std::string name{};
    ThemeDiagnosticSeverity severity{ThemeDiagnosticSeverity::error};
    // The file at fault: `theme.json`, or a sheet as `<theme>/<path>`.
    std::string file{};
    // Counting from 1; 0 where the finding has no place in a file.
    std::uint32_t line{};
    std::uint32_t column{};
    // The rule and property, where the finding is inside one.
    std::string rule{};
    std::string property{};
    std::string detail{};
};

// The installed theme packages an `@import` may name (D2): each verified
// whole when installed (its manifest, every file's hash, and every
// `@import` of its sheet listed in its manifest), and found by its name.
class ThemeLibrary final {
public:
    ThemeLibrary();
    ~ThemeLibrary();
    ThemeLibrary(ThemeLibrary&&) noexcept;
    ThemeLibrary& operator=(ThemeLibrary&&) noexcept;
    ThemeLibrary(const ThemeLibrary&) = delete;
    ThemeLibrary& operator=(const ThemeLibrary&) = delete;

    // Verifies `package` and installs it, replacing an installed package of
    // its name. Returns the refusal's diagnostics, or none when installed;
    // a refused package changes nothing. A library holds at most 256
    // packages.
    [[nodiscard]] std::vector<ThemeCompileDiagnostic> install(const ThemePackage& package);
    // Removes the package named `name`; whether there was one.
    bool remove(std::string_view name) noexcept;
    [[nodiscard]] bool contains(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    friend struct ThemeLibraryAccess;
    struct State;
    std::unique_ptr<State> state_;
};

// The built-in theme a program theme is compiled onto (stage T4 phase 6):
// its recipes are the floor every rule the sheets leave unset falls back
// to.
// - windows_professional: the stock Windows-familiar theme,
//   `windows-professional` (owner, 2026-11-10): its floor's recipes, light
//   and authored dark, and its parts sheet (named
//   `windows-professional/parts.css` in diagnostics) placed first in the
//   cascade, so the package's sheets override it.
// - gui_forms: GUI.Forms' own theme, `gui-forms`: its recipes, and its light
//   sheet (gui_forms_light_sheet(), named `gui-forms/light.css` in
//   diagnostics) placed first in the cascade, so the package's sheets
//   override it and may read its colour roles; its dark variant is derived
//   by rule, so the package's colours, written in light terms, darken by the
//   same rule in a dark window.
enum class ThemeBase : std::uint8_t { windows_professional, gui_forms };

// The base of the process default theme (default_compiled_theme()), the
// one place that chooses it: gui-forms, as default_theme() (stage T4
// phase 6, owner decision 2026-11-08).
inline constexpr ThemeBase default_theme_base = ThemeBase::gui_forms;

// What a compilation reads besides the package.
struct ThemeCompileOptions final {
    // The control types a selector may name (the stock ones and the
    // application's own); a name outside them is a warning that drops its
    // rule.
    std::span<const ControlAnatomy> controls{};
    // The application values the application publishes; a package listing
    // another is refused (E606).
    std::span<const std::string> published_values{};
    // The program icons a user theme may replace (ApplicationResources::
    // icon_declarations); a package compiles no icons.
    std::span<const IconDeclaration> icons{};
    // The host's colour hints the sheets' env() reads (T4 theme variants),
    // as compiled: a theme compiled again reads new hints. Empty, every
    // env() reads its fallback. ApplicationResources::compile_theme_package
    // and compile_user_theme pass the resources' (system_colour_hints)
    // where these are empty.
    SystemColourHints system_colours{};
    // The theme the sheets compile onto: the process default's base
    // (default_theme_base, gui-forms) unless the caller chooses
    // windows_professional. With gui_forms, `controls` names the stock
    // control types too: the light sheet's rules for a type outside it are
    // warnings that drop them, named for `gui-forms/light.css`.
    ThemeBase base{default_theme_base};
    // The effects a sheet's `effect` may name (EP-3), borrowed for the call;
    // usually built_in_effect_registry(). A name it does not hold refuses
    // the package (E417, at the name). Null, only the name's form is
    // checked, and a control checks its effect when it attaches it.
    const EffectRegistry* effects{};
};

// What setting a control's inline style gave (Control::set_inline_style):
// applied, with any warnings (none for empty text, which clears the
// style), or refused with its diagnostics, the control keeping its
// previous inline style.
class InlineStyleResult final {
public:
    [[nodiscard]] static InlineStyleResult applied(std::vector<ThemeCompileDiagnostic> warnings) noexcept;
    [[nodiscard]] static InlineStyleResult refused(std::vector<ThemeCompileDiagnostic> diagnostics) noexcept;

    [[nodiscard]] bool succeeded() const noexcept;
    // The warnings of a style applied, or the diagnostics of one refused.
    [[nodiscard]] std::span<const ThemeCompileDiagnostic> diagnostics() const noexcept;

private:
    struct Applied final {
        std::vector<ThemeCompileDiagnostic> warnings{};
    };
    struct Refused final {
        std::vector<ThemeCompileDiagnostic> diagnostics{};
    };
    explicit InlineStyleResult(std::variant<Applied, Refused> state) noexcept;

    std::variant<Applied, Refused> state_;
};

// What compiling a theme package gave: compiled, a theme with any
// warnings, or refused with its diagnostics.
class ThemeCompileResult final {
public:
    [[nodiscard]] static ThemeCompileResult compiled(std::shared_ptr<const Theme> theme,
                                                     std::vector<ThemeCompileDiagnostic> warnings) noexcept;
    [[nodiscard]] static ThemeCompileResult refused(std::vector<ThemeCompileDiagnostic> diagnostics) noexcept;

    [[nodiscard]] bool succeeded() const noexcept;
    // The theme of a compilation that succeeded; null for a refusal.
    [[nodiscard]] std::shared_ptr<const Theme> theme() const noexcept;
    // The warnings of a compilation that succeeded, or the diagnostics of
    // one refused.
    [[nodiscard]] std::span<const ThemeCompileDiagnostic> diagnostics() const noexcept;

private:
    struct Compiled final {
        std::shared_ptr<const Theme> theme{};
        std::vector<ThemeCompileDiagnostic> warnings{};
    };
    struct Refused final {
        std::vector<ThemeCompileDiagnostic> diagnostics{};
    };
    explicit ThemeCompileResult(std::variant<Compiled, Refused> state) noexcept;

    std::variant<Compiled, Refused> state_;
};

// Compiles `package` with its `@import` closure among `library`'s packages
// (one cascade: imported sheets first, and the later definition of a
// colour role or `@keyframes` wins) onto the built-in professional theme.
// Its image resources are loaded into `images` and owned by the compiled
// theme: each is removed from `images` when the last theme holding it is
// released, so replacing or dropping a theme removes its images. Themes
// compiled through one library that load the same bytes into one
// registry share one image. A refusal leaves none of its own. The package
// itself need not be installed. Compiles through one library, and the
// release of their themes, are not safe to run concurrently.
[[nodiscard]] ThemeCompileResult compile_theme_package(const ThemePackage& package, const ThemeLibrary& library,
                                                       ImageRegistry& images, const ThemeCompileOptions& options);

// The most packages compile_theme_packages takes: 16, as many as one
// package's `@import` closure may hold.
inline constexpr std::size_t maximum_theme_packages = 16U;

// Compiles an application's theme (APP_WINDOW_RULES_DESIGN.md section A):
// `options.base`, then `packages` in their order, each with its `@import`
// closure among `library`'s packages, as one cascade, so a later package
// overrides an earlier one and may read its colour roles. A package
// another imports again keeps its earlier place. The theme is named after
// the last package, or is the base alone, under the base's id, when
// `packages` is empty. Every package is verified before any is compiled;
// two of one name, or more than maximum_theme_packages, are refused
// (E601), and the findings name each package's files. The combined sheets
// are bounded as one closure's are (1 MiB). Images are owned as
// compile_theme_package's are; compile_theme_package is the one-package
// case.
[[nodiscard]] ThemeCompileResult compile_theme_packages(std::span<const ThemePackage> packages,
                                                        const ThemeLibrary& library, ImageRegistry& images,
                                                        const ThemeCompileOptions& options);

// What validating theme packages found (validate_theme_packages): whether
// they compile, and every finding, warnings included.
struct ThemeValidation final {
    bool valid{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
};

// Checks `packages` as compile_theme_packages would compile them, keeping
// no theme and no image: for a test or a build step to catch a bad sheet
// before Application::create (gui_forms_add_theme_check in
// GUIFormsThemes.cmake runs it at build time). The images the sheets name
// are decoded into a registry of the call's own, released before it
// returns. Like compile_theme_package, not concurrently with another
// compilation through `library`.
[[nodiscard]] ThemeValidation validate_theme_packages(std::span<const ThemePackage> packages,
                                                      const ThemeLibrary& library,
                                                      const ThemeCompileOptions& options);

// One finding as one line, `<file>:<line>:<column>: <severity> <code>
// <name>: <detail>` (the place left out where the finding has none, then
// the rule and property where it is inside one), as the build check and a
// test print it.
[[nodiscard]] std::string format_theme_diagnostic(const ThemeCompileDiagnostic& diagnostic);

// The process default theme: the theme a window shows while no application
// is current, the compiled base default_theme_base names alone. Built on
// the first call (thread-safe) and never recompiled; every call returns
// the same theme.
[[nodiscard]] std::shared_ptr<const Theme> default_compiled_theme();

// A user theme's files (T3 user themes, notes:planning/stages/
// T3-theme-language/USER_THEMES.md): its name, the stem of its main file
// `themes/<name>.css`; that file's bytes; and the files of its folder
// `themes/<name>/`, each by its path inside the folder, as the host read
// them. The folder's top-level `.css` files are the theme's sub-sheets, its
// top-level `.png` files its image resources (`url("resource:wood.png")`),
// and `icons/<identifier>.png` its icon overrides; nothing else is admitted.
struct UserThemeFiles final {
    std::string name{};
    std::string main_sheet{};
    std::vector<ThemePackageFile> files{};
    // A sheet named on the command line or in GUI_FORMS_THEME_SHEET is a
    // theme by being named: its `@theme` header is implied (no engine
    // bound, version or author), and one it has is read as usual.
    bool header_implied{};
};

// A user theme's limits (USER_THEMES.md section 2): its folder holds at
// most 256 files and 16 MiB. Its sheets, with the packages they import,
// are one closure under the package limits (at most 16 imports, 1 MiB).
inline constexpr std::size_t maximum_user_theme_files = 256U;
inline constexpr std::size_t maximum_user_theme_bytes = std::size_t{16} * 1024U * 1024U;

// The header of a user theme's main file, which makes the file a theme.
struct UserThemeHeader final {
    // Whether the main file opens with one: a file without it is no theme.
    bool present{};
    std::string version{};
    std::string author{};
    std::string provenance{};
};

// Compiles a user theme as the package its files imply (USER_THEMES.md
// section 3): its sub-sheets in filename order (byte order of their names)
// and then its main file, one cascade; an `@import` of a sub-sheet
// (`@import "editor.css";`) places it there instead, and an `@import` of
// another name imports the program's package of that name from `program`,
// pinned to the installed one. The main file opens with an `@theme` header
// (refused without, E601); its `engine` descriptor bounds the engine
// (E607). `set` chooses the levels: ThemeRuleSet::user for a theme layered
// under the program's (ApplicationResources::set_user_theme under the
// layered policy), ThemeRuleSet::program for one replacing it. Images are
// owned as compile_theme_package's are. `header`, when given, receives the
// main file's header once it is read, whether or not the theme then
// compiles (present false where the main file has none or does not read).
[[nodiscard]] ThemeCompileResult compile_user_theme(const UserThemeFiles& files, const ThemeLibrary& program,
                                                    ImageRegistry& images, const ThemeCompileOptions& options,
                                                    ThemeRuleSet set, UserThemeHeader* header = nullptr);

// One of the program's packages as a user theme's files, for export
// (USER_THEMES.md section 7): its sheet verbatim after an `@theme` header
// of its manifest's version, author, provenance and engine bound, and its
// `.png` resources as the folder's images. A resource of another kind
// cannot be a user theme's file; it is left out and named in `omitted`.
struct ThemePackageExport final {
    UserThemeFiles files{};
    std::vector<std::string> omitted{};
};

// Every package of `library`, in name order, as export writes it.
[[nodiscard]] std::vector<ThemePackageExport> export_theme_packages(const ThemeLibrary& library);

// A theme package of one sheet: an application's theme as one plain CSS
// file, edited and read again at start-up without a rebuild. The manifest
// is made here, naming the package `name` and the sheet
// `sheet.theme.css` with its sha256 (gui-forms-theme/1, engine >=0.3,
// version 1.0.0). The name is checked when the package is compiled, as
// any manifest's is (E601); a quote, backslash or control character in it
// is escaped, so the manifest is always well-formed JSON.
[[nodiscard]] ThemePackage theme_sheet_package(std::string_view sheet, std::string_view name);

// As compile_theme_package, for the installed package named `name`; an
// unknown name is refused (E603).
[[nodiscard]] ThemeCompileResult compile_installed_theme(std::string_view name, const ThemeLibrary& library,
                                                         ImageRegistry& images, const ThemeCompileOptions& options);

} // namespace gui_forms
