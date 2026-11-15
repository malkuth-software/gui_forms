#include "gui_forms/drawing_c_api.h"
#include "gui_forms/drawing_raster_c_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
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
    curved_paths_fill_and_stroke();
    return 0;
}
