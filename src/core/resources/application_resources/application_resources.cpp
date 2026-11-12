#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/application/user_themes/user_themes.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window.hpp"

#include "../../theme/theme_compiler/control_inline_style.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../surface_root/access/surface_resource_access.hpp"

#include <cstdio>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {
// Whether `window` is in `windows`.
bool listed(const std::vector<SurfaceRoot*>& windows, const SurfaceRoot* window) noexcept {
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if (windows[index] == window) return true;
    }
    return false;
}
} // namespace

ApplicationResources::ApplicationResources(ImageRegistryLimits limits)
    : ui_thread_(std::this_thread::get_id()),
      images_(limits),
      theme_(default_theme()),
      program_theme_(theme_) {}

ApplicationResources::~ApplicationResources() = default;

void ApplicationResources::require_access(const char* operation) const {
    if (check_access()) return;
    std::string message = "GUI.Forms application resources: ";
    message += operation;
    message += " off the UI thread";
    throw std::logic_error(message);
}

void ApplicationResources::attach(SurfaceRoot& window) {
    require_access("window attach");
    windows_.push_back(&window);
}

void ApplicationResources::detach(SurfaceRoot& window) noexcept {
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        if (windows_[index] != &window) continue;
        windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

void ApplicationResources::set_theme(std::shared_ptr<const Theme> theme) {
    require_access("theme mutation");
    if (!theme) throw std::invalid_argument("application theme may not be null");
    if ((*theme).definition().rule_set != ThemeRuleSet::program) {
        throw std::invalid_argument("a user theme is not an application theme");
    }
    // A user theme replacing the program's stays in force.
    if (user_theme_ != nullptr && user_theme_policy_ == UserThemePolicy::replaces_program_sheet) {
        program_theme_ = std::move(theme);
        return;
    }
    if (theme_ == theme) return;
    std::vector<detail::InlineRestyle> restyles{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    for (SurfaceRoot* const window : windows_) {
        if (!detail::SurfaceResourceAccess::collect_inline_restyles(*window, *theme, restyles, diagnostics)) {
            throw std::invalid_argument(detail::inline_restyle_refusal("application theme", diagnostics));
        }
    }
    detail::SurfaceResourceAccess::commit_inline_restyles(restyles);
    theme_ = std::move(theme);
    program_theme_ = theme_;
    // A window's theme_changed handler may create or destroy windows; walk a
    // copy of the list and skip windows that left it meanwhile.
    const std::vector<SurfaceRoot*> windows = windows_;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if (listed(windows_, windows[index])) detail::SurfaceResourceAccess::apply_theme(*windows[index], theme_);
    }
}

void ApplicationResources::set_automatic_colour_scheme(bool automatic) {
    require_access("automatic colour scheme");
    if (automatic_colour_scheme_ == automatic) return;
    automatic_colour_scheme_ = automatic;
    // A handler of a window's change may create or destroy windows.
    const std::vector<SurfaceRoot*> windows = windows_;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if (listed(windows_, windows[index])) detail::SurfaceResourceAccess::follow_host_colour_scheme(*windows[index], automatic);
    }
}

bool ApplicationResources::theme_variant_missing_reported(std::string_view theme_id,
                                                          ColourScheme scheme) const noexcept {
    for (const VariantReport& report : variant_reports_) {
        if (report.theme_id == theme_id && report.scheme == scheme) return true;
    }
    return false;
}

void ApplicationResources::note_theme_variant_missing(const Theme& theme, ColourScheme scheme) {
    if (theme_variant_missing_reported(theme.id(), scheme)) return;
    variant_reports_.push_back(VariantReport{.theme_id = std::string(theme.id()), .scheme = scheme});
    theme_language::ThemeDiagnostic warning{};
    warning.code = theme_language::DiagnosticCode::theme_variant_missing;
    warning.file = "theme:" + std::string(theme.id());
    warning.detail = std::string(colour_scheme_name(scheme)) + "; shown in gui-forms " +
                     std::string(colour_scheme_name(scheme));
    theme_language::ThemeDiagnosticList list{};
    list.add(std::move(warning));
    theme_language::write_theme_diagnostics(list, stderr);
}

