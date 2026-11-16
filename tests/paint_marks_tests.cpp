// The batched marks and patterns of the Painter vocabulary (IMGUI_IMPLOT_STUDY
// P7; NODE_ENGINE_AND_VALUE_BAR_STUDY 1A, pieces C1, C7, C9, C17): their
// value helpers, their recording and replay as one display command each
// (display-trace cases: every call is written as text with its arguments and
// compared exactly), the base painter's fallbacks, and the part primitives
// composed from them (paint_recipes.hpp: C2, C3, C4); and the curved paths
// (paint_path.hpp, exhibit gap E7): values, recording and fallbacks.
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"
#include "gui_forms/types/paint_recipes/paint_recipes.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/layout/radial_layout/radial_layout.hpp"
#include "../src/core/display/recording_painter/recording_painter.hpp"
#include "../src/core/display/replay/replay_display_chunk.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr Color ink = Color::rgba(10, 20, 30);
constexpr Color paper = Color::rgba(240, 240, 240);
constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// Writes each call with its arguments, one line per call (the display-trace
// format of the batched marks). Numbers are written with the stream's
// default precision, which the cases' values print exactly.
class TracePainter final : public Painter {
public:
    void save() override { out << "save\n"; }
    void restore() override { out << "restore\n"; }
    void translate(Point offset) override { out << "translate " << point(offset) << '\n'; }
    void clip_rect(Rect rect) override { out << "clip_rect " << box(rect) << '\n'; }
    void clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) override {
        out << "clip_rounded_rect_corners " << box(rect) << ' ' << corners(radii) << '\n';
    }
    void stroke_rounded_rect(Rect rect, double radius, Color color, double width) override {
        out << "stroke_rounded_rect " << box(rect) << ' ' << radius << ' ' << colour(color) << ' ' << width << '\n';
    }
    void fill_rect(Rect rect, Color color) override { out << "fill_rect " << box(rect) << ' ' << colour(color) << '\n'; }
    void stroke_rect(Rect rect, Color color, double width) override {
        out << "stroke_rect " << box(rect) << ' ' << colour(color) << ' ' << width << '\n';
    }
    void draw_line(Point from, Point to, Color color, double width) override {
        out << "draw_line " << point(from) << ' ' << point(to) << ' ' << colour(color) << ' ' << width << '\n';
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color color) override {
        out << "draw_text " << point(origin) << " \"" << text << "\" " << colour(color) << '\n';
    }
    void draw_image(ImageId, Rect, double) override { out << "draw_image\n"; }
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override {
        out << "stroke_polyline " << points_text(points) << ' ' << stroke(style) << '\n';
    }
    void fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) override {
        out << "fill_polygon " << points_text(points) << ' ' << colour(color) << ' '
            << (rule == PolygonFillRule::even_odd ? "even_odd" : "nonzero") << '\n';
    }
    void fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color) override {
        out << "fill_band " << points_text(upper) << " / " << points_text(lower) << ' ' << colour(color) << '\n';
    }
    void fill_rects(std::span<const Rect> rects, Color color) override {
        out << "fill_rects";
        for (const Rect rect : rects) out << ' ' << box(rect);
        out << ' ' << colour(color) << '\n';
    }
    void fill_rects_each(std::span<const Rect> rects, std::span<const Color> colors) override {
        out << "fill_rects_each";
        for (std::size_t index = 0U; index < rects.size() && index < colors.size(); ++index) {
            out << ' ' << box(rects[index]) << ':' << colour(colors[index]);
        }
        out << '\n';
    }
    void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) override {
        out << "stroke_segments";
        for (const PaintSegment segment : segments) out << ' ' << point(segment.from) << '>' << point(segment.to);
        out << ' ' << stroke(style) << '\n';
    }
    void draw_markers(std::span<const Point> centers, const MarkerStyle& style) override {
        out << "draw_markers " << points_text(centers) << " shape=" << static_cast<unsigned>(style.shape)
            << " size=" << style.size << " fill=" << colour(style.fill) << " edge=" << colour(style.edge) << ','
            << style.edge_width << '\n';
    }
    void fill_conic_gradient(Rect rect, Point center, double start_degrees,
                             std::span<const GradientStop> stops) override {
        out << "fill_conic_gradient " << box(rect) << ' ' << point(center) << ' ' << start_degrees;
        for (const GradientStop stop : stops) out << ' ' << stop.offset << ':' << colour(stop.color);
        out << '\n';
    }
    void fill_checker(Rect rect, const CheckerPattern& pattern) override {
        out << "fill_checker " << box(rect) << " cell=" << pattern.cell << ' ' << colour(pattern.first) << ' '
            << colour(pattern.second) << " anchor=" << point(pattern.anchor) << '\n';
    }
    void fill_grid_pattern(Rect rect, const GridPattern& pattern) override {
        out << "fill_grid_pattern " << box(rect) << " origin=" << point(pattern.origin) << " spacing=" << pattern.spacing
            << " major_every=" << pattern.major_every << ' ' << colour(pattern.minor) << ' ' << colour(pattern.major)
            << " width=" << pattern.line_width << '\n';
    }
    void fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color) override {
        out << "fill_rounded_rect_corners " << box(rect) << ' ' << corners(radii) << ' ' << colour(color) << '\n';
    }
    void stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width) override {
        out << "stroke_rounded_rect_corners " << box(rect) << ' ' << corners(radii) << ' ' << colour(color) << ' '
            << width << '\n';
    }
    void fill_path(PaintPathView path, Color color, PolygonFillRule rule) override {
        out << "fill_path " << path_text(path) << ' ' << colour(color) << ' '
            << (rule == PolygonFillRule::even_odd ? "even_odd" : "nonzero") << '\n';
    }
    void stroke_path(PaintPathView path, const StrokeStyle& style) override {
        out << "stroke_path " << path_text(path) << ' ' << stroke(style) << '\n';
    }
    void clip_path(PaintPathView path, PolygonFillRule rule) override {
        out << "clip_path " << path_text(path) << ' ' << (rule == PolygonFillRule::even_odd ? "even_odd" : "nonzero")
            << '\n';
    }
    void fill_radial_gradient_spread(Rect rect, Point center, Size radii, std::span<const GradientStop> stops,
                                     GradientSpreadMode spread) override {
        static constexpr std::array<const char*, 3> spreads{{"pad", "repeat", "reflect"}};
        const std::size_t index = static_cast<std::size_t>(spread);
        out << "fill_radial_gradient_spread " << box(rect) << ' ' << point(center) << ' ' << radii.width << ','
            << radii.height << ' ' << (index < spreads.size() ? spreads[index] : "?");
        for (const GradientStop stop : stops) out << ' ' << stop.offset << ':' << colour(stop.color);
        out << '\n';
    }

    void fill_hatch(Rect rect, const HatchPattern& pattern) override {
        out << "fill_hatch " << box(rect) << " style=" << static_cast<unsigned>(pattern.style)
            << " cell=" << pattern.cell << ' ' << colour(pattern.fore) << ' ' << colour(pattern.back)
            << " anchor=" << point(pattern.anchor) << '\n';
    }
    void fill_focal_radial_gradient(Rect rect, Point center, Size radii, Point focus,
                                    std::span<const GradientStop> stops, GradientSpreadMode spread) override {
        out << "fill_focal_radial_gradient " << box(rect) << ' ' << point(center) << ' ' << radii.width << ','
            << radii.height << " focus=" << point(focus) << " spread=" << static_cast<unsigned>(spread);
        for (const GradientStop stop : stops) out << ' ' << stop.offset << ':' << colour(stop.color);
        out << '\n';
    }

    std::ostringstream out;

