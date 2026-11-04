#pragma once

#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gui_forms {

class CompiledThemeRules;
class Theme;
class ThemeCompileResult;
class ThemeLibrary;
class Window;
struct ThemeCompileOptions;
struct ThemePackage;
struct UserThemeFiles;
struct UserThemeHeader;

// How a program admits user themes (T3 user themes, notes:planning/stages/
// T3-theme-language/USER_THEMES.md section 4.3).
enum class UserThemePolicy : std::uint8_t {
    // The program's theme is authoritative: a user theme's ordinary
    // declarations stand only where the program's declare nothing, and its
    // `!important` ones over the program's ordinary declarations, never
    // over the program's `!important` ones.
    layered,
    // A selected user theme takes the program theme's place whole; the
    // program's inline styles keep their order over it.
    replaces_program_sheet,
    // No user themes.
    refused,
};

// Whether a user theme may replace a program icon (T3 user themes,
// USER_THEMES.md section 6): `fixed` is the icon's counterpart of
// `!important`, for such as a security indicator or the icon of the
// settings entry leading back to the built-in theme.
enum class IconOverride : std::uint8_t { overridable, fixed };

// One program icon a user theme may name, by its identifier.
struct IconDeclaration final {
    std::string identifier{};
    IconOverride policy{IconOverride::overridable};
};

// Whether `identifier` is an icon identifier: segments of ASCII lowercase
// letters, digits and dashes, each beginning with a letter or digit,
// joined by `.` (as the locale keys), at most 128 bytes in all; for
// example `toolbar.save` or `check-mark`.
[[nodiscard]] bool icon_identifier_valid(std::string_view identifier) noexcept;

// The image registry and the theme of an application (stage DK decision 8),
// shared by every Window constructed with it. In those windows an ImageId
// names the same image, an ImageList belongs to all of them and a theme set
// on one is the theme of all, so a control subtree moved from one window to
// another keeps its images and its look. A Window constructed without one
// makes its own, so a single-window program behaves as before.
//
// Ownership: shared by the windows (and by anyone else holding it); the
// registry and its images live until the last holder releases it. Each
// Window registers itself while it lives and the list only observes them.
// Every member runs on the thread that constructed the object, which must be
// the windows' UI thread.
class ApplicationResources final {
public:
    explicit ApplicationResources(ImageRegistryLimits limits = {});
    ~ApplicationResources();
    ApplicationResources(const ApplicationResources&) = delete;
    ApplicationResources& operator=(const ApplicationResources&) = delete;

    [[nodiscard]] bool check_access() const noexcept {
        const bool same = std::this_thread::get_id() == ui_thread_;
        return same;
    }
    [[nodiscard]] const ImageRegistry& images() const noexcept { return images_; }
    [[nodiscard]] const Theme& theme() const noexcept { return *theme_; }
    [[nodiscard]] std::shared_ptr<const Theme> theme_ptr() const noexcept { return theme_; }
    // Replaces the theme of every window sharing these resources; each
    // remeasures and repaints, and publishes its theme_changed. The same as
    // Window::set_theme on any of them. Null is invalid_argument, and so is
    // a theme against which an inline style defining custom properties, of
    // a control taking the windows' theme, does not compile: each such
    // style is compiled against the new theme first, and nothing changes
    // where one fails (P12b).
    // A user theme (Theme with ThemeRuleSet::user) is invalid_argument.
    // While a user theme replaces the program's
    // (UserThemePolicy::replaces_program_sheet), the program's theme is
    // kept (program_theme) and the windows keep the user theme.
    void set_theme(std::shared_ptr<const Theme> theme);
    // The program's own theme: theme() but while a user theme replaces it.
    [[nodiscard]] const Theme& program_theme() const noexcept { return *program_theme_; }

    // Whether the windows follow the host's light or dark preference (T4
    // theme variants): off by default, so a program keeps the scheme it
    // sets in each window's PresentationSettings::colour_scheme. On, each
    // window takes the scheme its host reports (capability
    // presentation_preferences; light where the host reports none) now and
    // at each later report. A window whose theme has no variant for its
    // scheme shows GUI.Forms' own theme in it (Window::theme_fallback).
    void set_automatic_colour_scheme(bool automatic);
    [[nodiscard]] bool automatic_colour_scheme() const noexcept { return automatic_colour_scheme_; }
    // The host's colour hints as a window last reported them (T4 theme
    // variants), which compile_theme_package and compile_user_theme pass to
    // the sheets' env() where the options give none. Empty before any.
    [[nodiscard]] const SystemColourHints& system_colour_hints() const noexcept { return system_colour_hints_; }
    // Whether the warning W403 `theme_variant_missing` has been written for
    // `theme_id` in `scheme` by these resources' windows.
    [[nodiscard]] bool theme_variant_missing_reported(std::string_view theme_id, ColourScheme scheme) const noexcept;

