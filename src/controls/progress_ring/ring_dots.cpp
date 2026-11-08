#include "ring_dots.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gui_forms::detail {

namespace {

// A dot takes this share of its slot along the ring; the rest is the gap.
constexpr double dot_share = 0.62;

} // namespace

RingPlacement place_ring(Rect bounds, std::size_t count) noexcept {
    RingPlacement ring{};
    const double side = std::min(bounds.width, bounds.height);
    if (!(side > 0.0) || count == 0U) return ring;
    const double slot = std::numbers::pi * side / static_cast<double>(count);
    const double dot = std::min(slot * dot_share, side * 0.25);
    // The dots' centres lie half a dot inside the inscribed circle.
    const double radius = (side - dot) * 0.5;
    if (!(dot >= 1.0) || !(radius > 0.0)) return ring;
    ring.centre = Point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    ring.radius = radius;
    ring.dot = dot;
    return ring;
}

Rect ring_dot_bounds(const RingPlacement& ring, std::size_t index, std::size_t count) noexcept {
    const std::size_t slot = index % count;
    // Clockwise from twelve o'clock, in y-down coordinates.
    const double angle = 2.0 * std::numbers::pi * static_cast<double>(slot) / static_cast<double>(count);
    const double x = ring.centre.x + ring.radius * std::sin(angle);
    const double y = ring.centre.y - ring.radius * std::cos(angle);
    const Rect square{x - ring.dot * 0.5, y - ring.dot * 0.5, ring.dot, ring.dot};
    return square;
}

void paint_ring(Painter& painter, const Control& owner, const RingPlacement& ring, const RingLighting& lighting,
                AnatomyStateSet states) {
    if (lighting.count == 0U || !(ring.dot > 0.0)) return;
    const std::size_t lit = std::min(lighting.lit, lighting.count);
    PartPaint dot{};
    dot.family = ControlVisualRole::progress;
    dot.corner_radius = ring.dot * 0.5;
    AnatomyStateSet lit_states = states;
    lit_states.insert(AnatomyState::selected);
    for (std::size_t index = 0U; index < lighting.count; ++index) {
        // Whether `index` lies in the lit run from `first`, wrapping.
        const std::size_t offset = (index + lighting.count - lighting.first % lighting.count) % lighting.count;
        const bool is_lit = offset < lit;
        dot.bounds = ring_dot_bounds(ring, index, lighting.count);
        dot.role = is_lit ? lighting.lit_role : PartRole::track;
        dot.key = PartKey{is_lit ? lighting.lit_part : std::string_view{"track"}, {}, {}};
        dot.states = is_lit ? lit_states : states;
        paint_part(painter, owner, dot);
    }
}

} // namespace gui_forms::detail
