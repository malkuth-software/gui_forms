#include "gui_forms/canvas.hpp"
#include "gui_forms/window.hpp"
#include "skia_raster.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <chrono>
#include <string_view>

using namespace gui_forms;

void require_at(bool value, int line) { if (!value) { std::cerr << "failed at " << line << std::endl; std::abort(); } }
#define require(value) require_at((value), __LINE__)

void compare(ImageSampling sampling, double zoom, double scale, bool edited) {
    const std::shared_ptr<gui_drawing::Bitmap> bitmap = std::make_shared<gui_drawing::Bitmap>(1025, 1025);
    gui_drawing::BitmapEditView edit = (*bitmap).begin_edit({0, 0, 1025, 1025});
    for (int y = 0; y < 1025; ++y) for (int x = 0; x < 1025; ++x) {
        std::byte* pixel = edit.writable_data + y * edit.row_bytes + x * 4;
        const unsigned alpha = 80 + (x * 3 + y * 5) % 176;
        pixel[0] = std::byte((x % 251) * alpha / 255);
        pixel[1] = std::byte((y % 251) * alpha / 255);
        pixel[2] = std::byte(((x + y) % 251) * alpha / 255);
        pixel[3] = std::byte(alpha);
    }
    static_cast<void>((*bitmap).commit_edit(edit.token));
    const std::shared_ptr<RasterCanvas> canvas = make_control<RasterCanvas>(StableId("raster"));
    (*canvas).bitmap = bitmap;
    (*canvas).transparency_grid = false;
    (*canvas).canvas_background = Color::rgba(23, 37, 51);
    (*canvas).sampling = sampling;
    (*canvas).set_view(zoom, {470.3, 480.7});
    const Size size{180, 150};
    Window window(canvas, size);
    // The window is told its device scale, as a host does: painted
    // rectangles grow to whole pixels of that scale.
    window.set_scale(scale);
    window.perform_layout();
    render::SkiaRaster tiled;
    require(tiled.resize(size, scale));
    require(tiled.synchronize_images(window.image_resources()));
    DamageRegion damage;
    damage.add({0, 0, size.width, size.height});
    tiled.begin_frame(damage);
    require(window.paint(tiled).has_value());
    tiled.end_frame();
    if (edited) {
        (*bitmap).set_pixel(511, 511, gui_drawing::Color::from_name("red"));
        (*bitmap).set_pixel(512, 512, gui_drawing::Color::from_name("blue"));
        require((*canvas).synchronize_bitmap());
        require(tiled.synchronize_images(window.image_resources()));
        tiled.begin_frame(damage);
        require(window.paint(tiled).has_value());
        tiled.end_frame();
    }
    const gui_drawing::ImageSnapshot snapshot = (*bitmap).snapshot();
    ImageRegistry registry;
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(1025, 1025, snapshot.row_bytes(), snapshot.pixels());
    require(bool(loaded));
    render::SkiaRaster reference;
    require(reference.resize(size, scale));
    require(reference.synchronize_images(registry));
    reference.begin_frame(damage);
    reference.fill_rect({0, 0, size.width, size.height}, Color::rgba(23, 37, 51));
    reference.draw_image_region_sampled(loaded.image, {0, 0, 1025, 1025},
        {-470.3 * zoom, -480.7 * zoom, 1025 * zoom, 1025 * zoom}, sampling, 1);
    reference.end_frame();
    const unsigned char* a = static_cast<const unsigned char*>(tiled.pixels());
    const unsigned char* b = static_cast<const unsigned char*>(reference.pixels());
    unsigned maximum{};
    std::size_t differences{};
    for (std::size_t index = 0; index < tiled.byte_size(); ++index) {
        const unsigned distance = static_cast<unsigned>(std::abs(int(a[index]) - int(b[index])));
        maximum = std::max(maximum, distance);
        if (distance > 1) {
            if (differences < 12) std::cerr << "difference at " << index % tiled.row_bytes() / 4 << ',' << index / tiled.row_bytes() << " channel " << index % 4 << " tiled " << int(a[index]) << " reference " << int(b[index]) << '\n';
            ++differences;
        }
    }
    std::cout << "sampling=" << int(sampling) << " zoom=" << zoom << " scale=" << scale
              << " edited=" << edited << " max_channel_error=" << maximum << " differences=" << differences << '\n';
    require(maximum <= 1);
}
void large_native_probe() {
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const std::shared_ptr<gui_drawing::Bitmap> bitmap = std::make_shared<gui_drawing::Bitmap>(10000, 10000);
    const std::shared_ptr<RasterCanvas> canvas = make_control<RasterCanvas>(StableId("large.raster"));
    (*canvas).bitmap = bitmap;
    (*canvas).transparency_grid = false;
    ImageRegistryLimits limits;
    limits.maximum_total_encoded_bytes = 512ULL * 1024ULL * 1024ULL;
    limits.maximum_total_decoded_bytes = 512ULL * 1024ULL * 1024ULL;
    Window window(canvas, {1200, 800}, limits);
    window.perform_layout();
    render::SkiaRaster raster;
    require(raster.resize({1200, 800}, 2.0));
    require(raster.synchronize_images(window.image_resources()));
    DamageRegion damage;
    damage.add({0, 0, 1200, 800});
    raster.begin_frame(damage);
    require(window.paint(raster).has_value());
    raster.end_frame();
    const std::chrono::steady_clock::time_point initial = std::chrono::steady_clock::now();
    (*bitmap).set_pixel(333, 333, gui_drawing::Color::from_name("red"));
    require((*canvas).synchronize_bitmap());
    const std::chrono::steady_clock::time_point published = std::chrono::steady_clock::now();
    require(raster.synchronize_images(window.image_resources()));
    const std::chrono::steady_clock::time_point cached = std::chrono::steady_clock::now();
    raster.begin_frame(damage);
    require(window.paint(raster).has_value());
    raster.end_frame();
    const std::chrono::steady_clock::time_point painted = std::chrono::steady_clock::now();
    const unsigned char* pixels = static_cast<const unsigned char*>(raster.pixels());
    const std::size_t offset = 666 * raster.row_bytes() + 666 * 4;
    // The private CPU surface is RGBA; the canvas input remains BGRA.
    require(pixels[offset] == 255 && pixels[offset + 1] == 0 && pixels[offset + 2] == 0);
    std::cout << "100M native pixels, scale=2, initial_ms="
        << std::chrono::duration<double, std::milli>(initial - started).count()
        << ", publish_ms=" << std::chrono::duration<double, std::milli>(published - initial).count()
        << ", cache_ms=" << std::chrono::duration<double, std::milli>(cached - published).count()
        << ", paint_ms=" << std::chrono::duration<double, std::milli>(painted - cached).count() << std::endl;
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--large") { large_native_probe(); return 0; }
    for (ImageSampling sampling : {ImageSampling::nearest, ImageSampling::linear})
        for (double zoom : {1.0 / 64, 0.1, 0.25, 1.0, 1.25, 3.1})
            for (double scale : {1.0, 1.5, 2.0})
                for (bool edited : {false, true}) compare(sampling, zoom, scale, edited);
}