void ApplicationResources::set_user_theme_policy(UserThemePolicy policy) {
    require_access("user theme policy");
    if (!windows_.empty()) throw std::logic_error("the user theme policy is chosen before the first window");
    // A theme selected under another policy was compiled for that one.
    if (policy != user_theme_policy_) {
        user_theme_.reset();
        theme_ = program_theme_;
    }
    user_theme_policy_ = policy;
}

void ApplicationResources::set_user_theme(std::shared_ptr<const Theme> theme) {
    require_access("user theme selection");
    if (!windows_.empty()) throw std::logic_error("a user theme is chosen before the first window");
    if (user_theme_policy_ == UserThemePolicy::refused) throw std::logic_error("this program refuses user themes");
    const bool replaces = user_theme_policy_ == UserThemePolicy::replaces_program_sheet;
    if (theme != nullptr) {
        const ThemeRuleSet wanted = replaces ? ThemeRuleSet::program : ThemeRuleSet::user;
        if ((*theme).definition().rule_set != wanted) {
            throw std::invalid_argument(replaces ? "a replacing user theme is compiled as a program theme"
                                                 : "a layered user theme is compiled as a user theme");
        }
    }
    user_theme_ = std::move(theme);
    theme_ = replaces && user_theme_ != nullptr ? user_theme_ : program_theme_;
}

void ApplicationResources::hold_theme_request(std::unique_ptr<DeferredThemeRequest> request) {
    require_access("theme request");
    if (!windows_.empty()) throw std::logic_error("a theme request is held before the first window");
    held_theme_request_ = std::move(request);
}

void ApplicationResources::settle_theme_request() {
    require_access("theme request");
    if (held_theme_request_ == nullptr || !windows_.empty()) return;
    // Released first: a request that throws is not settled twice.
    const std::unique_ptr<DeferredThemeRequest> held = std::move(held_theme_request_);
    ThemeRequestReport report = apply_theme_request(*this, (*held).request, (*held).context);
    theme_request_report_ = std::make_unique<ThemeRequestReport>(std::move(report));
}

bool icon_identifier_valid(std::string_view identifier) noexcept {
    constexpr std::size_t maximum_icon_identifier_bytes = 128U;
    if (identifier.empty() || identifier.size() > maximum_icon_identifier_bytes) return false;
    bool segment_start = true;
    for (const char character : identifier) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (character == '.') {
            if (segment_start) return false;
            segment_start = true;
            continue;
        }
        if (character == '-' && segment_start) return false;
        if (!lower && !digit && character != '-') return false;
        segment_start = false;
    }
    const bool ended = !segment_start;
    return ended;
}

void ApplicationResources::register_icon(std::string_view identifier, ImageId image, IconOverride policy) {
    require_access("icon registration");
    if (!icon_identifier_valid(identifier)) throw std::invalid_argument("not an icon identifier");
    for (const IconDeclaration& declared : icons_) {
        if (declared.identifier == identifier) throw std::invalid_argument("the icon is already registered");
    }
    if (!images_.find(image)) throw std::invalid_argument("the icon's image is not one of these resources'");
    // Both reserved first, so that neither push_back can throw between
    // them.
    icons_.reserve(icons_.size() + 1U);
    icon_images_.reserve(icon_images_.size() + 1U);
    icons_.push_back(IconDeclaration{.identifier = std::string(identifier), .policy = policy});
    icon_images_.push_back(image);
}

std::optional<ImageId> ApplicationResources::icon(std::string_view identifier) const noexcept {
    for (std::size_t index = 0U; index < icons_.size(); ++index) {
        if (icons_[index].identifier != identifier) continue;
        if (icons_[index].policy == IconOverride::overridable && user_theme_ != nullptr) {
            const std::optional<ImageId> replaced = (*user_theme_).icon_override(identifier);
            if (replaced) return replaced;
        }
        return icon_images_[index];
    }
    return std::nullopt;
}

