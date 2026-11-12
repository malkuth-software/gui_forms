#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "skia_raster.hpp"
#include "text_identity_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// ADR-004 identity: text drawn through the raster must produce the same bytes
// on every platform. Each case hashes the whole raster after drawing; the
// expected values were produced on macOS arm64 and Alpine aarch64 and are
// checked by every CI platform. GUI_FORMS_TEXT_IDENTITY_PRINT=1 prints the
// observed hashes instead of checking them.
namespace {
using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

std::uint64_t raster_hash(const render::SkiaRaster& raster, const std::size_t kind, const std::size_t scale) {
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(raster.pixels());
    require(bytes != nullptr, "raster readable");
    text_identity::dump_pixels(bytes, raster.pixel_width(), raster.pixel_height(), raster.row_bytes(), kind, scale);
    return text_identity::pixel_hash(bytes, raster.pixel_width(), raster.pixel_height(), raster.row_bytes());
}

std::uint64_t draw_case(const std::size_t kind_index, const std::size_t scale_index) {
    const text_identity::Case kind = text_identity::cases[kind_index];
    const double scale = text_identity::scales[scale_index];
    render::SkiaRaster raster{};
    require(raster.resize(text_identity::scene_size, scale), "raster allocated");
    require(raster.register_typeface_file(FontRole::content, 400, false, GUI_FORMS_TEST_CONTROL_FONT), "font registered");
    DamageRegion damage{};
    if (kind == text_identity::Case::damage_region) {
        damage.add({0.0, 0.0, 60.0, 48.0});
        damage.add({90.0, 0.0, 70.0, 48.0});
    } else {
        damage.add({0.0, 0.0, text_identity::scene_size.width, text_identity::scene_size.height});
    }
    raster.begin_frame(damage);
    text_identity::paint_scene(raster, kind);
    raster.end_frame();
    return raster_hash(raster, kind_index, scale_index);
}

// Paints white, then black text inside the frame (shared compositor) or after
// end_frame, where the raster has no recorded clips and draws the same
// coverage as a Skia alpha mask (ADR-004 phase 7: there are no Skia glyphs).
void draw_white_and_text(render::SkiaRaster& raster, const double scale, const bool inside_frame) {
    require(raster.resize(text_identity::scene_size, scale), "raster allocated");
    require(raster.register_typeface_file(FontRole::content, 400, false, GUI_FORMS_TEST_CONTROL_FONT), "font registered");
    DamageRegion damage{};
    damage.add({0.0, 0.0, text_identity::scene_size.width, text_identity::scene_size.height});
    const FontSpec font{FontRole::content, 15, 400, false, 0.0};
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, text_identity::scene_size.width, text_identity::scene_size.height},
                     Color::rgba(255, 255, 255));
    if (inside_frame) raster.draw_text_utf8({4.25, 30.0}, "Hamburgefonstiv", font, Color::rgba(0, 0, 0));
    raster.end_frame();
    if (!inside_frame) raster.draw_text_utf8({4.25, 30.0}, "Hamburgefonstiv", font, Color::rgba(0, 0, 0));
}

// The mask path draws the shared rasterizer's coverage, so it differs from
// the shared compositor only by Skia's blend rounding.
void check_mask_fallback() {
    for (const double scale : text_identity::scales) {
        render::SkiaRaster direct{};
        render::SkiaRaster masked{};
        draw_white_and_text(direct, scale, true);
        draw_white_and_text(masked, scale, false);
        const std::uint8_t* const expected = static_cast<const std::uint8_t*>(direct.pixels());
        const std::uint8_t* const observed = static_cast<const std::uint8_t*>(masked.pixels());
        require(expected != nullptr && observed != nullptr, "rasters readable");
        int largest = 0;
        std::size_t ink = 0;
        for (std::uint32_t row = 0; row < direct.pixel_height(); ++row) {
            for (std::size_t column = 0; column < static_cast<std::size_t>(direct.pixel_width()) * 4U; ++column) {
                const std::size_t offset = static_cast<std::size_t>(row) * direct.row_bytes() + column;
                largest = std::max(largest, std::abs(static_cast<int>(expected[offset]) - static_cast<int>(observed[offset])));
                if (column % 4U == 0U && observed[offset] < 128U) ++ink;
            }
        }
        std::printf("mask-fallback %.1f max-difference=%d ink=%zu\n", scale, largest, ink);
        require(ink > 40U, "mask fallback draws the text");
        require(largest <= 2, "mask fallback matches the shared compositor within blend rounding");
    }
}

} // namespace

int main() {
    const char* const print = std::getenv("GUI_FORMS_TEXT_IDENTITY_PRINT");
    const bool printing = print != nullptr && std::strcmp(print, "1") == 0;
    bool mismatch = false;
    for (std::size_t kind = 0; kind < text_identity::cases.size(); ++kind) {
        for (std::size_t scale = 0; scale < text_identity::scales.size(); ++scale) {
            const std::uint64_t observed = draw_case(kind, scale);
            std::printf("text-identity %s %.1f 0x%016llxULL\n", text_identity::case_names[kind],
                        text_identity::scales[scale], static_cast<unsigned long long>(observed));
            if (!printing && observed != text_identity::expected[kind][scale]) mismatch = true;
        }
    }
    require(printing || !mismatch, "text output differs from the cross-platform expectation");
    check_mask_fallback();
    return 0;
}
