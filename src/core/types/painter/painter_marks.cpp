// The base Painter's fallbacks for the batched marks and patterns
// (painter.hpp, paint_marks.hpp). Each validates its whole input before it
// draws anything, then draws through the minimal primitives (fill_rect,
// draw_line, the rounded fills and clips), with fixed-size storage only.
#include "gui_forms/types.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "painter_support.hpp"
#include "../paint_marks/grid_lines.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gui_forms {
namespace {

constexpr double degrees_to_radians = 0.017453292519943295769;

bool finite_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool all_finite(std::span<const Point> points) noexcept {
    for (const Point point : points) {
        if (!finite_point(point)) return false;
    }
    return true;
}

// One crossing of a scan row with a polygon edge: where, and whether the
// edge runs down (+1) or up (-1).
struct ScanCrossing final {
    double x{};
    int winding{};
};

bool crossing_before(const ScanCrossing& left, const ScanCrossing& right) noexcept {
    return left.x < right.x;
}

// The most crossings one scan row keeps; an outline crossing a row more
// often than this is filled from its first crossings only.
constexpr std::size_t maximum_scan_crossings = 256U;
// The most scan rows a fallback polygon fill draws; a taller polygon is
// filled in proportionally taller rows.
constexpr double maximum_scan_rows = 4096.0;

// Fills one scan row's spans between sorted crossings: inside where the
// crossings so far are odd in number (even-odd) or their windings do not
// sum to zero (nonzero).
using SpanFiller = void (*)(Painter& painter, std::span<const ScanCrossing> crossings, double top, double height,
                            Color color);

void fill_even_odd_spans(Painter& painter, std::span<const ScanCrossing> crossings, double top, double height,
                         Color color) {
    for (std::size_t index = 0U; index + 1U < crossings.size(); index = index + 2U) {
        const double left = crossings[index].x;
        const double right = crossings[index + 1U].x;
        if (right > left) painter.fill_rect(Rect{left, top, right - left, height}, color);
    }
}

void fill_nonzero_spans(Painter& painter, std::span<const ScanCrossing> crossings, double top, double height,
                        Color color) {
    int winding = 0;
    for (std::size_t index = 0U; index + 1U < crossings.size(); ++index) {
        winding = winding + crossings[index].winding;
        if (winding == 0) continue;
        const double left = crossings[index].x;
        const double right = crossings[index + 1U].x;
        if (right > left) painter.fill_rect(Rect{left, top, right - left, height}, color);
    }
}

// Fills `points` by scan rows one unit tall (or taller, see above), each
// row's spans taken at its centre under `rule`. Inputs validated.
void scan_fill_polygon(Painter& painter, std::span<const Point> points, Color color, PolygonFillRule rule) {
    double top = points[0].y;
    double bottom = points[0].y;
    for (const Point point : points) {
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    const double first_row = std::floor(top);
    const double height = std::ceil(bottom) - first_row;
    if (!(height > 0.0)) return;
    const double step = height > maximum_scan_rows ? height / maximum_scan_rows : 1.0;
    const double rows = std::ceil(height / step);
    const SpanFiller fill_spans = rule == PolygonFillRule::even_odd ? &fill_even_odd_spans : &fill_nonzero_spans;
    std::array<ScanCrossing, maximum_scan_crossings> crossings{};
    for (double row = 0.0; row < rows; row = row + 1.0) {
        const double row_top = first_row + row * step;
        const double sample = row_top + step * 0.5;
        std::size_t count = 0U;
        for (std::size_t index = 0U; index < points.size() && count < crossings.size(); ++index) {
            const Point from = points[index];
            const Point to = points[(index + 1U) % points.size()];
            const bool downward = from.y <= sample && to.y > sample;
            const bool upward = to.y <= sample && from.y > sample;
            if (!downward && !upward) continue;
            const double amount = (sample - from.y) / (to.y - from.y);
            crossings[count].x = from.x + (to.x - from.x) * amount;
            crossings[count].winding = downward ? 1 : -1;
            count = count + 1U;
        }
        std::sort(crossings.begin(), crossings.begin() + static_cast<std::ptrdiff_t>(count), &crossing_before);
        fill_spans(painter, std::span<const ScanCrossing>(crossings.data(), count), row_top, step, color);
    }
}

// A filled marker's closed outline, drawn as lines (its edge).
void stroke_outline(Painter& painter, std::span<const Point> outline, Color color, double width) {
    for (std::size_t index = 0U; index < outline.size(); ++index) {
        const Point to = outline[(index + 1U) % outline.size()];
        painter.draw_line(outline[index], to, color, width);
    }
}

Color average_colour(Color first, Color second) noexcept {
    return detail::interpolate(first, second, 0.5);
}

} // namespace

void Painter::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    if (!valid_stroke_style(style) || points.size() < 2U) return;
    for (const Point point : points) {
        if (std::isinf(point.x) || std::isinf(point.y)) return;
    }
    bool have_previous = false;
    Point previous{};
    for (const Point point : points) {
        if (!finite_point(point)) {
            have_previous = false;
            continue;
        }
        if (have_previous) draw_line(previous, point, style.color, style.width);
        previous = point;
        have_previous = true;
    }
}