std::optional<ImageId> ApplicationResources::program_icon(std::string_view identifier) const noexcept {
    for (std::size_t index = 0U; index < icons_.size(); ++index) {
        if (icons_[index].identifier == identifier) return icon_images_[index];
    }
    return std::nullopt;
}

ThemeCompileResult ApplicationResources::compile_user_theme(const UserThemeFiles& files, const ThemeLibrary& program,
                                                           const ThemeCompileOptions& options,
                                                           UserThemeHeader* header) {
    require_access("user theme compilation");
    if (user_theme_policy_ == UserThemePolicy::refused) throw std::logic_error("this program refuses user themes");
    const ThemeRuleSet set =
        user_theme_policy_ == UserThemePolicy::replaces_program_sheet ? ThemeRuleSet::program : ThemeRuleSet::user;
    ThemeCompileOptions with_icons = options;
    with_icons.icons = icons_;
    if (with_icons.system_colours == SystemColourHints{}) with_icons.system_colours = system_colour_hints_;
    ThemeCompileResult result = gui_forms::compile_user_theme(files, program, images_, with_icons, set, header);
    return result;
}

ThemeCompileResult ApplicationResources::compile_theme_package(const ThemePackage& package, const ThemeLibrary& library,
                                                              const ThemeCompileOptions& options) {
    require_access("theme package compilation");
    if (options.system_colours == SystemColourHints{} && system_colour_hints_ != SystemColourHints{}) {
        ThemeCompileOptions hinted = options;
        hinted.system_colours = system_colour_hints_;
        ThemeCompileResult result = gui_forms::compile_theme_package(package, library, images_, hinted);
        return result;
    }
    ThemeCompileResult result = gui_forms::compile_theme_package(package, library, images_, options);
    return result;
}

ThemeCompileResult ApplicationResources::compile_theme_packages(std::span<const ThemePackage> packages,
                                                               const ThemeLibrary& library,
                                                               const ThemeCompileOptions& options) {
    require_access("theme package compilation");
    if (options.system_colours == SystemColourHints{} && system_colour_hints_ != SystemColourHints{}) {
        ThemeCompileOptions hinted = options;
        hinted.system_colours = system_colour_hints_;
        ThemeCompileResult result = gui_forms::compile_theme_packages(packages, library, images_, hinted);
        return result;
    }
    ThemeCompileResult result = gui_forms::compile_theme_packages(packages, library, images_, options);
    return result;
}

const CompiledThemeRules* ApplicationResources::user_theme_rules() const noexcept {
    if (user_theme_ == nullptr || user_theme_policy_ != UserThemePolicy::layered) return nullptr;
    return &(*user_theme_).engine().rules();
}

ImageLoadResult ApplicationResources::load_png(std::span<const std::byte> encoded) {
    require_access("PNG resource load");
    ImageLoadResult result = images_.load_png(encoded);
    return result;
}

ImageLoadResult ApplicationResources::load_bgra32_premultiplied(
    std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
    std::span<const std::byte> pixels) {
    require_access("BGRA resource load");
    ImageLoadResult result = images_.load_bgra32_premultiplied(width, height, row_bytes, pixels);
    return result;
}

ImageLoadResult ApplicationResources::replace_png(ImageId image, std::span<const std::byte> encoded) {
    require_access("PNG resource replacement");
    ImageLoadResult result = images_.replace_png(image, encoded);
    if (result) {
        for (std::size_t index = 0U; index < windows_.size(); ++index) {
            detail::SurfaceResourceAccess::repaint_for_image_replacement(*windows_[index]);
        }
    }
    return result;
}

bool ApplicationResources::remove_image(ImageId image) {
    require_access("PNG resource removal");
    if (!images_.remove(image)) return false;
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        detail::SurfaceResourceAccess::repaint_for_image_removal(*windows_[index]);
    }
    return true;
}

} // namespace gui_forms
