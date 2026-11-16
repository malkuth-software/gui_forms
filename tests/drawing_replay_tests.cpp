#include "gui_forms/drawing/drawing_replay/drawing_replay.hpp"
#include "gui_forms/drawing.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "drawing_replay_tests:" << line << ": " << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

// Names each call a replay makes, with the values the tests check.
class CallPainter final : public Painter {
public:
    void save() override {
        calls.push_back("save");
        ++depth;
    }
    void restore() override {
        calls.push_back("restore");
        --depth;
    }
    void translate(Point offset) override {
        calls.push_back("translate");
        last_offset = offset;
    }
    void clip_rect(Rect rect) override {
        calls.push_back("clip");
        last_clip = rect;
    }
    void clip_rounded_rect_corners(Rect rect, PaintCornerRadii) override {
        calls.push_back("clip_corners");
        last_clip = rect;
    }
    void fill_rect(Rect rect, Color color) override {
        calls.push_back("fill_rect");
        last_rect = rect;
        last_color = color;
    }
    void stroke_rect(Rect, Color, double width) override {
        calls.push_back("stroke_rect");
        last_width = width;
    }
    void draw_line(Point, Point, Color, double) override { calls.push_back("draw_line"); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { calls.push_back("text"); }
    void draw_image(ImageId, Rect, double) override { calls.push_back("image"); }
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override {
        calls.push_back("polyline");
        last_points.assign(points.begin(), points.end());
        last_style = style;
    }
    void fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) override {
        calls.push_back("polygon");
        last_points.assign(points.begin(), points.end());
        last_color = color;
        last_rule = rule;
    }
    void fill_rects(std::span<const Rect> rects, Color color) override {
        calls.push_back("fill_rects");
        last_count = rects.size();
        last_color = color;
    }
    void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) override {
        calls.push_back("segments");
        last_count = segments.size();
        last_style = style;
    }
    void draw_markers(std::span<const Point> centers, const MarkerStyle& style) override {
        calls.push_back("markers");
        last_count = centers.size();
        last_marker = style;
    }
    void fill_grid_pattern(Rect rect, const GridPattern&) override {
        calls.push_back("grid");
        last_rect = rect;
    }
    void clip_path(PaintPathView path, PolygonFillRule rule) override {
        calls.push_back("clip_path");
        last_verbs.assign(path.verbs.begin(), path.verbs.end());
        last_points.assign(path.points.begin(), path.points.end());
        last_rule = rule;
    }
    void fill_path(PaintPathView path, Color color, PolygonFillRule rule) override {
        calls.push_back("fill_path");
        last_verbs.assign(path.verbs.begin(), path.verbs.end());
        last_points.assign(path.points.begin(), path.points.end());
        last_color = color;
        last_rule = rule;
    }
    void stroke_path(PaintPathView path, const StrokeStyle& style) override {
        calls.push_back("stroke_path");
        last_verbs.assign(path.verbs.begin(), path.verbs.end());
        last_points.assign(path.points.begin(), path.points.end());
        last_style = style;
    }
    void fill_linear_gradient_spread(Rect rect, Point start, Point end, std::span<const GradientStop> stops,
                                     GradientSpreadMode spread) override {
        calls.push_back("linear");
        last_rect = rect;
        last_start = start;
        last_end = end;
        last_count = stops.size();
        last_spread = spread;
    }
    void fill_hatch(Rect rect, const HatchPattern& pattern) override {
        calls.push_back("hatch");
        last_rect = rect;
        last_hatch = pattern;
    }
    void fill_checker(Rect rect, const CheckerPattern& pattern) override {
        calls.push_back("checker");
        last_rect = rect;
        last_checker = pattern;
    }
    void fill_radial_gradient_spread(Rect rect, Point center, Size radii, std::span<const GradientStop> stops,
                                     GradientSpreadMode spread) override {
        calls.push_back("radial");
        last_rect = rect;
        last_start = center;
        last_radii = radii;
        last_count = stops.size();
        last_spread = spread;
    }
    void fill_focal_radial_gradient(Rect rect, Point center, Size radii, Point focus,
                                    std::span<const GradientStop> stops, GradientSpreadMode spread) override {
        calls.push_back("focal");
        last_rect = rect;
        last_start = center;
        last_end = focus;
        last_radii = radii;
        last_count = stops.size();
        last_spread = spread;
    }

    std::vector<std::string> calls;
    int depth{};
    Point last_offset{};
    Rect last_clip{};
    Rect last_rect{};
    Color last_color{};
    double last_width{};
    std::vector<Point> last_points;
    StrokeStyle last_style{};
    PolygonFillRule last_rule{PolygonFillRule::nonzero};
    std::size_t last_count{};
    MarkerStyle last_marker{};
    std::vector<PaintPathVerb> last_verbs;
    Point last_start{};
    Point last_end{};
    GradientSpreadMode last_spread{GradientSpreadMode::pad};
    HatchPattern last_hatch{};
    CheckerPattern last_checker{};
    Size last_radii{};
    std::shared_ptr<const PartRaster> last_raster;
    Point last_raster_origin{};

    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        calls.push_back("part_raster");
        last_raster = raster;
        last_raster_origin = origin;
    }
};

