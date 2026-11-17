// The batched marks and patterns drawn by the Skia raster (painter.hpp,
// paint_marks.hpp): each call's pixels where its geometry says they land,
// NaN gaps and dashes, the fill rules, markers, the conic gradient's
// direction, the per-corner rounding, the curved paths (exhibit gap E7:
// fill rules, strokes, and a frame repainted tile by tile drawing the bytes
// of one whole paint), and the replay of a recording drawing the same bytes
// as drawing directly; then the path clip (exact pixels on either side of
// its edges, the fill rules, a refused clip, a part raster composited
// through its coverage mask, byte for byte as under the same rectangle),
// the radial gradient's spreads and the image pattern's wraps (exact
// pixels where a tile's texels land on device pixel centres).
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
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
#include <span>
#include <stdexcept>
#include <string>
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

// A ring of two clockwise circles centred on (cx, cy), outer radius 14 and
// inner 6, each its own closed contour.
PaintPath ring_path(double cx, double cy) {
    PaintPath path;
    path.arc_to(Point{cx, cy}, Size{14.0, 14.0}, 0.0, 360.0);
    path.close();
    path.move_to(Point{cx + 6.0, cy});
    path.arc_to(Point{cx, cy}, Size{6.0, 6.0}, 0.0, 360.0);
    path.close();
    return path;
}

// A cubic from (4, 58) to (92, 58) bowing up to y = 50.5 at its midpoint
// (45.75, 50.5), with a quadratic hook after it.
PaintPath curve_path() {
    PaintPath path;
    path.move_to(Point{4.0, 58.0});
    path.cubic_to(Point{30.0, 48.0}, Point{60.0, 48.0}, Point{92.0, 58.0});
    path.quad_to(Point{94.0, 62.0}, Point{88.0, 62.0});
    return path;
}

void path_scene(Painter& painter) {
    const PaintPath even_odd = ring_path(24.0, 28.0);
    painter.fill_path(even_odd.view(), black, PolygonFillRule::even_odd);
    const PaintPath nonzero = ring_path(64.0, 28.0);
    painter.fill_path(nonzero.view(), blue, PolygonFillRule::nonzero);
    StrokeStyle stroke = black_stroke(3.0);
    stroke.cap = StrokeCap::round;
    stroke.join = StrokeJoin::round;
    const PaintPath curve = curve_path();
    painter.stroke_path(curve.view(), stroke);
}

void test_path_fill_and_stroke() {
    SkiaRaster raster;
    paint(raster, &path_scene);
    require(is_white(pixel_at(raster, 24U, 28U)), "even-odd must leave the ring's hole open");
    require(is_dark(pixel_at(raster, 24U, 18U)) && is_dark(pixel_at(raster, 14U, 28U)),
            "even-odd must fill between the circles");
    require(is_white(pixel_at(raster, 24U, 10U)), "the fill must stop at the outer circle");
    const Pixel centre = pixel_at(raster, 64U, 28U);
    require(centre.blue == 255U && centre.red == 0U, "nonzero must fill the same-wound hole");
    require(is_dark(pixel_at(raster, 45U, 50U)), "the stroke must lie on the cubic's midpoint");
    require(is_white(pixel_at(raster, 45U, 56U)) && is_white(pixel_at(raster, 45U, 45U)),
            "the stroke must stay within its width of the curve");
}

// The scene painted whole at `scale`, then again on a second raster in
// 16-unit tiles, each its own damaged frame clipped to the tile: the tiled
// frame must hold the whole frame's bytes (a path drawn under any damage
// draws the same pixels inside it).
void test_path_damage_invariance(double scale) {
    SkiaRaster whole;
    require(whole.resize(scene_size, scale) || whole.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion everything;
    everything.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    whole.begin_frame(everything);
    whole.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    path_scene(whole);
    whole.end_frame();

    SkiaRaster tiled;
    require(tiled.resize(scene_size, scale) || tiled.pixel_width() != 0U, "the raster must have a surface");
    tiled.begin_frame(everything);
    tiled.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, red);
    tiled.end_frame();
    for (double top = 0.0; top < scene_size.height; top = top + 16.0) {
        for (double left = 0.0; left < scene_size.width; left = left + 16.0) {
            DamageRegion tile;
            tile.add(Rect{left, top, 16.0, 16.0});
            tiled.begin_frame(tile);
            tiled.save();
            tiled.clip_damage(tile);
            tiled.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
            path_scene(tiled);
            tiled.restore();
            tiled.end_frame();
        }
    }
    require(tiled.byte_size() == whole.byte_size() &&
                std::memcmp(tiled.pixels(), whole.pixels(), whole.byte_size()) == 0,
            "a path repainted tile by tile must draw the bytes of one whole paint");
}

