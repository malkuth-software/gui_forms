#include "gui_forms/drawing/drawing_replay/drawing_replay.hpp"
#include "gui_forms/drawing.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
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

// Unsupported kinds and transforms are counted and draw nothing.
void unsupported_commands_are_counted() {
    gui_drawing::GraphicsRecorder recorder;
    gui_drawing::Pen pen(gui_drawing::Color::from_argb(0xff000000U), 1.0);
    gui_drawing::SolidBrush brush(gui_drawing::Color::from_argb(0xff000000U));
    recorder.clear(gui_drawing::Color::from_argb(0xffffffffU));
    recorder.fill_ellipse(brush, {0, 0, 4, 4});
    gui_drawing::HatchBrush hatch(gui_drawing::HatchStyle::cross, gui_drawing::Color::from_argb(0xff000000U),
                                  gui_drawing::Color::from_argb(0xffffffffU));
    recorder.fill_rectangle(hatch, {0, 0, 4, 4});
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

} // namespace

int main() {
    batched_marks_replay_under_their_state();
    gapped_marks_and_dashes_replay();
    unsupported_commands_are_counted();
    return 0;
}
