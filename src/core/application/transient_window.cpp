// Warm transient windows (APP_WINDOW_RULES_DESIGN Part S). See
// transient_window.hpp.

#include "transient_window.hpp"

#include <cmath>
#include <string_view>
#include <utility>

namespace gui_forms::detail {

namespace {
bool desktop_rect_valid(const Rect desktop) noexcept {
    const double bound = HostWindowing::maximum_desktop_coordinate;
    const bool finite = std::isfinite(desktop.x) && std::isfinite(desktop.y) && std::isfinite(desktop.width) &&
                        std::isfinite(desktop.height);
    if (!finite) return false;
    const bool valid = desktop.width >= 1.0 && desktop.height >= 1.0 && std::abs(desktop.x) <= bound &&
                       std::abs(desktop.y) <= bound;
    return valid;
}

// The stable-ID segment of a transient window of `kind` (lowercase letters
// joined by '-', as stable IDs require; the host trace keeps
// host_transient_kind_name's spelling).
std::string_view transient_id_segment(const HostTransientKind kind) noexcept {
    switch (kind) {
    case HostTransientKind::tool_tip: return "tool-tip";
    case HostTransientKind::splashscreen: return "splashscreen";
    case HostTransientKind::popup:
    default: return "popup";
    }
}
} // namespace

TransientWindow::TransientWindow(std::shared_ptr<NativePopupRecord> record, Control::Ptr root,
                                 std::weak_ptr<ApplicationWindowState> owner) noexcept
    : record_(std::move(record)), root_(std::move(root)), owner_(std::move(owner)) {}

std::unique_ptr<TransientWindow> TransientWindow::open(const TransientWindowOpening& opening) {
    if (native_popup_refusal(opening.owner, opening.kind) != NativePopupRefusal::none) return nullptr;
    const Control::Ptr root = make_control<Control>(StableId(transient_id_segment(opening.kind)));
    // Only what the window shows takes input; its empty rest (the shadow
    // round a face) passes points to the window beneath.
    (*root).hit_test_transparent = true;
    NativePopupOpening popup{};
    popup.owner = opening.owner;
    popup.serial = opening.serial;
    popup.name = transient_id_segment(opening.kind);
    popup.content = root;
    popup.bounds = Rect{0.0, 0.0, 1.0, 1.0};
    popup.focus = HostPopupFocus::passive;
    popup.kind = opening.kind;
    popup.initially_visible = false;
    popup.content_surface = true;
    popup.key_bindings = opening.key_bindings;
    popup.resources = opening.resources;
    std::shared_ptr<NativePopupRecord> record = open_native_popup_window(popup);
    if (!record) return nullptr;
    if (const Window* const model = (*record).model()) note_popup_backdrop(opening.owner, *model);
    std::unique_ptr<TransientWindow> window(new TransientWindow(std::move(record), root, opening.owner));
    return std::move(window);
}

TransientWindow::~TransientWindow() {
    detach_content();
    // Destroys the host window if it is still open; its closed callback
    // raises closed() before this returns.
    (*record_).disconnect();
}

bool TransientWindow::connected() const noexcept {
    const bool open = (*record_).connected();
    return open;
}

Window* TransientWindow::model() const noexcept {
    Window* window = (*record_).model();
    return window;
}

HostWindowId TransientWindow::id() const noexcept {
    const HostWindowId window = (*record_).id();
    return window;
}

void TransientWindow::detach_content() noexcept {
    if (!content_) return;
    const Control::Ptr content = std::move(content_);
    content_.reset();
    if ((*content).parent() == root_) static_cast<void>((*root_).remove_child((*content).runtime_id()));
}

bool TransientWindow::show(const Control::Ptr& content, const Rect desktop) {
    if (!connected() || !content || !desktop_rect_valid(desktop)) return false;
    if (content != content_) {
        detach_content();
        const bool detached = (*content).is_alive() && !(*content).parent() &&
                              (*content).attached_surface_root() == nullptr;
        if (!detached) return false;
        (*content).set_requested_bounds(Rect{0.0, 0.0, desktop.width, desktop.height});
        (*root_).add_child(content);
        content_ = content;
    } else {
        (*content).set_requested_bounds(Rect{0.0, 0.0, desktop.width, desktop.height});
    }
    const ApplicationWindowHandle& handle = (*record_).handle();
    const bool framed = handle.request_frame(desktop).accepted();
    const bool shown = framed && handle.show().accepted();
    if (!shown) {
        static_cast<void>(handle.hide());
        shown_ = false;
        detach_content();
        return false;
    }
    shown_ = true;
    if (const Window* const model = (*record_).model()) note_popup_backdrop(owner_, *model);
    return true;
}

bool TransientWindow::move(const Rect desktop) {
    if (!shown_ || !content_ || !connected() || !desktop_rect_valid(desktop)) return false;
    (*content_).set_requested_bounds(Rect{0.0, 0.0, desktop.width, desktop.height});
    const ApplicationWindowHandle& handle = (*record_).handle();
    const bool framed = handle.request_frame(desktop).accepted();
    return framed;
}

void TransientWindow::hide() noexcept {
    if (!shown_) return;
    shown_ = false;
    if (connected()) static_cast<void>((*record_).handle().hide());
}

void TransientWindow::clear() noexcept {
    // Hidden first, so the emptied window is never presented.
    hide();
    detach_content();
}

} // namespace gui_forms::detail
