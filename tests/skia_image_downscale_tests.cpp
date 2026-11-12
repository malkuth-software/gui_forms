// draw_image below 1x (owner, 2026-11-11, audit P11): the image is resampled
// through stx_conv (the interpolation seam, pixel-area) to the device size it
// is drawn at, and the copy is cached by image content and size. Draws at or
// above 1x are unchanged. This test checks both: a downscaled draw must equal
// the seam's own output for the same image and size (one quantization step
// of tolerance), and a draw at or above 1x must equal Skia's linear filter.
// It also prints the measurement the owner asked for: the distance of each
// path from the exact box average of the source, and the time of a
// downscaled thumbnail workload, before (Skia's linear filter, the path
// draw_image used to take, reached here through draw_image_region_sampled)
// and after.
//
// The box-average distance is printed, not asserted: one-pixel stripes
// averaged over a footprint of non-integer width are not uniformly grey (a
// footprint of 256/243 pixels mostly covers one stripe), and stx_conv's
// pixel-area sampling integrates its own reconstruction of the source, not
// the box of the source pixels.

#include "gui_forms/window.hpp"
#include "skia_raster.hpp"
#include "../src/core/interpolation/interpolation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// An opaque grey image of `size` square whose columns alternate black and
// white, one pixel each: the worst case for a filter that samples instead of
// integrating. Its true average is 127.5 everywhere.
std::vector<std::byte> stripes(int size) {
    std::vector<std::byte> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::byte level = (x % 2) == 0 ? std::byte{0} : std::byte{255};
            std::byte* const pixel = pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4U;
            pixel[0] = level;
            pixel[1] = level;
            pixel[2] = level;
            pixel[3] = std::byte{255};
        }
    }
    return pixels;
}

enum class Path : std::uint8_t { draw_image, linear_filter };

// One premultiplied channel in [0, 1] quantized to a byte, as the renderer
// quantizes its resampled copy.
std::uint8_t channel_byte(double value) {
    const double scaled = std::round(std::clamp(value, 0.0, 1.0) * 255.0);
    return static_cast<std::uint8_t>(scaled);
}

// The exact expected copy: `pixels` (premultiplied BGRA, `size` square)
// resampled by the interpolation seam (pixel-area) to a `target` square and
// quantized as the renderer does (colour clamped to alpha).
std::vector<std::uint8_t> expected_conv(const std::vector<std::byte>& pixels, int size, int target) {
    constexpr std::size_t channels = 4U;
    const std::size_t source_side = static_cast<std::size_t>(size);
    const std::size_t target_side = static_cast<std::size_t>(target);
    std::vector<double> source_samples(source_side * source_side * channels);
    std::vector<double> target_samples(target_side * target_side * channels);
    for (std::size_t index = 0U; index < source_samples.size(); ++index) {
        source_samples[index] = static_cast<double>(std::to_integer<std::uint8_t>(pixels[index])) / 255.0;
    }
    const interpolation::SourceImage from{source_samples, source_side, source_side, channels,
                                          source_side * channels};
    const interpolation::TargetImage to{target_samples, target_side, target_side, target_side * channels};
    const interpolation::Status status =
        interpolation::resample_image(from, to, interpolation::Sampling::pixel_area, interpolation::ValueRange{});
    require(status == interpolation::Status::ok, "the seam resamples the stripes");
    std::vector<std::uint8_t> expected(target_samples.size());
    for (std::size_t at = 0U; at < target_samples.size(); at += channels) {
        const double alpha = target_samples[at + 3U];
        expected[at] = channel_byte(std::min(target_samples[at], alpha));
        expected[at + 1U] = channel_byte(std::min(target_samples[at + 1U], alpha));
        expected[at + 2U] = channel_byte(std::min(target_samples[at + 2U], alpha));
        expected[at + 3U] = channel_byte(alpha);
    }
    return expected;
}

// The exact box average of the stripes over target column `column` of a
// `target` square: the white (odd) source columns' share of the footprint
// [column * size / target, (column + 1) * size / target), times 255.
double box_average(int size, int target, int column) {
    const double scale = static_cast<double>(size) / static_cast<double>(target);
    const double left = static_cast<double>(column) * scale;
    const double right = static_cast<double>(column + 1) * scale;
    const int first = static_cast<int>(std::floor(left));
    const int last = std::min(size - 1, static_cast<int>(std::ceil(right)) - 1);
    double white = 0.0;
    for (int cell = first; cell <= last; ++cell) {
        if (cell % 2 == 0) continue;
        const double covered =
            std::min(right, static_cast<double>(cell + 1)) - std::max(left, static_cast<double>(cell));
        if (covered > 0.0) white = white + covered;
    }
    const double average = 255.0 * white / (right - left);
    return average;
}

