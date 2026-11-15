#include "gui_forms/drawing_c_api.h"
#include "gui_forms/c/gf_abi.h"
#include "gui_forms/drawing_raster_c_api.h"

#include <stxlib/thread_records.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "drawing_raster_c_api_tests:" << line << ": "
              << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

[[nodiscard]] std::string_view view_of(const gd_string_view value) {
    const std::string_view result(value.data, static_cast<std::size_t>(value.size));
    return result;
}

// The raster module's answer for a family and style, with the font's
// measured width of a sample.
struct ResolvedFamily final {
    gd_font_resolution resolution{};
    double width{};
};

ResolvedFamily resolve(const gd_api_v0& api, std::string_view family, std::uint32_t style) {
    ResolvedFamily result{};
    gd_handle font{};
    CHECK(api.font_create({family.data(), family.size()}, 12.0, style, 3U, 1U, &font) == GD_OK);
    result.resolution.struct_size = sizeof(gd_font_resolution);
    CHECK(api.font_resolve(font, &result.resolution) == GD_OK);
    const char sample[] = "Hamburgefonstiv";
    gd_size measured{};
    CHECK(api.measure_string({sample, sizeof(sample) - 1U}, font, {}, 0.0, &measured) == GD_OK);
    result.width = measured.width;
    CHECK(api.release(font) == GD_OK);
    return result;
}

// Stage F: the substitution table first, the content family for a family
// nothing names, every answer reported and its face loaded.
void fonts_resolve_through_the_substitution_table(const gd_api_v0& api) {
    const ResolvedFamily calibri = resolve(api, "Calibri", 0U);
    CHECK(calibri.resolution.source == GD_FONT_SOURCE_SUBSTITUTION);
    CHECK(calibri.resolution.mapping == GD_FONT_MAPPING_METRIC_COMPATIBLE);
    CHECK(view_of(calibri.resolution.family) == "Carlito");
    CHECK(view_of(calibri.resolution.face_file) == "Carlito-Regular.ttf");
    CHECK(calibri.resolution.face_loaded == 1U);
    const ResolvedFamily carlito = resolve(api, "Carlito", 0U);
    CHECK(carlito.resolution.source == GD_FONT_SOURCE_BUNDLED);
    CHECK(carlito.width == calibri.width);

    const ResolvedFamily helvetica = resolve(api, "Helvetica", 1U);
    CHECK(helvetica.resolution.source == GD_FONT_SOURCE_ALIAS);
    CHECK(view_of(helvetica.resolution.face_file) == "Arimo-Bold.ttf");
    CHECK(helvetica.width == resolve(api, "Arial", 1U).width);

    // Selawik has no italic: its regular face draws, reported, unsheared.
    const ResolvedFamily segoe = resolve(api, "segoe ui", 2U);
    CHECK(segoe.resolution.source == GD_FONT_SOURCE_SUBSTITUTION);
    CHECK(segoe.resolution.mapping == GD_FONT_MAPPING_NEAREST);
    CHECK(segoe.resolution.requested_style == 2U);
    CHECK(segoe.resolution.face_style == 0U);
    CHECK(view_of(segoe.resolution.face_file) == "Selawik-Regular.ttf");
    CHECK(segoe.resolution.face_loaded == 1U);

    const ResolvedFamily generic = resolve(api, "monospace", 0U);
    CHECK(generic.resolution.source == GD_FONT_SOURCE_GENERIC);
    CHECK(view_of(generic.resolution.family) == "Cousine");

    const ResolvedFamily absent = resolve(api, "GUI Forms Absent Family", 0U);
    CHECK(absent.resolution.source == GD_FONT_SOURCE_CONTENT);
    CHECK(view_of(absent.resolution.family) == "Carlito");
    CHECK(absent.resolution.face_loaded == 1U);
    CHECK(absent.width == carlito.width);
}

} // namespace

