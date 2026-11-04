// The sweep scope's effect program. Every function here obeys the effect
// rules (gui_forms/engine/effect/effect.hpp): bounded loops only, no
// recursion, allocation, globals, clocks or host maths; drawing through the
// canvas alone.

#include "gui_forms/controls/instruments/sweep_scope/sweep_scope_effects.hpp"

// Built without the gui_forms plugin's effect pass (GUI_FORMS_EFFECT_VERIFY
// OFF, or a toolchain that cannot load the plugin), CMake defines this, and
// the effect registered here runs labelled unverified.
#if defined(GUI_FORMS_EFFECTS_UNVERIFIED)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma message("effect scope_sweep is unverified: its bounds are not checked")
#else
#warning "effect scope_sweep is unverified: its bounds are not checked"
#endif
#endif

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <array>
#include <cstdint>

namespace gui_forms {
namespace {

constexpr double tick_seconds = 1.0 / static_cast<double>(effect_ticks_per_second);
// The trace keeps this far from the plot's edges, in logical units.
constexpr double trace_margin = 2.0;

// The phase after `ticks` ticks of a cycle `period` seconds long.
double advanced_sweep_phase(double phase, std::uint32_t ticks, double period) noexcept {
    const double cycles = period > 0.0 ? static_cast<double>(ticks) * tick_seconds / period : 0.0;
    const double advanced = effect_fraction(phase + cycles);
    return advanced;
}

} // namespace

void ScopeSweep::run(const EffectView<SweepScope>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (!frame.still) phase_ = advanced_sweep_phase(phase_, frame.ticks, parent.period_seconds);
    const double width = parent.plot.width;
    const double height = parent.plot.height;
    if (!(width > trace_margin * 2.0) || !(height > trace_margin * 2.0)) return;
    // The trace: a rise to the peak above the level's place, then a fall to
    // the middle of the right edge.
    const double fraction = parent.fraction < 0.0 ? 0.0 : (parent.fraction > 1.0 ? 1.0 : parent.fraction);
    const double peak_x = trace_margin + fraction * (width - trace_margin * 2.0);
    const Point start{trace_margin, height - trace_margin};
    const Point peak{peak_x, trace_margin};
    const Point end{width - trace_margin, height * 0.5};
    canvas.stroke_line(start, peak, parent.rising, trace_width);
    canvas.stroke_line(peak, end, parent.falling, trace_width);
    // The sweep: a vertical line at the phase's place across the plot.
    const double phase = frame.still ? parent.still_phase : phase_;
    const double half = sweep_width * 0.5;
    const double raw_x = phase * width;
    const double sweep_x = raw_x < half ? half : (raw_x > width - half ? width - half : raw_x);
    canvas.stroke_line({sweep_x, 0.0}, {sweep_x, height}, parent.sweep, sweep_width);
}

void register_sweep_scope_effects(EffectRegistry& registry) {
    constexpr std::array<EffectEntry, 1> entries{
        make_effect_entry<ScopeSweep>(),
    };
    for (const EffectEntry& entry : entries) {
        static_cast<void>(registry.add(entry));
    }
}

} // namespace gui_forms