bool is_exactly(Pixel pixel, Color color) noexcept {
    return pixel.red == color.red && pixel.green == color.green && pixel.blue == color.blue && pixel.alpha == 255U;
}

bool is_near(Pixel left, Pixel right) noexcept {
    const bool near = std::abs(static_cast<int>(left.red) - static_cast<int>(right.red)) <= 2 &&
                      std::abs(static_cast<int>(left.green) - static_cast<int>(right.green)) <= 2 &&
                      std::abs(static_cast<int>(left.blue) - static_cast<int>(right.blue)) <= 2;
    return near;
}

// The clip scene's shapes: a right triangle whose legs lie on whole units
// (from (40, 4) along x to 88 and along y to 52: inside where x + y < 92),
// a 32-unit square ring with a 16-unit hole, both wound the same way, and
// a smaller ring like it.
PaintPath clip_triangle() {
    PaintPath triangle;
    triangle.move_to(Point{40.0, 4.0});
    triangle.line_to(Point{88.0, 4.0});
    triangle.line_to(Point{40.0, 52.0});
    triangle.close();
    return triangle;
}

PaintPath square_ring(double left, double top, double outer, double inset) {
    PaintPath ring;
    ring.move_to(Point{left, top});
    ring.line_to(Point{left + outer, top});
    ring.line_to(Point{left + outer, top + outer});
    ring.line_to(Point{left, top + outer});
    ring.close();
    ring.move_to(Point{left + inset, top + inset});
    ring.line_to(Point{left + outer - inset, top + inset});
    ring.line_to(Point{left + outer - inset, top + outer - inset});
    ring.line_to(Point{left + inset, top + outer - inset});
    ring.close();
    return ring;
}

void clip_scene(Painter& painter) {
    const Rect whole{0.0, 0.0, scene_size.width, scene_size.height};
    const PaintPath triangle = clip_triangle();
    painter.save();
    painter.clip_path(triangle.view(), PolygonFillRule::nonzero);
    painter.fill_rect(whole, red);
    painter.restore();
    const PaintPath ring = square_ring(4.0, 4.0, 32.0, 8.0);
    painter.save();
    painter.clip_path(ring.view(), PolygonFillRule::even_odd);
    painter.fill_rect(whole, blue);
    painter.restore();
    const PaintPath small_ring = square_ring(4.0, 40.0, 16.0, 4.0);
    painter.save();
    painter.clip_path(small_ring.view(), PolygonFillRule::nonzero);
    painter.fill_rect(whole, blue);
    painter.restore();
    // A refused clip hides everything until its restore.
    PaintPath broken;
    broken.line_to(Point{1.0, 1.0});
    painter.save();
    painter.clip_path(broken.view(), PolygonFillRule::nonzero);
    painter.fill_rect(whole, black);
    painter.restore();
}

