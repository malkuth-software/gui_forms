#include "gui_forms/layout/radial_layout/radial_layout.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gui_forms {
namespace {

constexpr double degrees_to_radians = 0.017453292519943295769;
constexpr double radians_to_degrees = 57.295779513082320877;
// The largest start angle a layout takes, so a sum of it and a turn stays
// exact enough to place by.
constexpr double maximum_start_degrees = 1'000'000.0;
// How far a direction must lean along an axis for an outward box to leave
// that axis centred: well above the engine cosine's error at 90 degrees.
constexpr double axis_lean = 1e-9;
// One arc step of a ring sector's outline, in degrees.
constexpr double ring_step_degrees = 6.0;

// `degrees` in [0, 360).
double normalised_degrees(double degrees) noexcept {
    const double turned = std::fmod(degrees, 360.0);
    const double wrapped = turned < 0.0 ? turned + 360.0 : turned;
    const double result = wrapped >= 360.0 ? 0.0 : wrapped;
    return result;
}

// Where a box `extent` long starts on one axis, given its anchor and how
// far the item's direction leans along the axis: past `threshold` either
// way it grows away from the centre, otherwise it is centred. An infinite
// threshold centres every box.
double box_start(double anchor, double extent, double lean, double threshold) noexcept {
    if (lean > threshold) return anchor;
    if (lean < -threshold) return anchor - extent;
    const double centred = anchor - extent * 0.5;
    return centred;
}

Point on_circle(Point center, double radius, double degrees) noexcept {
    const EngineSineCosine direction = engine_sine_cosine(degrees * degrees_to_radians);
    const Point point{center.x + radius * direction.cosine, center.y + radius * direction.sine};
    return point;
}

bool finite_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

} // namespace

bool valid_radial_layout(const RadialLayout& layout) noexcept {
    const bool centre = finite_point(layout.center);
    const bool radius = std::isfinite(layout.radius) && layout.radius > 0.0;
    const bool size = std::isfinite(layout.item_size.width) && std::isfinite(layout.item_size.height) &&
                      layout.item_size.width >= 0.0 && layout.item_size.height >= 0.0;
    const bool start = std::isfinite(layout.start_degrees) && std::abs(layout.start_degrees) <= maximum_start_degrees;
    const bool count = layout.count >= 1U && layout.count <= maximum_radial_items;
    const bool alignment = layout.alignment == RadialItemAlignment::centred ||
                           layout.alignment == RadialItemAlignment::outward;
    return centre && radius && size && start && count && alignment;
}

std::size_t place_radial_items(const RadialLayout& layout, std::span<RadialSlot> slots) noexcept {
    if (!valid_radial_layout(layout) || slots.size() < layout.count) return 0U;
    const double sweep = 360.0 / static_cast<double>(layout.count);
    const double threshold =
        layout.alignment == RadialItemAlignment::outward ? axis_lean : std::numeric_limits<double>::infinity();
    const double width = layout.item_size.width;
    const double height = layout.item_size.height;
    for (std::uint32_t index = 0U; index < layout.count; ++index) {
        const double direction = layout.start_degrees + sweep * static_cast<double>(index);
        const EngineSineCosine lean = engine_sine_cosine(direction * degrees_to_radians);
        const Point anchor{layout.center.x + layout.radius * lean.cosine,
                           layout.center.y + layout.radius * lean.sine};
        RadialSlot slot{};
        slot.anchor = anchor;
        slot.bounds = Rect{box_start(anchor.x, width, lean.cosine, threshold),
                           box_start(anchor.y, height, lean.sine, threshold), width, height};
        slot.direction_degrees = direction;
        slot.sweep_start = direction - sweep * 0.5;
        slot.sweep = sweep;
        slots[index] = slot;
    }
    return layout.count;
}

RadialPick pick_radial_item(const RadialLayout& layout, Point pointer, double dead_zone) noexcept {
    RadialPick pick{};
    if (!valid_radial_layout(layout) || !finite_point(pointer) || !std::isfinite(dead_zone)) return pick;
    const double dx = pointer.x - layout.center.x;
    const double dy = pointer.y - layout.center.y;
    const double distance = std::hypot(dx, dy);
    if (!(distance > 0.0) || distance <= dead_zone) return pick;
    const double direction = normalised_degrees(engine_arctangent2(dy, dx) * radians_to_degrees);
    const double sweep = 360.0 / static_cast<double>(layout.count);
    // From the first sector's start, so sector k spans [k sweep, (k+1) sweep).
    const double relative = normalised_degrees(direction - layout.start_degrees + sweep * 0.5);
    const double sector = std::floor(relative / sweep);
    const std::uint32_t last = layout.count - 1U;
    pick.picked = true;
    pick.index = std::min(static_cast<std::uint32_t>(sector), last);
    pick.direction_degrees = direction;
    return pick;
}

void paint_direction_ring(Painter& painter, const RadialLayout& layout, const RadialPick& pick,
                          const DirectionRingStyle& style) {
    const bool radii = std::isfinite(style.inner_radius) && std::isfinite(style.outer_radius) &&
                       style.inner_radius >= 0.0 && style.inner_radius < style.outer_radius;
    if (!valid_radial_layout(layout) || !radii) return;
    const double middle = (style.inner_radius + style.outer_radius) * 0.5;
    const double width = style.outer_radius - style.inner_radius;
    const Rect ring{layout.center.x - middle, layout.center.y - middle, middle * 2.0, middle * 2.0};
    painter.stroke_rounded_rect(ring, middle, style.track, width);
    if (!pick.picked || pick.index >= layout.count) return;
    if (layout.count == 1U) {
        painter.stroke_rounded_rect(ring, middle, style.pick, width);
        return;
    }
    // The sector: along the outer edge clockwise, back along the inner edge.
    // With two or more items it is at most half a turn, so its steps fit.
    const double sweep = 360.0 / static_cast<double>(layout.count);
    const double first = layout.start_degrees + sweep * static_cast<double>(pick.index) - sweep * 0.5;
    const std::size_t steps = static_cast<std::size_t>(std::ceil(sweep / ring_step_degrees));
    const double step = sweep / static_cast<double>(steps);
    std::array<Point, 2U * (maximum_ring_arc_steps + 1U)> outline{};
    for (std::size_t index = 0U; index <= steps; ++index) {
        outline[index] = on_circle(layout.center, style.outer_radius, first + step * static_cast<double>(index));
    }
    for (std::size_t index = 0U; index <= steps; ++index) {
        const double degrees = first + sweep - step * static_cast<double>(index);
        outline[steps + 1U + index] = on_circle(layout.center, style.inner_radius, degrees);
    }
    const std::size_t point_count = 2U * (steps + 1U);
    painter.fill_polygon(std::span<const Point>(outline.data(), point_count), style.pick, PolygonFillRule::nonzero);
}

} // namespace gui_forms