// The 13 points of a circle as four joined cubic quarters (the 0.5523
// approximation), clockwise in y-down coordinates from (cx + r, cy).
std::vector<gd_point> circle_beziers(double cx, double cy, double r) {
    const double k = 0.5523 * r;
    std::vector<gd_point> points{{cx + r, cy}, {cx + r, cy + k}, {cx + k, cy + r}, {cx, cy + r},
                                 {cx - k, cy + r}, {cx - r, cy + k}, {cx - r, cy}, {cx - r, cy - k},
                                 {cx - k, cy - r}, {cx, cy - r}, {cx + k, cy - r}, {cx + r, cy - k},
                                 {cx + r, cy}};
    return points;
}

// The colour of one pixel after running `recorder` over a fresh white
// 64 x 64 bitmap.
gd_color executed_pixel(const gd_api_v0& api, gd_handle recorder, std::uint32_t x, std::uint32_t y) {
    gd_handle bitmap{};
    CHECK(api.bitmap_create(64, 64, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap) == GD_OK);
    std::uint64_t executed{};
    CHECK(api.recorder_execute(recorder, bitmap, &executed) == GD_OK);
    gd_color pixel{};
    CHECK(api.bitmap_get_pixel(bitmap, x, y, &pixel) == GD_OK);
    CHECK(api.release(bitmap) == GD_OK);
    return pixel;
}

// ABI 0.6 (exhibit gap E7): a ring of two Bezier circles filled even-odd
// leaves its hole and filled non-zero covers it; a quadratic curve stroked
// with a pen lands on the curve's midpoint.
void curved_paths_fill_and_stroke() {
    gd_api_v0 api{};
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_6, &api) == GD_OK);
    CHECK(api.graphics_path_add_beziers != nullptr);
    const gd_color white{UINT32_C(0xffffffff), 0};
    const gd_color black{UINT32_C(0xff000000), 0};
    gd_handle brush{};
    gd_handle pen{};
    gd_handle ring{};
    gd_handle curve{};
    CHECK(api.solid_brush_create(black, &brush) == GD_OK);
    CHECK(api.pen_create(black, 3.0, &pen) == GD_OK);
    CHECK(api.graphics_path_create(GD_FILL_ALTERNATE, &ring) == GD_OK);
    const std::vector<gd_point> outer = circle_beziers(32.0, 32.0, 24.0);
    const std::vector<gd_point> inner = circle_beziers(32.0, 32.0, 10.0);
    CHECK(api.graphics_path_start_figure(ring) == GD_OK);
    CHECK(api.graphics_path_add_beziers(ring, outer.data(), outer.size()) == GD_OK);
    CHECK(api.graphics_path_close_figure(ring) == GD_OK);
    CHECK(api.graphics_path_start_figure(ring) == GD_OK);
    CHECK(api.graphics_path_add_beziers(ring, inner.data(), inner.size()) == GD_OK);
    CHECK(api.graphics_path_close_figure(ring) == GD_OK);

    gd_handle even_odd{};
    CHECK(api.recorder_create(&even_odd) == GD_OK);
    CHECK(api.recorder_clear(even_odd, white) == GD_OK);
    CHECK(api.recorder_fill_path(even_odd, brush, ring) == GD_OK);
    CHECK(executed_pixel(api, even_odd, 32, 32).argb == white.argb);
    CHECK(executed_pixel(api, even_odd, 32, 14).argb == black.argb);
    CHECK(executed_pixel(api, even_odd, 2, 2).argb == white.argb);

    CHECK(api.graphics_path_set_fill_mode(ring, GD_FILL_WINDING) == GD_OK);
    gd_handle nonzero{};
    CHECK(api.recorder_create(&nonzero) == GD_OK);
    CHECK(api.recorder_clear(nonzero, white) == GD_OK);
    CHECK(api.recorder_fill_path(nonzero, brush, ring) == GD_OK);
    CHECK(executed_pixel(api, nonzero, 32, 32).argb == black.argb);

    CHECK(api.graphics_path_create(GD_FILL_ALTERNATE, &curve) == GD_OK);
    CHECK(api.graphics_path_add_quadratic(curve, {4.0, 60.0}, {32.0, 40.0}, {60.0, 60.0}) == GD_OK);
    gd_handle stroked{};
    CHECK(api.recorder_create(&stroked) == GD_OK);
    CHECK(api.recorder_clear(stroked, white) == GD_OK);
    CHECK(api.recorder_draw_path(stroked, pen, curve) == GD_OK);
    // The curve's midpoint (t = 1/2) is (32, 50).
    const gd_color on_curve = executed_pixel(api, stroked, 32, 50);
    CHECK((on_curve.argb & 0x00ffffffU) < 0x00404040U);
    CHECK(executed_pixel(api, stroked, 32, 57).argb == white.argb);

    CHECK(api.release(stroked) == GD_OK);
    CHECK(api.release(nonzero) == GD_OK);
    CHECK(api.release(even_odd) == GD_OK);
    CHECK(api.release(curve) == GD_OK);
    CHECK(api.release(ring) == GD_OK);
    CHECK(api.release(pen) == GD_OK);
    CHECK(api.release(brush) == GD_OK);
}

