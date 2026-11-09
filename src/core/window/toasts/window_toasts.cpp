#include "gui_forms/window.hpp"

#include "toast_presenter.hpp"

#include <stdexcept>

namespace gui_forms {

std::uint64_t Window::show_toast(const Control::Ptr& owner, const Control::Ptr& content,
                                 std::chrono::milliseconds duration) {
    require_root_thread("toast show");
    if (!owner || !(*owner).is_alive() || (*owner).attached_surface_root() != this ||
        !(*owner).effectively_visible()) {
        throw std::invalid_argument("GUI.Forms toast owner must be a live, visible control attached to its Window");
    }
    if (!content || !(*content).is_alive()) {
        throw std::invalid_argument("GUI.Forms toast content must be a live control");
    }
    if ((*content).parent() || (*content).attached_surface_root() != nullptr) {
        throw std::invalid_argument("GUI.Forms toast content may not have a parent or be in a window");
    }
    if (duration.count() < 0) {
        throw std::invalid_argument("GUI.Forms toast duration may not be negative");
    }
    const std::uint64_t id = (*toasts_).show(owner, content, duration);
    return id;
}

bool Window::dismiss_toast(const Control& content, ToastDismissReason reason) {
    require_root_thread("toast dismiss");
    const bool dismissed = (*toasts_).dismiss(content, reason);
    return dismissed;
}

void Window::hide_toast() {
    require_root_thread("toast hide");
    (*toasts_).hide();
}

bool Window::toast_visible() const noexcept {
    const bool visible = (*toasts_).visible();
    return visible;
}

Control::Ptr Window::toast_content() const noexcept {
    Control::Ptr content = (*toasts_).content();
    return content;
}

std::uint64_t Window::toast_id() const noexcept {
    const std::uint64_t id = (*toasts_).id();
    return id;
}

void Window::raise_toast_dismissed(const ToastDismissal& dismissal) {
    toast_dismissed_.emit(dismissal);
}

} // namespace gui_forms