void test_path_clip() {
    SkiaRaster raster;
    paint(raster, &clip_scene);
    require(is_exactly(pixel_at(raster, 44U, 8U), red) && is_exactly(pixel_at(raster, 40U, 4U), red) &&
                is_exactly(pixel_at(raster, 40U, 48U), red),
            "a fill under a path clip must cover the path's interior exactly, to its whole-unit edges");
    require(is_exactly(pixel_at(raster, 39U, 10U), white) && is_exactly(pixel_at(raster, 60U, 3U), white),
            "nothing may draw beyond a path clip's whole-unit edges");
    require(is_exactly(pixel_at(raster, 80U, 40U), white) && is_exactly(pixel_at(raster, 86U, 50U), white),
            "a path clip must hide its bounding box's corner outside the path");
    require(is_exactly(pixel_at(raster, 6U, 6U), blue) && is_exactly(pixel_at(raster, 20U, 20U), white),
            "an even-odd path clip must leave the ring's hole open");
    require(is_exactly(pixel_at(raster, 12U, 48U), blue) && is_exactly(pixel_at(raster, 5U, 41U), blue),
            "a nonzero path clip must fill the same-wound hole");
    require(is_exactly(pixel_at(raster, 90U, 60U), white) && is_exactly(pixel_at(raster, 2U, 2U), white),
            "a refused path clip must draw nothing until its restore");
}

void test_part_raster_under_path_clip() {
    // A 16-pixel opaque red raster at (40, 4) under a triangle whose
    // hypotenuse runs from (56, 4) to (40, 20): the raster's own composite
    // follows the path through its coverage mask.
    const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{16U, 16U}, DeviceScale{1.0});
    require(static_cast<bool>(raster), "the part raster must be made");
    const PartRasterView view = (*raster).view();
    for (std::size_t at = 0U; at + 3U < view.pixels.size(); at = at + 4U) {
        view.pixels[at] = 0U;
        view.pixels[at + 1U] = 0U;
        view.pixels[at + 2U] = 255U;
        view.pixels[at + 3U] = 255U;
    }
    (*raster).record_opacity();
    const std::shared_ptr<const PartRaster> shared = raster;
    PaintPath triangle;
    triangle.move_to(Point{40.0, 4.0});
    triangle.line_to(Point{56.0, 4.0});
    triangle.line_to(Point{40.0, 20.0});
    triangle.close();
    SkiaRaster target;
    require(target.resize(scene_size, 1.0) || target.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    target.begin_frame(damage);
    target.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    target.save();
    target.clip_path(triangle.view(), PolygonFillRule::nonzero);
    target.draw_part_raster(shared, Point{40.0, 4.0}, 1.0);
    target.restore();
    target.end_frame();
    require(is_exactly(pixel_at(target, 42U, 6U), red) && is_exactly(pixel_at(target, 40U, 4U), red),
            "a part raster under a path clip must draw inside the path");
    require(is_exactly(pixel_at(target, 54U, 18U), white) && is_exactly(pixel_at(target, 50U, 14U), white),
            "a part raster under a path clip must not draw in its box outside the path");
}

// An opaque red raster composited under `clip`, drawn into `target` over
// white: the path clip's coverage mask and a clip rectangle reach the same
// shared composite.
std::shared_ptr<const PartRaster> red_raster(const std::uint32_t side) {
    const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{side, side}, DeviceScale{1.0});
    require(static_cast<bool>(raster), "the part raster must be made");
    const PartRasterView view = (*raster).view();
    for (std::size_t at = 0U; at + 3U < view.pixels.size(); at = at + 4U) {
        view.pixels[at] = 0U;
        view.pixels[at + 1U] = 0U;
        view.pixels[at + 2U] = 255U;
        view.pixels[at + 3U] = 255U;
    }
    (*raster).record_opacity();
    return raster;
}

enum class ClipKind : std::uint8_t { rectangle, path };

