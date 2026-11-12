#include "gui_forms/scene/scene_view/scene_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gui_forms {

namespace {

[[nodiscard]] bool finite_point(WorldPoint point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

// The outward unit normal of an edge (y down).
[[nodiscard]] WorldPoint edge_normal(SceneEdge edge) noexcept {
    switch (edge) {
    case SceneEdge::left: return WorldPoint{-1.0, 0.0};
    case SceneEdge::right: return WorldPoint{1.0, 0.0};
    case SceneEdge::top: return WorldPoint{0.0, -1.0};
    case SceneEdge::bottom: return WorldPoint{0.0, 1.0};
    default: break;
    }
    // An unknown edge leaves to the right, as an output does.
    return WorldPoint{1.0, 0.0};
}

// The tangent length at one end: `along` is the far end's distance along
// this end's normal (negative when it lies behind the edge), `sideways`
// its distance across it.
[[nodiscard]] double tangent_length(double along, double sideways, const SoftLinkShape& shape) noexcept {
    double length = std::fabs(along) * 0.5;
    if (length < shape.shortest) length = shape.shortest;
    if (length > shape.longest) length = shape.longest;
    if (along < 0.0) length += std::fabs(sideways) * shape.back_link;
    return length;
}

[[nodiscard]] double distance_to_segment(Point a, Point b, Point point) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    double t = 0.0;
    if (length_squared > 0.0) t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / length_squared;
    t = std::clamp(t, 0.0, 1.0);
    const double nearest_x = a.x + dx * t;
    const double nearest_y = a.y + dy * t;
    const double distance = std::hypot(point.x - nearest_x, point.y - nearest_y);
    return distance;
}

// The turn a -> b -> c: positive one way, negative the other, zero when
// collinear.
[[nodiscard]] double turn(Point a, Point b, Point c) noexcept {
    const double value = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return value;
}

[[nodiscard]] bool within(double value, double low, double high) noexcept {
    const double lower = std::min(low, high);
    const double upper = std::max(low, high);
    const bool inside = value >= lower && value <= upper;
    return inside;
}

[[nodiscard]] bool opposite_signs(double first, double second) noexcept {
    const bool opposite = (first > 0.0 && second < 0.0) || (first < 0.0 && second > 0.0);
    return opposite;
}

} // namespace

// -- the view transform ------------------------------------------------------------

bool valid_scene_view(const SceneViewTransform& view) noexcept {
    const bool valid = finite_point(view.origin) && std::isfinite(view.zoom) && view.zoom > 0.0;
    return valid;
}

double clamp_scene_zoom(double zoom, SceneZoomRange range) noexcept {
    if (!(zoom > range.minimum)) return range.minimum;
    if (zoom > range.maximum) return range.maximum;
    return zoom;
}

SceneViewTransform scene_zoomed_about(const SceneViewTransform& view, Point anchor, double factor,
                                      SceneZoomRange range) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0 || !valid_scene_view(view)) return view;
    const WorldPoint fixed = view.to_world(anchor);
    SceneViewTransform zoomed{};
    zoomed.zoom = clamp_scene_zoom(view.zoom * factor, range);
    zoomed.origin.x = fixed.x - anchor.x / zoomed.zoom;
    zoomed.origin.y = fixed.y - anchor.y / zoomed.zoom;
    return zoomed;
}

SceneViewTransform scene_panned(const SceneViewTransform& view, Point delta) noexcept {
    if (!valid_scene_view(view) || !std::isfinite(delta.x) || !std::isfinite(delta.y)) return view;
    SceneViewTransform panned = view;
    panned.origin.x -= delta.x / view.zoom;
    panned.origin.y -= delta.y / view.zoom;
    return panned;
}