private:
    static std::string point(Point value) {
        std::ostringstream text;
        text << value.x << ',' << value.y;
        return text.str();
    }
    static std::string box(Rect value) {
        std::ostringstream text;
        text << value.x << ',' << value.y << ',' << value.width << ',' << value.height;
        return text.str();
    }
    static std::string colour(Color value) {
        std::ostringstream text;
        text << '#' << static_cast<unsigned>(value.red) << '.' << static_cast<unsigned>(value.green) << '.'
             << static_cast<unsigned>(value.blue) << '.' << static_cast<unsigned>(value.alpha);
        return text.str();
    }
    static std::string corners(PaintCornerRadii value) {
        std::ostringstream text;
        text << value.top_left << '/' << value.top_right << '/' << value.bottom_right << '/' << value.bottom_left;
        return text.str();
    }
    static std::string points_text(std::span<const Point> values) {
        std::string text;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (index != 0U) text += ';';
            text += point(values[index]);
        }
        return text;
    }
    // Each verb as its letter (M L Q C A Z) followed by its point slots.
    static std::string path_text(PaintPathView path) {
        static constexpr std::array<char, 6> letters{{'M', 'L', 'Q', 'C', 'A', 'Z'}};
        std::string text;
        std::size_t consumed = 0U;
        for (const PaintPathVerb verb : path.verbs) {
            const std::size_t index = static_cast<std::size_t>(verb);
            text += index < letters.size() ? letters[index] : '?';
            const std::size_t count = paint_path_points_for(verb);
            if (count != 0U && consumed + count <= path.points.size()) {
                text += points_text(path.points.subspan(consumed, count));
            }
            consumed = consumed + count;
        }
        return text;
    }
    static std::string stroke(const StrokeStyle& style) {
        std::ostringstream text;
        text << colour(style.color) << " w=" << style.width << " join=" << static_cast<unsigned>(style.join)
             << " cap=" << static_cast<unsigned>(style.cap) << " dashes=";
        for (std::size_t index = 0U; index < style.dash_count; ++index) {
            if (index != 0U) text << ',';
            text << style.dashes[index];
        }
        text << '@' << style.dash_phase;
        return text.str();
    }
};

// A minimal painter: only the pure primitives, so every batched call takes
// the base fallback. Counts what the fallbacks draw.
class MinimalPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { clips.push_back(rect); }
    void fill_rect(Rect rect, Color color) override {
        fills.push_back(rect);
        fill_colours.push_back(color);
    }
    void stroke_rect(Rect, Color, double) override { ++strokes; }
    void draw_line(Point from, Point to, Color, double) override {
        PaintSegment segment{};
        segment.from = from;
        segment.to = to;
        lines.push_back(segment);
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    double filled_area() const {
        double total = 0.0;
        for (const Rect rect : fills) total = total + rect.width * rect.height;
        return total;
    }

    std::size_t saves{};
    std::size_t restores{};
    std::size_t strokes{};
    std::vector<Rect> clips;
    std::vector<Rect> fills;
    std::vector<Color> fill_colours;
    std::vector<PaintSegment> lines;
};

StrokeStyle dashed_stroke() {
    StrokeStyle style{};
    style.color = ink;
    style.width = 2.0;
    style.join = StrokeJoin::round;
    style.cap = StrokeCap::square;
    style.dashes[0] = 4.0;
    style.dashes[1] = 2.0;
    style.dash_count = 2U;
    style.dash_phase = 1.0;
    return style;
}

void test_style_validation() {
    StrokeStyle stroke{};
    require(valid_stroke_style(stroke), "the default stroke must be valid");
    require(valid_stroke_style(dashed_stroke()), "an even, positive dash pattern must be valid");
    StrokeStyle odd = dashed_stroke();
    odd.dash_count = 3U;
    StrokeStyle zero = dashed_stroke();
    zero.dashes[0] = 0.0;
    zero.dashes[1] = 0.0;
    StrokeStyle thin{};
    thin.width = 0.0;
    StrokeStyle limit{};
    limit.miter_limit = 0.5;
    StrokeStyle phase{};
    phase.dash_phase = not_a_number;
    require(!valid_stroke_style(odd) && !valid_stroke_style(zero) && !valid_stroke_style(thin) &&
                !valid_stroke_style(limit) && !valid_stroke_style(phase),
            "odd or all-zero dashes, a zero width, a miter limit under 1 or a NaN phase must be refused");

    MarkerStyle marker{};
    require(valid_marker_style(marker), "the default marker must be valid");
    MarkerStyle empty{};
    empty.size = 0.0;
    MarkerStyle unknown{};
    unknown.shape = static_cast<MarkerShape>(200);
    require(!valid_marker_style(empty) && !valid_marker_style(unknown), "a zero size or unknown shape must be refused");

    GridPattern grid{};
    require(valid_grid_pattern(grid), "the default grid must be valid");
    GridPattern fine{};
    fine.spacing = 1.0;
    GridPattern none{};
    none.major_every = 0U;
    require(!valid_grid_pattern(fine) && !valid_grid_pattern(none),
            "a spacing under minimum_grid_spacing or majors every 0 lines must be refused");
}

void test_marker_geometry() {
    std::array<Point, maximum_marker_vertices> outline{};
    const std::size_t square = marker_outline(MarkerShape::square, Point{10.0, 10.0}, 4.0, outline);
    require(square == 4U && outline[0] == (Point{8.0, 8.0}) && outline[2] == (Point{12.0, 12.0}),
            "a square marker must span its size about its centre");
    const std::size_t up = marker_outline(MarkerShape::triangle_up, Point{0.0, 0.0}, 2.0, outline);
    require(up == 3U && outline[0] == (Point{0.0, -1.0}) && outline[1] == (Point{1.0, 1.0}),
            "the upward triangle must point up (y down)");
    require(marker_outline(MarkerShape::circle, Point{}, 2.0, outline) == 0U &&
                marker_outline(MarkerShape::plus, Point{}, 2.0, outline) == 0U,
            "circles and stroked shapes have no outline");
    std::array<PaintSegment, maximum_marker_lines> lines{};
    require(marker_lines(MarkerShape::plus, Point{}, 2.0, lines) == 2U &&
                lines[0].from == (Point{-1.0, 0.0}) && lines[1].to == (Point{0.0, 1.0}),
            "a plus must cross at its centre");
    require(marker_lines(MarkerShape::asterisk, Point{}, 2.0, lines) == 3U &&
                marker_lines(MarkerShape::diamond, Point{}, 2.0, lines) == 0U,
            "an asterisk has three strokes and a filled shape none");
}