[[nodiscard]] std::size_t count_calls(const CallPainter& painter, std::string_view name) {
    std::size_t count = 0U;
    for (const std::string& call : painter.calls) {
        if (call == name) ++count;
    }
    return count;
}

// The batched marks and rectangles replay through the Painter's batch
// calls, each under its own state, and the painter's state is balanced.
void batched_marks_replay_under_their_state() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xffff0000U), 2.0);
    pen.set_line_join(gui_drawing::LineJoin::bevel, 4.0);
    pen.set_line_cap(gui_drawing::LineCap::round);
    gui_drawing::SolidBrush brush(gui_drawing::Color::from_argb(0x800000ffU));
    const gui_drawing::GraphicsStateToken token = recorder.save();
    recorder.set_clip({0, 0, 50, 40});
    recorder.translate(10, 5);
    const gui_drawing::PointF line[] = {{0, 0}, {10, 10}, {20, 0}};
    recorder.draw_lines(pen, line);
    recorder.restore(token);
    const gui_drawing::RectF bars[] = {{0, 0, 2, 2}, {4, 0, 2, 2}};
    recorder.fill_rectangles(brush, bars);
    const gui_drawing::PointF segments[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    recorder.draw_line_segments(pen, segments);
    const gui_drawing::PointF polygon[] = {{0, 0}, {4, 0}, {2, 3}};
    recorder.fill_polygon(brush, polygon, gui_drawing::FillMode::winding);
    gui_drawing::MarkerStyle marker;
    marker.shape = gui_drawing::MarkerShape::diamond;
    marker.size = 8.0;
    const gui_drawing::PointF centers[] = {{5, 5}};
    recorder.draw_markers(centers, marker);
    recorder.fill_proportion(brush, {0, 0, 100, 10}, gui_drawing::CornerRadii{2, 2, 2, 2}, 0.25,
                             gui_drawing::FillDirection::right_to_left);

    CallPainter painter;
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter);
    // save, set_clip, translate, restore are state; six marks drawn.
    CHECK(report.state == 4U);
    CHECK(report.drawn == 6U);
    CHECK(report.transformed == 0U && report.unsupported == 0U);
    CHECK(painter.depth == 0);
    CHECK(count_calls(painter, "save") == 6U);
    // The polyline drew under its clip and translation.
    CHECK(painter.calls[0] == "save" && painter.calls[1] == "clip" && painter.calls[2] == "translate");
    CHECK(painter.calls[3] == "polyline");
    CHECK(count_calls(painter, "fill_rects") == 1U);
    CHECK(count_calls(painter, "segments") == 1U);
    CHECK(count_calls(painter, "markers") == 1U);
    CHECK(painter.last_marker.shape == MarkerShape::diamond);
    // The polygon kept its points and its winding rule.
    CHECK(count_calls(painter, "polygon") == 1U);
    CHECK(painter.last_rule == PolygonFillRule::nonzero);
    CHECK(painter.last_points.size() == 3U);
    // The proportion: a quarter from the right, under the track's corners.
    CHECK(count_calls(painter, "clip_corners") == 1U);
    CHECK(painter.last_rect == (Rect{75, 0, 25, 10}));
    CHECK(painter.last_color == Color::rgba(0U, 0U, 255U, 128U));
    CHECK(painter.last_style.join == StrokeJoin::bevel);
    CHECK(painter.last_style.cap == StrokeCap::round);
    CHECK(painter.last_style.width == 2.0);
}

