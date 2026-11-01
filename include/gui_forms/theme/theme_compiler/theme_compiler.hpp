#pragma once

// Theme packages compiled into themes (stage T3, PLAN.md P10): the one
// public seam of the theme language. A package is a `theme.json` manifest
// and the files it lists, given as bytes; nothing is read from a file
// system or a network. Compiling a package gives a Theme, or a refusal with
// its diagnostics; the caller applies the theme through the atomic
// Window::set_theme or ApplicationResources::set_theme, so a refused theme
// never half-applies and the running one stays (B2, RC003).

#include "gui_forms/anatomy/types/anatomy_types.hpp"
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

// What a compilation reads besides the package.
struct ThemeCompileOptions final {
    // The control types a selector may name (the stock ones and the
    // application's own); a name outside them is a warning that drops its
    // rule.
    std::span<const ControlAnatomy> controls{};
    // The application values the application publishes; a package listing
    // another is refused (E606).
    std::span<const std::string> published_values{};
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
};

// A user theme's limits (USER_THEMES.md section 2): its folder holds at
// most 256 files and 16 MiB. Its sheets, with the packages they import,
// are one closure under the package limits (at most 16 imports, 1 MiB).
inline constexpr std::size_t maximum_user_theme_files = 256U;
inline constexpr std::size_t maximum_user_theme_bytes = std::size_t{16} * 1024U * 1024U;

// The header of a user theme's main file, which makes the file a theme.
struct UserThemeHeader final {
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
// header of a compiled theme.
[[nodiscard]] ThemeCompileResult compile_user_theme(const UserThemeFiles& files, const ThemeLibrary& program,
                                                    ImageRegistry& images, const ThemeCompileOptions& options,
                                                    ThemeRuleSet set, UserThemeHeader* header = nullptr);

// As compile_theme_package, for the installed package named `name`; an
// unknown name is refused (E603).
[[nodiscard]] ThemeCompileResult compile_installed_theme(std::string_view name, const ThemeLibrary& library,
                                                         ImageRegistry& images, const ThemeCompileOptions& options);

} // namespace gui_forms