void composite_under(SkiaRaster& target, const ClipKind kind, const Rect clip) {
    require(target.resize(scene_size, 1.0) || target.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    target.begin_frame(damage);
    target.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    target.save();
    if (kind == ClipKind::path) {
        PaintPath box;
        box.move_to(Point{clip.x, clip.y});
        box.line_to(Point{clip.x + clip.width, clip.y});
        box.line_to(Point{clip.x + clip.width, clip.y + clip.height});
        box.line_to(Point{clip.x, clip.y + clip.height});
        box.close();
        target.clip_path(box.view(), PolygonFillRule::nonzero);
    } else {
        target.clip_rect(clip);
    }
    target.draw_part_raster(red_raster(32U), Point{8.0, 6.0}, 1.0);
    target.restore();
    target.end_frame();
}

void test_path_clip_mask_matches_rectangle() {
    // Edges across at quarters and down at halves of a pixel: the mask's
    // horizontal coverage is exact, and a half lies on its 1/16 rows, so
    // the mask gives each pixel the rectangle's exact area coverage and the
    // composite the same bytes.
    const Rect clip{10.25, 8.5, 20.5, 12.0};
    SkiaRaster by_rectangle;
    composite_under(by_rectangle, ClipKind::rectangle, clip);
    SkiaRaster by_path;
    composite_under(by_path, ClipKind::path, clip);
    const std::size_t width = static_cast<std::size_t>(scene_size.width);
    const std::size_t height = static_cast<std::size_t>(scene_size.height);
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const Pixel expected = pixel_at(by_rectangle, x, y);
            const Pixel drawn = pixel_at(by_path, x, y);
            require(expected.red == drawn.red && expected.green == drawn.green && expected.blue == drawn.blue &&
                        expected.alpha == drawn.alpha,
                    "a part raster under a rectangular path clip must composite the clip rectangle's bytes");
        }
    }
    const Pixel corner = pixel_at(by_path, 10U, 8U);
    require(corner.red == 255U && corner.green > 0U && corner.green < 255U,
            "the clip's corner pixel must be blended by its partial coverage");
    require(is_exactly(pixel_at(by_path, 20U, 14U), red) && is_white(pixel_at(by_path, 9U, 14U)) &&
                is_white(pixel_at(by_path, 20U, 21U)),
            "the path clip must keep the raster inside it and the ground outside");
}

// Red at the centre to blue at 8 units, centred on a device pixel's
// centre, so the pixels 4, 12 and the others along the row lie at whole
// fractions of the radius.
void radial_spread(SkiaRaster& raster, GradientSpreadMode spread) {
    require(raster.resize(scene_size, 1.0) || raster.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, red}, GradientStop{1.0, blue}}};
    raster.fill_radial_gradient_spread(Rect{0.0, 0.0, scene_size.width, scene_size.height}, Point{48.5, 32.5},
                                       Size{8.0, 8.0}, stops, spread);
    raster.end_frame();
}

void test_radial_spreads() {
    SkiaRaster padded;
    radial_spread(padded, GradientSpreadMode::pad);
    require(is_exactly(pixel_at(padded, 60U, 32U), blue), "a padded radial gradient keeps its last colour outside");
    SkiaRaster repeated;
    radial_spread(repeated, GradientSpreadMode::repeat);
    require(is_near(pixel_at(repeated, 60U, 32U), pixel_at(repeated, 52U, 32U)) &&
                is_near(pixel_at(repeated, 62U, 32U), pixel_at(repeated, 54U, 32U)),
            "a repeating radial gradient must start its stops again past its radius");
    SkiaRaster reflected;
    radial_spread(reflected, GradientSpreadMode::reflect);
    require(is_near(pixel_at(reflected, 62U, 32U), pixel_at(reflected, 50U, 32U)) &&
                !is_near(pixel_at(reflected, 62U, 32U), pixel_at(reflected, 54U, 32U)),
            "a reflected radial gradient must run its stops back past its radius");
    SkiaRaster refused;
    radial_spread(refused, static_cast<GradientSpreadMode>(9));
    require(is_exactly(pixel_at(refused, 48U, 32U), white), "an unknown spread must draw nothing");
}

