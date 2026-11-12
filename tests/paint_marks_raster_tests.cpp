// The batched marks and patterns drawn by the Skia raster (painter.hpp,
// paint_marks.hpp): each call's pixels where its geometry says they land,
// NaN gaps and dashes, the fill rules, markers, the conic gradient's
// direction, the per-corner rounding, and the replay of a recording drawing
// the same bytes as drawing directly.
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "skia_raster.hpp"
#include "../src/core/display/recording_painter/recording_painter.hpp"
#include "../src/core/display/replay/replay_display_chunk.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;
using gui_forms::render::SkiaRaster;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr Size scene_size{96.0, 64.0};
constexpr Color white = Color::rgba(255, 255, 255);
constexpr Color black = Color::rgba(0, 0, 0);
constexpr Color red = Color::rgba(255, 0, 0);
constexpr Color blue = Color::rgba(0, 0, 255);
constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// One device pixel's straight channels (the raster is premultiplied BGRA8;
// these scenes draw opaque colours on an opaque ground, so straight and
// premultiplied agree).
struct Pixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};
};

Pixel pixel_at(const SkiaRaster& raster, std::size_t x, std::size_t y) {
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(raster.pixels());
    const std::size_t offset = y * raster.row_bytes() + x * 4U;
    Pixel result{};
    result.blue = bytes[offset];
    result.green = bytes[offset + 1U];
    result.red = bytes[offset + 2U];
    result.alpha = bytes[offset + 3U];
    return result;
}

bool is_white(Pixel pixel) noexcept {
    return pixel.red == 255U && pixel.green == 255U && pixel.blue == 255U;
}

bool is_dark(Pixel pixel) noexcept {
    return pixel.red < 64U && pixel.green < 64U && pixel.blue < 64U;
}

// Paints `scene` over white at scale 1, whole frame.
using Scene = void (*)(Painter& painter);

void paint(SkiaRaster& raster, Scene scene) {
    require(raster.resize(scene_size, 1.0) || raster.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    scene(raster);
    raster.end_frame();
}

StrokeStyle black_stroke(double width) {
    StrokeStyle style{};
    style.color = black;
    style.width = width;
    return style;
}

void polyline_scene(Painter& painter) {
    const std::array<Point, 5> points{
        {Point{2.0, 10.5}, Point{10.0, 10.5}, Point{not_a_number, 0.0}, Point{20.0, 10.5}, Point{30.0, 10.5}}};
    painter.stroke_polyline(points, black_stroke(1.0));
    StrokeStyle dashed = black_stroke(1.0);
    dashed.dashes[0] = 4.0;
    dashed.dashes[1] = 4.0;
    dashed.dash_count = 2U;
    const std::array<PaintSegment, 1> segment{{PaintSegment{Point{0.0, 20.5}, Point{40.0, 20.5}}}};
    painter.stroke_segments(segment, dashed);
}

void test_polyline_gaps_and_dashes() {
    SkiaRaster raster;
    paint(raster, &polyline_scene);
    require(is_dark(pixel_at(raster, 5U, 10U)) && is_dark(pixel_at(raster, 25U, 10U)),
            "both runs of the polyline must be stroked");
    require(is_white(pixel_at(raster, 15U, 10U)), "the NaN point must leave a gap");
    require(is_white(pixel_at(raster, 5U, 5U)), "the stroke must stay on its line");
    require(is_dark(pixel_at(raster, 2U, 20U)) && is_white(pixel_at(raster, 6U, 20U)) &&
                is_dark(pixel_at(raster, 10U, 20U)),
            "a 4-on 4-off dash must alternate along the segment");
}

void fill_scene(Painter& painter) {
    const std::array<Point, 8> twice{{Point{4.0, 4.0}, Point{20.0, 4.0}, Point{20.0, 20.0}, Point{4.0, 20.0},
                                      Point{4.0, 4.0}, Point{20.0, 4.0}, Point{20.0, 20.0}, Point{4.0, 20.0}}};
    painter.fill_polygon(twice, black, PolygonFillRule::even_odd);
    const std::array<Point, 8> again{{Point{30.0, 4.0}, Point{46.0, 4.0}, Point{46.0, 20.0}, Point{30.0, 20.0},
                                      Point{30.0, 4.0}, Point{46.0, 4.0}, Point{46.0, 20.0}, Point{30.0, 20.0}}};
    painter.fill_polygon(again, black, PolygonFillRule::nonzero);
    const std::array<Point, 3> upper{{Point{0.0, 30.0}, Point{20.0, 30.0}, Point{40.0, 30.0}}};
    const std::array<Point, 3> lower{{Point{0.0, 40.0}, Point{20.0, 40.0}, Point{40.0, 40.0}}};
    painter.fill_band(upper, lower, blue);
}

void test_fill_rules_and_bands() {
    SkiaRaster raster;
    paint(raster, &fill_scene);
    require(is_white(pixel_at(raster, 12U, 12U)), "even-odd must leave a doubly wound square empty");
    require(is_dark(pixel_at(raster, 38U, 12U)), "nonzero must fill a doubly wound square");
    const Pixel band = pixel_at(raster, 20U, 35U);
    require(band.blue == 255U && band.red == 0U, "the band must fill between its polylines");
    require(is_white(pixel_at(raster, 20U, 45U)), "the band must stop at its lower polyline");
}

void marker_scene(Painter& painter) {
    MarkerStyle square{};
    square.shape = MarkerShape::square;
    square.size = 8.0;
    square.fill = red;
    square.edge = black;
    const std::array<Point, 2> squares{{Point{60.0, 12.0}, Point{not_a_number, 0.0}}};
    painter.draw_markers(squares, square);
    MarkerStyle plus{};
    plus.shape = MarkerShape::plus;
    plus.size = 9.0;
    plus.fill = red;
    plus.edge = black;
    plus.edge_width = 1.0;
    const std::array<Point, 1> pluses{{Point{80.5, 12.5}}};
    painter.draw_markers(pluses, plus);
}

void test_markers() {
    SkiaRaster raster;
    paint(raster, &marker_scene);
    const Pixel inside = pixel_at(raster, 60U, 12U);
    require(inside.red == 255U && inside.green == 0U, "a square marker must fill its inside");
    require(is_white(pixel_at(raster, 70U, 12U)), "a square marker must stay within its size");
    require(is_dark(pixel_at(raster, 80U, 12U)) && is_dark(pixel_at(raster, 84U, 12U)) &&
                is_white(pixel_at(raster, 84U, 16U)),
            "a plus marker strokes its two lines in the edge colour and never fills");
}

void conic_scene(Painter& painter) {
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, red}, GradientStop{1.0, blue}}};
    painter.fill_conic_gradient(Rect{0.0, 0.0, 40.0, 40.0}, Point{20.0, 20.0}, 0.0, stops);
}

