#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include "../../theme/theme_compiler/control_inline_style.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {
// Whether `window` is in `windows`.
bool listed(const std::vector<Window*>& windows, const Window* window) noexcept {
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if (windows[index] == window) return true;
    }
    return false;
}
} // namespace

ApplicationResources::ApplicationResources(ImageRegistryLimits limits)
    : ui_thread_(std::this_thread::get_id()), images_(limits), theme_(default_theme()) {}

ApplicationResources::~ApplicationResources() = default;

void ApplicationResources::require_access(const char* operation) const {
    if (check_access()) return;
    std::string message = "GUI.Forms application resources: ";
    message += operation;
    message += " off the UI thread";
    throw std::logic_error(message);
}

void ApplicationResources::attach(Window& window) {
    require_access("window attach");
    windows_.push_back(&window);
}

void ApplicationResources::detach(Window& window) noexcept {
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        if (windows_[index] != &window) continue;
        windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

void ApplicationResources::set_theme(std::shared_ptr<const Theme> theme) {
    require_access("theme mutation");
    if (!theme) throw std::invalid_argument("application theme may not be null");
    if (theme_ == theme) return;
    std::vector<detail::InlineRestyle> restyles{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    for (Window* const window : windows_) {
        if (!(*window).collect_inline_restyles(*theme, restyles, diagnostics)) {
            throw std::invalid_argument(detail::inline_restyle_refusal("application theme", diagnostics));
        }
    }
    Window::commit_inline_restyles(restyles);
    theme_ = std::move(theme);
    // A window's theme_changed handler may create or destroy windows; walk a
    // copy of the list and skip windows that left it meanwhile.
    const std::vector<Window*> windows = windows_;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if (listed(windows_, windows[index])) (*windows[index]).apply_theme(theme_);
    }
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
            (*windows_[index]).repaint_for_image_replacement();
        }
    }
    return result;
}

bool ApplicationResources::remove_image(ImageId image) {
    require_access("PNG resource removal");
    if (!images_.remove(image)) return false;
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        (*windows_[index]).repaint_for_image_removal();
    }
    return true;
}

} // namespace gui_forms