// The pixels of a 160 x 160 white bitmap after `recorder` runs over it.
std::vector<std::uint32_t> executed_pixels(const gd_api_v0& api, gd_handle recorder) {
    constexpr std::uint32_t side = 160U;
    gd_handle bitmap{};
    CHECK(api.bitmap_create(side, side, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap) == GD_OK);
    std::uint64_t executed{};
    CHECK(api.recorder_execute(recorder, bitmap, &executed) == GD_OK);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
    for (std::uint32_t y = 0U; y < side; ++y) {
        for (std::uint32_t x = 0U; x < side; ++x) {
            gd_color pixel{};
            CHECK(api.bitmap_get_pixel(bitmap, x, y, &pixel) == GD_OK);
            pixels[static_cast<std::size_t>(y) * side + x] = pixel.argb;
        }
    }
    CHECK(api.release(bitmap) == GD_OK);
    return pixels;
}

// The dark pixels (red below 128) of executed_pixels: their count, box and
// centroid, in pixel-centre coordinates.
struct Ink final {
    std::size_t count{};
    double left{};
    double top{};
    double right{};
    double bottom{};
    double centre_x{};
    double centre_y{};
};

Ink ink_of(const std::vector<std::uint32_t>& pixels) {
    constexpr std::uint32_t side = 160U;
    Ink ink{};
    ink.left = side;
    ink.top = side;
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (std::uint32_t y = 0U; y < side; ++y) {
        for (std::uint32_t x = 0U; x < side; ++x) {
            const std::uint32_t red = (pixels[static_cast<std::size_t>(y) * side + x] >> 16U) & 0xffU;
            if (red >= 128U) continue;
            ink.count = ink.count + 1U;
            ink.left = std::min(ink.left, static_cast<double>(x));
            ink.top = std::min(ink.top, static_cast<double>(y));
            ink.right = std::max(ink.right, static_cast<double>(x + 1U));
            ink.bottom = std::max(ink.bottom, static_cast<double>(y + 1U));
            sum_x += static_cast<double>(x) + 0.5;
            sum_y += static_cast<double>(y) + 0.5;
        }
    }
    if (ink.count == 0U) return ink;
    ink.centre_x = sum_x / static_cast<double>(ink.count);
    ink.centre_y = sum_y / static_cast<double>(ink.count);
    return ink;
}

// What one string draws at (80, 80) on white: level through
// recorder_draw_string, or turned through recorder_draw_string_rotated.
struct TurnedDraw final {
    gd_handle font{};
    gd_handle format{};
    gd_handle brush{};
    bool level{};
    double degrees{};
};

constexpr std::string_view turned_sample = "Axis 42";
constexpr gd_point turned_origin{80.0, 80.0};

std::vector<std::uint32_t> draw_turned(const gd_api_v0& api, const TurnedDraw& draw) {
    gd_handle recorder{};
    CHECK(api.recorder_create(&recorder) == GD_OK);
    CHECK(api.recorder_clear(recorder, gd_color{UINT32_C(0xffffffff), 0}) == GD_OK);
    const gd_string_view text{turned_sample.data(), turned_sample.size()};
    if (draw.level) {
        CHECK(api.recorder_draw_string(recorder, text, draw.font, draw.brush, turned_origin, draw.format) == GD_OK);
    } else {
        CHECK(api.recorder_draw_string_rotated(recorder, text, draw.font, draw.brush, turned_origin, draw.degrees,
                                               draw.format) == GD_OK);
    }
    std::vector<std::uint32_t> pixels = executed_pixels(api, recorder);
    CHECK(api.release(recorder) == GD_OK);
    return pixels;
}

