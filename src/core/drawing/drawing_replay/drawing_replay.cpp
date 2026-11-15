#include "gui_forms/drawing/drawing_replay/drawing_replay.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
        return command.brush.kind == BrushKind::solid ? ReplayClass::drawable : ReplayClass::unsupported;
    case CommandKind::draw_rectangle:
    case CommandKind::draw_line:
    case CommandKind::draw_lines:
    case CommandKind::draw_line_segments:
    case CommandKind::draw_markers:
    case CommandKind::fill_grid_pattern:
        return ReplayClass::drawable;
    case CommandKind::clear:
    case CommandKind::draw_string:
    case CommandKind::draw_ellipse:
    case CommandKind::fill_ellipse:
    case CommandKind::draw_path:
    case CommandKind::fill_path:
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
    StrokeStyle stroke;
    switch (command.kind) {
    case CommandKind::fill_rectangle:
        painter.fill_rect(rect_of(command.rect), fill);
        return true;
    case CommandKind::fill_rectangles:
        scratch.rects.clear();
        for (const gui_drawing::RectF rect : command.rects) scratch.rects.push_back(rect_of(rect));
        painter.fill_rects(scratch.rects, fill);
        return true;
    case CommandKind::fill_polygon: {
        write_points(command.points, scratch.points);
        const PolygonFillRule rule = command.path.fill_mode == gui_drawing::FillMode::winding
            ? PolygonFillRule::nonzero : PolygonFillRule::even_odd;
        painter.fill_polygon(scratch.points, fill, rule);
        return true;
    }
    case CommandKind::fill_proportion: {
        const Rect track = rect_of(command.rect);
        const Rect part = proportion_part(track, command.scalar, command.direction);
        const PaintCornerRadii radii{command.radii.top_left, command.radii.top_right,
                                     command.radii.bottom_right, command.radii.bottom_left};
        painter.clip_rounded_rect_corners(track, radii);
        painter.fill_rect(part, fill);
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