// The gapped marks replay as their runs; dashes follow the executor.
void gapped_marks_and_dashes_replay() {
    const double gap = std::numeric_limits<double>::quiet_NaN();
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xff000000U), 2.0);
    pen.set_dash_style(gui_drawing::DashStyle::dash);
    const gui_drawing::PointF runs[] = {{0, 0}, {1, 1}, {gap, 0}, {3, 3}, {4, 4}};
    recorder.draw_polyline_runs(pen, runs);
    const gui_drawing::RectF cells[] = {{0, 0, 1, 1}, {1, 0, 1, 1}};
    const gui_drawing::Color colours[] = {gui_drawing::Color::from_argb(0xffff0000U),
                                          gui_drawing::Color::from_argb(0xff00ff00U)};
    recorder.fill_rectangles_each(cells, colours);
    recorder.draw_rectangle(pen, {0, 0, 10, 10});

    CallPainter painter;
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter);
    CHECK(report.drawn == 5U);
    // The dashed rectangle is a closed polyline, its dashes 3w and w.
    CHECK(count_calls(painter, "polyline") == 3U);
    CHECK(count_calls(painter, "stroke_rect") == 0U);
    CHECK(painter.last_points.size() == 5U);
    CHECK(painter.last_style.dash_count == 2U);
    CHECK(painter.last_style.dashes[0] == 6.0 && painter.last_style.dashes[1] == 2.0);
    CHECK(count_calls(painter, "fill_rects") == 2U);
}

// Paths and ellipses replay as curved paths, figure by figure as the
// executor builds them; non-solid brushes fill under the shape's clip.
void paths_ellipses_and_brushes_replay() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xff000000U), 1.0);
    gui_drawing::SolidBrush brush(gui_drawing::Color::from_argb(0xff00ff00U));
    recorder.fill_ellipse(brush, {0, 0, 20, 10});

    // Two lines joined at (10, 0), then one starting elsewhere (a new
    // figure), closed.
    gui_drawing::GraphicsPath path(gui_drawing::FillMode::winding);
    path.add_line({0, 0}, {10, 0});
    path.add_line({10, 0}, {10, 10});
    path.add_line({20, 20}, {30, 20});
    path.close_figure();
    recorder.draw_path(pen, path);

    gui_drawing::LinearGradientBrush linear({0, 0, 40, 20}, gui_drawing::Color::from_argb(0xffff0000U),
                                            gui_drawing::Color::from_argb(0xff0000ffU), 0.0,
                                            gui_drawing::WrapMode::tile_flip_x);
    recorder.fill_path(linear, path);

    gui_drawing::HatchBrush hatch(gui_drawing::HatchStyle::cross, gui_drawing::Color::from_argb(0xff000000U),
                                  gui_drawing::Color::from_argb(0xffffffffU));
    recorder.fill_rectangle(hatch, {2, 4, 6, 8});

    CallPainter painter;
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter);
    CHECK(report.drawn == 4U && report.unsupported == 0U);
    CHECK(painter.depth == 0);
    // The solid ellipse fills as a path; the pen draws the path's stroke.
    CHECK(count_calls(painter, "fill_path") == 1U);
    CHECK(count_calls(painter, "stroke_path") == 1U);
    // The path's verbs, as the last path call (the clip of the gradient
    // fill) saw them: M L L M L Z.
    const std::vector<PaintPathVerb> verbs{PaintPathVerb::move_to, PaintPathVerb::line_to, PaintPathVerb::line_to,
                                           PaintPathVerb::move_to, PaintPathVerb::line_to, PaintPathVerb::close};
    CHECK(count_calls(painter, "clip_path") == 1U);
    CHECK(painter.last_verbs == verbs);
    CHECK(painter.last_rule == PolygonFillRule::nonzero);
    // The gradient: along x through the bounds' centre (angle 0), the flip
    // wrap reflecting, filled over the path's bounds.
    CHECK(count_calls(painter, "linear") == 1U);
    CHECK(painter.last_start == (Point{0.0, 10.0}) && painter.last_end == (Point{40.0, 10.0}));
    CHECK(painter.last_spread == GradientSpreadMode::reflect && painter.last_count == 2U);
    // The hatch: over the rectangle, anchored at the recording's origin.
    CHECK(count_calls(painter, "hatch") == 1U);
    CHECK(painter.last_rect == (Rect{2, 4, 6, 8}));
    CHECK(painter.last_hatch.style == HatchStyle::cross && painter.last_hatch.cell == 8.0 &&
          painter.last_hatch.anchor == (Point{0.0, 0.0}) &&
          painter.last_hatch.fore == Color::rgba(0U, 0U, 0U, 255U) &&
          painter.last_hatch.back == Color::rgba(255U, 255U, 255U, 255U));
}