// ABI 0.7: a string turned about its origin keeps its ink inside its turned
// layout box (the box a consumer damages) and reads along the turned
// baseline, at several angles; 0 degrees draws recorder_draw_string's
// pixels.
void rotated_strings_turn_their_ink() {
    gd_api_v0 api{};
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_7, &api) == GD_OK);
    CHECK(api.recorder_draw_string_rotated != nullptr);
    const char family[] = "Portsmouth Rapids";
    TurnedDraw draw{};
    CHECK(api.font_create({family, sizeof(family) - 1U}, 16.0, 0U, 3U, 1U, &draw.font) == GD_OK);
    CHECK(api.string_format_create(0U, &draw.format) == GD_OK);
    CHECK(api.solid_brush_create(gd_color{UINT32_C(0xff000000), 0}, &draw.brush) == GD_OK);
    gd_size measured{};
    CHECK(api.measure_string({turned_sample.data(), turned_sample.size()}, draw.font, draw.format, 0.0, &measured) ==
          GD_OK);
    CHECK(measured.width > 30.0 && measured.height > 16.0);

    draw.level = true;
    const std::vector<std::uint32_t> level = draw_turned(api, draw);
    draw.level = false;
    draw.degrees = 0.0;
    CHECK(draw_turned(api, draw) == level);

    constexpr double pi = 3.14159265358979323846;
    // The draw_string overhang (a sixth of the size) and a pixel of
    // antialiasing.
    const double slack = 16.0 / 6.0 + 1.5;
    const std::array<double, 5> angles{30.0, 90.0, 135.0, 200.0, -45.0};
    for (const double degrees : angles) {
        draw.degrees = degrees;
        const Ink ink = ink_of(draw_turned(api, draw));
        CHECK(ink.count >= 40U);
        const double radians = degrees * (pi / 180.0);
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        // The turned layout box: along u = (cos, -sin) by the width and down
        // v = (sin, cos) by the height from the origin.
        const std::array<double, 4> along_corner{0.0, measured.width, 0.0, measured.width};
        const std::array<double, 4> down_corner{0.0, 0.0, measured.height, measured.height};
        double box_left = turned_origin.x;
        double box_top = turned_origin.y;
        double box_right = turned_origin.x;
        double box_bottom = turned_origin.y;
        for (std::size_t corner = 0U; corner < along_corner.size(); ++corner) {
            const double x = turned_origin.x + along_corner[corner] * cosine + down_corner[corner] * sine;
            const double y = turned_origin.y - along_corner[corner] * sine + down_corner[corner] * cosine;
            box_left = std::min(box_left, x);
            box_top = std::min(box_top, y);
            box_right = std::max(box_right, x);
            box_bottom = std::max(box_bottom, y);
        }
        CHECK(ink.left >= box_left - slack && ink.top >= box_top - slack);
        CHECK(ink.right <= box_right + slack && ink.bottom <= box_bottom + slack);
        const double dx = ink.centre_x - turned_origin.x;
        const double dy = ink.centre_y - turned_origin.y;
        const double along = dx * cosine - dy * sine;
        const double down = dx * sine + dy * cosine;
        CHECK(std::fabs(along - measured.width * 0.5) < measured.width * 0.25);
        CHECK(down > 0.0 && down < measured.height);
    }

    CHECK(api.release(draw.brush) == GD_OK);
    CHECK(api.release(draw.format) == GD_OK);
    CHECK(api.release(draw.font) == GD_OK);
}

// The BGRA bytes of one pixel of a lent target.
struct TargetPixel final {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t alpha{};
};

TargetPixel target_pixel(const std::vector<std::uint8_t>& pixels, std::size_t row_bytes, std::size_t x,
                         std::size_t y) {
    const std::size_t at = y * row_bytes + x * 4U;
    const TargetPixel result{pixels[at], pixels[at + 1U], pixels[at + 2U], pixels[at + 3U]};
    return result;
}

