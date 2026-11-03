#pragma once

// The sweep scope's effect program (effect programs design note, section 8):
// the level's trace and the sweep line crossing it. Its part is the scope's
// plot.

#include "gui_forms/controls/instruments/sweep_scope/sweep_scope.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"

#include <string_view>

namespace gui_forms {

class EffectRegistry;

// What the scope's effect may read: a snapshot taken when the frame runs.
// Geometry is in plot-local logical units (the plot's top-left corner is the
// origin); colours come from the scope's theme.
template <>
struct EffectView<SweepScope> final {
    static constexpr std::string_view parent_name{"sweep_scope"};

    // (value − minimum) ÷ (maximum − minimum), in [0, 1].
    double fraction{};
    // The plot's logical size.
    Size plot{};
    // The trace's rising and falling strokes, and the sweep line.
    Color rising{};
    Color falling{};
    Color sweep{};
    // One crossing of the plot, in seconds.
    double period_seconds{1.234568};
    // The phase the sweep shows while still (reduced motion, ambient motion
    // off): the middle of the plot.
    double still_phase{0.5};
};

// The trace (a rise from the lower left to a peak above the level's place,
// and a fall to the right edge) and the sweep line crossing the plot once a
// period.
class ScopeSweep final {
public:
    using parent_type = SweepScope;
    static constexpr double trace_width = 2.0;
    static constexpr double sweep_width = 1.0;
    static constexpr EffectDeclaration declaration{
        .name = "scope_sweep",
        .engine_version = 1U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 400U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const EffectView<SweepScope>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    double phase_{};
};

// Adds the sweep scope's effects to `registry`.
void register_sweep_scope_effects(EffectRegistry& registry);

} // namespace gui_forms