void test_grid_levels() {
    const GridLevelBand band{};
    const GridLevel unit = select_grid_level(16.0, 1.0, band);
    require(unit.level == 0 && unit.minor_spacing == 16.0, "spacing 16 at zoom 1 is level 0");
    require(std::abs(unit.fade - (4.0 / 36.0)) < 1e-15, "the fade is the spacing's place across the band");
    const GridLevel far = select_grid_level(16.0, 0.125, band);
    // 16 × 0.125 = 2, then 8, then 32: two levels coarser.
    require(far.level == 2 && far.minor_spacing == 32.0, "zooming out picks a coarser level");
    // 16 × 8 = 128, then 32: one level finer.
    const GridLevel near = select_grid_level(16.0, 8.0, band);
    require(near.level == -1 && near.minor_spacing == 32.0, "zooming in picks a finer level");
    require(near.minor_spacing >= band.minimum_spacing && near.minor_spacing < band.maximum_spacing,
            "a chosen level lies in the band");
    GridLevelBand wrong{};
    wrong.maximum_spacing = 40.0;
    const GridLevel refused = select_grid_level(16.0, 1.0, wrong);
    require(refused.level == 0 && refused.fade == 1.0, "a band not spanning a factor of 4 is refused");

    GridLevel born{};
    born.minor_spacing = 12.0;
    born.fade = 0.0;
    GridLevel ripe{};
    ripe.minor_spacing = 47.0;
    ripe.fade = 1.0;
    const Color minor = Color::rgba(100, 100, 100, 200);
    const Color major = Color::rgba(0, 0, 255, 255);
    const GridPattern at_birth = grid_pattern_at_level(born, Point{}, minor, major, 1.0);
    const GridPattern at_end = grid_pattern_at_level(ripe, Point{}, minor, major, 1.0);
    require(at_birth.minor.alpha == 0U && at_birth.major_every == 4U, "new minor lines start transparent");
    require(at_end.minor == major, "minor lines end looking as the majors they become");
}

void test_one_command_per_batch_and_exact_replay() {
    // The caller's storage: reused after each call, so the recording must
    // have copied it.
    std::array<Point, 4> points{{Point{0.0, 0.0}, Point{10.0, 5.0}, Point{not_a_number, 0.0}, Point{20.0, 0.0}}};
    std::array<Rect, 2> rects{{Rect{0.0, 0.0, 2.0, 4.0}, Rect{3.0, 0.0, 2.0, 6.0}}};
    std::array<Color, 2> colours{{ink, paper}};
    std::array<PaintSegment, 1> segments{{PaintSegment{Point{1.0, 1.0}, Point{1.0, 9.0}}}};
    std::array<Point, 2> lower{{Point{0.0, 10.0}, Point{10.0, 10.0}}};
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, ink}, GradientStop{1.0, paper}}};
    MarkerStyle marker{};
    marker.shape = MarkerShape::diamond;
    marker.size = 5.0;
    marker.fill = ink;
    marker.edge = paper;
    CheckerPattern checker{};
    checker.cell = 4.0;
    checker.first = paper;
    checker.second = ink;
    checker.anchor = Point{1.0, 2.0};
    GridPattern grid{};
    grid.origin = Point{3.0, 4.0};
    grid.minor = paper;
    grid.major = ink;
    const PaintCornerRadii radii{4.0, 0.0, 0.0, 4.0};

    detail::RecordingPainter recorder;
    recorder.stroke_polyline(points, dashed_stroke());
    recorder.fill_polygon(std::span<const Point>(points.data(), 2U), ink, PolygonFillRule::even_odd);
    recorder.fill_band(std::span<const Point>(points.data(), 2U), lower, paper);
    recorder.fill_rects(rects, ink);
    recorder.fill_rects_each(rects, colours);
    recorder.stroke_segments(segments, dashed_stroke());
    recorder.draw_markers(std::span<const Point>(points.data(), 2U), marker);
    recorder.fill_conic_gradient(Rect{0.0, 0.0, 20.0, 20.0}, Point{10.0, 10.0}, -90.0, stops);
    recorder.fill_checker(Rect{0.0, 0.0, 16.0, 8.0}, checker);
    recorder.fill_grid_pattern(Rect{0.0, 0.0, 64.0, 32.0}, grid);
    recorder.fill_rounded_rect_corners(Rect{0.0, 0.0, 30.0, 12.0}, radii, ink);
    recorder.stroke_rounded_rect_corners(Rect{0.0, 0.0, 30.0, 12.0}, radii, paper, 1.0);
    points.fill(Point{99.0, 99.0});
    rects.fill(Rect{});
    colours.fill(Color{});
    segments.fill(PaintSegment{});
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, 64.0, 32.0});
    require((*chunk).commands().size() == 12U, "each batched call must record exactly one command");

    TracePainter trace;
    const std::uint64_t replayed = detail::replay_display_chunk(*chunk, trace);
    require(replayed == 12U, "every command must replay");
    const std::string expected =
        "stroke_polyline 0,0;10,5;nan,0;20,0 #10.20.30.255 w=2 join=1 cap=2 dashes=4,2@1\n"
        "fill_polygon 0,0;10,5 #10.20.30.255 even_odd\n"
        "fill_band 0,0;10,5 / 0,10;10,10 #240.240.240.255\n"
        "fill_rects 0,0,2,4 3,0,2,6 #10.20.30.255\n"
        "fill_rects_each 0,0,2,4:#10.20.30.255 3,0,2,6:#240.240.240.255\n"
        "stroke_segments 1,1>1,9 #10.20.30.255 w=2 join=1 cap=2 dashes=4,2@1\n"
        "draw_markers 0,0;10,5 shape=2 size=5 fill=#10.20.30.255 edge=#240.240.240.255,1\n"
        "fill_conic_gradient 0,0,20,20 10,10 -90 0:#10.20.30.255 1:#240.240.240.255\n"
        "fill_checker 0,0,16,8 cell=4 #240.240.240.255 #10.20.30.255 anchor=1,2\n"
        "fill_grid_pattern 0,0,64,32 origin=3,4 spacing=16 major_every=4 #240.240.240.255 #10.20.30.255 width=1\n"
        "fill_rounded_rect_corners 0,0,30,12 4/0/0/4 #10.20.30.255\n"
        "stroke_rounded_rect_corners 0,0,30,12 4/0/0/4 #240.240.240.255 1\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "the batched commands must replay exactly what was recorded");
}