bool is_opaque_red(const TargetPixel pixel) {
    return pixel.blue == 0U && pixel.green == 0U && pixel.red == 255U && pixel.alpha == 255U;
}

bool is_sentinel_blue(const TargetPixel pixel) {
    return pixel.blue == 255U && pixel.green == 0U && pixel.red == 0U && pixel.alpha == 255U;
}

// ABI 1.0's bridge (seal decision 12): a recording in logical units drawn
// into a lent GUI.Forms paint target at its scale, only inside its damage,
// rows row_bytes apart; the recording is left as it was.
void paint_target_draws_scaled_inside_damage(const gd_api_v0& api) {
    CHECK(api.recorder_paint_target != nullptr);
    gd_handle recorder{};
    gd_handle brush{};
    CHECK(api.recorder_create(&recorder) == GD_OK);
    CHECK(api.solid_brush_create(gd_color{UINT32_C(0xffff0000), 0}, &brush) == GD_OK);
    CHECK(api.recorder_fill_rectangle(recorder, brush, gd_rect{0, 0, 4, 4}) == GD_OK);

    // 12 x 10 device pixels with 8 bytes of padding per row; opaque blue
    // pixels, padding 0xab.
    constexpr std::size_t width = 12U;
    constexpr std::size_t height = 10U;
    constexpr std::size_t row_bytes = width * 4U + 8U;
    std::vector<std::uint8_t> pixels(row_bytes * height, std::uint8_t{0xab});
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t at = y * row_bytes + x * 4U;
            pixels[at] = 255U;
            pixels[at + 1U] = 0U;
            pixels[at + 2U] = 0U;
            pixels[at + 3U] = 255U;
        }
    }
    gf_paint_target target{};
    target.struct_size = sizeof(target);
    target.width = static_cast<std::uint32_t>(width);
    target.height = static_cast<std::uint32_t>(height);
    target.row_bytes = row_bytes;
    target.pixels = pixels.data();
    target.scale = 2.0;
    // Logical 0..3 square: device pixels 0..6.
    target.damage = gf_logical_rect{0.0, 0.0, 3.0, 3.0};
    std::uint64_t executed{};
    CHECK(api.recorder_paint_target(recorder, &target, &executed) == GD_OK);
    CHECK(executed == 1U);
    // Inside the rectangle (device 0..8) and the damage: drawn.
    CHECK(is_opaque_red(target_pixel(pixels, row_bytes, 1U, 1U)));
    CHECK(is_opaque_red(target_pixel(pixels, row_bytes, 5U, 5U)));
    // Inside the rectangle, outside the damage: left as it was.
    CHECK(is_sentinel_blue(target_pixel(pixels, row_bytes, 7U, 7U)));
    CHECK(is_sentinel_blue(target_pixel(pixels, row_bytes, 6U, 2U)));
    // Outside both.
    CHECK(is_sentinel_blue(target_pixel(pixels, row_bytes, 10U, 8U)));
    // The row padding is never written.
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t offset = width * 4U; offset < row_bytes; ++offset) {
            CHECK(pixels[y * row_bytes + offset] == 0xabU);
        }
    }
    // The recording is unchanged and draws again, here over the whole
    // target at scale 1: device 0..4.
    std::uint64_t count{};
    CHECK(api.recorder_command_count(recorder, &count) == GD_OK);
    CHECK(count == 1U);
    target.scale = 1.0;
    target.damage = gf_logical_rect{0.0, 0.0, 12.0, 10.0};
    CHECK(api.recorder_paint_target(recorder, &target, &executed) == GD_OK);
    CHECK(executed == 1U);
    CHECK(is_opaque_red(target_pixel(pixels, row_bytes, 3U, 3U)));
    CHECK(is_sentinel_blue(target_pixel(pixels, row_bytes, 7U, 7U)));
    // A wrong kind of handle is refused as a kind, not drawn.
    CHECK(api.recorder_paint_target(brush, &target, &executed) == GD_ERROR_WRONG_HANDLE_KIND);
    CHECK(api.release(brush) == GD_OK);
    CHECK(api.release(recorder) == GD_OK);
    CHECK(api.recorder_paint_target(recorder, &target, &executed) == GD_ERROR_STALE_HANDLE);
}