// The path's verbs reach the painter in order.
void path_verbs_follow_the_executor() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xff000000U), 1.0);
    gui_drawing::GraphicsPath path(gui_drawing::FillMode::alternate);
    path.add_line({0, 0}, {10, 0});
    path.add_line({10, 0}, {10, 10});
    path.add_line({20, 20}, {30, 20});
    path.close_figure();
    path.add_ellipse({0, 0, 4, 2});
    recorder.draw_path(pen, path);
    CallPainter painter;
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter);
    CHECK(report.drawn == 1U);
    const std::vector<PaintPathVerb> verbs{PaintPathVerb::move_to, PaintPathVerb::line_to, PaintPathVerb::line_to,
                                           PaintPathVerb::move_to, PaintPathVerb::line_to, PaintPathVerb::close,
                                           PaintPathVerb::move_to, PaintPathVerb::arc_to,  PaintPathVerb::close};
    CHECK(painter.last_verbs == verbs);
    // The ellipse's move lands on its rightmost point, its arc about (2, 1)
    // with radii (2, 1) through a full turn from 0.
    CHECK(painter.last_points.size() == 9U);
    CHECK(painter.last_points[5] == (Point{4.0, 1.0}));
    CHECK(painter.last_points[6] == (Point{2.0, 1.0}) && painter.last_points[7] == (Point{2.0, 1.0}) &&
          painter.last_points[8] == (Point{0.0, 360.0}));
}

// Unsupported kinds and transforms are counted and draw nothing.
void unsupported_commands_are_counted() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xff000000U), 1.0);
    recorder.clear(gui_drawing::Color::from_argb(0xffffffffU));
    const gui_drawing::Bitmap tile(2U, 2U);
    gui_drawing::TextureBrush texture(tile);
    recorder.fill_ellipse(texture, {0, 0, 4, 4});
    recorder.fill_rectangle(texture, {0, 0, 4, 4});
    recorder.set_transform(gui_drawing::Matrix(2.0, 0.0, 0.0, 2.0, 0.0, 0.0));
    recorder.draw_line(pen, {0, 0}, {1, 1});

    CallPainter painter;
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter);
    CHECK(report.unsupported == 3U);
    CHECK(report.state == 1U);
    CHECK(report.transformed == 1U);
    CHECK(report.drawn == 0U);
    CHECK(painter.calls.empty());
}

// The blue, green, red and alpha bytes of a baked texture's pixel.
[[nodiscard]] std::uint32_t raster_bgra(const PartRaster& raster, std::uint32_t x, std::uint32_t y) {
    const std::size_t offset = static_cast<std::size_t>(y) * raster.row_bytes() + static_cast<std::size_t>(x) * 4U;
    std::uint32_t value = 0U;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        value = (value << 8U) | static_cast<std::uint32_t>(raster.pixels()[offset + channel]);
    }
    return value;
}

constexpr std::uint32_t baked_red = 0x0000ffffU;
constexpr std::uint32_t baked_blue = 0xff0000ffU;

// Replays one fill of `brush` over `area` at `scale` with `interpolation`
// into `painter`.
DrawingReplayReport replay_texture_fill(const gui_drawing::TextureBrush& brush, gui_drawing::RectF area,
                                        double scale, gui_drawing::InterpolationMode interpolation,
                                        CallPainter& painter) {
    gui_drawing::GraphicsRecorder recorder;
    recorder.set_quality(gui_drawing::SmoothingMode::default_mode, interpolation,
                         gui_drawing::PixelOffsetMode::default_mode, gui_drawing::CompositingMode::source_over,
                         gui_drawing::CompositingQuality::default_mode);
    recorder.fill_rectangle(brush, area);
    const DrawingReplayOptions options{scale};
    const DrawingReplayReport report = replay_drawing_commands(recorder.commands(), painter, options);
    return report;
}