void test_image_pattern_wraps() {
    // A 2 x 1 image, red then blue, in tiles of its own size at scale 1 from
    // a whole-unit origin: every device pixel centre lies on a texel centre,
    // so each pixel is a texel's colour exactly.
    const Control::Ptr root = make_control<Control>(StableId("pattern.wraps"));
    Window window(root, scene_size);
    const std::array<std::byte, 8> strip{{std::byte{0U}, std::byte{0U}, std::byte{255U}, std::byte{255U},
                                          std::byte{255U}, std::byte{0U}, std::byte{0U}, std::byte{255U}}};
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(2U, 1U, 8U, strip);
    require(static_cast<bool>(loaded), "the strip image must load");
    const std::array<ImagePatternWrap, 3> wraps{{ImagePatternWrap::tile, ImagePatternWrap::mirror,
                                                 ImagePatternWrap::clamp}};
    // Each wrap's eight columns: r red, b blue (ImagePatternWrap).
    const std::array<const char*, 3> expected{{"rbrbrbrb", "rbbrrbbr", "rbbbbbbb"}};
    for (std::size_t index = 0U; index < wraps.size(); ++index) {
        SkiaRaster raster;
        require(raster.resize(scene_size, 1.0) || raster.pixel_width() != 0U, "the raster must have a surface");
        require(raster.synchronize_images(window.image_resources()), "image synchronization failed");
        DamageRegion damage;
        damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
        raster.begin_frame(damage);
        raster.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
        raster.fill_image_pattern(loaded.image, Size{2.0, 1.0}, Rect{0.0, 40.0, 8.0, 4.0}, Size{2.0, 1.0},
                                  wraps[index], 1.0);
        raster.end_frame();
        const std::string columns(expected[index]);
        for (std::size_t row = 40U; row < 44U; ++row) {
            for (std::size_t column = 0U; column < columns.size(); ++column) {
                const Color wanted = columns[column] == 'r' ? red : blue;
                require(is_exactly(pixel_at(raster, column, row), wanted),
                        "each pattern pixel must be the texel its wrap maps it to");
            }
        }
        require(is_exactly(pixel_at(raster, 8U, 41U), white), "the pattern must stay within its destination");
    }
}

void hatch_scene(Painter& painter) {
    HatchPattern cross{};
    cross.style = HatchStyle::cross;
    cross.fore = black;
    cross.back = white;
    painter.fill_hatch(Rect{0.0, 0.0, 16.0, 16.0}, cross);
    HatchPattern diagonal{};
    diagonal.style = HatchStyle::backward_diagonal;
    diagonal.fore = red;
    diagonal.back = white;
    diagonal.anchor = Point{32.0, 0.0};
    painter.fill_hatch(Rect{32.0, 0.0, 16.0, 16.0}, diagonal);
}

void test_hatch() {
    // Cells of 8 units at scale 1: each square is one device pixel.
    SkiaRaster raster;
    paint(raster, &hatch_scene);
    require(is_exactly(pixel_at(raster, 3U, 0U), black) && is_exactly(pixel_at(raster, 0U, 3U), black) &&
                is_exactly(pixel_at(raster, 11U, 5U), black) && is_exactly(pixel_at(raster, 13U, 11U), black),
            "a cross hatch marks column 3 and row 3 of every cell");
    require(is_exactly(pixel_at(raster, 0U, 0U), white) && is_exactly(pixel_at(raster, 5U, 5U), white) &&
                is_exactly(pixel_at(raster, 12U, 9U), white),
            "a cross hatch leaves the other squares in the back colour");
    require(is_exactly(pixel_at(raster, 37U, 5U), red) && is_exactly(pixel_at(raster, 45U, 13U), red) &&
                is_exactly(pixel_at(raster, 37U, 6U), white) && is_exactly(pixel_at(raster, 48U, 0U), white),
            "a backward diagonal marks the squares where column equals row, inside its rectangle");
}