// One thread's raster draw (owner rule 2, G4b): its executor and font
// registrations are its raster record, released when it ends. Results are
// checked on the test's thread after the join.
struct ThreadDraw final {
    const gd_api_v0* api{};
    gd_result executed{GD_ERROR_INTERNAL};
    std::uint32_t pixel{};
    // After stxlib::end_thread_records: the raster call refused quietly.
    bool end_records_first{};
};

struct ThreadDrawEntry final {
    ThreadDraw* draw{};

    void operator()() const {
        const gd_api_v0& api = *(*draw).api;
        gd_handle bitmap{};
        gd_handle recorder{};
        if (api.bitmap_create(4, 4, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap) != GD_OK) return;
        if (api.recorder_create(&recorder) == GD_OK &&
            api.recorder_clear(recorder, gd_color{UINT32_C(0xff2266aa), 0}) == GD_OK) {
            if ((*draw).end_records_first) stxlib::end_thread_records();
            std::uint64_t executed{};
            (*draw).executed = api.recorder_execute(recorder, bitmap, &executed);
            gd_color pixel{};
            if (api.bitmap_get_pixel(bitmap, 1, 1, &pixel) == GD_OK) (*draw).pixel = pixel.argb;
        }
        static_cast<void>(api.release(recorder));
        static_cast<void>(api.release(bitmap));
    }
};

// 64 threads started and joined one after another, each drawing once
// through its own executor; then one thread whose records have ended, whose
// draw is refused with GD_ERROR_NOT_AVAILABLE and nothing else. Leaks of
// released records show in the sanitizer runs (LeakSanitizer).
void thread_executors_end_with_their_threads(const gd_api_v0& api) {
    constexpr std::size_t thread_count = 64U;
    for (std::size_t index = 0U; index < thread_count; ++index) {
        ThreadDraw draw{&api};
        const ThreadDrawEntry entry{&draw};
        std::thread thread(entry);
        thread.join();
        CHECK(draw.executed == GD_OK);
        CHECK(draw.pixel == UINT32_C(0xff2266aa));
    }
    ThreadDraw ended{&api};
    ended.end_records_first = true;
    const ThreadDrawEntry entry{&ended};
    std::thread thread(entry);
    thread.join();
    CHECK(ended.executed == GD_ERROR_NOT_AVAILABLE);
}

