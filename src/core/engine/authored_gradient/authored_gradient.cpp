#include "gui_forms/engine/authored_gradient/authored_gradient.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The resolution is part of an assembly the engine bakes, so it is computed
// without fused multiply-add, with the engine's own sine, cosine and
// arctangent (T2 plan, "Numerics"): the same inputs give the same layer on
// every host.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms {

namespace {

constexpr double pi = 3.14159265358979323846264338327950288;
constexpr double degrees_to_radians = pi / 180.0;
constexpr double radians_to_degrees = 180.0 / pi;
constexpr double square_root_of_two = 1.41421356237309504880168872420969808;
// 2^-20 of the gradient between coincident stops (CSS's hard stops).
constexpr double hard_stop_separation = 1.0 / 1048576.0;

double resolved_length(BoxLength length, double extent) noexcept {
    const double resolved = length.fraction * extent + length.units;
    return resolved;
}

std::uint8_t rounded_channel(double channel) noexcept {
    const double rounded = std::clamp(std::floor(channel + 0.5), 0.0, 255.0);
    return static_cast<std::uint8_t>(rounded);
}

std::uint8_t mixed_channel(std::uint8_t from, std::uint8_t to, double fraction) noexcept {
    const double low = static_cast<double>(from);
    const double high = static_cast<double>(to);
    const std::uint8_t channel = rounded_channel(low + (high - low) * fraction);
    return channel;
}

// The engine's interpolation of straight colour, rounded to 8 bits.
Color mixed_colour(Color from, Color to, double fraction) noexcept {
    const Color mixed{mixed_channel(from.red, to.red, fraction), mixed_channel(from.green, to.green, fraction),
                      mixed_channel(from.blue, to.blue, fraction), mixed_channel(from.alpha, to.alpha, fraction)};
    return mixed;
}

// CSS's placement (css-images-3, "color stop fixup") into `placed`, which
// is cleared first: each stop's offset along the gradient in its own
// measure (a fraction of the line, the ray or the turn), not yet within
// [0, 1]. `line` is the gradient's length in logical units, by which a
// position's units are divided (0: units are ignored).
void place_stops(std::span<const AuthoredGradientStop> stops, double line, std::vector<GradientStop>& placed) {
    placed.clear();
    const std::size_t count = stops.size();
    double greatest = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const AuthoredGradientStop& stop = stops[index];
        GradientStop place{};
        place.color = stop.colour;
        if (stop.positioned) {
            const double along = line > 0.0 ? stop.position.units / line : 0.0;
            place.offset = stop.position.fraction + along;
        } else if (index == 0U) {
            place.offset = 0.0;
        } else if (index + 1U == count) {
            place.offset = 1.0;
        } else {
            // Placed below, between its fixed neighbours.
            place.offset = greatest;
        }
        if (index > 0U) place.offset = std::max(place.offset, greatest);
        greatest = place.offset;
        placed.push_back(place);
    }
    // The unpositioned stops between two fixed ones (the first, the last
    // and the positioned), evenly.
    std::size_t before = 0U;
    for (std::size_t index = 1U; index < count; ++index) {
        const bool fixed = stops[index].positioned || index + 1U == count;
        if (!fixed) continue;
        const std::size_t gap = index - before;
        const double from = placed[before].offset;
        const double to = placed[index].offset;
        for (std::size_t inner = before + 1U; inner < index; ++inner) {
            const double step = static_cast<double>(inner - before) / static_cast<double>(gap);
            placed[inner].offset = from + (to - from) * step;
        }
        before = index;
    }
}

// The colour just past `offset` along the placed stops, padded at the ends.
Color colour_after(std::span<const GradientStop> placed, double offset) noexcept {
    std::size_t next = 0U;
    while (next < placed.size() && placed[next].offset <= offset) next = next + 1U;
    if (next == 0U) return placed.front().color;
    if (next == placed.size()) return placed.back().color;
    const GradientStop& low = placed[next - 1U];
    const GradientStop& high = placed[next];
    const double fraction = (offset - low.offset) / (high.offset - low.offset);
    const Color colour = mixed_colour(low.color, high.color, fraction);
    return colour;
}

