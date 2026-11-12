// Measures what synchronize_images pays inside a frame for one new PNG
// resource (audit P12): the ResourcePixelDecoder decoding a 3840 x 2160
// RGBA image to premultiplied BGRA, the raster's target. It prints the
// encoded size and the best and median of several decodes in milliseconds.
// A probe, not a test: it is built on demand and never registered with
// CTest, so it never gates on timing.
#include "../../src/core/image/resource_pixels.hpp"
#include "gui_forms/resources.hpp"
#include "stx_png/encoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::uint32_t width = 3840U;
constexpr std::uint32_t height = 2160U;
constexpr std::size_t rounds = 7U;

// A photograph-like image: smooth gradients with per-pixel noise, so the
// encoder's filters and deflate see neither a flat field nor pure noise.
void fill_samples(Bytes& samples) {
    std::uint32_t noise = 0x12345678U;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            noise = noise * 1664525U + 1013904223U;
            const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4U;
            const std::uint32_t jitter = (noise >> 28U) & 7U;
            samples[at] = static_cast<std::byte>((x * 255U / width + jitter) & 255U);
            samples[at + 1U] = static_cast<std::byte>((y * 255U / height + jitter) & 255U);
            samples[at + 2U] = static_cast<std::byte>(((x + y) / 32U + jitter) & 255U);
            samples[at + 3U] = static_cast<std::byte>(255U - (jitter * 4U));
        }
    }
}

} // namespace

int main() {
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    const std::size_t byte_count = row_bytes * height;
    Bytes samples(byte_count);
    fill_samples(samples);
    stx_png::PngEncodeInput input{};
    input.width = width;
    input.height = height;
    input.row_bytes = row_bytes;
    input.pixels = samples;
    input.alpha = stx_png::PngSourceAlpha::straight;
    Bytes file{};
    if (!stx_png::encode_png(input, file)) {
        std::cerr << "png decode probe: the fixture did not encode\n";
        return 1;
    }
    gui_forms::ImageRegistry registry{};
    const gui_forms::ImageLoadResult loaded = registry.load_image(file);
    if (!static_cast<bool>(loaded)) {
        std::cerr << "png decode probe: the registry refused the fixture\n";
        return 1;
    }
    const std::optional<gui_forms::ImageResourceView> view = registry.find(loaded.image);
    if (!view.has_value()) {
        std::cerr << "png decode probe: the admitted image is missing\n";
        return 1;
    }
    const gui_forms::image::PixelTarget target{gui_forms::image::PixelOrder::bgra,
                                               gui_forms::image::PixelAlpha::premultiplied};
    gui_forms::image::ResourcePixelDecoder decoder{};
    Bytes pixels(byte_count);
    std::array<double, rounds> milliseconds{};
    for (std::size_t round = 0; round < rounds; ++round) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const gui_forms::image::ResourcePixelResult decoded = decoder.decode(*view, target, pixels, row_bytes);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if (decoded.error != gui_forms::image::ResourcePixelError::none) {
            std::cerr << "png decode probe: the decode failed\n";
            return 1;
        }
        const std::chrono::duration<double, std::milli> elapsed = end - start;
        milliseconds[round] = elapsed.count();
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "png decode probe: " << width << "x" << height << " RGBA, " << file.size()
              << " encoded bytes, best " << milliseconds[0] << " ms, median " << milliseconds[rounds / 2U]
              << " ms over " << rounds << " decodes\n";
    return 0;
}