// Draws the image over a `target` square at the origin by `path`, on black.
void draw_stripes(render::SkiaRaster& raster, const ImageRegistry& registry, ImageId image, int size, int target,
                  Path path) {
    require(raster.synchronize_images(registry), "images synchronize");
    const Size canvas{256.0, 256.0};
    DamageRegion damage;
    damage.add({0.0, 0.0, canvas.width, canvas.height});
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, canvas.width, canvas.height}, Color::rgba(0, 0, 0));
    const Rect destination{0.0, 0.0, static_cast<double>(target), static_cast<double>(target)};
    if (path == Path::draw_image) raster.draw_image(image, destination, 1.0);
    else {
        const Rect source{0.0, 0.0, static_cast<double>(size), static_cast<double>(size)};
        raster.draw_image_region_sampled(image, source, destination, ImageSampling::linear, 1.0);
    }
    raster.end_frame();
}

// The largest distance of a blue channel from the box average over the
// target's interior (one pixel in from each edge).
double box_error(const render::SkiaRaster& raster, int size, int target) {
    const std::uint8_t* const pixels = static_cast<const std::uint8_t*>(raster.pixels());
    const std::size_t row_bytes = raster.row_bytes();
    double worst = 0.0;
    for (int y = 1; y < target - 1; ++y) {
        for (int x = 1; x < target - 1; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(x) * 4U;
            const double blue = static_cast<double>(pixels[at]);
            worst = std::max(worst, std::abs(blue - box_average(size, target, x)));
        }
    }
    return worst;
}

// The largest distance of any channel of the drawn `target` square from
// `expected` (row-major, four bytes a pixel).
int conv_difference(const render::SkiaRaster& raster, const std::vector<std::uint8_t>& expected, int target) {
    const std::uint8_t* const pixels = static_cast<const std::uint8_t*>(raster.pixels());
    const std::size_t row_bytes = raster.row_bytes();
    const std::size_t side = static_cast<std::size_t>(target);
    int worst = 0;
    for (std::size_t y = 0U; y < side; ++y) {
        for (std::size_t index = 0U; index < side * 4U; ++index) {
            const int drawn = static_cast<int>(pixels[y * row_bytes + index]);
            const int wanted = static_cast<int>(expected[y * side * 4U + index]);
            worst = std::max(worst, std::abs(drawn - wanted));
        }
    }
    return worst;
}

void downscale_matches_conv() {
    const int size = 256;
    const std::vector<std::byte> pixels = stripes(size);
    ImageRegistry registry;
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), static_cast<std::uint64_t>(size) * 4U, pixels);
    require(bool(loaded), "the stripe image loads");
    render::SkiaRaster raster;
    require(raster.resize({256.0, 256.0}, 1.0), "the raster sizes");
    std::printf("scale  box_error_linear  box_error_conv  conv_vs_seam\n");
    const double scales[] = {0.95, 0.9, 0.75, 0.6, 0.5, 0.33, 0.25, 0.1};
    for (const double scale : scales) {
        const int target = static_cast<int>(std::lround(size * scale));
        draw_stripes(raster, registry, loaded.image, size, target, Path::linear_filter);
        const double before = box_error(raster, size, target);
        draw_stripes(raster, registry, loaded.image, size, target, Path::draw_image);
        const double after = box_error(raster, size, target);
        const std::vector<std::uint8_t> expected = expected_conv(pixels, size, target);
        const int difference = conv_difference(raster, expected, target);
        std::printf("%.2f   %6.1f            %6.1f          %d\n", scale, before, after, difference);
        // The drawn copy is the seam's output at the device size, drawn 1:1.
        require(difference <= 1, "a downscaled draw is stx_conv's pixel-area resampling");
    }
}