void test_focal_radial() {
    // Red at the focus, 8 units left of the centre, to blue on a circle of
    // 16 units: the stops crowd towards the near edge. Its rectangle starts
    // at x 16: padded, it paints blue beyond the circle too, and the 8 by 8
    // corner is kept for the refused focus below.
    SkiaRaster raster;
    require(raster.resize(scene_size, 1.0) || raster.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    const std::array<GradientStop, 2> stops{{GradientStop{0.0, red}, GradientStop{1.0, blue}}};
    const Rect focal_area{16.0, 0.0, scene_size.width - 16.0, scene_size.height};
    raster.fill_focal_radial_gradient(focal_area, Point{48.5, 32.5}, Size{16.0, 16.0}, Point{40.5, 32.5}, stops,
                                      GradientSpreadMode::pad);
    raster.fill_focal_radial_gradient(Rect{0.0, 0.0, 8.0, 8.0}, Point{4.0, 4.0}, Size{2.0, 2.0}, Point{7.0, 4.0},
                                      stops, GradientSpreadMode::pad);
    raster.end_frame();
    const Pixel at_focus = pixel_at(raster, 40U, 32U);
    const Pixel right_of_focus = pixel_at(raster, 44U, 32U);
    const Pixel left_of_focus = pixel_at(raster, 36U, 32U);
    require(at_focus.red >= 250U && at_focus.blue <= 5U, "the focus takes the first stop");
    require(static_cast<int>(right_of_focus.red) > static_cast<int>(left_of_focus.red) + 40,
            "offsets grow faster towards the edge nearer the focus");
    require(is_exactly(pixel_at(raster, 2U, 2U), white), "a focus outside the ellipse draws nothing");
}

// A bar 16 by 4 about (48, 32), turned a quarter about its centre: it
// stands from y 24 to 40 across x 46 to 50. A hatch inside a half turn
// lands mirrored.
void transform_scene(Painter& painter) {
    PaintTransformGroup group{};
    group.bounds = Rect{36.0, 20.0, 24.0, 24.0};
    group.transform = paint_rotation_about(Point{48.0, 32.0}, 90.0);
    painter.begin_transform(group);
    painter.fill_rect(Rect{40.0, 30.0, 16.0, 4.0}, red);
    painter.end_transform();
    PaintTransformGroup moved{};
    moved.bounds = Rect{0.0, 0.0, 4.0, 4.0};
    moved.transform.dx = 70.0;
    moved.transform.dy = 50.0;
    painter.begin_transform(moved);
    painter.fill_rect(Rect{0.0, 0.0, 8.0, 8.0}, blue);
    painter.end_transform();
}

void test_transform_group() {
    SkiaRaster raster;
    paint(raster, &transform_scene);
    require(is_exactly(pixel_at(raster, 47U, 25U), red) && is_exactly(pixel_at(raster, 48U, 38U), red),
            "the turned bar stands upright");
    require(is_exactly(pixel_at(raster, 41U, 32U), white) && is_exactly(pixel_at(raster, 54U, 32U), white) &&
                is_exactly(pixel_at(raster, 47U, 22U), white),
            "nothing is left where the bar lay before the turn");
    require(is_exactly(pixel_at(raster, 70U, 50U), blue) && is_exactly(pixel_at(raster, 73U, 53U), blue) &&
                is_exactly(pixel_at(raster, 74U, 51U), white) && is_exactly(pixel_at(raster, 71U, 54U), white),
            "a translated group draws inside its bounds only");
    require(raster.transform_refusals() == 0U, "valid groups are not refused");
}

// Rotation tier 1 (claude/geometry-1). A 64 x 64 checkerboard of single
// black and white pixels, drawn through a quarter turn at a quarter scale:
// it is area-resampled to 16 x 16 first, every target pixel the mean of a
// 4 x 4 block (eight black, eight white), so the turned square is an even
// mid grey rather than an aliased pattern.
std::shared_ptr<const PartRaster> checkerboard_raster() {
    std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{64U, 64U}, DeviceScale{1.0});
    require(raster != nullptr, "the checkerboard raster must be made");
    const PartRasterView view = (*raster).view();
    for (std::size_t y = 0U; y < 64U; ++y) {
        for (std::size_t x = 0U; x < 64U; ++x) {
            const std::uint8_t level = ((x + y) % 2U) == 0U ? std::uint8_t{0U} : std::uint8_t{255U};
            const std::size_t at = y * view.row_bytes + x * 4U;
            view.pixels[at] = level;
            view.pixels[at + 1U] = level;
            view.pixels[at + 2U] = level;
            view.pixels[at + 3U] = 255U;
        }
    }
    (*raster).record_opacity();
    return raster;
}