void Painter::fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) {
    const bool known_rule = rule == PolygonFillRule::nonzero || rule == PolygonFillRule::even_odd;
    if (!known_rule || points.size() < 3U || !all_finite(points)) return;
    scan_fill_polygon(*this, points, color, rule);
}

void Painter::fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color) {
    if (upper.size() < 2U || upper.size() != lower.size() || !all_finite(upper) || !all_finite(lower)) return;
    std::array<Point, 4> quad{};
    for (std::size_t index = 0U; index + 1U < upper.size(); ++index) {
        quad = {upper[index], upper[index + 1U], lower[index + 1U], lower[index]};
        scan_fill_polygon(*this, quad, color, PolygonFillRule::nonzero);
    }
}

void Painter::fill_rects(std::span<const Rect> rects, Color color) {
    for (const Rect rect : rects) {
        if (rect.empty() || !rect.finite()) continue;
        fill_rect(rect, color);
    }
}

void Painter::fill_rects_each(std::span<const Rect> rects, std::span<const Color> colors) {
    if (rects.size() != colors.size()) return;
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        const Rect rect = rects[index];
        if (rect.empty() || !rect.finite()) continue;
        fill_rect(rect, colors[index]);
    }
}

void Painter::stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) {
    if (!valid_stroke_style(style)) return;
    for (const PaintSegment segment : segments) {
        if (!finite_point(segment.from) || !finite_point(segment.to)) continue;
        draw_line(segment.from, segment.to, style.color, style.width);
    }
}

// The base marker fallbacks draw every marker's fill, then every marker's
// edge.
void Painter::draw_markers(std::span<const Point> centers, const MarkerStyle& style) {
    if (!valid_marker_style(style)) return;
    const bool draw_fill = style.fill.alpha != 0U;
    const bool draw_edge = style.edge.alpha != 0U && style.edge_width > 0.0;
    const double half = style.size * 0.5;
    if (style.shape == MarkerShape::circle) {
        for (std::size_t index = 0U; draw_fill && index < centers.size(); ++index) {
            const Point center = centers[index];
            if (!finite_point(center)) continue;
            fill_rounded_rect(Rect{center.x - half, center.y - half, style.size, style.size}, half, style.fill);
        }
        for (std::size_t index = 0U; draw_edge && index < centers.size(); ++index) {
            const Point center = centers[index];
            if (!finite_point(center)) continue;
            const Rect box{center.x - half, center.y - half, style.size, style.size};
            stroke_rounded_rect(box, half, style.edge, style.edge_width);
        }
        return;
    }
    std::array<Point, maximum_marker_vertices> outline{};
    const bool outlined = marker_outline(style.shape, Point{}, style.size, outline) != 0U;
    if (outlined) {
        for (std::size_t index = 0U; draw_fill && index < centers.size(); ++index) {
            const Point center = centers[index];
            if (!finite_point(center)) continue;
            const std::size_t count = marker_outline(style.shape, center, style.size, outline);
            scan_fill_polygon(*this, std::span<const Point>(outline.data(), count), style.fill,
                              PolygonFillRule::nonzero);
        }
        for (std::size_t index = 0U; draw_edge && index < centers.size(); ++index) {
            const Point center = centers[index];
            if (!finite_point(center)) continue;
            const std::size_t count = marker_outline(style.shape, center, style.size, outline);
            stroke_outline(*this, std::span<const Point>(outline.data(), count), style.edge, style.edge_width);
        }
        return;
    }
    if (!draw_edge) return;
    std::array<PaintSegment, maximum_marker_lines> lines{};
    for (const Point center : centers) {
        if (!finite_point(center)) continue;
        const std::size_t count = marker_lines(style.shape, center, style.size, lines);
        for (std::size_t index = 0U; index < count; ++index) {
            draw_line(lines[index].from, lines[index].to, style.edge, style.edge_width);
        }
    }
}