int main() {
    gd_api_v0 api{};
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_1, &api) == GD_OK);
    CHECK(api.raster_service_install != nullptr);
    CHECK(api.recorder_execute != nullptr);
    CHECK(api.bitmap_encode_png != nullptr);
    CHECK(api.bitmap_decode_png != nullptr);
    CHECK(api.measure_string != nullptr);
    CHECK(api.recorder_execute_from != nullptr);
    CHECK(gdr_initialize() == GD_OK);
    CHECK(gdr_initialize() == GD_OK);

    gd_handle bitmap{};
    gd_handle recorder{};
    CHECK(api.bitmap_create(8, 6, GD_PIXEL_BGRA32_PREMULTIPLIED,
                            &bitmap) == GD_OK);
    CHECK(api.recorder_create(&recorder) == GD_OK);
    CHECK(api.recorder_clear(
              recorder, gd_color{UINT32_C(0xff336699), 0}) == GD_OK);
    std::uint64_t executed{};
    CHECK(api.recorder_execute(recorder, bitmap, &executed) == GD_OK);
    CHECK(executed == 1U);
    gd_color pixel{};
    CHECK(api.bitmap_get_pixel(bitmap, 0, 0, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xff336699));

    CHECK(api.recorder_clear(
              recorder, gd_color{UINT32_C(0xff993311), 0}) == GD_OK);
    CHECK(api.recorder_execute_from(recorder, bitmap, 1U, &executed) == GD_OK);
    CHECK(executed == 1U);
    CHECK(api.bitmap_get_pixel(bitmap, 0, 0, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xff993311));
    CHECK(api.recorder_execute_from(recorder, bitmap, 2U, &executed) == GD_OK);
    CHECK(executed == 0U);

    std::uint64_t required{};
    CHECK(api.bitmap_encode_png(bitmap, nullptr, 0, &required) ==
          GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(required > 32U);
    std::vector<std::byte> png(static_cast<std::size_t>(required));
    CHECK(api.bitmap_encode_png(bitmap, png.data(), png.size(), &required) == GD_OK);
    gd_handle decoded{};
    CHECK(api.bitmap_decode_png(png.data(), png.size(), &decoded) == GD_OK);
    CHECK(api.bitmap_get_pixel(decoded, 7, 5, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xff993311));

    const char family[] = "Portsmouth Rapids";
    gd_handle font{};
    gd_handle format{};
    CHECK(api.font_create({family, sizeof(family) - 1U}, 12.0, 0U,
                          3U, 1U, &font) == GD_OK);
    CHECK(api.string_format_create(0U, &format) == GD_OK);
    const char narrow[] = "iiii";
    const char wide[] = "WWWW";
    gd_size narrow_size{};
    gd_size wide_size{};
    CHECK(api.measure_string({narrow, sizeof(narrow) - 1U}, font, {}, 0.0,
                             &narrow_size) == GD_OK);
    CHECK(api.measure_string({wide, sizeof(wide) - 1U}, font, format, 0.0,
                             &wide_size) == GD_OK);
    CHECK(narrow_size.width > 0.0);
    CHECK(wide_size.width > narrow_size.width);
    CHECK(wide_size.height > 12.0);
    const char wrapped[] = "alpha beta gamma";
    gd_size unbounded{};
    gd_size bounded{};
    CHECK(api.measure_string({wrapped, sizeof(wrapped) - 1U}, font, format,
                             0.0, &unbounded) == GD_OK);
    CHECK(api.measure_string({wrapped, sizeof(wrapped) - 1U}, font, format,
                             unbounded.width / 2.0, &bounded) == GD_OK);
    CHECK(bounded.width <= unbounded.width / 2.0);
    CHECK(bounded.height > unbounded.height);
    // N60: a hyphen is a line-break opportunity (UAX #14). A width that
    // fits "alpha-be" breaks after the hyphen, so the widest line is
    // "alpha-" as measured alone, and the text takes two lines.
    const char hyphenated[] = "alpha-beta";
    const char first_line[] = "alpha-";
    const char fitted[] = "alpha-be";
    gd_size one_line{};
    gd_size first_size{};
    gd_size fitted_size{};
    gd_size broken{};
    CHECK(api.measure_string({hyphenated, sizeof(hyphenated) - 1U}, font, format, 0.0, &one_line) == GD_OK);
    CHECK(api.measure_string({first_line, sizeof(first_line) - 1U}, font, format, 0.0, &first_size) == GD_OK);
    CHECK(api.measure_string({fitted, sizeof(fitted) - 1U}, font, format, 0.0, &fitted_size) == GD_OK);
    CHECK(api.measure_string({hyphenated, sizeof(hyphenated) - 1U}, font, format, fitted_size.width,
                             &broken) == GD_OK);
    CHECK(broken.height > one_line.height);
    CHECK(broken.width == first_size.width);

    CHECK(api.release(format) == GD_OK);
    CHECK(api.release(font) == GD_OK);
    CHECK(api.release(decoded) == GD_OK);
    CHECK(api.release(recorder) == GD_OK);
    CHECK(api.release(bitmap) == GD_OK);

    gd_api_v0 current{};
    current.struct_size = sizeof(current);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_2, &current) == GD_OK);
    CHECK(current.font_resolve != nullptr);
    fonts_resolve_through_the_substitution_table(current);
    gd_api_v0 sealed{};
    sealed.struct_size = sizeof(sealed);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_1_0, &sealed) == GD_OK);
    paint_target_draws_scaled_inside_damage(sealed);
    curved_paths_fill_and_stroke();
    rotated_strings_turn_their_ink();
    thread_executors_end_with_their_threads(sealed);
    return 0;
}