// At or above 1x draw_image takes the direct path: its pixels match Skia's
// linear filter over the same whole-pixel rectangle.
void one_and_above_unchanged() {
    const int size = 32;
    std::vector<std::byte> pixels(static_cast<std::size_t>(size) * size * 4U);
    for (int index = 0; index < size * size; ++index) {
        const std::uint8_t alpha = static_cast<std::uint8_t>(96 + index % 160);
        pixels[static_cast<std::size_t>(index) * 4U] = std::byte(static_cast<std::uint8_t>((index * 7) % 256 * alpha / 255));
        pixels[static_cast<std::size_t>(index) * 4U + 1U] = std::byte(static_cast<std::uint8_t>((index * 3) % 256 * alpha / 255));
        pixels[static_cast<std::size_t>(index) * 4U + 2U] = std::byte(static_cast<std::uint8_t>((index * 5) % 256 * alpha / 255));
        pixels[static_cast<std::size_t>(index) * 4U + 3U] = std::byte(alpha);
    }
    ImageRegistry registry;
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), static_cast<std::uint64_t>(size) * 4U, pixels);
    require(bool(loaded), "the image loads");
    const double sizes[] = {32.0, 48.0, 64.0};
    for (const double destination_size : sizes) {
        render::SkiaRaster direct;
        render::SkiaRaster reference;
        require(direct.resize({80.0, 80.0}, 1.0) && reference.resize({80.0, 80.0}, 1.0), "rasters size");
        require(direct.synchronize_images(registry) && reference.synchronize_images(registry), "images synchronize");
        DamageRegion damage;
        damage.add({0.0, 0.0, 80.0, 80.0});
        const Rect destination{4.0, 4.0, destination_size, destination_size};
        direct.begin_frame(damage);
        direct.draw_image(loaded.image, destination, 1.0);
        direct.end_frame();
        reference.begin_frame(damage);
        reference.draw_image_region_sampled(loaded.image, {0.0, 0.0, 32.0, 32.0}, destination, ImageSampling::linear, 1.0);
        reference.end_frame();
        const std::size_t bytes = direct.row_bytes() * 80U;
        require(std::memcmp(direct.pixels(), reference.pixels(), bytes) == 0,
                "a draw at or above 1x keeps its pixels");
    }
}

// The workload: 48 thumbnails of four 256-pixel images drawn at 48 pixels,
// 20 frames. The first frame of the new path makes the copies; the others
// draw them from the cache.
void thumbnail_workload() {
    const int size = 256;
    ImageRegistry registry;
    std::vector<ImageId> images;
    for (int variant = 0; variant < 4; ++variant) {
        std::vector<std::byte> pixels = stripes(size);
        for (std::size_t index = 0U; index < pixels.size(); index += 4U) {
            pixels[index + static_cast<std::size_t>(variant % 3)] = std::byte{static_cast<std::uint8_t>(40 * variant)};
        }
        const ImageLoadResult loaded = registry.load_bgra32_premultiplied(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), static_cast<std::uint64_t>(size) * 4U, pixels);
        require(bool(loaded), "a thumbnail source loads");
        images.push_back(loaded.image);
    }
    render::SkiaRaster raster;
    require(raster.resize({400.0, 300.0}, 1.0), "the raster sizes");
    require(raster.synchronize_images(registry), "images synchronize");
    DamageRegion damage;
    damage.add({0.0, 0.0, 400.0, 300.0});
    const Path paths[] = {Path::linear_filter, Path::draw_image};
    for (const Path path : paths) {
        double first_frame = 0.0;
        double later_frames = 0.0;
        for (int frame = 0; frame < 20; ++frame) {
            const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
            raster.begin_frame(damage);
            for (int index = 0; index < 48; ++index) {
                const Rect destination{static_cast<double>((index % 8) * 50), static_cast<double>((index / 8) * 50),
                                       48.0, 48.0};
                const ImageId image = images[static_cast<std::size_t>(index) % images.size()];
                if (path == Path::draw_image) raster.draw_image(image, destination, 1.0);
                else raster.draw_image_region_sampled(image, {0.0, 0.0, 256.0, 256.0}, destination,
                                                      ImageSampling::linear, 1.0);
            }
            raster.end_frame();
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (frame == 0) first_frame = elapsed;
            else later_frames += elapsed;
        }
        std::printf("workload %s: first frame %.3f ms, later frames %.3f ms each\n",
                    path == Path::draw_image ? "conv" : "linear", first_frame, later_frames / 19.0);
    }
}

} // namespace

int main() {
    try {
        downscale_matches_conv();
        one_and_above_unchanged();
        thumbnail_workload();
    } catch (const std::exception& error) {
        std::cerr << "skia image downscale test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