void test_conic_stops_are_checked_when_recorded() {
    detail::RecordingPainter recorder;
    const std::array<GradientStop, 1> single{{GradientStop{0.0, ink}}};
    bool refused = false;
    try {
        recorder.fill_conic_gradient(Rect{0.0, 0.0, 4.0, 4.0}, Point{2.0, 2.0}, 0.0, single);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a conic gradient with invalid stops must be refused as the other gradients are");
}

void test_fallback_polyline_breaks_at_nan() {
    MinimalPainter painter;
    const std::array<Point, 5> points{
        {Point{0.0, 0.0}, Point{1.0, 0.0}, Point{2.0, 0.0}, Point{not_a_number, 0.0}, Point{4.0, 0.0}}};
    painter.stroke_polyline(points, dashed_stroke());
    require(painter.lines.size() == 2U && painter.lines[1].to == (Point{2.0, 0.0}),
            "a NaN point must break the polyline and a run of one point draw nothing");
    MinimalPainter refused;
    const std::array<Point, 2> infinite{{Point{0.0, 0.0}, Point{std::numeric_limits<double>::infinity(), 0.0}}};
    refused.stroke_polyline(infinite, dashed_stroke());
    StrokeStyle bad{};
    bad.width = -1.0;
    refused.stroke_polyline(points, bad);
    require(refused.lines.empty(), "an infinite point or an invalid style must draw nothing");
}

void test_fallback_fills() {
    MinimalPainter square;
    const std::array<Point, 4> outline{{Point{0.0, 0.0}, Point{10.0, 0.0}, Point{10.0, 10.0}, Point{0.0, 10.0}}};
    square.fill_polygon(outline, ink, PolygonFillRule::nonzero);
    require(square.fills.size() == 10U && std::abs(square.filled_area() - 100.0) < 1e-9,
            "a fallback polygon must be filled by unit rows covering its area");

    // A square wound twice: nonzero fills it, even-odd leaves it empty.
    const std::array<Point, 8> twice{{Point{0.0, 0.0}, Point{4.0, 0.0}, Point{4.0, 4.0}, Point{0.0, 4.0},
                                      Point{0.0, 0.0}, Point{4.0, 0.0}, Point{4.0, 4.0}, Point{0.0, 4.0}}};
    MinimalPainter nonzero;
    nonzero.fill_polygon(twice, ink, PolygonFillRule::nonzero);
    MinimalPainter even_odd;
    even_odd.fill_polygon(twice, ink, PolygonFillRule::even_odd);
    require(std::abs(nonzero.filled_area() - 16.0) < 1e-9 && even_odd.filled_area() == 0.0,
            "the fill rule must decide a doubly wound outline");

    MinimalPainter band;
    const std::array<Point, 3> upper{{Point{0.0, 0.0}, Point{5.0, 0.0}, Point{10.0, 0.0}}};
    const std::array<Point, 3> base{{Point{0.0, 4.0}, Point{5.0, 4.0}, Point{10.0, 4.0}}};
    band.fill_band(upper, base, ink);
    require(std::abs(band.filled_area() - 40.0) < 1e-9, "a band must fill between its polylines");

    MinimalPainter bars;
    const std::array<Rect, 3> rects{{Rect{0.0, 0.0, 2.0, 2.0}, Rect{}, Rect{4.0, 0.0, 2.0, 2.0}}};
    bars.fill_rects(rects, ink);
    const std::array<Color, 2> too_few{{ink, paper}};
    bars.fill_rects_each(rects, too_few);
    require(bars.fills.size() == 2U, "empty rectangles are skipped and mismatched colours draw nothing");
}

void test_fallback_markers() {
    MinimalPainter painter;
    MarkerStyle style{};
    style.shape = MarkerShape::plus;
    style.size = 4.0;
    style.edge = ink;
    const std::array<Point, 3> centers{{Point{0.0, 0.0}, Point{not_a_number, 0.0}, Point{10.0, 10.0}}};
    painter.draw_markers(centers, style);
    require(painter.lines.size() == 4U, "two finite plus markers draw two lines each");
    MinimalPainter filled;
    style.shape = MarkerShape::square;
    style.fill = paper;
    filled.draw_markers(centers, style);
    require(std::abs(filled.filled_area() - 32.0) < 1e-9 && filled.lines.size() == 8U,
            "square markers fill their area and outline their four edges");
}

void test_fallback_patterns() {
    MinimalPainter checker;
    CheckerPattern pattern{};
    pattern.cell = 4.0;
    pattern.first = paper;
    pattern.second = ink;
    checker.fill_checker(Rect{0.0, 0.0, 8.0, 8.0}, pattern);
    require(checker.fills.size() == 3U && checker.fill_colours[0] == paper && checker.fills[1] == (Rect{4.0, 0.0, 4.0, 4.0}),
            "a checker fills the first colour and then the odd cells");
    MinimalPainter fine;
    pattern.cell = 0.5;
    fine.fill_checker(Rect{0.0, 0.0, 8.0, 8.0}, pattern);
    require(fine.fills.size() == 1U && fine.fill_colours[0] == Color::rgba(125, 130, 135, 255),
            "a checker finer than one unit fills the average colour");

    MinimalPainter grid;
    GridPattern lines{};
    lines.origin = Point{0.0, 0.0};
    lines.spacing = 10.0;
    lines.major_every = 2U;
    lines.minor = paper;
    lines.major = ink;
    grid.fill_grid_pattern(Rect{0.0, 0.0, 40.0, 20.0}, lines);
    // Columns at 0 through 40 (5), rows at 0 through 20 (3); majors at even
    // indices, drawn after the minors.
    require(grid.fills.size() == 8U && grid.saves == 1U && grid.restores == 1U, "a grid draws each line once, clipped");
    require(grid.fill_colours[0] == paper && grid.fill_colours.back() == ink, "minor lines go under the majors");

    MinimalPainter conic;
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, ink}, GradientStop{1.0, paper}}};
    conic.fill_conic_gradient(Rect{0.0, 0.0, 10.0, 10.0}, Point{5.0, 5.0}, 0.0, stops);
    require(conic.saves == 1U && conic.clips.size() == 1U && !conic.fills.empty(),
            "a fallback conic gradient fills wedges inside its rectangle");
}

void test_proportion_and_masked_fill() {
    const Rect track{10.0, 20.0, 100.0, 10.0};
    require(proportion_rect(track, 0.25, FillDirection::left_to_right) == (Rect{10.0, 20.0, 25.0, 10.0}) &&
                proportion_rect(track, 0.25, FillDirection::right_to_left) == (Rect{85.0, 20.0, 25.0, 10.0}) &&
                proportion_rect(track, 0.5, FillDirection::bottom_to_top) == (Rect{10.0, 25.0, 100.0, 5.0}) &&
                proportion_rect(track, 2.0, FillDirection::top_to_bottom) == track,
            "a proportion must cover its share of the track from the named edge, clamped to the track");
    require(proportion_rect(track, not_a_number, FillDirection::left_to_right).empty(), "NaN must cover nothing");

    TracePainter trace;
    const PaintCornerRadii radii{5.0, 5.0, 5.0, 5.0};
    paint_masked_fill(trace, track, radii, 0.25, FillDirection::left_to_right, ink);
    paint_masked_fill(trace, track, radii, 0.0, FillDirection::left_to_right, ink);
    const std::string expected = "save\n"
                                 "clip_rounded_rect_corners 10,20,100,10 5/5/5/5\n"
                                 "fill_rect 10,20,25,10 #10.20.30.255\n"
                                 "restore\n";
    require(trace.out.str() == expected,
            "a masked fill must clip to the track's rounded shape, and an empty one draw nothing");
}

void test_contrast_rule() {
    require(std::abs(wcag_contrast_ratio(Color::rgba(0, 0, 0), Color::rgba(255, 255, 255)) - 21.0) < 1e-9,
            "black on white is 21:1");
    const Color light = Color::rgba(255, 255, 255);
    const Color dark = Color::rgba(0, 0, 0);
    require(contrasting_text_color(Color::rgba(20, 40, 140), light, dark) == light,
            "a dark fill takes the light text");
    require(contrasting_text_color(Color::rgba(250, 220, 60), light, dark) == dark,
            "a light fill takes the dark text");
}

void test_text_over_fill_uses_complementary_clips() {
    TracePainter trace;
    const FontSpec font{};
    paint_text_over_fill(trace, Rect{0.0, 0.0, 100.0, 20.0}, Rect{0.0, 0.0, 40.0, 20.0}, Point{30.0, 14.0}, "42 %",
                         font, ink, paper);
    const std::string expected = "save\n"
                                 "clip_rect 0,0,40,20\n"
                                 "draw_text 30,14 \"42 %\" #240.240.240.255\n"
                                 "restore\n"
                                 "save\n"
                                 "clip_rect 40,0,60,20\n"
                                 "draw_text 30,14 \"42 %\" #10.20.30.255\n"
                                 "restore\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "text over a fill must be drawn once on each side of the fill's edge");

    TracePainter middle;
    paint_text_over_fill(middle, Rect{0.0, 0.0, 100.0, 20.0}, Rect{20.0, 5.0, 40.0, 10.0}, Point{0.0, 14.0}, "x",
                         font, ink, paper);
    const std::string clips = middle.out.str();
    std::size_t bands = 0U;
    for (std::size_t at = clips.find("clip_rect "); at != std::string::npos; at = clips.find("clip_rect ", at + 1U)) {
        bands = bands + 1U;
    }
    require(bands == 5U, "a fill inside the track leaves four bands around it");

    TracePainter empty;
    paint_text_over_fill(empty, Rect{0.0, 0.0, 100.0, 20.0}, Rect{}, Point{0.0, 14.0}, "x", font, ink, paper);
    require(empty.out.str() == "save\nclip_rect 0,0,100,20\ndraw_text 0,14 \"x\" #10.20.30.255\nrestore\n",
            "with no fill the text is drawn once in the track colour");
}

