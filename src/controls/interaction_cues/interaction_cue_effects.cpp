// The drop-cue pulse and the scrub glow. Every function here obeys the
// effect rules (gui_forms/engine/effect/effect.hpp): bounded loops only, no
// recursion, allocation, globals, clocks or host maths; drawing through the
// canvas alone.

#include "gui_forms/controls/dock_site_effects.hpp"
#include "gui_forms/controls/numeric_up_down_effects.hpp"

// Built without the gui_forms plugin's effect pass (GUI_FORMS_EFFECT_VERIFY
// OFF, or a toolchain that cannot load the plugin), CMake defines this, and
// the effects registered here run labelled unverified.
#if defined(GUI_FORMS_EFFECTS_UNVERIFIED)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma message("effects drop_pulse and scrub_glow are unverified: their bounds are not checked")
#else
#warning "effects drop_pulse and scrub_glow are unverified: their bounds are not checked"
#endif
#endif

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <array>
#include <cstdint>

namespace gui_forms {
namespace {

constexpr double tick_seconds = 1.0 / static_cast<double>(effect_ticks_per_second);
constexpr double full_turn = 6.283185307179586;
// The pulse's edge opacity at the bottom and top of a breath, and its
// tint's at the top (the tint vanishes at the bottom).
constexpr double pulse_low = 0.35;
constexpr double pulse_high = 0.95;
constexpr double pulse_tint = 0.18;
// The glow's edge and tint opacity at transition 1.
constexpr double glow_edge = 0.9;
constexpr double glow_tint = 0.12;

// The rectangle an edge `width` wide strokes inside a part of `size`: its
// centre line half the width in from the part's sides. Empty (zero size)
// when the part is narrower than the edge.
Rect inner_edge(Size size, double width) noexcept {
    const double half = width * 0.5;
    const double inner_width = effect_clamp(size.width - width, 0.0, size.width);
    const double inner_height = effect_clamp(size.height - width, 0.0, size.height);
    const Rect edge{half, half, inner_width, inner_height};
    return edge;
}

} // namespace

void DropPulse::run(const EffectView<DockSite>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (!frame.still) {
        const double turns = static_cast<double>(frame.ticks) * tick_seconds / drop_pulse_period_seconds;
        phase_ = effect_fraction(phase_ + turns);
    }
    // The breath starts at its low level: 0.5 - 0.5 cos, as a sine a
    // quarter turn behind.
    const double wave = 0.5 + 0.5 * effect_sine((phase_ - 0.25) * full_turn);
    const double level = frame.still ? 0.5 : wave;
    const double width = effect_clamp(parent.edge, 0.5, 8.0);
    const Rect edge = inner_edge(frame.part, width);
    if (edge.width <= 0.0 || edge.height <= 0.0) return;
    if (!frame.still) {
        const Color tint = effect_faded(frame.colours.secondary, pulse_tint * level);
        if (tint.alpha != 0U) canvas.fill_rect(Rect{0.0, 0.0, frame.part.width, frame.part.height}, tint);
    }
    const Color stroke = effect_faded(frame.colours.primary, effect_mix(pulse_low, pulse_high, level));
    if (stroke.alpha == 0U) return;
    canvas.stroke_rect(edge, stroke, width);
}

void ScrubGlow::run(const EffectView<NumericUpDown>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    const double level = effect_smoothstep(0.0, 1.0, effect_clamp(frame.transition, 0.0, 1.0));
    if (!(level > 0.0)) return;
    const double width = effect_clamp(parent.edge, 0.5, 8.0);
    const Rect edge = inner_edge(frame.part, width);
    if (edge.width <= 0.0 || edge.height <= 0.0) return;
    const Color tint = effect_faded(frame.colours.secondary, glow_tint * level);
    if (tint.alpha != 0U) canvas.fill_rect(Rect{0.0, 0.0, frame.part.width, frame.part.height}, tint);
    const Color stroke = effect_faded(frame.colours.primary, glow_edge * level);
    if (stroke.alpha == 0U) return;
    canvas.stroke_rect(edge, stroke, width);
}

void register_dock_site_effects(EffectRegistry& registry) {
    constexpr std::array<EffectEntry, 1> entries{
        make_effect_entry<DropPulse>(),
    };
    for (const EffectEntry& entry : entries) {
        static_cast<void>(registry.add(entry));
    }
}

void register_numeric_up_down_effects(EffectRegistry& registry) {
    constexpr std::array<EffectEntry, 1> entries{
        make_effect_entry<ScrubGlow>(),
    };
    for (const EffectEntry& entry : entries) {
        static_cast<void>(registry.add(entry));
    }
}

} // namespace gui_forms
