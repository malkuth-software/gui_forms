// The node canvas's link flow. Every function here obeys the effect rules
// (gui_forms/engine/effect/effect.hpp): bounded loops only, no recursion,
// allocation, globals, clocks or host maths; drawing through the canvas
// alone.

#include "gui_forms/controls/link_flow_effects.hpp"

// Built without the gui_forms plugin's effect pass (GUI_FORMS_EFFECT_VERIFY
// OFF, or a toolchain that cannot load the plugin), CMake defines this, and
// the effect registered here runs labelled unverified.
#if defined(GUI_FORMS_EFFECTS_UNVERIFIED)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma message("effect link_flow is unverified: its bounds are not checked")
#else
#warning "effect link_flow is unverified: its bounds are not checked"
#endif
#endif

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <array>
#include <cstdint>

namespace gui_forms {
namespace {

constexpr double tick_seconds = 1.0 / static_cast<double>(effect_ticks_per_second);
// A bead is a dash this short with round caps: a disc one stroke wide.
constexpr double bead_length = 0.01;

// `value` held to [minimum, maximum]; a NaN is `minimum`.
double held(double value, double minimum, double maximum) noexcept {
    if (!(value >= minimum)) return minimum;
    if (value > maximum) return maximum;
    return value;
}

// The phase after `ticks` ticks at `speed` view units a second over marks
// `spacing` apart, as a fraction of one pitch.
double advanced_flow_phase(double phase, std::uint32_t ticks, double speed, double spacing) noexcept {
    const double distance = static_cast<double>(ticks) * tick_seconds * speed;
    const double pitches = distance / spacing;
    const double advanced = effect_fraction(phase + pitches);
    return advanced;
}

} // namespace

void LinkFlow::run(const EffectView<NodeCanvas>& parent, const EffectFrame& frame, EffectCanvas& canvas) {
    if (parent.style == CanvasFlowStyle::none) return;
    const double spacing = held(parent.spacing, minimum_canvas_flow_spacing, maximum_canvas_flow_spacing);
    const double speed = held(parent.speed, 0.0, maximum_canvas_flow_speed * 64.0);
    if (!frame.still) phase_ = advanced_flow_phase(phase_, frame.ticks, speed, spacing);
    const double phase = frame.still ? 0.0 : phase_;
    const double width = held(parent.width, 0.5, maximum_recipe_stroke_width);
    StrokeStyle style{};
    style.color = frame.colours.primary;
    style.join = StrokeJoin::round;
    style.dash_count = 2U;
    // The pattern starts one pitch behind its phase, so a growing phase
    // moves the marks forward along each link, from its first point.
    style.dash_phase = (1.0 - phase) * spacing;
    if (parent.style == CanvasFlowStyle::dots) {
        style.width = width * 0.5;
        style.cap = StrokeCap::round;
        style.dashes[0] = bead_length;
        style.dashes[1] = spacing - bead_length;
    }
    if (parent.style == CanvasFlowStyle::dashes) {
        style.width = width + 1.0;
        style.cap = StrokeCap::butt;
        style.dashes[0] = spacing * 0.5;
        style.dashes[1] = spacing * 0.5;
    }
    if (style.color.alpha == 0U) return;
    for (const std::size_t index : bounded_range<maximum_flow_links>(parent.links.count)) {
        const EffectPoints& points = parent.links.items[index];
        if (points.count < 2U) continue;
        canvas.stroke_polyline(points, style);
    }
}

bool link_flow_wants_frames(const EffectView<NodeCanvas>& view, bool motion_reduced) noexcept {
    if (motion_reduced) return false;
    if (view.style == CanvasFlowStyle::none) return false;
    if (!(view.speed > 0.0)) return false;
    bool flowing = false;
    for (const std::size_t index : bounded_range<maximum_flow_links>(view.links.count)) {
        flowing = flowing || view.links.items[index].count >= 2U;
    }
    return flowing;
}

Color link_flow_mark_colour(const CanvasLook& look) noexcept {
    const Color canvas = look.background.fill;
    return Color::rgba(canvas.red, canvas.green, canvas.blue, 255);
}

void register_link_flow_effects(EffectRegistry& registry) {
    constexpr std::array<EffectEntry, 1> entries{
        make_effect_entry<LinkFlow>(),
    };
    for (const EffectEntry& entry : entries) {
        static_cast<void>(registry.add(entry));
    }
}

} // namespace gui_forms
