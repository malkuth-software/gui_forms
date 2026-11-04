#include "layered_material_lab.hpp"

#include "skia_raster.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;
namespace lab = gui_forms::layered_material_lab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> studio_patch_pixels() {
    constexpr std::size_t extent = 12U;
    std::vector<std::byte> pixels(extent * extent * 4U);
    for (std::size_t y = 0U; y < extent; ++y) {
        for (std::size_t x = 0U; x < extent; ++x) {
            Color color = Color::rgba(232, 235, 231);
            if (x == 0U || y == 0U) color = Color::rgba(255, 255, 250);
            if (x == extent - 1U || y == extent - 1U) {
                color = Color::rgba(119, 137, 149);
            } else if (x <= 2U || y <= 2U) {
                color = Color::rgba(247, 244, 232);
            } else if (x >= extent - 3U || y >= extent - 3U) {
                color = Color::rgba(198, 210, 216);
            }
            const std::size_t offset = (y * extent + x) * 4U;
            pixels[offset] = static_cast<std::byte>(color.blue);
            pixels[offset + 1U] = static_cast<std::byte>(color.green);
            pixels[offset + 2U] = static_cast<std::byte>(color.red);
            pixels[offset + 3U] = static_cast<std::byte>(color.alpha);
        }
    }
    return pixels;
}

struct Pixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};
};

Pixel pixel_at(const render::SkiaRaster& raster, int x, int y, int scale = 2) {
    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(raster.pixels());
    const std::size_t offset = static_cast<std::size_t>(y * scale) *
            raster.row_bytes() + static_cast<std::size_t>(x * scale) * 4U;
    // SkiaRaster stores premultiplied BGRA8 (SK2).
    return {pixels[offset + 2U], pixels[offset + 1U], pixels[offset],
            pixels[offset + 3U]};
}

unsigned channel_distance(std::uint8_t left, std::uint8_t right) {
    return left > right ? left - right : right - left;
}

unsigned pixel_distance(Pixel left, Pixel right) {
    return channel_distance(left.red, right.red) +
           channel_distance(left.green, right.green) +
           channel_distance(left.blue, right.blue);
}

void render_materials(render::SkiaRaster& raster, const ImageRegistry& images,
                      ImageId studio_patch) {
    require(raster.synchronize_images(images),
            "CPU raster rejected the deterministic material image registry");
    require(raster.resize({500.0, 240.0}, 2.0),
            "CPU raster could not allocate the material test surface");
    DamageRegion damage;
    damage.add({0.0, 0.0, 500.0, 240.0});
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, 500.0, 240.0},
                     Color::rgba(218, 224, 228));
    paint_surface_material(raster, {20.0, 20.0, 210.0, 90.0},
                           lab::watercolor_fresco_material());
    paint_surface_material(raster, {270.0, 20.0, 210.0, 90.0},
                           lab::office_pearl_material());
    paint_surface_material(raster, {20.0, 150.0, 210.0, 70.0},
                           lab::workshop_graphite_material());
    paint_surface_material(raster, {270.0, 150.0, 210.0, 70.0},
                           lab::studio_caption_material(studio_patch));
    raster.end_frame();
}

void verify_material_hierarchy(const render::SkiaRaster& raster) {
    const Pixel watercolor_left = pixel_at(raster, 42, 64);
    const Pixel watercolor_right = pixel_at(raster, 210, 64);
    const Pixel watercolor_dark_edge = pixel_at(raster, 120, 109);
    const Pixel watercolor_specular = pixel_at(raster, 120, 107);
    require(watercolor_left.blue > watercolor_left.red + 25U &&
                watercolor_right.red > watercolor_right.blue &&
                pixel_distance(watercolor_specular, Pixel{}) >
                    pixel_distance(watercolor_dark_edge, Pixel{}),
            "Watercolor raster must retain blue/coral geography and separate bright/dark lower keylines");

    const Pixel pearl_high = pixel_at(raster, 360, 32);
    const Pixel pearl_low = pixel_at(raster, 360, 98);
    require(pearl_high.red > pearl_low.red &&
                pearl_high.green > pearl_low.green &&
                pearl_high.blue > pearl_low.blue,
            "Office Pearl raster must remain visibly raised from bright crown to cool low edge");

    unsigned graphite_min = 255U;
    unsigned graphite_max = 0U;
    for (int y = 170; y < 204; ++y) {
        for (int x = 50; x < 196; ++x) {
            const Pixel value = pixel_at(raster, x, y);
            const unsigned luminance =
                (static_cast<unsigned>(value.red) * 3U +
                 static_cast<unsigned>(value.green) * 6U +
                 static_cast<unsigned>(value.blue)) / 10U;
            graphite_min = std::min(graphite_min, luminance);
            graphite_max = std::max(graphite_max, luminance);
        }
    }
    require(graphite_min >= 55U && graphite_max <= 105U &&
                graphite_max > graphite_min,
            "Graphite raster must preserve subtle non-flat middle-value texture without becoming black");

    const Pixel studio_filament = pixel_at(raster, 276, 184);
    const Pixel studio_center = pixel_at(raster, 370, 184);
    require(studio_filament.blue > studio_filament.red + 30U &&
                studio_center.red > 180U && studio_center.green > 185U &&
                studio_center.blue > 180U,
            "Studio raster must preserve its blue identity filament and warm cap-inset factual well");
}

} // namespace

int main() {
    try {
        ImageRegistry images;
        const std::vector<std::byte> patch_pixels = studio_patch_pixels();
        const ImageLoadResult patch = images.load_bgra32_premultiplied(
            12U, 12U, 48U, patch_pixels);
        require(static_cast<bool>(patch),
                "deterministic Studio nine-patch must load");

        gui_forms::render::SkiaRaster skia;
        render_materials(skia, images, patch.image);
        verify_material_hierarchy(skia);
        std::cout << "gui_forms_layered_material_raster_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_layered_material_raster_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