void Painter::fill_conic_gradient(Rect rect, Point center, double start_degrees,
                                  std::span<const GradientStop> stops) {
    if (rect.empty() || !rect.finite() || !finite_point(center) || !std::isfinite(start_degrees) ||
        !valid_gradient_stops(stops)) {
        return;
    }
    // Wedges reaching past the farthest corner cover the whole rectangle.
    const double reach_x = std::max(std::abs(rect.x - center.x), std::abs(rect.right() - center.x));
    const double reach_y = std::max(std::abs(rect.y - center.y), std::abs(rect.bottom() - center.y));
    const double radius = std::sqrt(reach_x * reach_x + reach_y * reach_y) + 1.0;
    constexpr unsigned wedges = 90U;
    constexpr double wedge_degrees = 360.0 / static_cast<double>(wedges);
    save();
    clip_rect(rect);
    std::array<Point, 3> wedge{};
    for (unsigned index = 0U; index < wedges; ++index) {
        const double first_angle = (start_degrees + static_cast<double>(index) * wedge_degrees) * degrees_to_radians;
        const double second_angle = first_angle + wedge_degrees * degrees_to_radians;
        const EngineSineCosine first = engine_sine_cosine(first_angle);
        const EngineSineCosine second = engine_sine_cosine(second_angle);
        wedge = {center, Point{center.x + radius * first.cosine, center.y + radius * first.sine},
                 Point{center.x + radius * second.cosine, center.y + radius * second.sine}};
        const double offset = (static_cast<double>(index) + 0.5) / static_cast<double>(wedges);
        scan_fill_polygon(*this, wedge, detail::sample_gradient(stops, offset), PolygonFillRule::nonzero);
    }
    restore();
}

void Painter::fill_checker(Rect rect, const CheckerPattern& pattern) {
    if (rect.empty() || !rect.finite() || !std::isfinite(pattern.cell) || pattern.cell <= 0.0 ||
        !finite_point(pattern.anchor)) {
        return;
    }
    const double first_column = std::floor((rect.x - pattern.anchor.x) / pattern.cell);
    const double last_column = std::ceil((rect.right() - pattern.anchor.x) / pattern.cell);
    const double first_row = std::floor((rect.y - pattern.anchor.y) / pattern.cell);
    const double last_row = std::ceil((rect.bottom() - pattern.anchor.y) / pattern.cell);
    const double cells = (last_column - first_column) * (last_row - first_row);
    if (pattern.cell < 1.0 || !std::isfinite(cells) || cells > static_cast<double>(maximum_checker_cells)) {
        fill_rect(rect, average_colour(pattern.first, pattern.second));
        return;
    }
    fill_rect(rect, pattern.first);
    const std::int64_t column_begin = static_cast<std::int64_t>(first_column);
    const std::int64_t column_end = static_cast<std::int64_t>(last_column);
    const std::int64_t row_begin = static_cast<std::int64_t>(first_row);
    const std::int64_t row_end = static_cast<std::int64_t>(last_row);
    for (std::int64_t row = row_begin; row < row_end; ++row) {
        for (std::int64_t column = column_begin; column < column_end; ++column) {
            if ((column + row) % 2 == 0) continue;
            const Rect cell{pattern.anchor.x + static_cast<double>(column) * pattern.cell,
                            pattern.anchor.y + static_cast<double>(row) * pattern.cell, pattern.cell, pattern.cell};
            const Rect shown = Rect::intersection(cell, rect);
            if (!shown.empty()) fill_rect(shown, pattern.second);
        }
    }
}

void Painter::fill_grid_pattern(Rect rect, const GridPattern& pattern) {
    if (rect.empty() || !rect.finite() || !valid_grid_pattern(pattern)) return;
    const double half = pattern.line_width * 0.5;
    const detail::GridLineRange columns =
        detail::grid_line_range(rect.x, rect.right(), pattern.origin.x, pattern.spacing, half);
    const detail::GridLineRange rows =
        detail::grid_line_range(rect.y, rect.bottom(), pattern.origin.y, pattern.spacing, half);
    save();
    clip_rect(rect);
    // Minor lines first, so the majors cover the crossings.
    for (int pass = 0; pass < 2; ++pass) {
        const bool majors = pass == 1;
        const Color colour = majors ? pattern.major : pattern.minor;
        for (std::int64_t index = columns.first; index < columns.first + columns.count; ++index) {
            if (detail::grid_line_is_major(index, pattern.major_every) != majors) continue;
            const double x = pattern.origin.x + static_cast<double>(index) * pattern.spacing;
            fill_rect(Rect{x - half, rect.y, pattern.line_width, rect.height}, colour);
        }
        for (std::int64_t index = rows.first; index < rows.first + rows.count; ++index) {
            if (detail::grid_line_is_major(index, pattern.major_every) != majors) continue;
            const double y = pattern.origin.y + static_cast<double>(index) * pattern.spacing;
            fill_rect(Rect{rect.x, y - half, rect.width, pattern.line_width}, colour);
        }
    }
    restore();
}

void Painter::fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color) {
    if (rect.empty()) return;
    save();
    clip_rounded_rect_corners(rect, radii);
    fill_rect(rect, color);
    restore();
}

void Painter::stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width) {
    const double top = std::max(radii.top_left, radii.top_right);
    const double bottom = std::max(radii.bottom_right, radii.bottom_left);
    stroke_rounded_rect(rect, std::max(top, bottom), color, width);
}

} // namespace gui_forms
