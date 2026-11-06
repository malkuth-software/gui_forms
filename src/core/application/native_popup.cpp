// Native popups (owner, 2026-11-04): the record of one popup window and
// its opening through the run's windowing. See native_popup.hpp.
#include "native_popup_record.hpp"

#include "application_state.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace gui_forms::detail {

NativePopupRecord::NativePopupRecord(Window* model) noexcept : model_(model) {}

void NativePopupRecord::bind(ApplicationWindowHandle window) noexcept {
    window_ = std::move(window);
}

void NativePopupRecord::notify_closed() {
    if (window_closed_) return;
    window_closed_ = true;
    closed_.emit();
}

void NativePopupClosed::operator()() const {
    const std::shared_ptr<NativePopupRecord> alive = record.lock();
    if (alive) (*alive).notify_closed();
}

void NativePopupRecord::disconnect() noexcept {
    if (window_closed_ || !window_.active()) return;
    const ApplicationWindows windows = window_.windows();
    // A refusal (the run ended, or the window is gone already) leaves
    // nothing to close.
    static_cast<void>(windows.destroy_window(window_));
}

bool NativePopupRecord::connected() const noexcept {
    const bool open = !window_closed_ && window_.active();
    return open;
}

Window* NativePopupRecord::model() const noexcept {
    if (!connected()) return nullptr;
    return model_;
}

HostWindowId NativePopupRecord::id() const noexcept {
    if (!connected()) return HostWindowId{};
    const HostWindowId window = window_.id();
    return window;
}

namespace {
// The owner's run while the owner is a ready, open window of it; else null.
std::shared_ptr<ApplicationRunState> owner_run(const std::shared_ptr<ApplicationWindowState>& owner) noexcept {
    if (!owner || !(*owner).ready || (*owner).closed) return nullptr;
    std::shared_ptr<ApplicationRunState> run = (*owner).run.lock();
    if (!run || (*run).closed || (*run).windowing == nullptr) return nullptr;
    return run;
}

// The work area of the monitor under `point`, else of the first monitor;
// none when the host reports no monitors.
std::optional<Rect> work_area_at(const HostMonitorResult& monitors, Point point) noexcept {
    if (!monitors.status.accepted() || monitors.monitors.empty()) return std::nullopt;
    for (std::size_t index = 0U; index < monitors.monitors.size(); ++index) {
        if (monitors.monitors[index].frame.contains(point)) return monitors.monitors[index].work_area;
    }
    return monitors.monitors.front().work_area;
}
} // namespace

NativePopupRefusal native_popup_refusal(const std::weak_ptr<ApplicationWindowState>& owner) noexcept {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run || !(*state).id.valid()) return NativePopupRefusal::no_run;
    const HostWindowing& windowing = *(*run).windowing;
    const bool available = windowing.capabilities().supports(HostCapability::runtime_windows) &&
                           windowing.supports_popup_windows();
    if (!available) return NativePopupRefusal::unsupported;
    return NativePopupRefusal::none;
}

std::shared_ptr<NativePopupRecord> open_native_popup_window(const NativePopupOpening& opening) {
    if (native_popup_refusal(opening.owner) != NativePopupRefusal::none) return nullptr;
    const std::shared_ptr<ApplicationWindowState> owner = opening.owner.lock();
    const ApplicationWindowHandle owner_handle = ApplicationHandleAccess::make(owner);
    const HostWindowFrameResult owner_frame = owner_handle.frame();
    if (!owner_frame.status.accepted()) return nullptr;

    // Placement: the bounds in the owner's client area, as desktop logical
    // units, kept inside the work area under the popup's origin.
    const Rect client = owner_frame.frame.content;
    Rect placed{client.x + opening.bounds.x, client.y + opening.bounds.y, opening.bounds.width,
                opening.bounds.height};
    const ApplicationWindows windows = owner_handle.windows();
    const HostMonitorResult monitors = windows.monitors();
    const std::optional<Rect> work_area = work_area_at(monitors, Point{placed.x, placed.y});
    if (work_area.has_value()) placed = clamp_to_work_area(placed, *work_area);

    const Size size{opening.bounds.width, opening.bounds.height};
    std::unique_ptr<Window> model = std::make_unique<Window>(opening.content, size, opening.resources);
    Window* borrowed = model.get();
    (*model).set_key_bindings(opening.key_bindings);
    // A popup's clear pixels show what lies beneath (its shadow, rounded
    // corners): without a chain it asks for the transparent backdrop.
    if (opening.backdrop.has_value()) {
        (*model).set_backdrop_chain(opening.backdrop);
    } else {
        static_cast<void>((*model).request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    }

    ApplicationWindow entry{};
    entry.stable_id = (*owner).stable_id + "/popup." + std::to_string(opening.serial);
    entry.owner_id = (*owner).stable_id;
    entry.model = std::move(model);
    entry.options.title = std::string();
    entry.options.initial_size = size;
    entry.options.minimum_size = Size{1.0, 1.0};
    entry.options.minimizable = false;
    // The popup's own alpha surface: its shadow and any clear part show
    // what lies beneath (owner, 2026-11-04, answer 4).
    entry.options.non_opaque = true;
    HostPopupPlacement popup{};
    popup.focus = opening.focus;
    popup.origin = DesktopLogicalPoint{placed.x, placed.y};
    entry.popup = popup;
    // Made before the window, so the window's closed callback can reach it.
    std::shared_ptr<NativePopupRecord> record = std::make_shared<NativePopupRecord>(borrowed);
    entry.options.closed = NativePopupClosed{record};
    const ApplicationWindowOpenResult opened = windows.open_window(std::move(entry));
    if (!opened.status.accepted() || !opened.window.active()) return nullptr;
    (*record).bind(opened.window);
    return record;
}

} // namespace gui_forms::detail