SceneViewTransform scene_framed(const SceneViewTransform& view, const WorldRect& world, Size size, double margin,
                                SceneZoomRange range) noexcept {
    const double usable_width = size.width - 2.0 * margin;
    const double usable_height = size.height - 2.0 * margin;
    if (!valid_world_rect(world) || !(usable_width > 0.0) || !(usable_height > 0.0) || !valid_scene_view(view)) {
        return view;
    }
    SceneViewTransform framed{};
    framed.zoom = view.zoom;
    if (world.width > 0.0 && world.height > 0.0) {
        framed.zoom = clamp_scene_zoom(std::min(usable_width / world.width, usable_height / world.height), range);
    } else if (world.width > 0.0) {
        framed.zoom = clamp_scene_zoom(usable_width / world.width, range);
    } else if (world.height > 0.0) {
        framed.zoom = clamp_scene_zoom(usable_height / world.height, range);
    }
    const double centre_x = world.x + world.width / 2.0;
    const double centre_y = world.y + world.height / 2.0;
    framed.origin.x = centre_x - size.width / (2.0 * framed.zoom);
    framed.origin.y = centre_y - size.height / (2.0 * framed.zoom);
    return framed;
}

// -- level of detail -----------------------------------------------------------------

bool valid_scene_detail_thresholds(const SceneDetailThresholds& thresholds) noexcept {
    const bool finite = std::isfinite(thresholds.silhouette) && std::isfinite(thresholds.compact) &&
                        std::isfinite(thresholds.full) && std::isfinite(thresholds.hysteresis);
    const bool ordered = thresholds.silhouette > 0.0 && thresholds.silhouette < thresholds.compact &&
                         thresholds.compact < thresholds.full;
    const bool valid = finite && ordered && thresholds.hysteresis >= 1.0;
    return valid;
}

SceneDetailLevel scene_detail_level_for(double zoom, const SceneDetailThresholds& thresholds) noexcept {
    if (!std::isfinite(zoom) || !valid_scene_detail_thresholds(thresholds)) return SceneDetailLevel::full;
    if (zoom >= thresholds.full) return SceneDetailLevel::full;
    if (zoom >= thresholds.compact) return SceneDetailLevel::compact;
    if (zoom >= thresholds.silhouette) return SceneDetailLevel::silhouette;
    return SceneDetailLevel::overview;
}

SceneDetailLevel next_scene_detail_level(SceneDetailLevel current, double zoom,
                                         const SceneDetailThresholds& thresholds) noexcept {
    if (!std::isfinite(zoom) || !valid_scene_detail_thresholds(thresholds)) return current;
    const double factor = thresholds.hysteresis;
    // The threshold between level k and level k + 1, for k = 0 to 2.
    const std::array<double, 3> between{thresholds.silhouette, thresholds.compact, thresholds.full};
    std::size_t level = static_cast<std::size_t>(current);
    if (level > 3U) level = static_cast<std::size_t>(scene_detail_level_for(zoom, thresholds));
    // Upward while the next threshold is passed with its margin; then
    // downward while the current one is lost with its margin. A level is
    // never both, since threshold / h <= threshold * h.
    while (level < 3U && zoom >= between[level] * factor) ++level;
    while (level > 0U && zoom < between[level - 1U] / factor) --level;
    const SceneDetailLevel next = static_cast<SceneDetailLevel>(level);
    return next;
}

std::string_view scene_detail_level_name(SceneDetailLevel level) noexcept {
    switch (level) {
    case SceneDetailLevel::overview: return "overview";
    case SceneDetailLevel::silhouette: return "silhouette";
    case SceneDetailLevel::compact: return "compact";
    case SceneDetailLevel::full: return "full";
    default: break;
    }
    return "unknown";
}

// -- links ----------------------------------------------------------------------------