// With the painter's display scale, a texture brush bakes into one part
// raster over its area's device pixels, wrapped per axis as the executor
// wraps it (tile, mirror, clamp), moved by its transform, nearest or
// bilinear; an over-large fill is counted unsupported.
void texture_brushes_replay_as_baked_rasters() {
    gui_drawing::Bitmap tile(2U, 1U);
    tile.set_pixel(0U, 0U, gui_drawing::Color::from_argb(0xffff0000U));
    tile.set_pixel(1U, 0U, gui_drawing::Color::from_argb(0xff0000ffU));
    const gui_drawing::InterpolationMode nearest = gui_drawing::InterpolationMode::nearest;

    gui_drawing::TextureBrush tiled(tile, gui_drawing::WrapMode::tile);
    CallPainter tiled_painter;
    CHECK(replay_texture_fill(tiled, {0, 0, 4, 1}, 1.0, nearest, tiled_painter).drawn == 1U);
    CHECK(count_calls(tiled_painter, "part_raster") == 1U && count_calls(tiled_painter, "clip") == 1U);
    CHECK(tiled_painter.last_clip == (Rect{0, 0, 4, 1}) && tiled_painter.last_raster_origin == (Point{0, 0}));
    CHECK(tiled_painter.depth == 0);
    const PartRaster& tiled_raster = *tiled_painter.last_raster;
    CHECK(tiled_raster.extent() == (DeviceExtent{4U, 1U}) && tiled_raster.opaque());
    CHECK(raster_bgra(tiled_raster, 0U, 0U) == baked_red && raster_bgra(tiled_raster, 1U, 0U) == baked_blue &&
          raster_bgra(tiled_raster, 2U, 0U) == baked_red && raster_bgra(tiled_raster, 3U, 0U) == baked_blue);

    gui_drawing::TextureBrush mirrored(tile, gui_drawing::WrapMode::tile_flip_x);
    CallPainter mirrored_painter;
    CHECK(replay_texture_fill(mirrored, {0, 0, 4, 1}, 1.0, nearest, mirrored_painter).drawn == 1U);
    const PartRaster& mirrored_raster = *mirrored_painter.last_raster;
    CHECK(raster_bgra(mirrored_raster, 0U, 0U) == baked_red && raster_bgra(mirrored_raster, 1U, 0U) == baked_blue &&
          raster_bgra(mirrored_raster, 2U, 0U) == baked_blue && raster_bgra(mirrored_raster, 3U, 0U) == baked_red);

    gui_drawing::TextureBrush clamped(tile, gui_drawing::WrapMode::clamp);
    CallPainter clamped_painter;
    CHECK(replay_texture_fill(clamped, {0, 0, 4, 1}, 1.0, nearest, clamped_painter).drawn == 1U);
    const PartRaster& clamped_raster = *clamped_painter.last_raster;
    CHECK(raster_bgra(clamped_raster, 0U, 0U) == baked_red && raster_bgra(clamped_raster, 1U, 0U) == baked_blue &&
          raster_bgra(clamped_raster, 2U, 0U) == baked_blue && raster_bgra(clamped_raster, 3U, 0U) == baked_blue);

    // Moved one unit right, the texel left of the origin is the tile's
    // last one.
    gui_drawing::TextureBrush moved(tile, gui_drawing::WrapMode::tile);
    moved.set_transform(gui_drawing::Matrix(1.0, 0.0, 0.0, 1.0, 1.0, 0.0));
    CallPainter moved_painter;
    CHECK(replay_texture_fill(moved, {0, 0, 2, 1}, 1.0, nearest, moved_painter).drawn == 1U);
    const PartRaster& moved_raster = *moved_painter.last_raster;
    CHECK(raster_bgra(moved_raster, 0U, 0U) == baked_blue && raster_bgra(moved_raster, 1U, 0U) == baked_red);

    // At scale 2 the area covers twice the device pixels, and a fractional
    // area rounds out to whole pixels (0.5 to 3.5 becomes 0 to 4). Moved a
    // quarter unit right, the third column's centre (x 1.25) samples the
    // image at u 1, halfway between the texels' centres, so bilinear
    // sampling mixes them evenly: (255 * 128 * 256 + 32768) / 65536 = 128.
    gui_drawing::TextureBrush halfway(tile, gui_drawing::WrapMode::tile);
    halfway.set_transform(gui_drawing::Matrix(1.0, 0.0, 0.0, 1.0, 0.25, 0.0));
    CallPainter scaled_painter;
    CHECK(replay_texture_fill(halfway, {0.25, 0, 1.5, 1}, 2.0, gui_drawing::InterpolationMode::bilinear,
                              scaled_painter).drawn == 1U);
    const PartRaster& scaled_raster = *scaled_painter.last_raster;
    CHECK(scaled_raster.extent() == (DeviceExtent{4U, 2U}) && scaled_raster.scale() == (DeviceScale{2.0}));
    CHECK(scaled_painter.last_raster_origin == (Point{0.0, 0.0}));
    CHECK(scaled_painter.last_clip == (Rect{0.25, 0, 1.5, 1}));
    CHECK(raster_bgra(scaled_raster, 2U, 0U) == 0x800080ffU);
    CHECK(raster_bgra(scaled_raster, 2U, 1U) == 0x800080ffU);

    // A fill of more than 2^24 device pixels is left out; without a
    // display scale every texture fill is.
    CallPainter large_painter;
    const DrawingReplayReport large =
        replay_texture_fill(tiled, {0, 0, 5000, 5000}, 1.0, nearest, large_painter);
    CHECK(large.unsupported == 1U && large.drawn == 0U && count_calls(large_painter, "part_raster") == 0U);
    CallPainter unscaled_painter;
    const DrawingReplayReport unscaled =
        replay_texture_fill(tiled, {0, 0, 4, 1}, 0.0, nearest, unscaled_painter);
    CHECK(unscaled.unsupported == 1U && unscaled_painter.calls.empty());
}