void test_joined_segments() {
    require(joined_segment_position(0U, 1U) == JoinedSegmentPosition::only &&
                joined_segment_position(0U, 3U) == JoinedSegmentPosition::first &&
                joined_segment_position(1U, 3U) == JoinedSegmentPosition::middle &&
                joined_segment_position(2U, 3U) == JoinedSegmentPosition::last,
            "positions must follow the index in the group");
    const PaintCornerRadii column_first = joined_segment_radii(JoinedSegmentPosition::first, JoinAxis::column, 3.0);
    require(column_first == (PaintCornerRadii{3.0, 3.0, 0.0, 0.0}), "a column's first segment rounds its top");

    TracePainter trace;
    const std::array<Rect, 3> segments{
        {Rect{0.0, 0.0, 20.0, 10.0}, Rect{20.0, 0.0, 20.0, 10.0}, Rect{40.0, 0.0, 20.0, 10.0}}};
    const std::array<Color, 3> faces{{paper, ink, paper}};
    JoinedSegmentsStyle style{};
    style.radius = 4.0;
    style.edge = ink;
    style.seam = ink;
    paint_joined_segments(trace, segments, faces, JoinAxis::row, style);
    const std::string expected = "fill_rounded_rect_corners 0,0,20,10 4/0/0/4 #240.240.240.255\n"
                                 "fill_rounded_rect_corners 20,0,20,10 0/0/0/0 #10.20.30.255\n"
                                 "fill_rounded_rect_corners 40,0,20,10 0/4/4/0 #240.240.240.255\n"
                                 "draw_line 20,0 20,10 #10.20.30.255 1\n"
                                 "draw_line 40,0 40,10 #10.20.30.255 1\n"
                                 "stroke_rounded_rect 0,0,60,10 4 #10.20.30.255 1\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "a joined row rounds only its ends and draws one seam per neighbour pair");
}

// Piece C21: a radial layout of 2 to 12 items places every anchor on the
// circle at equal angles, picks each item from its own direction, and its
// direction ring traces as one stroke and one sector polygon.
bool close_to(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

RadialLayout pie(std::uint32_t count) {
    RadialLayout layout{};
    layout.center = Point{50.0, 50.0};
    layout.radius = 20.0;
    layout.item_size = Size{20.0, 10.0};
    layout.count = count;
    return layout;
}

// The point `distance` from the pie's centre at `degrees`.
Point pie_point(double degrees, double distance) {
    constexpr double degrees_to_radians = 0.017453292519943295769;
    const Point point{50.0 + distance * std::cos(degrees * degrees_to_radians),
                      50.0 + distance * std::sin(degrees * degrees_to_radians)};
    return point;
}

void test_radial_layout_places_and_picks() {
    std::array<RadialSlot, maximum_radial_items> slots{};
    for (std::uint32_t count = 2U; count <= 12U; ++count) {
        const RadialLayout layout = pie(count);
        require(place_radial_items(layout, slots) == count, "a valid layout places every item");
        const double sweep = 360.0 / static_cast<double>(count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            const RadialSlot& slot = slots[index];
            const double distance = std::hypot(slot.anchor.x - 50.0, slot.anchor.y - 50.0);
            require(close_to(distance, 20.0, 1e-12), "every anchor lies on the circle");
            require(slot.direction_degrees == -90.0 + sweep * static_cast<double>(index), "equal angles from the start");
            require(slot.sweep == sweep && slot.sweep_start == slot.direction_degrees - sweep * 0.5,
                    "each sector is centred on its direction");
            require(close_to(slot.bounds.x + 10.0, slot.anchor.x, 1e-12) &&
                        close_to(slot.bounds.y + 5.0, slot.anchor.y, 1e-12),
                    "a centred box is centred on its anchor");
            const RadialPick at_anchor = pick_radial_item(layout, slot.anchor, 4.0);
            require(at_anchor.picked && at_anchor.index == index, "an item's own direction picks it");
            // Just inside each edge of the sector still picks the item.
            const Point early = pie_point(slot.sweep_start + 0.5, 30.0);
            const Point late = pie_point(slot.sweep_start + sweep - 0.5, 30.0);
            require(pick_radial_item(layout, early, 4.0).index == index, "the sector's first edge picks the item");
            require(pick_radial_item(layout, late, 4.0).index == index, "the sector's last edge picks the item");
        }
        require(!pick_radial_item(layout, Point{51.0, 51.0}, 4.0).picked, "the dead zone picks nothing");
        require(!pick_radial_item(layout, Point{50.0, 50.0}, 0.0).picked, "the centre picks nothing");
    }

    // Outward boxes grow away from the centre; an axis the direction does
    // not lean along stays centred.
    RadialLayout outward = pie(4U);
    outward.alignment = RadialItemAlignment::outward;
    require(place_radial_items(outward, slots) == 4U, "an outward layout places");
    require(close_to(slots[0].bounds.x, 40.0, 1e-12) && close_to(slots[0].bounds.y, 20.0, 1e-12),
            "the top item sits above its anchor, centred across");
    require(close_to(slots[1].bounds.x, 70.0, 1e-12) && close_to(slots[1].bounds.y, 45.0, 1e-12),
            "the right item starts at its anchor");
    require(close_to(slots[2].bounds.y, 70.0, 1e-12), "the bottom item hangs below its anchor");
    require(close_to(slots[3].bounds.x, 10.0, 1e-12), "the left item ends at its anchor");

    // Refusals write nothing.
    slots.fill(RadialSlot{});
    require(place_radial_items(pie(0U), slots) == 0U, "no items");
    require(place_radial_items(pie(17U), slots) == 0U, "too many items");
    RadialLayout flat = pie(3U);
    flat.radius = 0.0;
    require(place_radial_items(flat, slots) == 0U, "a zero radius");
    RadialLayout lost = pie(3U);
    lost.center = Point{not_a_number, 0.0};
    require(place_radial_items(lost, slots) == 0U, "a centre that is not finite");
    require(!pick_radial_item(lost, Point{0.0, 0.0}, 0.0).picked, "an invalid layout picks nothing");
    require(place_radial_items(pie(3U), std::span<RadialSlot>(slots.data(), 2U)) == 0U, "storage too short");
    require(slots[0] == RadialSlot{}, "a refusal leaves the storage alone");
}

// Keeps the polygon a ring paints.
class RingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override { ++strokes; }
    void fill_polygon(std::span<const Point> points, Color color, PolygonFillRule) override {
        polygon.assign(points.begin(), points.end());
        polygon_colour = color;
    }

    std::size_t strokes{};
    std::vector<Point> polygon;
    Color polygon_colour{};
};