SceneCubic soft_link_cubic(WorldPoint from, SceneEdge from_edge, WorldPoint to, SceneEdge to_edge,
                           const SoftLinkShape& shape) noexcept {
    const WorldPoint out_normal = edge_normal(from_edge);
    const WorldPoint in_normal = edge_normal(to_edge);
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    // Along each end's normal toward the other end, and across it.
    const double from_along = dx * out_normal.x + dy * out_normal.y;
    const double from_sideways = dx * out_normal.y - dy * out_normal.x;
    const double to_along = -(dx * in_normal.x + dy * in_normal.y);
    const double to_sideways = dx * in_normal.y - dy * in_normal.x;
    const double from_length = tangent_length(from_along, from_sideways, shape);
    const double to_length = tangent_length(to_along, to_sideways, shape);
    SceneCubic cubic{};
    cubic.p0 = from;
    cubic.p1 = WorldPoint{from.x + out_normal.x * from_length, from.y + out_normal.y * from_length};
    cubic.p2 = WorldPoint{to.x + in_normal.x * to_length, to.y + in_normal.y * to_length};
    cubic.p3 = to;
    return cubic;
}

WorldRect scene_cubic_bounds(const SceneCubic& cubic) noexcept {
    const double left = std::min(std::min(cubic.p0.x, cubic.p1.x), std::min(cubic.p2.x, cubic.p3.x));
    const double right = std::max(std::max(cubic.p0.x, cubic.p1.x), std::max(cubic.p2.x, cubic.p3.x));
    const double top = std::min(std::min(cubic.p0.y, cubic.p1.y), std::min(cubic.p2.y, cubic.p3.y));
    const double bottom = std::max(std::max(cubic.p0.y, cubic.p1.y), std::max(cubic.p2.y, cubic.p3.y));
    const WorldRect bounds{left, top, right - left, bottom - top};
    return bounds;
}

std::size_t flatten_scene_cubic(const SceneCubic& cubic, std::size_t segments, const SceneViewTransform& view,
                                std::span<Point> out) noexcept {
    const std::size_t pieces = segments == 0U ? 1U : segments;
    const std::size_t count = std::min(pieces + 1U, out.size());
    const double step = 1.0 / static_cast<double>(pieces);
    for (std::size_t index = 0U; index < count; ++index) {
        const double t = static_cast<double>(index) * step;
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        const WorldPoint world{b0 * cubic.p0.x + b1 * cubic.p1.x + b2 * cubic.p2.x + b3 * cubic.p3.x,
                               b0 * cubic.p0.y + b1 * cubic.p1.y + b2 * cubic.p2.y + b3 * cubic.p3.y};
        out[index] = view.to_view(world);
    }
    return count;
}

double scene_distance_to_polyline(std::span<const Point> points, Point point) noexcept {
    if (points.empty()) return std::numeric_limits<double>::infinity();
    if (points.size() == 1U) return std::hypot(point.x - points[0].x, point.y - points[0].y);
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 1U; index < points.size(); ++index) {
        const double distance = distance_to_segment(points[index - 1U], points[index], point);
        if (distance < nearest) nearest = distance;
    }
    return nearest;
}

bool scene_segments_cross(Point a, Point b, Point c, Point d) noexcept {
    const double d1 = turn(c, d, a);
    const double d2 = turn(c, d, b);
    const double d3 = turn(a, b, c);
    const double d4 = turn(a, b, d);
    if (opposite_signs(d1, d2) && opposite_signs(d3, d4)) return true;
    // Collinear touching: an end lies on the other segment.
    if (d1 == 0.0 && within(a.x, c.x, d.x) && within(a.y, c.y, d.y)) return true;
    if (d2 == 0.0 && within(b.x, c.x, d.x) && within(b.y, c.y, d.y)) return true;
    if (d3 == 0.0 && within(c.x, a.x, b.x) && within(c.y, a.y, b.y)) return true;
    if (d4 == 0.0 && within(d.x, a.x, b.x) && within(d.y, a.y, b.y)) return true;
    return false;
}

bool scene_polyline_crosses_segment(std::span<const Point> points, Point c, Point d) noexcept {
    for (std::size_t index = 1U; index < points.size(); ++index) {
        if (scene_segments_cross(points[index - 1U], points[index], c, d)) return true;
    }
    return false;
}

} // namespace gui_forms
