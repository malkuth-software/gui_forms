// Native popups (owner, 2026-11-04): the record of one popup window and
// its opening through the run's windowing. See native_popup.hpp.
#include "native_popup_record.hpp"

#include "application_state.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace gui_forms::detail {

NativePopupRecord::NativePopupRecord(Window* model) noexcept : model_() {
    if (model != nullptr) model_ = (*model).window_ref();
}

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
    Window* const model = model_.get();
    return model;
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

NativePopupRefusal native_popup_refusal(const std::weak_ptr<ApplicationWindowState>& owner,
                                        const HostTransientKind kind) noexcept {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run || !(*state).id.valid()) return NativePopupRefusal::no_run;
    const HostWindowing& windowing = *(*run).windowing;
    const bool available = windowing.capabilities().supports(HostCapability::runtime_windows) &&
                           windowing.supports_popup_windows();
    if (!available) return NativePopupRefusal::unsupported;
    if (host_transient_is_splash(kind) && !windowing.supports_splash_windows()) {
        return NativePopupRefusal::unsupported;
    }
    return NativePopupRefusal::none;
}

bool native_popup_translucent(const std::weak_ptr<ApplicationWindowState>& owner, const SurfaceRoot& model) noexcept {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run || !(*run).windowing) return true;
    if (!(*(*run).windowing).transient_windows_translucent()) return false;
    // The grant a popup of the owner would have: the same host's compositor
    // and the owner's settings, which the popup shares.
    const PresentationSettings& settings = model.presentation_settings();
    const WindowBackdropConditions conditions{true, settings.high_contrast, settings.reduced_transparency};
    const WindowBackdropRequest request = WindowBackdropRequest::preferring(WindowBackdrop::transparent);
    const WindowBackdropGrant grant = resolve_window_backdrop(request, model.compositor_state(), conditions);
    const bool predicted = grant.granted != WindowBackdrop::opaque;
    // What the run's popup windows were really granted overrules the
    // prediction once known.
    const bool translucent = predicted && (*run).popup_alpha.value_or(true);
    return translucent;
}

void note_popup_backdrop(const std::weak_ptr<ApplicationWindowState>& owner, const SurfaceRoot& popup) noexcept {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run || popup.compositor_state().generation.value == 0U) return;
    (*run).popup_alpha = popup.backdrop_grant().granted != WindowBackdrop::opaque;
}

std::optional<Rect> desktop_placement(const std::weak_ptr<ApplicationWindowState>& owner, const Rect bounds,
                                      HostMonitorResult& monitors) {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run) return std::nullopt;
    const ApplicationWindowHandle owner_handle = ApplicationHandleAccess::make(state);
    const HostWindowFrameResult owner_frame = owner_handle.frame();
    if (!owner_frame.status.accepted()) return std::nullopt;
    // The bounds in the owner's client area, as desktop logical units, kept
    // inside the work area under the origin.
    const Rect client = owner_frame.frame.content;
    Rect placed{client.x + bounds.x, client.y + bounds.y, bounds.width, bounds.height};
    try {
        static_cast<void>((*(*run).windowing).query_monitors(monitors));
    } catch (...) {
        monitors.status.error = HostServiceError::backend_failure;
        monitors.monitors.clear();
    }
    const std::optional<Rect> work_area = work_area_at(monitors, Point{placed.x, placed.y});
    if (work_area.has_value()) placed = clamp_to_work_area(placed, *work_area);
    return placed;
}

std::optional<Rect> client_work_area(const std::weak_ptr<ApplicationWindowState>& owner, const Point client_point,
                                    HostMonitorResult& monitors) {
    const std::shared_ptr<ApplicationWindowState> state = owner.lock();
    const std::shared_ptr<ApplicationRunState> run = owner_run(state);
    if (!run) return std::nullopt;
    const ApplicationWindowHandle owner_handle = ApplicationHandleAccess::make(state);
    const HostWindowFrameResult owner_frame = owner_handle.frame();
    if (!owner_frame.status.accepted()) return std::nullopt;
    const Rect client = owner_frame.frame.content;
    try {
        static_cast<void>((*(*run).windowing).query_monitors(monitors));
    } catch (...) {
        monitors.status.error = HostServiceError::backend_failure;
        monitors.monitors.clear();
    }
    const std::optional<Rect> desktop = work_area_at(monitors, Point{client.x + client_point.x, client.y + client_point.y});
    if (!desktop.has_value()) return std::nullopt;
    const Rect area{(*desktop).x - client.x, (*desktop).y - client.y, (*desktop).width, (*desktop).height};
    return area;
}

std::shared_ptr<NativePopupRecord> open_native_popup_window(const NativePopupOpening& opening) {
    if (native_popup_refusal(opening.owner, opening.kind) != NativePopupRefusal::none) return nullptr;
    if (host_transient_is_splash(opening.kind) && opening.focus != HostPopupFocus::passive) return nullptr;
    const std::shared_ptr<ApplicationWindowState> owner = opening.owner.lock();
    const ApplicationWindowHandle owner_handle = ApplicationHandleAccess::make(owner);
    HostMonitorResult monitors{};
    const std::optional<Rect> placement = desktop_placement(opening.owner, opening.bounds, monitors);
    if (!placement.has_value()) return nullptr;
    const Rect placed = *placement;
    const ApplicationWindows windows = owner_handle.windows();

    const Size size{opening.bounds.width, opening.bounds.height};
    std::unique_ptr<Window> model = std::make_unique<Window>(opening.content, size, opening.resources);
    Window* borrowed = model.get();
    (*model).set_key_bindings(opening.key_bindings);
    // A popup's clear pixels show what lies beneath (its shadow, rounded
    // corners): without a chain it asks for the transparent backdrop. A
    // content surface (a menu panel, a list, a tooltip bubble) draws every
    // surface itself, so the backplane under its shadow stays clear.
    if (opening.content_surface) static_cast<void>((*model).declare_content_surface());
    if (opening.backdrop.has_value()) {
        (*model).set_backdrop_chain(opening.backdrop);
    } else {
        static_cast<void>((*model).request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    }

    ApplicationWindow entry{};
    entry.stable_id = (*owner).stable_id + "/" + std::string(opening.name) + "." + std::to_string(opening.serial);
    entry.owner_id = (*owner).stable_id;
    entry.model = std::move(model);
    entry.options.title = std::string();
    entry.options.initial_size = size;
    entry.options.minimum_size = Size{1.0, 1.0};
    entry.options.minimizable = false;
    // The popup's own alpha surface: its shadow and any clear part show
    // what lies beneath (owner, 2026-11-04, answer 4).
    entry.options.non_opaque = true;
    entry.options.initially_visible = opening.initially_visible;
    HostPopupPlacement popup{};
    popup.kind = opening.kind;
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