// The colour just short of `offset` along the placed stops, padded at the
// ends.
Color colour_before(std::span<const GradientStop> placed, double offset) noexcept {
    std::size_t next = placed.size();
    while (next > 0U && placed[next - 1U].offset >= offset) next = next - 1U;
    if (next == 0U) return placed.front().color;
    if (next == placed.size()) return placed.back().color;
    const GradientStop& low = placed[next - 1U];
    const GradientStop& high = placed[next];
    const double fraction = (offset - low.offset) / (high.offset - low.offset);
    const Color colour = mixed_colour(low.color, high.color, fraction);
    return colour;
}

// The placed stops within [0, 1], with the colours at both ends, into
// `stops`, which is cleared first.
void clipped_stops(std::span<const GradientStop> placed, std::vector<GradientStop>& stops) {
    stops.clear();
    stops.push_back(GradientStop{0.0, colour_after(placed, 0.0)});
    for (const GradientStop& stop : placed) {
        if (stop.offset > 0.0 && stop.offset < 1.0) stops.push_back(stop);
    }
    stops.push_back(GradientStop{1.0, colour_before(placed, 1.0)});
}

// The placed stops of one period, from `first` over `span`, as offsets in
// [0, 1], into `stops`, which is cleared first.
void period_stops(std::span<const GradientStop> placed, double first, double span, std::vector<GradientStop>& stops) {
    stops.clear();
    for (const GradientStop& stop : placed) {
        const double offset = (stop.offset - first) / span;
        stops.push_back(GradientStop{offset, stop.color});
    }
    // The ends exactly, whatever the division rounded.
    stops.front().offset = 0.0;
    stops.back().offset = 1.0;
}

// One period's stops (`period`, from 0 to 1) begun `phase` into the period
// instead of at its start, into `stops`, which is cleared first: the stops
// past 1 − phase come first, moved back by a period.
void rotated_stops(std::span<const GradientStop> period, double phase, std::vector<GradientStop>& stops) {
    stops.clear();
    const double seam = 1.0 - phase;
    stops.push_back(GradientStop{0.0, colour_after(period, seam)});
    for (const GradientStop& stop : period) {
        if (stop.offset > seam) stops.push_back(GradientStop{stop.offset - seam, stop.color});
    }
    for (const GradientStop& stop : period) {
        if (stop.offset < seam) stops.push_back(GradientStop{stop.offset + phase, stop.color});
    }
    stops.push_back(GradientStop{1.0, colour_before(period, seam)});
}

// Separates coincident stops by hard_stop_separation, keeping the ends at
// exactly 0 and 1. `stops` has at least two, from 0 to 1, not decreasing.
void separate_stops(std::vector<GradientStop>& stops) noexcept {
    const std::size_t count = stops.size();
    for (std::size_t index = 1U; index < count; ++index) {
        const double least = stops[index - 1U].offset + hard_stop_separation;
        if (stops[index].offset < least) stops[index].offset = least;
    }
    if (stops.back().offset <= 1.0) {
        stops.back().offset = 1.0;
        return;
    }
    // Crowded at the end: separated backward from 1 instead.
    stops.back().offset = 1.0;
    for (std::size_t index = count - 1U; index > 0U; --index) {
        const double most = stops[index].offset - hard_stop_separation;
        if (stops[index - 1U].offset > most) stops[index - 1U].offset = most;
    }
    stops.front().offset = 0.0;
}

// CSS's colour for a repeating gradient of no length: the average of the
// gradient with the same colours evenly spaced. `stops` has at least two.
Color average_colour(std::span<const AuthoredGradientStop> stops) noexcept {
    const std::size_t segments = stops.size() - 1U;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
    for (std::size_t index = 0U; index < segments; ++index) {
        const Color low = stops[index].colour;
        const Color high = stops[index + 1U].colour;
        red = red + (static_cast<double>(low.red) + static_cast<double>(high.red)) * 0.5;
        green = green + (static_cast<double>(low.green) + static_cast<double>(high.green)) * 0.5;
        blue = blue + (static_cast<double>(low.blue) + static_cast<double>(high.blue)) * 0.5;
        alpha = alpha + (static_cast<double>(low.alpha) + static_cast<double>(high.alpha)) * 0.5;
    }
    const double count = static_cast<double>(segments);
    const Color average{rounded_channel(red / count), rounded_channel(green / count), rounded_channel(blue / count),
                        rounded_channel(alpha / count)};
    return average;
}

