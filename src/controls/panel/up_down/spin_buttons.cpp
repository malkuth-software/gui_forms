#include "spin_buttons.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gui_forms {

SpinHalves spin_halves(Rect column) noexcept {
    const double middle = std::floor(std::max(0.0, column.height) * 0.5);
    SpinHalves halves;
    halves.upper = {column.x, column.y, column.width, middle};
    halves.lower = {column.x, column.y + middle, column.width,
                    std::max(0.0, column.height - middle)};
    return halves;
}

Rect spin_glyph(Rect half) noexcept {
    const double side = std::floor(std::min(half.width, half.height) * 0.5);
    if (side < 1.0) return {};
    const double x = std::floor(half.x + (half.width - side) * 0.5);
    const double y = std::floor(half.y + (half.height - side) * 0.5);
    const Rect glyph{x, y, side, side};
    return glyph;
}

SpinPartGeometry spin_part_geometry(const SpinButtons& spin) {
    const SpinHalves halves = spin_halves(spin.committed_arranged_bounds());
    SpinPartGeometry parts;
    parts.upper_arrow = PartGeometry::from_bounds(halves.upper);
    parts.lower_arrow = PartGeometry::from_bounds(halves.lower);
    parts.upper_glyph = PartGeometry::from_bounds(spin_glyph(halves.upper));
    parts.lower_glyph = PartGeometry::from_bounds(spin_glyph(halves.lower));
    parts.upper_arrow.states = spin.half_states(SpinHalf::upper);
    parts.lower_arrow.states = spin.half_states(SpinHalf::lower);
    parts.upper_glyph.states = parts.upper_arrow.states;
    parts.lower_glyph.states = parts.lower_arrow.states;
    return parts;
}

SpinButtons::SpinButtons(StableId stable_id) : Control(std::move(stable_id)) {
    set_focusable(false);
    cursor = CursorKind::hand;
}

AnatomyStateSet SpinButtons::half_states(SpinHalf half) const noexcept {
    AnatomyStateSet states;
    if (half == SpinHalf::none) return states;
    if (hot_ == half) states.insert(AnatomyState::hot);
    if (pressed_ == half) states.insert(AnatomyState::pressed);
    return states;
}

void SpinButtons::paint_half(Painter& painter, Rect half, SpinHalf which, PartMark arrow) const {
    PartPaint actuator;
    actuator.role = PartRole::actuator;
    actuator.bounds = half;
    actuator.states = half_states(which);
    actuator.family = ControlVisualRole::button;
    paint_part(painter, *this, actuator);
    PartPaint glyph = actuator;
    glyph.role = PartRole::glyph;
    glyph.bounds = spin_glyph(half);
    glyph.mark = arrow;
    paint_part(painter, *this, glyph);
}

void SpinButtons::on_paint(Painter& painter, Rect) {
    const SpinHalves halves = spin_halves(client_rectangle());
    paint_half(painter, halves.upper, SpinHalf::upper, PartMark::arrow_up);
    paint_half(painter, halves.lower, SpinHalf::lower, PartMark::arrow_down);
}

SpinHalf SpinButtons::half_at(Point window_position) const {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(window_position)) return SpinHalf::none;
    const SpinHalves halves = spin_halves({0.0, 0.0, bounds.width, bounds.height});
    const double local_y = window_position.y - bounds.y;
    const SpinHalf half = local_y < halves.lower.y ? SpinHalf::upper : SpinHalf::lower;
    return half;
}

void SpinButtons::show(SpinHalf hot, SpinHalf pressed) {
    if (hot_ == hot && pressed_ == pressed) return;
    hot_ = hot;
    pressed_ = pressed;
    invalidate(Dirty::paint);
    // The owning up-down control publishes these states on its arrow parts.
    const Control::Ptr owner = parent();
    if (owner) (*owner).invalidate(Dirty::semantics);
}

void SpinButtons::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::leave) {
        show(SpinHalf::none, pressed_);
        return;
    }
    const SpinHalf half = half_at(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        show(half, pressed_);
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down && half != SpinHalf::none) {
        show(half, half);
        const int step = half == SpinHalf::upper ? 1 : -1;
        stepped_.emit(step);
        event.handled = true;
    } else if (event.action == PointerAction::up) {
        show(half, SpinHalf::none);
        event.handled = true;
    }
}

} // namespace gui_forms
