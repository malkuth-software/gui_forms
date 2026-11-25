// The pointer-motion probe (layout liveness): the layout work a run of
// pointer moves causes, from the window's metrics.

#include "gui_forms/layout/diagnostics/pointer_motion_probe.hpp"

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <cmath>
#include <stdexcept>

namespace gui_forms {

PointerMotionCost measure_pointer_motion(SurfaceRoot& window, Rect area, std::uint32_t moves) {
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.width) ||
        !std::isfinite(area.height) || !(area.width > 0.0) || !(area.height > 0.0)) {
        throw std::invalid_argument("measure_pointer_motion needs a finite, nonempty area");
    }
    window.perform_layout();
    const MetricsSnapshot before = window.metrics_snapshot();
    // A path that sweeps the area diagonally and back, at steps that do not
    // repeat a point (37 and 23 are prime to each other and to the count).
    for (std::uint32_t index = 0U; index < moves; ++index) {
        const double across = static_cast<double>((index * 37U) % 101U) / 100.0;
        const double down = static_cast<double>((index * 23U) % 103U) / 102.0;
        PointerEvent event{};
        event.action = PointerAction::move;
        event.position = Point{area.x + across * area.width, area.y + down * area.height};
        static_cast<void>(window.dispatch_pointer(event));
    }
    const MetricsSnapshot after = window.metrics_snapshot();
    PointerMotionCost cost{};
    cost.moves = moves;
    cost.measure_passes = after.measure_passes - before.measure_passes;
    cost.arrange_passes = after.arrange_passes - before.arrange_passes;
    cost.controls_measured = after.controls_measured - before.controls_measured;
    cost.controls_arranged = after.controls_arranged - before.controls_arranged;
    cost.read_barrier_flushes = after.read_barrier_flushes - before.read_barrier_flushes;
    return cost;
}

} // namespace gui_forms