// The quarter turn at a quarter scale: x' = -y / 4 + 40, y' = x / 4 + 8, so
// the raster's logical 64 x 64 lands on device x 24 to 40, y 8 to 24.
PaintTransform quarter_turn_at_quarter_scale() {
    PaintTransform map{};
    map.xx = 0.0;
    map.yx = 0.25;
    map.xy = -0.25;
    map.yy = 0.0;
    map.dx = 40.0;
    map.dy = 8.0;
    return map;
}

void turned_raster_scene(Painter& painter) {
    const std::shared_ptr<const PartRaster> raster = checkerboard_raster();
    PaintTransformGroup group{};
    group.bounds = Rect{0.0, 0.0, 64.0, 64.0};
    group.transform = quarter_turn_at_quarter_scale();
    painter.begin_transform(group);
    painter.draw_part_raster(raster, Point{0.0, 0.0}, 1.0);
    painter.end_transform();
}

void test_turned_part_raster_is_area_resampled() {
    SkiaRaster raster;
    paint(raster, &turned_raster_scene);
    require(raster.transformed_resamples() == 1U, "a raster minified through a turning map is resampled first");
    bool even = true;
    for (std::size_t y = 11U; y < 21U; ++y) {
        for (std::size_t x = 27U; x < 37U; ++x) {
            const Pixel pixel = pixel_at(raster, x, y);
            const bool grey = pixel.red >= 126U && pixel.red <= 130U && pixel.green == pixel.red &&
                              pixel.blue == pixel.red;
            if (!grey) even = false;
        }
    }
    require(even, "the turned checkerboard is an even mid grey inside its square");
    require(is_white(pixel_at(raster, 20U, 16U)) && is_white(pixel_at(raster, 32U, 28U)),
            "nothing is drawn beyond the turned square");
}

// An isolated group inside a turning transform group (rotation tier 1): two
// overlapping red fills at half opacity show as one half-red bar, turned,
// where without isolation the group drew them opaque (and was refused).
void turned_group_scene(Painter& painter) {
    PaintTransformGroup turn{};
    turn.bounds = Rect{36.0, 20.0, 24.0, 24.0};
    turn.transform = paint_rotation_about(Point{48.0, 32.0}, 90.0);
    painter.begin_transform(turn);
    PaintGroup group{};
    group.bounds = Rect{40.0, 30.0, 16.0, 4.0};
    group.opacity = 0.5;
    painter.begin_group(group);
    painter.fill_rect(Rect{40.0, 30.0, 16.0, 4.0}, red);
    painter.fill_rect(Rect{40.0, 30.0, 10.0, 4.0}, red);
    painter.end_group();
    painter.end_transform();
}

void test_turned_group_is_isolated() {
    SkiaRaster raster;
    paint(raster, &turned_group_scene);
    require(raster.group_refusals() == 0U, "a group under a turning map is isolated, not refused");
    const Pixel overlapped = pixel_at(raster, 47U, 26U);
    const Pixel single = pixel_at(raster, 47U, 38U);
    require(overlapped.red == 255U && overlapped.green >= 120U && overlapped.green <= 136U &&
                single.red == 255U && single.green == overlapped.green && single.blue == overlapped.blue,
            "the turned group is composited once at half opacity, its overlap not doubled");
    require(is_white(pixel_at(raster, 47U, 22U)) && is_white(pixel_at(raster, 41U, 32U)),
            "the group is clipped to its turned bounds");
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
    path_scene(painter);
    clip_scene(painter);
    hatch_scene(painter);
    transform_scene(painter);
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
        test_path_fill_and_stroke();
        test_path_damage_invariance(1.0);
        test_path_damage_invariance(2.0);
        test_path_clip();
        test_part_raster_under_path_clip();
        test_path_clip_mask_matches_rectangle();
        test_radial_spreads();
        test_image_pattern_wraps();
        test_hatch();
        test_focal_radial();
        test_transform_group();
        test_turned_part_raster_is_area_resampled();
        test_turned_group_is_isolated();
        test_replay_draws_the_same_bytes();
        std::cout << "gui_forms_paint_marks_raster_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_paint_marks_raster_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