void test_direction_ring_trace() {
    const RadialLayout layout = pie(4U);
    DirectionRingStyle style{};
    style.inner_radius = 10.0;
    style.outer_radius = 20.0;
    style.track = ink;
    style.pick = paper;
    const RadialPick right = pick_radial_item(layout, Point{80.0, 50.0}, 4.0);
    require(right.picked && right.index == 1U, "the right of a four-item pie picks item 1");

    TracePainter idle;
    paint_direction_ring(idle, layout, RadialPick{}, style);
    require(idle.out.str() == "stroke_rounded_rect 35,35,30,30 15 #10.20.30.255 10\n", "an idle ring is its track");
    TracePainter picked;
    paint_direction_ring(picked, layout, right, style);
    const std::string trace = picked.out.str();
    require(trace.rfind("stroke_rounded_rect 35,35,30,30 15 #10.20.30.255 10\nfill_polygon ", 0) == 0,
            "a picked ring is its track, then the sector");
    require(trace.find("#240.240.240.255 nonzero\n") != std::string::npos, "the sector in the pick colour");

    // The sector of item 1 runs from -45 to 45 degrees: 15 steps along each
    // edge, outer then inner.
    RingPainter ring;
    paint_direction_ring(ring, layout, right, style);
    require(ring.strokes == 1U && ring.polygon.size() == 32U, "one track and a 32-point sector");
    require(ring.polygon_colour == paper, "the sector is filled in the pick colour");
    for (std::size_t index = 0U; index < 16U; ++index) {
        const Point outer = ring.polygon[index];
        const Point inner = ring.polygon[16U + index];
        require(close_to(std::hypot(outer.x - 50.0, outer.y - 50.0), 20.0, 1e-9), "outer edge on the outer radius");
        require(close_to(std::hypot(inner.x - 50.0, inner.y - 50.0), 10.0, 1e-9), "inner edge on the inner radius");
    }
    require(close_to(ring.polygon.front().x, 50.0 + 20.0 * std::sqrt(0.5), 1e-9) &&
                close_to(ring.polygon.front().y, 50.0 - 20.0 * std::sqrt(0.5), 1e-9),
            "the sector starts at -45 degrees on the outer edge");

    // One item: the pick is the whole annulus; a bad style paints nothing.
    RingPainter whole;
    paint_direction_ring(whole, pie(1U), pick_radial_item(pie(1U), Point{0.0, 0.0}, 1.0), style);
    require(whole.strokes == 2U && whole.polygon.empty(), "a one-item pick strokes the whole ring");
    DirectionRingStyle inverted = style;
    inverted.inner_radius = 30.0;
    TracePainter refused;
    paint_direction_ring(refused, layout, right, inverted);
    require(refused.out.str().empty(), "an inner radius past the outer paints nothing");
}

// A closed shape of every verb: a line, a quadratic, a cubic and an arc.
PaintPath every_verb_path() {
    PaintPath path;
    path.reserve(6U, 12U);
    path.move_to(Point{0.0, 0.0});
    path.line_to(Point{10.0, 0.0});
    path.quad_to(Point{20.0, 0.0}, Point{20.0, 10.0});
    path.cubic_to(Point{20.0, 20.0}, Point{10.0, 30.0}, Point{0.0, 20.0});
    path.arc_to(Point{0.0, 10.0}, Size{10.0, 10.0}, 90.0, 180.0);
    path.close();
    return path;
}

// Exhibit gap E7: the path's validation, bounds and flattening, as values.
void test_path_values() {
    const PaintPath path = every_verb_path();
    const PaintPathView view = path.view();
    require(valid_paint_path(view), "a path of every verb is valid");
    const Rect bounds = paint_path_bounds(view);
    require(bounds == (Rect{-10.0, 0.0, 30.0, 30.0}),
            "the bounds hold the control points and the arc's whole ellipse");

    PaintPath no_start;
    no_start.line_to(Point{1.0, 1.0});
    require(!valid_paint_path(no_start.view()), "a line with no current point is refused");
    PaintPath flat_arc;
    flat_arc.arc_to(Point{0.0, 0.0}, Size{0.0, 4.0}, 0.0, 90.0);
    require(!valid_paint_path(flat_arc.view()), "an arc of zero radius is refused");
    PaintPath infinite;
    infinite.move_to(Point{0.0, 0.0});
    infinite.line_to(Point{std::numeric_limits<double>::infinity(), 0.0});
    require(!valid_paint_path(infinite.view()), "an infinite point is refused");
    const std::array<PaintPathVerb, 1> short_verbs{{PaintPathVerb::cubic_to}};
    const std::array<Point, 2> two{{Point{}, Point{}}};
    PaintPathView short_points{};
    short_points.verbs = short_verbs;
    short_points.points = two;
    require(!valid_paint_path(short_points), "too few points for the verbs are refused");
    require(paint_path_bounds(short_points) == Rect{}, "an invalid path has empty bounds");

    std::array<Point, 512> points{};
    std::array<FlattenedContour, 4> contours{};
    const FlattenedPath flat = flatten_paint_path(view, 0.25, points, contours);
    require(flat.contour_count == 1U && contours[0].closed && !flat.truncated,
            "one closed contour, flattened whole");
    require(points[0] == (Point{0.0, 0.0}) && points[flat.point_count - 1U] == (Point{0.0, 0.0}),
            "a closed contour starts and ends at its start");
    // Every flattened point of the arc's half lies on its circle, and the
    // chords' sagitta stays within the tolerance.
    bool on_curves = true;
    for (std::size_t index = 0U; index < flat.point_count; ++index) {
        const Point point = points[index];
        if (point.x < 0.0) {
            const double radius = std::sqrt(point.x * point.x + (point.y - 10.0) * (point.y - 10.0));
            if (std::abs(radius - 10.0) > 1e-9) on_curves = false;
        }
    }
    require(on_curves, "the arc's points lie on its circle");
    require(flat.point_count > 12U, "the curves are subdivided");

    std::array<Point, 8> few{};
    const FlattenedPath cut = flatten_paint_path(view, 0.25, few, contours);
    require(cut.truncated && cut.point_count <= few.size(), "full storage truncates without overrunning");
    require(flatten_paint_path(view, 0.0, points, contours).point_count == 0U, "a zero tolerance is refused");

    // A full turn is held to one turn and ends where it starts.
    PaintPath circle;
    circle.arc_to(Point{5.0, 5.0}, Size{5.0, 5.0}, 0.0, 720.0);
    const FlattenedPath round = flatten_paint_path(circle.view(), 0.25, points, contours);
    require(round.contour_count == 1U && std::abs(points[round.point_count - 1U].x - 10.0) < 1e-9 &&
                std::abs(points[round.point_count - 1U].y - 5.0) < 1e-9,
            "a sweep past a turn is held to one turn");
    require(stroke_bounds_outset(2.0, 4.0) == 4.0 && std::abs(stroke_bounds_outset(2.0, 1.0) - 1.4142135623730951) <
                                                         1e-12,
            "the stroke outset covers miters and square caps");
}