// A radial gradient brush replays as a radial fill, or a focal one when
// its focus has moved, with its wrap mode as the spread.
void radial_gradient_brushes_replay() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::RadialGradientBrush plain({8, 8}, 6.0, gui_drawing::Color::from_argb(0xffff0000U),
                                           gui_drawing::Color::from_argb(0xff0000ffU), gui_drawing::WrapMode::tile);
    recorder.fill_rectangle(plain, {0, 0, 16, 16});
    CallPainter first;
    CHECK(replay_drawing_commands(recorder.commands(), first).drawn == 1U);
    CHECK(count_calls(first, "radial") == 1U && first.last_start == (Point{8, 8}) &&
          first.last_radii == (Size{6, 6}) && first.last_count == 2U &&
          first.last_spread == GradientSpreadMode::repeat);

    gui_drawing::RadialGradientBrush focal({8, 8}, 6.0, gui_drawing::Color::from_argb(0xffff0000U),
                                           gui_drawing::Color::from_argb(0xff0000ffU));
    focal.set_focus({5, 8});
    gui_drawing::GraphicsRecorder focused;
    focused.fill_rectangle(focal, {0, 0, 16, 16});
    CallPainter second;
    CHECK(replay_drawing_commands(focused.commands(), second).drawn == 1U);
    CHECK(count_calls(second, "focal") == 1U && second.last_end == (Point{5, 8}) &&
          second.last_spread == GradientSpreadMode::pad);

    // An ellipse replays with its two radii.
    focal.set_radii(6.0, 3.0);
    gui_drawing::GraphicsRecorder elliptical;
    elliptical.fill_rectangle(focal, {0, 0, 16, 16});
    CallPainter third;
    CHECK(replay_drawing_commands(elliptical.commands(), third).drawn == 1U);
    CHECK(count_calls(third, "focal") == 1U && third.last_radii == (Size{6, 3}));
}

} // namespace

int main() {
    batched_marks_replay_under_their_state();
    gapped_marks_and_dashes_replay();
    paths_ellipses_and_brushes_replay();
    path_verbs_follow_the_executor();
    unsupported_commands_are_counted();
    radial_gradient_brushes_replay();
    texture_brushes_replay_as_baked_rasters();
    return 0;
}
