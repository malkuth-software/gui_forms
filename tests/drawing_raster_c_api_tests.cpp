#include "gui_forms/drawing_c_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

extern "C" gd_result gdr_initialize(void);

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
    return 0;
}
