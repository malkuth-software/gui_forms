// Application-scope windowing in one surface (surface_manager_internal.hpp):
// every window of the run is a desk layer. The desktop is the surface, in
// its logical units, with one monitor at the surface's scale.
#include "surface_manager_internal.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace gui_forms::host::surface_detail {

SurfaceWindowing::SurfaceWindowing(Runtime& runtime) : HostWindowing(runtime.capabilities()), runtime_(runtime) {}

HostWindowAdmission SurfaceWindowing::open(SurfaceApplicationWindow entry, bool runtime) {
    HostWindowAdmission admission;
    if (!entry.model || entry.stable_id.empty()) {
        admission.status.error = HostServiceError::invalid_argument;
        return admission;
    }
    HostWindowId owner_id{};
    if (!entry.owner_id.empty()) {
        const stxlib::Optional<HostWindowId> found = find(entry.owner_id);
        const HostWindowId* const found_id = found.value();
        if (found_id == nullptr || runtime_.window(*found_id) == nullptr) {
            admission.status.error = HostServiceError::invalid_argument;
            return admission;
        }
        owner_id = *found_id;
    }
    const stxlib::Optional<HostPopupPlacement> popup = entry.popup;
    const HostWindowKind kind = host_window_kind(owner_id.valid(), entry.tool_window, entry.floating, popup.present());
    const bool floating = host_window_kind_traits(kind).floating;
    HostWindowDescription description;
    description.stable_id = entry.stable_id;
    description.owner = owner_id;
    description.kind = kind;
    if (popup.present()) description.transient = ((*popup.value())).kind;
    admission = admit(description);
    if (!admission.status.accepted()) return admission;
    const HostWindowId id = admission.window;
    const bool initially_visible = entry.options.initially_visible;
    if (floating) entry.options.initially_visible = false;
    // Where the layer opens: the primary window fills the surface, a popup
    // is where the program put it, every other window is centred over its
    // owner (cascaded when that spot is taken).
    const Size extent = runtime_.desk.extent();
    Rect content{};
    if (kind == HostWindowKind::primary) {
        content = Rect{0.0, 0.0, extent.width, extent.height};
    } else if (popup.present()) {
        content = Rect{((*popup.value())).origin.x, ((*popup.value())).origin.y, entry.options.initial_size.width,
                       entry.options.initial_size.height};
    } else {
        const Size wanted{std::min(entry.options.initial_size.width, std::max(64.0, extent.width - 16.0)),
                          std::min(entry.options.initial_size.height,
                                   std::max(64.0, extent.height - runtime_.desk.theme().title_bar_height - 8.0))};
        content = runtime_.desk.placement(kind, owner_id, wanted);
    }
    SurfaceDeskLayerInit layer{};
    layer.id = id;
    layer.owner = owner_id;
    layer.kind = kind;
    layer.transient = popup.present() ? ((*popup.value())).kind : HostTransientKind::popup;
    layer.content = content;
    layer.minimum = entry.options.minimum_size;
    layer.resizable = popup.absent();
    layer.minimizable = popup.absent() && entry.options.minimizable;
    layer.outside_modality = entry.outside_modality;
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

bool SurfaceWindowing::supports_popup_windows() const noexcept { return true; }

bool SurfaceWindowing::supports_splash_windows() const noexcept { return true; }

bool SurfaceWindowing::transient_windows_translucent() const noexcept { return true; }

bool SurfaceWindowing::supports_modal_windows() const noexcept { return true; }

void SurfaceWindowing::report_native_frame(NativeWindow& window) {
    if (window.closed || !live(window.window_id)) return;
    report_frame(window.window_id, window.frame());
}

HostWindowFrameResult SurfaceWindowing::frame_impl(HostWindowId window) {
    HostWindowFrameResult result;
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) {
        result.status.error = HostServiceError::after_shutdown;
        return result;
    }
    result.frame = (*found).frame();
    return result;
}

HostServiceStatus SurfaceWindowing::request_frame_impl(HostWindowId window, Rect content) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    // The primary window is the surface: the host decides its size.
    if (primary() == window) return {HostServiceError::unsupported};
    const std::optional<Rect> placed = runtime_.desk.set_content(window, content);
    if (!placed.has_value()) return {HostServiceError::backend_failure};
    (*found).apply_content(*placed);
    (*found).refresh_chrome();
    return {};
}

HostScreenPointResult SurfaceWindowing::pointer_position_impl() {
    HostScreenPointResult result;
    result.position = DesktopLogicalPoint{runtime_.pointer_position.x, runtime_.pointer_position.y};
    return result;
}

HostWindowAtResult SurfaceWindowing::window_at_impl(DesktopLogicalPoint point) {
    HostWindowAtResult result;
    const SurfaceDeskHit hit = runtime_.desk.hit(Point{point.x, point.y});
    // A host dialog's layer is no window of the run's table.
    if (hit.window.valid() && live(hit.window)) result.window = hit.window;
    return result;
}

HostServiceStatus SurfaceWindowing::bring_to_front_impl(HostWindowId window) {
    if (runtime_.window(window) == nullptr) return {HostServiceError::after_shutdown};
    runtime_.desk.raise(window);
    runtime_.scene_changed();
    return {};
}

HostServiceStatus SurfaceWindowing::destroy_window_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).close(HostCloseReason::application, true);
    return {};
}

HostMonitorResult SurfaceWindowing::query_monitors_impl() {
    HostMonitorResult result;
    result.monitors.resize(1U);
    HostMonitor& monitor = result.monitors.front();
    monitor.id.assign(runtime_.host.monitor_id());
    monitor.frame = Rect{0.0, 0.0, runtime_.extent.width, runtime_.extent.height};
    monitor.work_area = monitor.frame;
    monitor.scale = runtime_.scale;
    monitor.primary = true;
    return result;
}

HostServiceStatus SurfaceWindowing::show_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).set_visible(true);
    return {};
}

HostServiceStatus SurfaceWindowing::hide_impl(HostWindowId window) {
    NativeWindow* found = runtime_.window(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*found).set_visible(false);
    return {};
}

// The nested loop blocks the program's thread as a desktop modal loop does
// (the host keeps its own events coming). The desk fixes the blocked
// windows when the loop begins: every window not owned by the modal one.
HostServiceStatus SurfaceWindowing::run_modal_impl(HostWindowId window) {
    if (runtime_.window(window) == nullptr) return {HostServiceError::after_shutdown};
    const stxlib::Optional<HostWindowId> owner_held = owner(window);
    const HostWindowId* const owner_id = owner_held.value();
    runtime_.begin_modal_scope(window);
    runtime_.activate(window, true);
    const HostWindowId main_window = primary();
    while (runtime_.window(window) != nullptr && !runtime_.quitting &&
           (!main_window.valid() || runtime_.window(main_window) != nullptr))
        runtime_.step();
    runtime_.desk.end_modal(window);
    if (owner_id != nullptr && runtime_.window(*owner_id) != nullptr) runtime_.activate(*owner_id, true);
    runtime_.scene_changed();
    return {};
}

} // namespace gui_forms::host::surface_detail