// The path calls record one command each, copy the caller's arrays, and
// replay exactly; the base fallbacks fill and stroke the flattened path.
void test_path_recording_and_fallbacks() {
    PaintPath path = every_verb_path();
    detail::RecordingPainter recorder;
    recorder.fill_path(path.view(), ink, PolygonFillRule::even_odd);
    recorder.stroke_path(path.view(), dashed_stroke());
    path.clear();
    path.move_to(Point{99.0, 99.0});
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, 64.0, 32.0});
    require((*chunk).commands().size() == 2U, "each path call records one command");
    TracePainter trace;
    require(detail::replay_display_chunk(*chunk, trace) == 2U, "both commands replay");
    const std::string shape = "M0,0L10,0Q20,0;20,10C20,20;10,30;0,20A0,10;10,10;90,180Z";
    const std::string expected = "fill_path " + shape + " #10.20.30.255 even_odd\n" + "stroke_path " + shape +
                                 " #10.20.30.255 w=2 join=1 cap=2 dashes=4,2@1\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "the path commands replay what was recorded");

    // The fallback: a 10-unit square drawn as a path fills by unit rows,
    // with a 4-unit square hole under even-odd and none under nonzero.
    PaintPath ring;
    ring.move_to(Point{0.0, 0.0});
    ring.line_to(Point{10.0, 0.0});
    ring.line_to(Point{10.0, 10.0});
    ring.line_to(Point{0.0, 10.0});
    ring.close();
    ring.move_to(Point{3.0, 3.0});
    ring.line_to(Point{7.0, 3.0});
    ring.line_to(Point{7.0, 7.0});
    ring.line_to(Point{3.0, 7.0});
    ring.close();
    MinimalPainter even_odd;
    even_odd.fill_path(ring.view(), ink, PolygonFillRule::even_odd);
    MinimalPainter nonzero;
    nonzero.fill_path(ring.view(), ink, PolygonFillRule::nonzero);
    require(std::abs(even_odd.filled_area() - 84.0) < 1e-9, "even-odd leaves the inner square open");
    require(std::abs(nonzero.filled_area() - 100.0) < 1e-9, "nonzero fills the same-wound inner square");

    MinimalPainter stroked;
    stroked.stroke_path(ring.view(), dashed_stroke());
    require(stroked.lines.size() == 8U, "each closed contour strokes its four sides, the last back to its start");
    MinimalPainter refused;
    PaintPath broken;
    broken.quad_to(Point{1.0, 1.0}, Point{2.0, 0.0});
    refused.fill_path(broken.view(), ink, PolygonFillRule::nonzero);
    refused.stroke_path(broken.view(), dashed_stroke());
    require(refused.fills.empty() && refused.lines.empty(), "an invalid path draws nothing");
}

// A painter that keeps every image region drawn, for the image pattern
// fallbacks (draw_image_region_sampled's base draws through
// draw_image_region).
class ImageRegionPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_image_region(ImageId, Rect source, Rect destination, double) override {
        sources.push_back(source);
        destinations.push_back(destination);
    }

    std::vector<Rect> sources;
    std::vector<Rect> destinations;
};

PaintPath clip_triangle() {
    PaintPath triangle;
    triangle.move_to(Point{0.0, 0.0});
    triangle.line_to(Point{20.0, 0.0});
    triangle.line_to(Point{0.0, 10.0});
    triangle.close();
    return triangle;
}

void test_path_clip_recording_and_fallback() {
    const PaintPath triangle = clip_triangle();
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, ink}, GradientStop{1.0, paper}}};
    detail::RecordingPainter recorder;
    recorder.save();
    recorder.clip_path(triangle.view(), PolygonFillRule::even_odd);
    recorder.fill_radial_gradient_spread(Rect{0.0, 0.0, 20.0, 10.0}, Point{5.0, 5.0}, Size{4.0, 2.0}, stops,
                                         GradientSpreadMode::reflect);
    recorder.restore();
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, 64.0, 32.0});
    require((*chunk).commands().size() == 4U, "the clip and the gradient record one command each");
    require((*chunk).commands()[1].rect == Rect{0.0, 0.0, 20.0, 10.0},
            "a path clip records its bounds for the readers that follow clips as rectangles");
    TracePainter trace;
    require(detail::replay_display_chunk(*chunk, trace) == 4U, "every command replays");
    const std::string expected =
        "save\n"
        "clip_path M0,0L20,0L0,10Z even_odd\n"
        "fill_radial_gradient_spread 0,0,20,10 5,5 4,2 reflect 0:#10.20.30.255 1:#240.240.240.255\n"
        "restore\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "the path clip and the spread replay what was recorded");

    bool refused = false;
    try {
        recorder.fill_radial_gradient_spread(Rect{0.0, 0.0, 4.0, 4.0}, Point{2.0, 2.0}, Size{2.0, 2.0}, stops,
                                             static_cast<GradientSpreadMode>(9));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a spread outside the enumeration is refused when recorded");

    // The fallback: a minimal painter clips to the path's bounding box; a
    // refused path or rule clips everything away.
    MinimalPainter minimal;
    minimal.clip_path(triangle.view(), PolygonFillRule::nonzero);
    require(minimal.clips.size() == 1U && minimal.clips.back() == Rect{0.0, 0.0, 20.0, 10.0},
            "a minimal painter clips to the path's bounds");
    PaintPath broken;
    broken.line_to(Point{1.0, 1.0});
    minimal.clip_path(broken.view(), PolygonFillRule::nonzero);
    minimal.clip_path(triangle.view(), static_cast<PolygonFillRule>(7));
    require(minimal.clips.size() == 3U && minimal.clips[1] == Rect{} && minimal.clips[2] == Rect{},
            "a refused path or rule clips to nothing");
    // The base spread pads (through fill_radial_gradient's rings, drawn as
    // rounded rectangles and so as fill_rect on this painter); an unknown
    // spread draws nothing.
    MinimalPainter padded;
    padded.fill_radial_gradient_spread(Rect{0.0, 0.0, 20.0, 10.0}, Point{5.0, 5.0}, Size{4.0, 2.0}, stops,
                                       GradientSpreadMode::repeat);
    MinimalPainter unknown;
    unknown.fill_radial_gradient_spread(Rect{0.0, 0.0, 20.0, 10.0}, Point{5.0, 5.0}, Size{4.0, 2.0}, stops,
                                        static_cast<GradientSpreadMode>(9));
    require(!padded.fills.empty() && unknown.fills.empty(),
            "the base spread draws a padded gradient and refuses an unknown spread");
}

void test_image_pattern_clamp_fallback() {
    // A 4 x 2 image in 8 x 4 tiles over 30 x 12: the tile, then its last
    // column stretched right, its last row down, and its corner pixel over
    // the rest.
    ImageRegionPainter painter;
    painter.fill_image_pattern(ImageId{7U}, Size{4.0, 2.0}, Rect{10.0, 20.0, 30.0, 12.0}, Size{8.0, 4.0},
                               ImagePatternWrap::clamp, 1.0);
    const std::array<Rect, 4> sources{{Rect{0.0, 0.0, 4.0, 2.0}, Rect{3.0, 0.0, 1.0, 2.0}, Rect{0.0, 1.0, 4.0, 1.0},
                                        Rect{3.0, 1.0, 1.0, 1.0}}};
    const std::array<Rect, 4> destinations{{Rect{10.0, 20.0, 8.0, 4.0}, Rect{18.0, 20.0, 22.0, 4.0},
                                             Rect{10.0, 24.0, 8.0, 8.0}, Rect{18.0, 24.0, 22.0, 8.0}}};
    require(painter.sources.size() == 4U && painter.destinations.size() == 4U,
            "a clamped pattern draws its tile and three extensions");
    for (std::size_t index = 0U; index < sources.size(); ++index) {
        require(painter.sources[index] == sources[index] && painter.destinations[index] == destinations[index],
                "each clamped region samples the tile's edge where the pattern extends it");
    }
    // A destination smaller than one tile draws the tile's visible part
    // only.
    ImageRegionPainter small;
    small.fill_image_pattern(ImageId{7U}, Size{4.0, 2.0}, Rect{0.0, 0.0, 4.0, 4.0}, Size{8.0, 4.0},
                             ImagePatternWrap::clamp, 1.0);
    require(small.sources.size() == 1U && small.sources[0] == Rect{0.0, 0.0, 2.0, 2.0} &&
                small.destinations[0] == Rect{0.0, 0.0, 4.0, 4.0},
            "a clamped pattern inside one tile draws that tile's part");
    // Mirror draws as tile on a minimal painter; an unknown wrap draws
    // nothing.
    ImageRegionPainter mirrored;
    mirrored.fill_image_pattern(ImageId{7U}, Size{4.0, 2.0}, Rect{0.0, 0.0, 16.0, 4.0}, Size{8.0, 4.0},
                                ImagePatternWrap::mirror, 1.0);
    ImageRegionPainter unknown;
    unknown.fill_image_pattern(ImageId{7U}, Size{4.0, 2.0}, Rect{0.0, 0.0, 16.0, 4.0}, Size{8.0, 4.0},
                               static_cast<ImagePatternWrap>(9), 1.0);
    require(mirrored.sources.size() == 2U && mirrored.sources[1] == Rect{0.0, 0.0, 4.0, 2.0} &&
                unknown.sources.empty(),
            "a minimal painter tiles a mirrored pattern unflipped and refuses an unknown wrap");
}