void test_conic_direction() {
    SkiaRaster raster;
    paint(raster, &conic_scene);
    // Just clockwise of +x (below the centre, y down): near offset 0.
    const Pixel start = pixel_at(raster, 35U, 21U);
    // Just anticlockwise of +x (above): near offset 1.
    const Pixel end = pixel_at(raster, 35U, 18U);
    // Straight down, a quarter turn: a quarter of the way to blue.
    const Pixel quarter = pixel_at(raster, 20U, 35U);
    require(start.red > 200U && start.blue < 60U, "the sweep must start red just clockwise of +x");
    require(end.blue > 200U && end.red < 60U, "the sweep must end blue just anticlockwise of +x");
    require(quarter.red > quarter.blue, "a quarter turn clockwise must still be nearer the first stop");
    require(is_white(pixel_at(raster, 50U, 20U)), "the conic must stay within its rectangle");
}

void corner_scene(Painter& painter) {
    const PaintCornerRadii radii{10.0, 0.0, 0.0, 10.0};
    painter.fill_rounded_rect_corners(Rect{10.0, 10.0, 40.0, 30.0}, radii, black);
}

void test_per_corner_rounding() {
    SkiaRaster raster;
    paint(raster, &corner_scene);
    require(is_white(pixel_at(raster, 10U, 10U)), "a rounded corner must leave its corner pixel unfilled");
    require(is_dark(pixel_at(raster, 49U, 10U)), "a square corner must fill its corner pixel");
    require(is_dark(pixel_at(raster, 49U, 39U)) && is_white(pixel_at(raster, 10U, 39U)),
            "each corner must take its own radius");
}

void pattern_scene(Painter& painter) {
    CheckerPattern checker{};
    checker.cell = 4.0;
    checker.first = white;
    checker.second = black;
    painter.fill_checker(Rect{0.0, 0.0, 16.0, 16.0}, checker);
    GridPattern grid{};
    grid.origin = Point{40.5, 0.5};
    grid.spacing = 10.0;
    grid.major_every = 2U;
    grid.minor = blue;
    grid.major = red;
    painter.fill_grid_pattern(Rect{40.0, 0.0, 40.0, 40.0}, grid);
}

void test_patterns() {
    SkiaRaster raster;
    paint(raster, &pattern_scene);
    require(is_white(pixel_at(raster, 1U, 1U)) && is_dark(pixel_at(raster, 5U, 1U)) &&
                is_dark(pixel_at(raster, 1U, 5U)) && is_white(pixel_at(raster, 5U, 5U)),
            "a checker must alternate cells from its anchor");
    const Pixel major = pixel_at(raster, 40U, 20U);
    const Pixel minor = pixel_at(raster, 50U, 25U);
    require(major.red == 255U && major.blue == 0U, "the line through the origin must be major");
    require(minor.blue == 255U && minor.red == 0U, "the next line must be minor");
    require(is_white(pixel_at(raster, 45U, 25U)), "between lines the ground must show");
}

// Every new call in one scene, so a recording's replay can be compared with
// drawing directly.
void all_marks_scene(Painter& painter) {
    polyline_scene(painter);
    fill_scene(painter);
    marker_scene(painter);
    conic_scene(painter);
    corner_scene(painter);
    pattern_scene(painter);
}

void replayed_scene(Painter& painter) {
    detail::RecordingPainter recorder;
    all_marks_scene(recorder);
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, scene_size.width, scene_size.height});
    static_cast<void>(detail::replay_display_chunk(*chunk, painter));
}

void test_replay_draws_the_same_bytes() {
    SkiaRaster direct;
    paint(direct, &all_marks_scene);
    std::vector<std::uint8_t> expected(direct.byte_size());
    std::memcpy(expected.data(), direct.pixels(), expected.size());
    SkiaRaster replayed;
    paint(replayed, &replayed_scene);
    require(replayed.byte_size() == expected.size() &&
                std::memcmp(replayed.pixels(), expected.data(), expected.size()) == 0,
            "replaying a recording of the batched marks must draw the bytes drawing them directly draws");
}

} // namespace

int main() {
    try {
        test_polyline_gaps_and_dashes();
        test_fill_rules_and_bands();
        test_markers();
        test_conic_direction();
        test_per_corner_rounding();
        test_patterns();
        test_replay_draws_the_same_bytes();
        std::cout << "gui_forms_paint_marks_raster_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_paint_marks_raster_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