    // User themes (T3), chosen before the program's first window: once a
    // window shares these resources, both are std::logic_error. The
    // default policy is layered.
    void set_user_theme_policy(UserThemePolicy policy);
    [[nodiscard]] UserThemePolicy user_theme_policy() const noexcept { return user_theme_policy_; }
    // Selects the user theme, or none for null. Under the layered policy it
    // must be compiled as a user theme (ThemeRuleSet::user), and every part
    // of these windows consults its rules beside its own theme's; under
    // replaces_program_sheet as a program theme, which becomes theme().
    // Anything else is std::invalid_argument, and under the refused policy
    // std::logic_error; nothing changes then.
    void set_user_theme(std::shared_ptr<const Theme> theme);
    // Compiles a user theme into these resources' registry
    // (compile_user_theme), at the levels the policy needs: a user theme's
    // under the layered policy, a program theme's under
    // replaces_program_sheet. `options.icons` is replaced by the icons
    // registered here. Selects nothing (set_user_theme does); the theme's
    // images live while it does. Under the refused policy std::logic_error.
    [[nodiscard]] ThemeCompileResult compile_user_theme(const UserThemeFiles& files, const ThemeLibrary& program,
                                                        const ThemeCompileOptions& options,
                                                        UserThemeHeader* header = nullptr);
    // Compiles a program theme package into these resources' registry
    // (compile_theme_package against `library`), so the theme's images are
    // images of every window sharing these resources. Selects nothing
    // (set_theme does); the theme's images live while it does, and a refusal
    // leaves none. On the UI thread (std::logic_error elsewhere).
    [[nodiscard]] ThemeCompileResult compile_theme_package(const ThemePackage& package, const ThemeLibrary& library,
                                                           const ThemeCompileOptions& options);
    // The selected user theme, or null.
    [[nodiscard]] const Theme* user_theme() const noexcept { return user_theme_.get(); }
    // The layered user theme's rules, or null where none is layered.
    [[nodiscard]] const CompiledThemeRules* user_theme_rules() const noexcept;

    // Program icons (T3 user themes). Registers `image`, an image of these
    // resources, as the icon `identifier`. std::invalid_argument when the
    // identifier is invalid or already registered, or the image is not
    // one of these resources'; nothing changes then. The image stays the
    // program's: removing it leaves icon() naming an image no longer there.
    void register_icon(std::string_view identifier, ImageId image, IconOverride policy);
    // The registered icons, in registration order, for a user theme's
    // compilation (ThemeCompileOptions::icons).
    [[nodiscard]] std::span<const IconDeclaration> icon_declarations() const noexcept { return icons_; }
    // The image to draw for the icon `identifier`: the selected user
    // theme's override of an overridable icon, else the program's image;
    // nullopt for an identifier not registered. A user theme is selected
    // before the first window, so a window resolves the same image for its
    // whole life.
    [[nodiscard]] std::optional<ImageId> icon(std::string_view identifier) const noexcept;
    // The program's own image for the icon `identifier`, whatever user
    // theme is selected; nullopt for an identifier not registered.
    [[nodiscard]] std::optional<ImageId> program_icon(std::string_view identifier) const noexcept;

    // The windows currently sharing these resources.
    [[nodiscard]] std::size_t window_count() const noexcept { return windows_.size(); }

    // Image operations, on the UI thread (std::logic_error elsewhere). Loads
    // change no window. A replacement or removal without a named consumer
    // repaints every sharing window, since any of them may show the image.
    [[nodiscard]] ImageLoadResult load_png(std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult load_bgra32_premultiplied(
        std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
        std::span<const std::byte> pixels);
    [[nodiscard]] ImageLoadResult replace_png(ImageId image, std::span<const std::byte> encoded);
    [[nodiscard]] bool remove_image(ImageId image);

private:
    friend class Window;
    friend class SurfaceRoot;
    void require_access(const char* operation) const;
    void attach(Window& window);
    void detach(Window& window) noexcept;
    // A window shows the fallback for `theme` in `scheme`: writes W403
    // to the log the first time for that theme and scheme.
    void note_theme_variant_missing(const Theme& theme, ColourScheme scheme);
    // A window's host reported `hints` (Window::set_compositor_state).
    void note_system_colour_hints(const SystemColourHints& hints) noexcept { system_colour_hints_ = hints; }
    // Window's consumer-scoped operations reach the registry directly.
    [[nodiscard]] ImageRegistry& registry() noexcept { return images_; }

    std::thread::id ui_thread_;
    ImageRegistry images_;
    std::shared_ptr<const Theme> theme_;
    // The program's theme, the same as theme_ but while a user theme
    // replaces it.
    std::shared_ptr<const Theme> program_theme_;
    std::shared_ptr<const Theme> user_theme_{};
    UserThemePolicy user_theme_policy_{UserThemePolicy::layered};
    bool automatic_colour_scheme_{};
    SystemColourHints system_colour_hints_{};
    // The (theme, scheme) pairs W403 was written for. Bounded by the themes
    // the program makes, two schemes each.
    struct VariantReport final {
        std::string theme_id{};
        ColourScheme scheme{ColourScheme::dark};
    };
    std::vector<VariantReport> variant_reports_{};
    // The registered icons and, at the same positions, their images.
    // Bounded by the icons the program registers.
    std::vector<IconDeclaration> icons_{};
    std::vector<ImageId> icon_images_{};
    // Observing: each Window attaches in its constructor and detaches in its
    // destructor. Bounded by the windows the program creates on this thread.
    std::vector<Window*> windows_;
};

} // namespace gui_forms
