#include "gui_forms/drawing/drawing_replay/drawing_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

namespace {

using gui_drawing::BrushKind;
using gui_drawing::CommandKind;
using gui_drawing::DashStyle;
using gui_drawing::DrawingCommand;
using gui_drawing::GraphicsState;

// Storage converted spans are written into, reused across commands.
struct ReplayScratch final {
    std::vector<Point> points;
    std::vector<Rect> rects;
    std::vector<PaintSegment> segments;
    // A recorded path, an ellipse or a non-solid polygon as a PaintPath.
    PaintPath path;
};

// How one command replays.
enum class ReplayClass : std::uint8_t { state, drawable, unsupported };

[[nodiscard]] Color color_of(gui_drawing::Color color) noexcept {
    if (color.is_empty()) return Color::rgba(0U, 0U, 0U, 0U);
    const Color result = Color::rgba(color.red(), color.green(), color.blue(), color.alpha());
    return result;
}

[[nodiscard]] Point point_of(gui_drawing::PointF point) noexcept {
    return Point{point.x, point.y};
}

[[nodiscard]] Rect rect_of(gui_drawing::RectF rect) noexcept {
    return Rect{rect.x, rect.y, rect.width, rect.height};
}

[[nodiscard]] bool is_translation(const gui_drawing::Matrix& transform) noexcept {
    return transform.m11() == 1.0 && transform.m12() == 0.0 && transform.m21() == 0.0 &&
           transform.m22() == 1.0;
}

[[nodiscard]] StrokeJoin join_of(gui_drawing::LineJoin join) noexcept {
    switch (join) {
    case gui_drawing::LineJoin::miter: return StrokeJoin::miter;
    case gui_drawing::LineJoin::bevel: return StrokeJoin::bevel;
    case gui_drawing::LineJoin::round: return StrokeJoin::round;
    default: break;
    }
    return StrokeJoin::miter;
}

[[nodiscard]] StrokeCap cap_of(gui_drawing::LineCap cap) noexcept {
    switch (cap) {
    case gui_drawing::LineCap::flat: return StrokeCap::butt;
    case gui_drawing::LineCap::square: return StrokeCap::square;
    case gui_drawing::LineCap::round: return StrokeCap::round;
    default: break;
    }
    return StrokeCap::butt;
}

// Writes the pen's StrokeStyle into `style`, the dashes as the raster
// executor makes them (preset_dash_pattern). False when a custom pattern
// does not fit a StrokeStyle; `style` is then unspecified.
[[nodiscard]] bool stroke_of(const gui_drawing::PenSnapshot& pen, StrokeStyle& style) noexcept {
    style = StrokeStyle{};
    style.color = color_of(pen.color);
    style.width = pen.width;
    style.join = join_of(pen.join);
    style.miter_limit = pen.miter_limit;
    style.cap = cap_of(pen.cap);
    const double width = pen.width;
    std::array<double, 6> preset{};
    std::size_t preset_count = 0U;
    switch (pen.dash_style) {
    case DashStyle::dash:
        preset = {3.0 * width, width};
        preset_count = 2U;
        break;
    case DashStyle::dot:
        preset = {width, width};
        preset_count = 2U;
        break;
    case DashStyle::dash_dot:
        preset = {3.0 * width, width, width, width};
        preset_count = 4U;
        break;
    case DashStyle::dash_dot_dot:
        preset = {3.0 * width, width, width, width, width, width};
        preset_count = 6U;
        break;
    case DashStyle::custom: {
        const std::size_t count = pen.dash_pattern.size();
        // The executor draws an odd or one-entry pattern solid.
        if (count < 2U || count % 2U != 0U) return true;
        if (count > maximum_dash_intervals) return false;
        std::copy(pen.dash_pattern.begin(), pen.dash_pattern.end(), style.dashes.begin());
        style.dash_count = count;
        return true;
    }
    case DashStyle::solid:
    default:
        break;
    }
    std::copy(preset.begin(), preset.begin() + static_cast<std::ptrdiff_t>(preset_count), style.dashes.begin());
    style.dash_count = preset_count;
    return true;
}

constexpr double degrees_to_radians = 0.017453292519943295769;

// A brush's gradient stops, when its colours and positions make valid
// stops (valid_gradient_stops); otherwise none, and the count is 0.
struct BrushStops final {
    std::array<GradientStop, maximum_gradient_stops> stops{};
    std::size_t count{};

