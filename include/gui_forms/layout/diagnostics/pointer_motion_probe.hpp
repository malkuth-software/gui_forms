#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstdint>

namespace gui_forms {

class SurfaceRoot;

// The layout work a run of pointer moves caused (layout liveness, see
// docs/user/layout.md, "Retained layout"): the growth of the window's
// MetricsSnapshot counters over the moves. A retained window lays out
// nothing when the pointer only moves; a control that re-flows from its
// arrange, or keeps asking for layout, shows here at once.
struct GUI_FORMS_API PointerMotionCost final {
    std::uint32_t moves{};
    std::uint64_t measure_passes{};
    std::uint64_t arrange_passes{};
    std::uint64_t controls_measured{};
    std::uint64_t controls_arranged{};
    std::uint64_t read_barrier_flushes{};

    // Whether the moves caused no layout at all.
    [[nodiscard]] bool lays_out_nothing() const noexcept {
        return measure_passes == 0U && arrange_passes == 0U && controls_measured == 0U &&
               controls_arranged == 0U && read_barrier_flushes == 0U;
    }
};

// For an application's own tests: lays out `window`, then moves the pointer
// `moves` times over `area` (window coordinates) along a fixed path that
// crosses it, as a hovering user would, and returns the layout work the
// moves caused. Open the largest content the application ships and scroll
// to its middle first; a retained window answers lays_out_nothing(). The
// moves are ordinary pointer events: hover handlers and tooltips run. A
// hover handler that changes another control's layout (a status bar's
// text) shows as that control's arrange, which is expected; a large page
// arranged again is the defect. std::invalid_argument for an area that is
// not finite or is empty. UI thread.
[[nodiscard]] GUI_FORMS_API PointerMotionCost measure_pointer_motion(SurfaceRoot& window, Rect area,
                                                                     std::uint32_t moves = 100U);

} // namespace gui_forms
