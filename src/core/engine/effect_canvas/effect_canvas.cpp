#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace gui_forms {
namespace {

// A colour with its alpha scaled by `factor` in [0, 1].
Color faded(Color color, double factor) noexcept {
    const double alpha = std::clamp(static_cast<double>(color.alpha) * factor, 0.0, 255.0);
    const Color result = Color::rgba(color.red, color.green, color.blue, static_cast<std::uint8_t>(alpha));
    return result;
}

bool finite_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

// The live stops of `gradient`, or none when its count is out of range.
std::span<const GradientStop> live_stops(const EffectGradient& gradient) noexcept {
    if (gradient.count < 2U || gradient.count > gradient.stops.size()) return {};
    const std::span<const GradientStop> stops(gradient.stops.data(), gradient.count);
    return stops;
}

// The live elements of a batch, or none when its count is past its
// capacity or below `minimum`.
template <typename Item>
std::span<const Item> live_batch(const EffectArray<Item, maximum_effect_batch>& batch, std::size_t minimum) noexcept {
    if (batch.count < minimum || batch.count > batch.items.size()) return {};
    const std::span<const Item> items(batch.items.data(), batch.count);
    return items;
}

// The smallest rectangle holding every point, or a rectangle that is not
// finite when a point is not.
Rect points_bounds(std::span<const Point> points) noexcept {
    double left = points.front().x;
    double top = points.front().y;
    double right = left;
    double bottom = top;
    for (const Point point : points) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    bool finite = true;
    for (const Point point : points) finite = finite && finite_point(point);
    const double poisoned = finite ? 0.0 : std::numeric_limits<double>::quiet_NaN();
    const Rect bounds{left + poisoned, top, right - left, bottom - top};
    return bounds;
}

// `bounds` grown by `margin` on every side.
Rect grown(Rect bounds, double margin) noexcept {
    const Rect result{bounds.x - margin, bounds.y - margin, bounds.width + margin * 2.0,
                      bounds.height + margin * 2.0};
    return result;
}

// The summed length of a polyline's edges.
double polyline_length(std::span<const Point> points) noexcept {
    double length = 0.0;
    for (std::size_t index = 1U; index < points.size(); ++index) {
        length = length + std::hypot(points[index].x - points[index - 1U].x, points[index].y - points[index - 1U].y);
    }
    return length;
}

} // namespace

EffectCanvas::EffectCanvas(Painter& painter, Size part, Insets outsets, double scale,
                           std::uint32_t per_device_pixel, PartCornerRadii corners,
                           const std::shared_ptr<const PartRaster>* backdrop) noexcept
    : painter_(painter), scale_(scale), part_(part), corners_(corners),
      layer_{-outsets.left, -outsets.top, part.width + outsets.left + outsets.right,
             part.height + outsets.top + outsets.bottom},
      backdrop_(backdrop) {
    const Rect layer = layer_;
    levels_[0].clip = layer;
    const double device_area = layer.area() * scale * scale;
    meter_.budget = device_area * static_cast<double>(per_device_pixel);
}

EffectCanvas::~EffectCanvas() {
    // Leaves the painter's save depth as the canvas found it.
    while (painter_saves_ > 0U) {
        painter_.restore();
        painter_saves_ = painter_saves_ - 1U;
    }
}