void test_hatch_squares() {
    // Squares marked per cell in each style.
    const std::array<HatchStyle, 6> styles{{HatchStyle::horizontal, HatchStyle::vertical,
                                            HatchStyle::forward_diagonal, HatchStyle::backward_diagonal,
                                            HatchStyle::cross, HatchStyle::diagonal_cross}};
    const std::array<std::size_t, 6> counts{{8U, 8U, 8U, 8U, 15U, 16U}};
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        std::size_t marked = 0U;
        for (std::uint32_t row = 0U; row < hatch_cell_squares; ++row) {
            for (std::uint32_t column = 0U; column < hatch_cell_squares; ++column) {
                if (hatch_square_marked(styles[index], column, row)) ++marked;
            }
        }
        require(marked == counts[index], "each hatch style marks its squares per cell");
    }
    require(hatch_square_marked(HatchStyle::horizontal, 5U, 3U) && !hatch_square_marked(HatchStyle::horizontal, 3U, 5U) &&
                hatch_square_marked(HatchStyle::forward_diagonal, 0U, 7U) &&
                hatch_square_marked(HatchStyle::backward_diagonal, 6U, 6U) &&
                !hatch_square_marked(HatchStyle::horizontal, 8U, 3U) &&
                !hatch_square_marked(static_cast<HatchStyle>(9), 3U, 3U),
            "hatch squares follow the documented lines");
    HatchPattern pattern{};
    require(valid_hatch_pattern(pattern), "the default hatch is valid");
    pattern.cell = 0.5;
    require(!valid_hatch_pattern(pattern), "a cell under one unit is refused");
}

void test_hatch_and_focal_recording_and_fallback() {
    HatchPattern hatch{};
    hatch.style = HatchStyle::cross;
    hatch.fore = ink;
    hatch.back = paper;
    hatch.anchor = Point{2.0, 3.0};
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, ink}, GradientStop{1.0, paper}}};
    detail::RecordingPainter recorder;
    recorder.fill_hatch(Rect{0.0, 0.0, 16.0, 8.0}, hatch);
    recorder.fill_focal_radial_gradient(Rect{0.0, 0.0, 20.0, 10.0}, Point{10.0, 5.0}, Size{8.0, 4.0},
                                        Point{8.0, 5.0}, stops, GradientSpreadMode::repeat);
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, 64.0, 32.0});
    TracePainter trace;
    require(detail::replay_display_chunk(*chunk, trace) == 2U, "both commands replay");
    const std::string expected =
        "fill_hatch 0,0,16,8 style=4 cell=8 #10.20.30.255 #240.240.240.255 anchor=2,3\n"
        "fill_focal_radial_gradient 0,0,20,10 10,5 8,4 focus=8,5 spread=1 0:#10.20.30.255 1:#240.240.240.255\n";
    const std::string observed = trace.out.str();
    if (observed != expected) std::cerr << "observed trace:\n" << observed;
    require(observed == expected, "the hatch and the focal gradient replay what was recorded");

    // The fallback hatch: the back colour, then row 3's eight unit squares.
    MinimalPainter minimal;
    HatchPattern rows{};
    rows.fore = ink;
    rows.back = paper;
    minimal.fill_hatch(Rect{0.0, 0.0, 8.0, 8.0}, rows);
    require(minimal.fills.size() == 9U && minimal.fills[0] == Rect{0.0, 0.0, 8.0, 8.0} &&
                minimal.fill_colours[0] == paper && minimal.fills[1] == Rect{0.0, 3.0, 1.0, 1.0} &&
                minimal.fills[8] == Rect{7.0, 3.0, 1.0, 1.0} && minimal.fill_colours[8] == ink,
            "the fallback hatch fills the back colour and its marked squares");
    MinimalPainter vast;
    vast.fill_hatch(Rect{0.0, 0.0, 4096.0, 4096.0}, rows);
    require(vast.fills.size() == 1U, "a hatch past the square bound fills one averaged colour");

    MinimalPainter outside;
    outside.fill_focal_radial_gradient(Rect{0.0, 0.0, 20.0, 10.0}, Point{10.0, 5.0}, Size{8.0, 4.0},
                                       Point{18.0, 5.0}, stops, GradientSpreadMode::pad);
    MinimalPainter inside;
    inside.fill_focal_radial_gradient(Rect{0.0, 0.0, 20.0, 10.0}, Point{10.0, 5.0}, Size{8.0, 4.0},
                                      Point{8.0, 5.0}, stops, GradientSpreadMode::pad);
    require(outside.fills.empty() && !inside.fills.empty() && inside.fill_colours[0] == paper,
            "a focus on or outside the ellipse draws nothing; inside, the fallback pads and draws rings");
    require(focus_inside_ellipse(Point{0.0, 0.0}, Size{2.0, 1.0}, Point{1.9, 0.0}) &&
                !focus_inside_ellipse(Point{0.0, 0.0}, Size{2.0, 1.0}, Point{0.0, 1.0}) &&
                !focus_inside_ellipse(Point{0.0, 0.0}, Size{0.0, 1.0}, Point{0.0, 0.0}),
            "the focus must lie strictly inside the ellipse");
}

void test_operation_names() {
    require(std::string_view(visual_paint_operation_name(VisualPaintOperation::stroke_polyline)) == "stroke_polyline" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::fill_grid_pattern)) ==
                    "fill_grid_pattern" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::fill_path)) == "fill_path" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::stroke_path)) == "stroke_path" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::clip_path)) == "clip_path" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::fill_radial_gradient_spread)) ==
                    "fill_radial_gradient_spread" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::fill_hatch)) == "fill_hatch" &&
                std::string_view(visual_paint_operation_name(VisualPaintOperation::fill_focal_radial_gradient)) ==
                    "fill_focal_radial_gradient",
            "the batched operations must have inspection names");
}

} // namespace

int main() {
    try {
        test_style_validation();
        test_marker_geometry();
        test_grid_levels();
        test_one_command_per_batch_and_exact_replay();
        test_conic_stops_are_checked_when_recorded();
        test_fallback_polyline_breaks_at_nan();
        test_fallback_fills();
        test_fallback_markers();
        test_fallback_patterns();
        test_proportion_and_masked_fill();
        test_contrast_rule();
        test_text_over_fill_uses_complementary_clips();
        test_joined_segments();
        test_path_values();
        test_path_recording_and_fallbacks();
        test_path_clip_recording_and_fallback();
        test_image_pattern_clamp_fallback();
        test_hatch_squares();
        test_hatch_and_focal_recording_and_fallback();
        test_operation_names();
        test_radial_layout_places_and_picks();
        test_direction_ring_trace();
        std::cout << "gui_forms_paint_marks_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_paint_marks_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
