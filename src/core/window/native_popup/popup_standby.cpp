// Warm Popup windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S):
// Window's show_popup_window family and the standby that keeps them.

#include "popup_standby.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace detail {

void PopupStandby::SlotClosed::operator()() const {
    (*standby).slot_closed(slot);
}

TransientWindow* PopupStandby::slot_window(const std::size_t slot) {
    PopupSlot& entry = slots_[slot];
    if (entry.window && (*entry.window).connected()) return entry.window.get();
    if (native_popup_refusal(owner_.application_window_, HostTransientKind::popup) != NativePopupRefusal::none) {
        return nullptr;
    }
    entry.closed.disconnect();
    entry.window.reset();
    TransientWindowOpening opening{};
    opening.owner = owner_.application_window_;
    opening.kind = HostTransientKind::popup;
    serial_ = serial_ + 1U;
    opening.serial = serial_;
    opening.key_bindings = owner_.key_bindings();
    opening.resources = owner_.resources();
    entry.window = TransientWindow::open(opening);
    if (!entry.window) return nullptr;
    entry.closed = (*entry.window).closed().subscribe(SlotClosed{this, slot});
    return entry.window.get();
}

bool PopupStandby::show(const std::size_t slot, const Control::Ptr& content, const Rect bounds) {
    if (slot >= slots_.size()) return false;
    const std::optional<Rect> desktop = desktop_placement(owner_.application_window_, bounds, monitors_);
    if (!desktop.has_value()) return false;
    TransientWindow* window = slot_window(slot);
    if (window == nullptr) return false;
    const bool shown = (*window).show(content, *desktop);
    return shown;
}

bool PopupStandby::move(const std::size_t slot, const Rect bounds) {
    if (slot >= slots_.size() || !slots_[slot].window) return false;
    const std::optional<Rect> desktop = desktop_placement(owner_.application_window_, bounds, monitors_);
    if (!desktop.has_value()) return false;
    const bool moved = (*slots_[slot].window).move(*desktop);
    return moved;
}

void PopupStandby::hide(const std::size_t slot) noexcept {
    if (slot >= slots_.size() || !slots_[slot].window) return;
    (*slots_[slot].window).hide();
}

Window* PopupStandby::model(const std::size_t slot) const noexcept {
    if (slot >= slots_.size() || !slots_[slot].window) return nullptr;
    const TransientWindow& window = *slots_[slot].window;
    if (!window.connected() || !window.shown()) return nullptr;
    Window* const model = window.model();
    return model;
}

void PopupStandby::slot_closed(const std::size_t slot) {
    PopupSlot& entry = slots_[slot];
    const bool was_shown = entry.window && (*entry.window).shown();
    // The window object stays until the next show replaces it: this runs
    // inside its closed() emission.
    if (was_shown) closed_.emit(slot);
}

} // namespace detail

bool Window::popup_windows_available() const {
    const bool available =
        detail::native_popup_refusal(application_window_, HostTransientKind::popup) == NativePopupRefusal::none;
    return available;
}

bool Window::popup_windows_translucent() const {
    const bool translucent = detail::native_popup_translucent(application_window_);
    return translucent;
}

detail::PopupStandby& Window::popup_standby() {
    if (!popup_standby_) popup_standby_ = std::make_shared<detail::PopupStandby>(*this);
    return *popup_standby_;
}

bool Window::show_popup_window(const std::size_t slot, const Control::Ptr& content, const Rect bounds) {
    require_root_thread("popup window");
    if (slot >= maximum_popup_windows) throw std::out_of_range("GUI.Forms popup window slot is out of range");
    const bool shown = popup_standby().show(slot, content, bounds);
    return shown;
}

bool Window::move_popup_window(const std::size_t slot, const Rect bounds) {
    require_root_thread("popup window");
    if (!popup_standby_) return false;
    const bool moved = (*popup_standby_).move(slot, bounds);
    return moved;
}

void Window::hide_popup_window(const std::size_t slot) noexcept {
    if (popup_standby_) (*popup_standby_).hide(slot);
}

Window* Window::popup_window_model(const std::size_t slot) const noexcept {
    if (!popup_standby_) return nullptr;
    Window* const model = (*popup_standby_).model(slot);
    return model;
}

Event<std::size_t>& Window::popup_window_closed() {
    Event<std::size_t>& closed = popup_standby().closed();
    return closed;
}

} // namespace gui_forms