// A linear gradient's CSS angle: its own, or its corner's for `box`, whose
// 50% line joins the two neighbouring corners.
double linear_angle(const AuthoredGradient& gradient, Size box) noexcept {
    if (gradient.corner == GradientCorner::none) return gradient.angle_degrees;
    const double diagonal = engine_arctangent2(box.height, box.width) * radians_to_degrees;
    switch (gradient.corner) {
    case GradientCorner::top_right: return diagonal;
    case GradientCorner::bottom_right: return 180.0 - diagonal;
    case GradientCorner::bottom_left: return 180.0 + diagonal;
    case GradientCorner::top_left: return 360.0 - diagonal;
    case GradientCorner::none: break;
    // A value outside the enumeration names no corner: the written angle.
    default: break;
    }
    return gradient.angle_degrees;
}

bool has_units(BoxLength length) noexcept {
    const bool units = length.units != 0.0;
    return units;
}

bool stops_have_units(std::span<const AuthoredGradientStop> stops) noexcept {
    for (const AuthoredGradientStop& stop : stops) {
        if (stop.positioned && has_units(stop.position)) return true;
    }
    return false;
}

bool finite_length(BoxLength length) noexcept {
    const bool finite = std::isfinite(length.fraction) && std::isfinite(length.units);
    return finite;
}

void resolve_linear(const AuthoredGradient& gradient, Size box, std::vector<GradientStop>& placed,
                    MaterialFillLayer& layer) {
    const double angle = linear_angle(gradient, box);
    const EngineSineCosine turned = engine_sine_cosine(angle * degrees_to_radians);
    // CSS's gradient line: its ends at the corners the angle reaches.
    const double line = std::abs(box.width * turned.sine) + std::abs(box.height * turned.cosine);
    place_stops(gradient.stops, line, placed);
    std::vector<GradientStop> stops{};
    stops.reserve(maximum_gradient_stops);
    if (!gradient.repeating) {
        clipped_stops(placed, stops);
        separate_stops(stops);
        layer = MaterialFillLayer::linear_css_angle(angle, std::move(stops));
        return;
    }
    const double first = placed.front().offset;
    const double last = placed.back().offset;
    const double span = last - first;
    if (span <= 0.0) {
        layer = MaterialFillLayer::solid(average_colour(gradient.stops));
        return;
    }
    period_stops(placed, first, span, stops);
    separate_stops(stops);
    if (first == 0.0 && last == 1.0) {
        layer = MaterialFillLayer::linear_css_angle(angle, std::move(stops), GradientSpreadMode::repeat);
        return;
    }
    if (line <= 0.0) {
        // An empty box: the line has no length to repeat along.
        layer = MaterialFillLayer::solid(average_colour(gradient.stops));
        return;
    }
    // The CSS gradient line, from its 0 to its 1, narrowed to the period.
    const Point direction{turned.sine, -turned.cosine};
    const Point line_start{box.width * 0.5 - direction.x * line * 0.5, box.height * 0.5 - direction.y * line * 0.5};
    const Point start{line_start.x + direction.x * first * line, line_start.y + direction.y * first * line};
    const Point end{line_start.x + direction.x * last * line, line_start.y + direction.y * last * line};
    layer = MaterialFillLayer::repeating_linear(start, end, std::move(stops), MaterialCoordinateSpace::logical);
}