Rect EffectCanvas::to_part(Rect rect) const noexcept {
    const Point offset = levels_[depth_].offset;
    const Rect moved{rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
    return moved;
}

bool EffectCanvas::charge(Rect bounds, double coverage, std::uint32_t per_pixel) noexcept {
    meter_.calls = meter_.calls + 1U;
    if (!bounds.finite() || !std::isfinite(coverage)) {
        meter_.refused = meter_.refused + 1U;
        return false;
    }
    const Rect visible = Rect::intersection(to_part(bounds), levels_[depth_].clip);
    const double area = std::min(visible.area(), std::max(0.0, coverage));
    const double cost = area * scale_ * scale_ * static_cast<double>(per_pixel);
    if (meter_.charged + cost > meter_.budget) {
        meter_.refused = meter_.refused + 1U;
        return false;
    }
    meter_.charged = meter_.charged + cost;
    return true;
}

void EffectCanvas::refuse() noexcept {
    meter_.calls = meter_.calls + 1U;
    meter_.refused = meter_.refused + 1U;
}

void EffectCanvas::fill_rect(Rect rect, Color color) noexcept {
    if (!charge(rect, rect.area(), 1U)) return;
    painter_.fill_rect(rect, color);
}

void EffectCanvas::fill_rounded_rect(Rect rect, double radius, Color color) noexcept {
    if (!std::isfinite(radius) || !charge(rect, rect.area(), 2U)) return;
    painter_.fill_rounded_rect(rect, std::max(0.0, radius), color);
}

void EffectCanvas::fill_circle(Point center, double radius, Color color) noexcept {
    const Rect box{center.x - radius, center.y - radius, radius * 2.0, radius * 2.0};
    if (!finite_point(center) || !(radius > 0.0) || !charge(box, box.area(), 2U)) return;
    painter_.fill_rounded_rect(box, radius, color);
}

void EffectCanvas::stroke_rect(Rect rect, Color color, double width) noexcept {
    const double perimeter = 2.0 * (std::abs(rect.width) + std::abs(rect.height));
    const Rect bounds{rect.x - width * 0.5, rect.y - width * 0.5, rect.width + width, rect.height + width};
    if (!(width > 0.0) || !charge(bounds, perimeter * width, 2U)) return;
    painter_.stroke_rect(rect, color, width);
}

void EffectCanvas::stroke_circle(Point center, double radius, Color color, double width) noexcept {
    constexpr double tau = 6.28318530717958647692;
    const double outer = radius + width * 0.5;
    const Rect bounds{center.x - outer, center.y - outer, outer * 2.0, outer * 2.0};
    const Rect box{center.x - radius, center.y - radius, radius * 2.0, radius * 2.0};
    if (!finite_point(center) || !(radius > 0.0) || !(width > 0.0) ||
        !charge(bounds, tau * radius * width, 2U)) {
        return;
    }
    painter_.stroke_rounded_rect(box, radius, color, width);
}

void EffectCanvas::stroke_line(Point from, Point to, Color color, double width) noexcept {
    const double half = width * 0.5;
    const Rect bounds{std::min(from.x, to.x) - half, std::min(from.y, to.y) - half,
                      std::abs(to.x - from.x) + width, std::abs(to.y - from.y) + width};
    const double length = std::hypot(to.x - from.x, to.y - from.y);
    if (!finite_point(from) || !finite_point(to) || !(width > 0.0) ||
        !charge(bounds, (length + width) * width, 2U)) {
        return;
    }
    painter_.draw_line(from, to, color, width);
}

void EffectCanvas::fill_linear_gradient(Rect rect, Point start, Point end, const EffectGradient& gradient,
                                        bool repeat) noexcept {
    const std::span<const GradientStop> stops = live_stops(gradient);
    if (stops.empty() || !finite_point(start) || !finite_point(end) || !charge(rect, rect.area(), 3U)) return;
    if (repeat) {
        painter_.fill_linear_gradient_spread(rect, start, end, stops, GradientSpreadMode::repeat);
        return;
    }
    painter_.fill_linear_gradient(rect, start, end, stops);
}

void EffectCanvas::fill_radial_gradient(Rect rect, Point center, Size radii, const EffectGradient& gradient) noexcept {
    const std::span<const GradientStop> stops = live_stops(gradient);
    if (stops.empty() || !finite_point(center) || !(radii.width > 0.0) || !(radii.height > 0.0) ||
        !charge(rect, rect.area(), 4U)) {
        return;
    }
    painter_.fill_radial_gradient(rect, center, radii, stops);
}

void EffectCanvas::soft_dot(Point center, double radius, Color color) noexcept {
    const Rect box{center.x - radius, center.y - radius, radius * 2.0, radius * 2.0};
    if (!finite_point(center) || !(radius > 0.0) || !charge(box, box.area(), 4U)) return;
    const std::array<GradientStop, 3> stops{{
        {0.0, color},
        {0.45, faded(color, 0.5)},
        {1.0, faded(color, 0.0)},
    }};
    painter_.fill_radial_gradient(box, center, {radius, radius}, stops);
}

void EffectCanvas::streak(Point head, Point tail, double width, Color color) noexcept {
    const Rect bounds{std::min(head.x, tail.x) - width, std::min(head.y, tail.y) - width,
                      std::abs(head.x - tail.x) + width * 2.0, std::abs(head.y - tail.y) + width * 2.0};
    const double length = std::hypot(head.x - tail.x, head.y - tail.y);
    if (!finite_point(head) || !finite_point(tail) || !(width > 0.0) ||
        !charge(bounds, (length + width * 4.0) * width, 4U)) {
        return;
    }
    // The tail is fainter than the head.
    painter_.draw_line(tail, head, faded(color, 0.55), width);
    const Rect dot{head.x - width, head.y - width, width * 2.0, width * 2.0};
    const std::array<GradientStop, 2> stops{{{0.0, color}, {1.0, faded(color, 0.0)}}};
    painter_.fill_radial_gradient(dot, head, {width, width}, stops);
}

void EffectCanvas::stroke_polyline(const EffectPoints& points, const StrokeStyle& style) noexcept {
    const std::span<const Point> live = live_batch(points, 2U);
    if (live.empty() || !valid_stroke_style(style)) {
        refuse();
        return;
    }
    // A miter can reach `miter_limit` half widths past a joint.
    const double reach = style.width * 0.5 * std::max(1.0, style.miter_limit);
    const double coverage = (polyline_length(live) + style.width * static_cast<double>(live.size())) * style.width;
    if (!charge(grown(points_bounds(live), reach), coverage, 2U)) return;
    painter_.stroke_polyline(live, style);
}

void EffectCanvas::fill_polygon(const EffectPoints& points, Color color, PolygonFillRule rule) noexcept {
    const std::span<const Point> live = live_batch(points, 3U);
    const bool known_rule = rule == PolygonFillRule::nonzero || rule == PolygonFillRule::even_odd;
    if (live.empty() || !known_rule) {
        refuse();
        return;
    }
    const Rect bounds = points_bounds(live);
    if (!charge(bounds, bounds.area(), 2U)) return;
    painter_.fill_polygon(live, color, rule);
}

void EffectCanvas::fill_band(const EffectPoints& upper, const EffectPoints& lower, Color color) noexcept {
    const std::span<const Point> top = live_batch(upper, 2U);
    const std::span<const Point> bottom = live_batch(lower, 2U);
    if (top.empty() || top.size() != bottom.size()) {
        refuse();
        return;
    }
    const Rect bounds = Rect::united(points_bounds(top), points_bounds(bottom));
    if (!charge(bounds, bounds.area(), 2U)) return;
    painter_.fill_band(top, bottom, color);
}

void EffectCanvas::fill_rects(const EffectRects& rects, Color color) noexcept {
    const std::span<const Rect> live = live_batch(rects, 1U);
    if (live.empty()) {
        refuse();
        return;
    }
    Rect bounds = live.front();
    double coverage = 0.0;
    for (const Rect rect : live) {
        bounds = Rect::united(bounds, rect);
        coverage = coverage + std::abs(rect.area());
    }
    if (!charge(bounds, coverage, 1U)) return;
    painter_.fill_rects(live, color);
}

void EffectCanvas::stroke_segments(const EffectSegments& segments, const StrokeStyle& style) noexcept {
    const std::span<const PaintSegment> live = live_batch(segments, 1U);
    if (live.empty() || !valid_stroke_style(style)) {
        refuse();
        return;
    }
    // The bounds of every end, and the length the strokes cover.
    std::array<Point, 2U * maximum_effect_batch> ends{};
    double length = 0.0;
    for (std::size_t index = 0U; index < live.size(); ++index) {
        ends[2U * index] = live[index].from;
        ends[2U * index + 1U] = live[index].to;
        length = length + std::hypot(live[index].to.x - live[index].from.x, live[index].to.y - live[index].from.y);
    }
    const std::span<const Point> points(ends.data(), 2U * live.size());
    const double coverage = (length + style.width * static_cast<double>(live.size())) * style.width;
    if (!charge(grown(points_bounds(points), style.width), coverage, 2U)) return;
    painter_.stroke_segments(live, style);
}

void EffectCanvas::draw_markers(const EffectPoints& centers, const MarkerStyle& style) noexcept {
    const std::span<const Point> live = live_batch(centers, 1U);
    if (live.empty() || !valid_marker_style(style)) {
        refuse();
        return;
    }
    const double extent = style.size + style.edge_width;
    const double coverage = extent * extent * static_cast<double>(live.size());
    if (!charge(grown(points_bounds(live), extent * 0.5), coverage, 3U)) return;
    painter_.draw_markers(live, style);
}

void EffectCanvas::fill_conic_gradient(Rect rect, Point center, double start_degrees,
                                       const EffectGradient& gradient) noexcept {
    const std::span<const GradientStop> stops = live_stops(gradient);
    if (stops.empty() || !finite_point(center) || !std::isfinite(start_degrees)) {
        refuse();
        return;
    }
    if (!charge(rect, rect.area(), 4U)) return;
    painter_.fill_conic_gradient(rect, center, start_degrees, stops);
}

void EffectCanvas::fill_checker(Rect rect, const CheckerPattern& pattern) noexcept {
    const bool cell = std::isfinite(pattern.cell) && pattern.cell > 0.0 && finite_point(pattern.anchor);
    if (!cell) {
        refuse();
        return;
    }
    if (!charge(rect, rect.area(), 2U)) return;
    painter_.fill_checker(rect, pattern);
}

void EffectCanvas::save() noexcept {
    if (depth_ >= maximum_effect_save_depth) {
        meter_.refused = meter_.refused + 1U;
        return;
    }
    levels_[depth_ + 1U] = levels_[depth_];
    levels_[depth_ + 1U].painter_saved = false;
    depth_ = depth_ + 1U;
}

void EffectCanvas::restore() noexcept {
    if (depth_ == 0U) return;
    if (levels_[depth_].painter_saved) {
        painter_.restore();
        painter_saves_ = painter_saves_ - 1U;
    }
    depth_ = depth_ - 1U;
}

void EffectCanvas::clip_rect(Rect rect) noexcept {
    if (!rect.finite()) return;
    Level& level = levels_[depth_];
    level.clip = Rect::intersection(level.clip, to_part(rect));
    // The painter's own state changes only inside a save, so the level's
    // restore undoes it; at depth 0 the canvas saves once for the frame.
    if (!level.painter_saved) {
        painter_.save();
        painter_saves_ = painter_saves_ + 1U;
        level.painter_saved = true;
    }
    painter_.clip_rect(rect);
}

void EffectCanvas::clip_part_shape() noexcept {
    Level& level = levels_[depth_];
    const Rect part{0.0, 0.0, part_.width, part_.height};
    level.clip = Rect::intersection(level.clip, part);
    if (!level.painter_saved) {
        painter_.save();
        painter_saves_ = painter_saves_ + 1U;
        level.painter_saved = true;
    }
    // The part's box in the painter's current coordinates, which the
    // canvas's offset has moved.
    const Rect box{-level.offset.x, -level.offset.y, part_.width, part_.height};
    PaintCornerRadii radii{};
    radii.top_left = corners_.top_left;
    radii.top_right = corners_.top_right;
    radii.bottom_right = corners_.bottom_right;
    radii.bottom_left = corners_.bottom_left;
    painter_.clip_rounded_rect_corners(box, radii);
}

void EffectCanvas::draw_backdrop(Rect rect, double opacity) noexcept {
    const bool lent = backdrop_ != nullptr && *backdrop_ != nullptr;
    if (!lent || !std::isfinite(opacity)) {
        meter_.calls = meter_.calls + 1U;
        meter_.refused = meter_.refused + 1U;
        return;
    }
    if (!charge(rect, rect.area(), 2U)) return;
    // The layer's corner in the painter's current coordinates, which the
    // canvas's offset has moved.
    const Level& level = levels_[depth_];
    const Point origin{layer_.x - level.offset.x, layer_.y - level.offset.y};
    painter_.save();
    painter_.clip_rect(rect);
    painter_.draw_part_raster(*backdrop_, origin, std::clamp(opacity, 0.0, 1.0));
    painter_.restore();
}

void EffectCanvas::translate(Point offset) noexcept {
    if (!finite_point(offset)) return;
    Level& level = levels_[depth_];
    if (!level.painter_saved) {
        painter_.save();
        painter_saves_ = painter_saves_ + 1U;
        level.painter_saved = true;
    }
    level.offset = Point{level.offset.x + offset.x, level.offset.y + offset.y};
    painter_.translate(offset);
}

} // namespace gui_forms
