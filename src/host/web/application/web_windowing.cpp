// Application-scope windowing in one canvas (web_host_internal.hpp): every
// window of the run is a desk layer. The desktop is the canvas, in its CSS
// pixels (desktop logical units), with one monitor at the page's
// devicePixelRatio.
#include "web_host_internal.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace gui_forms::host::web_detail {

WebWindowing::WebWindowing(Runtime& runtime) : HostWindowing(web_capabilities()), runtime_(runtime) {}

HostWindowAdmission WebWindowing::open(WebApplicationWindow entry, bool runtime) {
    HostWindowAdmission admission;
    if (!entry.model || entry.stable_id.empty()) {
        admission.status.error = HostServiceError::invalid_argument;
        return admission;
    }
    HostWindowId owner_id{};
    if (!entry.owner_id.empty()) {
        const std::optional<HostWindowId> found = find(entry.owner_id);
        if (!found.has_value() || runtime_.window(*found) == nullptr) {
            admission.status.error = HostServiceError::invalid_argument;
            return admission;
        }
        owner_id = *found;
    }
    const std::optional<HostPopupPlacement> popup = entry.popup;
    const HostWindowKind kind = host_window_kind(owner_id.valid(), entry.tool_window, entry.floating, popup.has_value());
    const bool floating = host_window_kind_traits(kind).floating;
    HostWindowDescription description;
    description.stable_id = entry.stable_id;
    description.owner = owner_id;
    description.kind = kind;
    if (popup.has_value()) description.transient = (*popup).kind;
    admission = admit(description);
    if (!admission.status.accepted()) return admission;
    const HostWindowId id = admission.window;
    const bool initially_visible = entry.options.initially_visible;
    if (floating) entry.options.initially_visible = false;
    // Where the layer opens: the primary window fills the canvas, a popup
    // is where the program put it, every other window is centred over its
    // owner (cascaded when that spot is taken).
    const Size extent = runtime_.desk.extent();
    Rect content{};
    if (kind == HostWindowKind::primary) {
        content = Rect{0.0, 0.0, extent.width, extent.height};
    } else if (popup.has_value()) {
        content = Rect{(*popup).origin.x, (*popup).origin.y, entry.options.initial_size.width,
                       entry.options.initial_size.height};
    } else {
        const Size wanted{std::min(entry.options.initial_size.width, std::max(64.0, extent.width - 16.0)),
                          std::min(entry.options.initial_size.height,
                                   std::max(64.0, extent.height - WebDesk::title_bar_height - 8.0))};
        content = runtime_.desk.placement(kind, owner_id, wanted);
    }
    WebDeskLayerInit layer{};
    layer.id = id;
    layer.owner = owner_id;
    layer.kind = kind;
    layer.transient = popup.has_value() ? (*popup).kind : HostTransientKind::popup;
    layer.content = content;
    layer.minimum = entry.options.minimum_size;
    layer.resizable = !popup.has_value();
    if (!runtime_.desk.add(layer)) {
        retire(id);
        admission.status.error = HostServiceError::too_large;
        admission.window = {};
        return admission;
    }
    const std::optional<Rect> placed = runtime_.desk.content(id);
    if (placed.has_value()) content = *placed;
    std::shared_ptr<NativeWindow> window;
    try {
        window = runtime_.add(std::move(entry), id, content, !runtime);
    } catch (...) {
        runtime_.desk.remove(id);
        retire(id);
        if (!runtime) throw;
        admission.status.error = HostServiceError::backend_failure;
        admission.window = {};
        return admission;
    }
    if (!runtime) {
        window_created(id, initially_visible);
        return admission;
    }
    (*window).ready();
    report_native_frame(*window);
    window_created(id, initially_visible);
    return admission;
}

bool WebWindowing::supports_popup_windows() const noexcept { return true; }

bool WebWindowing::supports_splash_windows() const noexcept { return true; }

bool WebWindowing::transient_windows_translucent() const noexcept { return true; }

bool WebWindowing::supports_modal_windows() const noexcept { return true; }

void WebWindowing::report_native_frame(NativeWindow& window) {
    if (window.closed || !live(window.window_id)) return;
    report_frame(window.window_id, window.frame());
}

HostWindowFrameResult WebWindowing::frame_impl(HostWindowId window) {
    HostWindowFrameResult result;
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) {
        result.status.error = HostServiceError::after_shutdown;
        return result;
    }
    result.frame = (*found).frame();
    return result;
}

HostServiceStatus WebWindowing::request_frame_impl(HostWindowId window, Rect content) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    // The primary window is the canvas: the page decides its size.
    if (primary() == window) return {HostServiceError::unsupported};
    const std::optional<Rect> placed = runtime_.desk.set_content(window, content);
    if (!placed.has_value()) return {HostServiceError::backend_failure};
    (*found).apply_content(*placed);
    return {};
}

HostScreenPointResult WebWindowing::pointer_position_impl() {
    HostScreenPointResult result;
    result.position = DesktopLogicalPoint{runtime_.pointer_position.x, runtime_.pointer_position.y};
    return result;
}

HostWindowAtResult WebWindowing::window_at_impl(DesktopLogicalPoint point) {
    HostWindowAtResult result;
    const WebDeskHit hit = runtime_.desk.hit(Point{point.x, point.y});
    // A host dialog's layer is no window of the run's table.
    if (hit.window.valid() && live(hit.window)) result.window = hit.window;
    return result;
}

HostServiceStatus WebWindowing::bring_to_front_impl(HostWindowId window) {
    if (runtime_.window(window) == nullptr) return {HostServiceError::after_shutdown};
    runtime_.desk.raise(window);
    runtime_.scene_changed();
    return {};
}

HostServiceStatus WebWindowing::destroy_window_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).close(HostCloseReason::application, true);
    return {};
}

HostMonitorResult WebWindowing::query_monitors_impl() {
    HostMonitorResult result;
    result.monitors.resize(1U);
    HostMonitor& monitor = result.monitors.front();
    monitor.id.assign("web-canvas");
    monitor.frame = Rect{0.0, 0.0, runtime_.extent.width, runtime_.extent.height};
    monitor.work_area = monitor.frame;
    monitor.scale = runtime_.scale;
    monitor.primary = true;
    return result;
}

HostServiceStatus WebWindowing::show_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).set_visible(true);
    return {};
}

HostServiceStatus WebWindowing::hide_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).set_visible(false);
    return {};
}

// The nested loop blocks the program's thread as a desktop modal loop does
// (the page keeps running on its own thread). The desk fixes the blocked
// windows when the loop begins: every window not owned by the modal one.
HostServiceStatus WebWindowing::run_modal_impl(HostWindowId window) {
    if (runtime_.window(window) == nullptr) return {HostServiceError::after_shutdown};
    const std::optional<HostWindowId> owner_id = owner(window);
    runtime_.desk.begin_modal(window);
    runtime_.activate(window, true);
    const HostWindowId main_window = primary();
    while (runtime_.window(window) != nullptr && !runtime_.quitting &&
           (!main_window.valid() || runtime_.window(main_window) != nullptr))
        runtime_.step();
    runtime_.desk.end_modal(window);
    if (owner_id.has_value() && runtime_.window(*owner_id) != nullptr) runtime_.activate(*owner_id, true);
    runtime_.scene_changed();
    return {};
}

} // namespace gui_forms::host::web_detail