// A radial gradient's radii about `centre` in a box of `box`.
Size radial_radii(const AuthoredGradient& gradient, Point centre, Size box) noexcept {
    const double left = std::abs(centre.x);
    const double right = std::abs(box.width - centre.x);
    const double top = std::abs(centre.y);
    const double bottom = std::abs(box.height - centre.y);
    const double near_x = std::min(left, right);
    const double far_x = std::max(left, right);
    const double near_y = std::min(top, bottom);
    const double far_y = std::max(top, bottom);
    switch (gradient.extent) {
    case RadialExtent::lengths: {
        if (gradient.circle) return Size{gradient.radius_x.units, gradient.radius_x.units};
        const Size radii{resolved_length(gradient.radius_x, box.width), resolved_length(gradient.radius_y, box.height)};
        return radii;
    }
    case RadialExtent::closest_side: {
        if (!gradient.circle) return Size{near_x, near_y};
        const double radius = std::min(near_x, near_y);
        return Size{radius, radius};
    }
    case RadialExtent::farthest_side: {
        if (!gradient.circle) return Size{far_x, far_y};
        const double radius = std::max(far_x, far_y);
        return Size{radius, radius};
    }
    case RadialExtent::closest_corner: {
        // An ellipse keeps closest-side's proportions.
        if (!gradient.circle) return Size{near_x * square_root_of_two, near_y * square_root_of_two};
        const double radius = std::sqrt(near_x * near_x + near_y * near_y);
        return Size{radius, radius};
    }
    case RadialExtent::farthest_corner: {
        if (!gradient.circle) return Size{far_x * square_root_of_two, far_y * square_root_of_two};
        const double radius = std::sqrt(far_x * far_x + far_y * far_y);
        return Size{radius, radius};
    }
    }
    return Size{};
}

void resolve_radial(const AuthoredGradient& gradient, Size box, MaterialCoordinateSpace space,
                    std::vector<GradientStop>& placed, MaterialFillLayer& layer) {
    const Point centre{resolved_length(gradient.centre_x, box.width), resolved_length(gradient.centre_y, box.height)};
    Size radii = radial_radii(gradient, centre, box);
    if (!(radii.width > 0.0) || !(radii.height > 0.0)) {
        // A gradient of no size draws its last colour, as CSS draws a
        // circle of none.
        layer = MaterialFillLayer::solid(gradient.stops.back().colour);
        return;
    }
    // The gradient ray is the horizontal radius.
    place_stops(gradient.stops, radii.width, placed);
    std::vector<GradientStop> stops{};
    stops.reserve(maximum_gradient_stops);
    if (!gradient.repeating) {
        const double last = placed.back().offset;
        if (last > 1.0) {
            radii = Size{radii.width * last, radii.height * last};
            for (GradientStop& stop : placed) stop.offset = stop.offset / last;
        }
        clipped_stops(placed, stops);
        separate_stops(stops);
        layer = MaterialFillLayer::radial(centre, radii, std::move(stops), space);
        return;
    }
    const double first = placed.front().offset;
    const double span = placed.back().offset - first;
    if (span <= 0.0) {
        layer = MaterialFillLayer::solid(average_colour(gradient.stops));
        return;
    }
    // The radii of one period; the engine's periods begin at the centre,
    // so the stops are rotated by the first stop's place in its period.
    radii = Size{radii.width * span, radii.height * span};
    std::vector<GradientStop> period{};
    period.reserve(maximum_gradient_stops);
    period_stops(placed, first, span, period);
    const double periods = first / span;
    const double phase = periods - std::floor(periods);
    if (phase == 0.0) {
        stops = std::move(period);
    } else {
        rotated_stops(period, phase, stops);
    }
    separate_stops(stops);
    layer = MaterialFillLayer::repeating_radial(centre, radii, std::move(stops), space);
}

void resolve_conic(const AuthoredGradient& gradient, Size box, MaterialCoordinateSpace space,
                   std::vector<GradientStop>& placed, MaterialFillLayer& layer) {
    const Point centre{resolved_length(gradient.centre_x, box.width), resolved_length(gradient.centre_y, box.height)};
    place_stops(gradient.stops, 0.0, placed);
    std::vector<GradientStop> stops{};
    stops.reserve(maximum_gradient_stops);
    if (!gradient.repeating) {
        clipped_stops(placed, stops);
        separate_stops(stops);
        layer = MaterialFillLayer::conic(centre, gradient.angle_degrees, std::move(stops), space);
        return;
    }
    const double first = placed.front().offset;
    const double span = placed.back().offset - first;
    if (span <= 0.0) {
        layer = MaterialFillLayer::solid(average_colour(gradient.stops));
        return;
    }
    const double from = gradient.angle_degrees + first * 360.0;
    if (span >= 1.0) {
        // One period covers the turn: it never repeats within it.
        for (GradientStop& stop : placed) stop.offset = stop.offset - first;
        clipped_stops(placed, stops);
        separate_stops(stops);
        layer = MaterialFillLayer::conic(centre, from, std::move(stops), space);
        return;
    }
    period_stops(placed, first, span, stops);
    separate_stops(stops);
    layer = MaterialFillLayer::repeating_conic(centre, from, span * 360.0, std::move(stops), space);
}

} // namespace