    [[nodiscard]] std::span<const GradientStop> view() const& noexcept {
        return std::span<const GradientStop>(stops.data(), count);
    }
    std::span<const GradientStop> view() const&& = delete;
};

void fill_brush_stops(const gui_drawing::BrushSnapshot& brush, BrushStops& written) noexcept {
    written.count = 0U;
    const std::size_t count = brush.colors.size();
    if (count < 2U || count != brush.positions.size() || count > maximum_gradient_stops) return;
    for (std::size_t index = 0U; index < count; ++index) {
        written.stops[index] = GradientStop{brush.positions[index], color_of(brush.colors[index])};
    }
    written.count = count;
    if (!valid_gradient_stops(written.view())) written.count = 0U;
}

// The raster executor's tile mode for a gradient brush's wrap mode, as a
// spread: tile repeats, the flips reflect, clamp pads.
[[nodiscard]] GradientSpreadMode spread_of(gui_drawing::WrapMode wrap) noexcept {
    switch (wrap) {
    case gui_drawing::WrapMode::tile: return GradientSpreadMode::repeat;
    case gui_drawing::WrapMode::tile_flip_x:
    case gui_drawing::WrapMode::tile_flip_y:
    case gui_drawing::WrapMode::tile_flip_xy: return GradientSpreadMode::reflect;
    case gui_drawing::WrapMode::clamp: return GradientSpreadMode::pad;
    default: break;
    }
    return GradientSpreadMode::pad;
}

// The Painter's hatch style for a brush's, or false for a value outside the
// enumeration.
[[nodiscard]] bool hatch_style_of(gui_drawing::HatchStyle style, HatchStyle& written) noexcept {
    switch (style) {
    case gui_drawing::HatchStyle::horizontal: written = HatchStyle::horizontal; return true;
    case gui_drawing::HatchStyle::vertical: written = HatchStyle::vertical; return true;
    case gui_drawing::HatchStyle::forward_diagonal: written = HatchStyle::forward_diagonal; return true;
    case gui_drawing::HatchStyle::backward_diagonal: written = HatchStyle::backward_diagonal; return true;
    case gui_drawing::HatchStyle::cross: written = HatchStyle::cross; return true;
    case gui_drawing::HatchStyle::diagonal_cross: written = HatchStyle::diagonal_cross; return true;
    default: break;
    }
    return false;
}

// Whether the painter can draw `brush` (replay_drawing_commands): every kind
// but texture, whose pixels have no image identity here; a gradient whose
// stops are not valid gradient stops is drawn in its primary colour, as the
// executor draws it.
[[nodiscard]] bool brush_replayable(const gui_drawing::BrushSnapshot& brush) noexcept {
    HatchStyle style{};
    switch (brush.kind) {
    case BrushKind::solid:
    case BrushKind::linear_gradient:
    case BrushKind::path_gradient:
    case BrushKind::conic_gradient: return true;
    case BrushKind::checker: return brush.cell_size > 0.0 && std::isfinite(brush.cell_size);
    case BrushKind::hatch: return hatch_style_of(brush.hatch_style, style);
    case BrushKind::texture:
    default: break;
    }
    return false;
}

// Fills `area` with `brush`'s paint as the raster executor shades it, in
// the recording's coordinates (patterns anchored at its origin), under the
// clip the caller set. The brush passed brush_replayable.
void fill_with_brush(Painter& painter, const gui_drawing::BrushSnapshot& brush, Rect area) {
    const Color primary = color_of(brush.primary);
    BrushStops stops;
    switch (brush.kind) {
    case BrushKind::checker: {
        CheckerPattern checker{};
        checker.cell = brush.cell_size;
        checker.first = primary;
        checker.second = color_of(brush.secondary);
        painter.fill_checker(area, checker);
        return;
    }
    case BrushKind::hatch: {
        HatchPattern hatch{};
        if (!hatch_style_of(brush.hatch_style, hatch.style)) return;
        hatch.fore = primary;
        hatch.back = color_of(brush.secondary);
        painter.fill_hatch(area, hatch);
        return;
    }
    case BrushKind::linear_gradient:
    case BrushKind::path_gradient:
    case BrushKind::conic_gradient: break;
    case BrushKind::solid:
    default:
        painter.fill_rect(area, primary);
        return;
    }
    fill_brush_stops(brush, stops);
    if (stops.count == 0U) {
        painter.fill_rect(area, primary);
        return;
    }
    if (brush.kind == BrushKind::conic_gradient) {
        const double start = std::fmod(brush.angle, 360.0);
        painter.fill_conic_gradient(area, point_of(brush.center), start, stops.view());
        return;
    }
    if (brush.kind == BrushKind::linear_gradient) {
        // The executor's line: through the bounds' centre at the brush's
        // angle, as long as the bounds' extent along it.
        const double radians = brush.angle * degrees_to_radians;
        const double along_x = std::cos(radians);
        const double along_y = std::sin(radians);
        const double half_length =
            std::abs(along_x) * brush.bounds.width / 2.0 + std::abs(along_y) * brush.bounds.height / 2.0;
        const Point centre{brush.bounds.x + brush.bounds.width / 2.0, brush.bounds.y + brush.bounds.height / 2.0};
        const Point start{centre.x - along_x * half_length, centre.y - along_y * half_length};
        const Point end{centre.x + along_x * half_length, centre.y + along_y * half_length};
        painter.fill_linear_gradient_spread(area, start, end, stops.view(), spread_of(brush.wrap_mode));
        return;
    }
    // A path gradient: the executor's circle about the centre through the
    // farthest boundary point.
    double radius = 0.0;
    for (const gui_drawing::PointF point : brush.points) {
        radius = std::max(radius, std::hypot(point.x - brush.center.x, point.y - brush.center.y));
    }
    if (!(radius > 0.0)) {
        painter.fill_rect(area, primary);
        return;
    }
    painter.fill_radial_gradient_spread(area, point_of(brush.center), Size{radius, radius}, stops.view(),
                                        spread_of(brush.wrap_mode));
}

// The point at `degrees` on the ellipse inscribed in `bounds` (clockwise,
// y down, from +x).
[[nodiscard]] Point ellipse_point(gui_drawing::RectF bounds, double degrees) noexcept {
    const double radians = degrees * degrees_to_radians;
    const Point result{bounds.x + bounds.width * 0.5 + bounds.width * 0.5 * std::cos(radians),
                       bounds.y + bounds.height * 0.5 + bounds.height * 0.5 * std::sin(radians)};
    return result;
}

// Appends the ellipse inscribed in `bounds` to `path` as a closed contour
// of its own (a move to its rightmost point, a full-turn arc, a close).
// An ellipse with no area appends nothing, as the executor draws none.
void append_ellipse(gui_drawing::RectF bounds, PaintPath& path) {
    const Size radii{bounds.width * 0.5, bounds.height * 0.5};
    if (!(radii.width > 0.0) || !(radii.height > 0.0)) return;
    const Point centre{bounds.x + radii.width, bounds.y + radii.height};
    path.move_to(Point{centre.x + radii.width, centre.y});
    path.arc_to(centre, radii, 0.0, 360.0);
    path.close();
}

// Writes `source` into `path` with the raster executor's figure rules
// (to_sk_path): a line or curve whose start is not the current point
// starts a new figure; a rectangle and an ellipse are closed figures of
// their own; an arc extends the current figure (a straight join from the
// current point) or starts one at its own start. False for an element
// outside the enumeration (the executor refuses the path).
[[nodiscard]] bool write_paint_path(const gui_drawing::PathSnapshot& source, PaintPath& path) {
    path.clear();
    bool has_current = false;
    bool contour_open = false;
    gui_drawing::PointF current{};
    for (const gui_drawing::PathElement& element : source.elements) {
        switch (element.verb) {
        case gui_drawing::PathVerb::start_figure: has_current = false; break;
        case gui_drawing::PathVerb::line:
        case gui_drawing::PathVerb::quadratic:
        case gui_drawing::PathVerb::bezier:
            if (!has_current || current != element.first) path.move_to(point_of(element.first));
            if (element.verb == gui_drawing::PathVerb::line) {
                path.line_to(point_of(element.second));
                current = element.second;
            } else if (element.verb == gui_drawing::PathVerb::quadratic) {
                path.quad_to(point_of(element.second), point_of(element.third));
                current = element.third;
            } else {
                path.cubic_to(point_of(element.second), point_of(element.third), point_of(element.fourth));
                current = element.fourth;
            }
            has_current = true;
            contour_open = true;
            break;
        case gui_drawing::PathVerb::rectangle: {
            const Rect box = rect_of(element.rect);
            path.move_to(Point{box.x, box.y});
            path.line_to(Point{box.x + box.width, box.y});
            path.line_to(Point{box.x + box.width, box.y + box.height});
            path.line_to(Point{box.x, box.y + box.height});
            path.close();
            has_current = false;
            contour_open = false;
            break;
        }
        case gui_drawing::PathVerb::ellipse:
            append_ellipse(element.rect, path);
            has_current = false;
            contour_open = false;
            break;
        case gui_drawing::PathVerb::arc: {
            const Size radii{element.rect.width * 0.5, element.rect.height * 0.5};
            // An arc of an ellipse with no area draws nothing.
            if (!(radii.width > 0.0) || !(radii.height > 0.0)) break;
            const Point centre{element.rect.x + radii.width, element.rect.y + radii.height};
            if (!has_current) path.move_to(ellipse_point(element.rect, element.start_angle));
            path.arc_to(centre, radii, element.start_angle, element.sweep_angle);
            const Point end = ellipse_point(element.rect, element.start_angle + element.sweep_angle);
            current = gui_drawing::PointF{end.x, end.y};
            has_current = true;
            contour_open = true;
            break;
        }
        case gui_drawing::PathVerb::close_figure:
            if (contour_open) path.close();
            has_current = false;
            contour_open = false;
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] PolygonFillRule rule_of(gui_drawing::FillMode mode) noexcept {
    return mode == gui_drawing::FillMode::winding ? PolygonFillRule::nonzero : PolygonFillRule::even_odd;
}

[[nodiscard]] ReplayClass classify(const DrawingCommand& command) noexcept {
    switch (command.kind) {
    case CommandKind::save:
    case CommandKind::restore:
    case CommandKind::translate:
    case CommandKind::set_transform:
    case CommandKind::set_clip:
    case CommandKind::reset_clip:
    case CommandKind::set_quality:
        return ReplayClass::state;
    case CommandKind::fill_rectangle:
    case CommandKind::fill_rectangles:
    case CommandKind::fill_polygon:
    case CommandKind::fill_proportion:
    case CommandKind::fill_ellipse:
    case CommandKind::fill_path:
        return brush_replayable(command.brush) ? ReplayClass::drawable : ReplayClass::unsupported;
    case CommandKind::draw_rectangle:
    case CommandKind::draw_line:
    case CommandKind::draw_lines:
    case CommandKind::draw_line_segments:
    case CommandKind::draw_markers:
    case CommandKind::fill_grid_pattern:
    case CommandKind::draw_ellipse:
    case CommandKind::draw_path:
        return ReplayClass::drawable;
    case CommandKind::clear:
    case CommandKind::draw_string:
    case CommandKind::draw_image:
    default:
        break;
    }
    return ReplayClass::unsupported;
}

// The part of `track` a fill of `proportion` covers from `direction`'s
// edge (the executor's proportion_part).
[[nodiscard]] Rect proportion_part(Rect track, double proportion, gui_drawing::FillDirection direction) noexcept {
    const double across = track.width * proportion;
    const double up = track.height * proportion;
    switch (direction) {
    case gui_drawing::FillDirection::left_to_right: return Rect{track.x, track.y, across, track.height};
    case gui_drawing::FillDirection::right_to_left:
        return Rect{track.x + track.width - across, track.y, across, track.height};
    case gui_drawing::FillDirection::bottom_to_top:
        return Rect{track.x, track.y + track.height - up, track.width, up};
    case gui_drawing::FillDirection::top_to_bottom: return Rect{track.x, track.y, track.width, up};
    default: break;
    }
    return Rect{};
}

void write_points(std::span<const gui_drawing::PointF> source, std::vector<Point>& points) {
    points.clear();
    for (const gui_drawing::PointF point : source) points.push_back(point_of(point));
}

// Draws one drawable command, already under its state. False when its pen
// does not fit a StrokeStyle (nothing is drawn).
[[nodiscard]] bool draw_command(const DrawingCommand& command, Painter& painter, ReplayScratch& scratch) {
    const Color fill = color_of(command.brush.primary);
    const bool solid = command.brush.kind == BrushKind::solid;
    StrokeStyle stroke;
    switch (command.kind) {
    case CommandKind::fill_rectangle:
        if (solid) {
            painter.fill_rect(rect_of(command.rect), fill);
        } else {
            fill_with_brush(painter, command.brush, rect_of(command.rect));
        }
        return true;
    case CommandKind::fill_rectangles:
        scratch.rects.clear();
        for (const gui_drawing::RectF rect : command.rects) scratch.rects.push_back(rect_of(rect));
        if (solid) {
            painter.fill_rects(scratch.rects, fill);
            return true;
        }
        // Each rectangle filled alone: the pattern is anchored at the
        // recording's origin, so neighbours still join.
        for (const Rect rect : scratch.rects) fill_with_brush(painter, command.brush, rect);
        return true;
    case CommandKind::fill_polygon: {
        write_points(command.points, scratch.points);
        const PolygonFillRule rule = rule_of(command.path.fill_mode);
        if (solid) {
            painter.fill_polygon(scratch.points, fill, rule);
            return true;
        }
        if (scratch.points.size() < 3U) return true;
        scratch.path.clear();
        scratch.path.move_to(scratch.points.front());
        for (std::size_t index = 1U; index < scratch.points.size(); ++index) scratch.path.line_to(scratch.points[index]);
        scratch.path.close();
        if (!valid_paint_path(scratch.path.view())) return false;
        painter.clip_path(scratch.path.view(), rule);
        fill_with_brush(painter, command.brush, paint_path_bounds(scratch.path.view()));
        return true;
    }
    case CommandKind::fill_proportion: {
        const Rect track = rect_of(command.rect);
        const Rect part = proportion_part(track, command.scalar, command.direction);
        const PaintCornerRadii radii{command.radii.top_left, command.radii.top_right,
                                     command.radii.bottom_right, command.radii.bottom_left};
        painter.clip_rounded_rect_corners(track, radii);
        if (solid) {
            painter.fill_rect(part, fill);
        } else {
            fill_with_brush(painter, command.brush, part);
        }
        return true;
    }
    case CommandKind::fill_ellipse:
    case CommandKind::fill_path: {
        PolygonFillRule rule = PolygonFillRule::nonzero;
        if (command.kind == CommandKind::fill_ellipse) {
            scratch.path.clear();
            append_ellipse(command.rect, scratch.path);
        } else {
            if (!write_paint_path(command.path, scratch.path)) return false;
            rule = rule_of(command.path.fill_mode);
        }
        // An empty path (an ellipse with no area) draws nothing, as the
        // executor's does.
        if (scratch.path.view().verbs.empty()) return true;
        if (!valid_paint_path(scratch.path.view())) return false;
        if (solid) {
            painter.fill_path(scratch.path.view(), fill, rule);
            return true;
        }
        painter.clip_path(scratch.path.view(), rule);
        fill_with_brush(painter, command.brush, paint_path_bounds(scratch.path.view()));
        return true;
    }
    case CommandKind::draw_ellipse:
    case CommandKind::draw_path: {
        if (!stroke_of(command.pen, stroke)) return false;
        if (command.kind == CommandKind::draw_ellipse) {
            scratch.path.clear();
            append_ellipse(command.rect, scratch.path);
        } else if (!write_paint_path(command.path, scratch.path)) {
            return false;
        }
        if (scratch.path.view().verbs.empty()) return true;
        if (!valid_paint_path(scratch.path.view())) return false;
        painter.stroke_path(scratch.path.view(), stroke);
        return true;
    }
    case CommandKind::draw_rectangle: {
        if (!stroke_of(command.pen, stroke)) return false;
        const Rect rect = rect_of(command.rect);
        if (stroke.dash_count == 0U) {
            painter.stroke_rect(rect, stroke.color, stroke.width);
            return true;
        }
        scratch.points.clear();
        scratch.points.push_back(Point{rect.x, rect.y});
        scratch.points.push_back(Point{rect.x + rect.width, rect.y});
        scratch.points.push_back(Point{rect.x + rect.width, rect.y + rect.height});
        scratch.points.push_back(Point{rect.x, rect.y + rect.height});
        scratch.points.push_back(Point{rect.x, rect.y});
        painter.stroke_polyline(scratch.points, stroke);
        return true;
    }
    case CommandKind::draw_line:
        if (!stroke_of(command.pen, stroke)) return false;
        scratch.points.clear();
        scratch.points.push_back(point_of(command.first));
        scratch.points.push_back(point_of(command.second));
        painter.stroke_polyline(scratch.points, stroke);
        return true;
    case CommandKind::draw_lines:
        if (!stroke_of(command.pen, stroke)) return false;
        write_points(command.points, scratch.points);
        painter.stroke_polyline(scratch.points, stroke);
        return true;
    case CommandKind::draw_line_segments: {
        if (!stroke_of(command.pen, stroke)) return false;
        scratch.segments.clear();
        const std::size_t pairs = command.points.size() / 2U;
        for (std::size_t index = 0U; index < pairs; ++index) {
            const PaintSegment segment{point_of(command.points[index * 2U]),
                                       point_of(command.points[index * 2U + 1U])};
            scratch.segments.push_back(segment);
        }
        painter.stroke_segments(scratch.segments, stroke);
        return true;
    }
    case CommandKind::draw_markers: {
        write_points(command.points, scratch.points);
        MarkerStyle marker;
        marker.shape = static_cast<MarkerShape>(command.marker.shape);
        marker.size = command.marker.size;
        marker.fill = color_of(command.marker.fill);
        marker.edge = color_of(command.marker.edge);
        marker.edge_width = command.marker.edge_width;
        painter.draw_markers(scratch.points, marker);
        return true;
    }
    case CommandKind::fill_grid_pattern: {
        GridPattern grid;
        grid.origin = point_of(command.grid.origin);
        grid.spacing = command.grid.spacing;
        grid.major_every = command.grid.major_every;
        grid.minor = color_of(command.grid.minor);
        grid.major = color_of(command.grid.major);
        grid.line_width = command.grid.line_width;
        painter.fill_grid_pattern(rect_of(command.rect), grid);
        return true;
    }
    default:
        // The kinds classify() calls state or unsupported never reach here
        // (replay_drawing_commands counts them first); any other kind is
        // reported as not drawn.
        break;
    }
    return false;
}

} // namespace

DrawingReplayReport replay_drawing_commands(std::span<const gui_drawing::DrawingCommand> commands,
                                            Painter& painter) {
    DrawingReplayReport report;
    ReplayScratch scratch;
    for (const DrawingCommand& command : commands) {
        const ReplayClass replay = classify(command);
        if (replay == ReplayClass::state) {
            ++report.state;
            continue;
        }
        if (replay == ReplayClass::unsupported) {
            ++report.unsupported;
            continue;
        }
        const GraphicsState& state = command.state;
        if (!is_translation(state.transform)) {
            ++report.transformed;
            continue;
        }
        // The clip is in recording coordinates (the recorder applied the
        // transform when it was set), so it is made before the translation.
        painter.save();
        if (state.clip.has_value()) painter.clip_rect(rect_of(*state.clip));
        painter.translate(Point{state.transform.dx(), state.transform.dy()});
        const bool drawn = draw_command(command, painter, scratch);
        painter.restore();
        if (drawn) {
            ++report.drawn;
        } else {
            ++report.unsupported;
        }
    }
    return report;
}

} // namespace gui_forms