bool valid_authored_gradient(const AuthoredGradient& gradient) noexcept {
    const bool gradient_kind = gradient.kind == MaterialFillKind::linear_gradient ||
                               gradient.kind == MaterialFillKind::radial_gradient ||
                               gradient.kind == MaterialFillKind::conic_gradient;
    if (!gradient_kind || gradient.stops.empty() || gradient.stops.size() > maximum_authored_gradient_stops) {
        return false;
    }
    if (!std::isfinite(gradient.angle_degrees) || std::abs(gradient.angle_degrees) > 360'000.0) return false;
    if (static_cast<std::uint8_t>(gradient.corner) > static_cast<std::uint8_t>(GradientCorner::bottom_left) ||
        static_cast<std::uint8_t>(gradient.extent) > static_cast<std::uint8_t>(RadialExtent::farthest_corner)) {
        return false;
    }
    if (!finite_length(gradient.centre_x) || !finite_length(gradient.centre_y) ||
        !finite_length(gradient.radius_x) || !finite_length(gradient.radius_y)) {
        return false;
    }
    const bool lengths = gradient.extent == RadialExtent::lengths;
    const bool negative = gradient.radius_x.fraction < 0.0 || gradient.radius_x.units < 0.0 ||
                          gradient.radius_y.fraction < 0.0 || gradient.radius_y.units < 0.0;
    if (lengths && negative) return false;
    if (lengths && gradient.circle && gradient.radius_x.fraction != 0.0) return false;
    for (const AuthoredGradientStop& stop : gradient.stops) {
        if (!finite_length(stop.position)) return false;
        if (gradient.kind == MaterialFillKind::conic_gradient && stop.position.units != 0.0) return false;
    }
    return true;
}

bool authored_gradient_fixed(const AuthoredGradient& gradient) noexcept {
    if (gradient.corner != GradientCorner::none || stops_have_units(gradient.stops)) return false;
    if (has_units(gradient.centre_x) || has_units(gradient.centre_y)) return false;
    if (gradient.kind == MaterialFillKind::radial_gradient) {
        const bool fixed = !gradient.circle && !has_units(gradient.radius_x) && !has_units(gradient.radius_y);
        return fixed;
    }
    if (gradient.kind != MaterialFillKind::linear_gradient || !gradient.repeating) return true;
    // A repeating linear gradient keeps its CSS angle only over the whole
    // line; another period needs the box's line.
    std::vector<GradientStop> placed{};
    placed.reserve(gradient.stops.size());
    place_stops(gradient.stops, 0.0, placed);
    const bool whole = placed.front().offset == 0.0 && placed.back().offset == 1.0;
    return whole;
}

void resolve_authored_gradient(const AuthoredGradient& gradient, Size box, MaterialFillLayer& layer) {
    if (gradient.stops.size() == 1U) {
        layer = MaterialFillLayer::solid(gradient.stops.front().colour);
        return;
    }
    const bool fixed = authored_gradient_fixed(gradient);
    // A fixed gradient's geometry is a fraction of the box: resolved for a
    // unit box, in normalized coordinates.
    const Size extent = fixed ? Size{1.0, 1.0} : box;
    const MaterialCoordinateSpace space = fixed ? MaterialCoordinateSpace::normalized : MaterialCoordinateSpace::logical;
    std::vector<GradientStop> placed{};
    placed.reserve(gradient.stops.size());
    switch (gradient.kind) {
    case MaterialFillKind::linear_gradient: resolve_linear(gradient, extent, placed, layer); return;
    case MaterialFillKind::radial_gradient: resolve_radial(gradient, extent, space, placed, layer); return;
    case MaterialFillKind::conic_gradient: resolve_conic(gradient, extent, space, placed, layer); return;
    case MaterialFillKind::solid:
    case MaterialFillKind::image:
    case MaterialFillKind::procedural:
    case MaterialFillKind::finish: break;
    // A value outside the enumeration is no gradient: the empty layer.
    default: break;
    }
    layer = MaterialFillLayer{};
}

} // namespace gui_forms
