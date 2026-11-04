#pragma once

#include "gui_forms/types.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

// ADR-004 identity scene, shared by every raster that draws ordinary text.
// The same painter calls must produce the same bytes on every platform.
namespace gui_forms::text_identity {

enum class Case : std::uint8_t { plain, translucent, fractional_clip, rounded_clip, damage_region };

inline constexpr std::array<Case, 5> cases{Case::plain, Case::translucent, Case::fractional_clip, Case::rounded_clip,
                                           Case::damage_region};
inline constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
inline constexpr std::array<const char*, 5> case_names{"plain", "translucent", "fractional_clip", "rounded_clip",
                                                       "damage_region"};
inline constexpr Size scene_size{160.0, 48.0};

// Rasters are premultiplied BGRA8 on every host (SK2). Hashes and dumps are
// taken in RGBA order, as they were recorded, so each pixel's red and blue are
// exchanged first.

// Expected FNV-1a hashes of the raster's rows in RGBA order,
// [case][scale], from SkiaRaster, the one CPU painter: identical on macOS
// arm64 (Apple SDK) and Alpine aarch64 musl (Clang 22.1.3), 2026-10-11, and
// on Windows x64 (clang 22.1.8), 2026-11-02 (SK5).
inline constexpr std::array<std::array<std::uint64_t, 3>, 5> expected{{
    {0x8d50f64b74fcf8b8ULL, 0x471b6a11e111ec7dULL, 0xab5fc975326f3f28ULL},
    {0x83f2cc636fa71c45ULL, 0x6c602d56fa0ed64eULL, 0x63a26ab8baf146e2ULL},
    {0x9f2321491ff1b2c3ULL, 0x9f4f28a24374934eULL, 0x2822334624cfadd3ULL},
    {0x5746df86d820334eULL, 0x24e0cb400ebdd4adULL, 0x1733822b6219aa3dULL},
    {0x2a513350b2c7890fULL, 0x65c047c6320db78fULL, 0x06b94ee7cb3d0779ULL},
}};

// Paints the scene after the frame has begun (with its damage).
inline void paint_scene(Painter& painter, const Case kind) {
    painter.fill_rect({0.0, 0.0, scene_size.width, scene_size.height}, Color::rgba(250, 248, 240, 255));
    painter.fill_rect({0.0, 24.0, scene_size.width, 24.0}, Color::rgba(40, 70, 110, 255));
    // Carlito, the content face, on every host.
    const FontSpec font{.role = FontRole::content, .size = 15.0};
    Color colour = Color::rgba(20, 20, 30, 255);
    if (kind == Case::translucent) colour = Color::rgba(200, 60, 40, 170);
    painter.save();
    if (kind == Case::fractional_clip) painter.clip_rect({10.3, 0.0, 100.4, 48.0});
    if (kind == Case::rounded_clip) painter.clip_rounded_rect({4.0, 6.0, 120.0, 34.0}, 12.0);
    painter.translate({2.0, 0.0});
    painter.draw_text_utf8({3.25, 20.5}, "Hamburgefonstiv 0123 Help", font, colour);
    painter.draw_text_utf8({3.25, 41.0}, "quick brown fox jumps", font, Color::rgba(245, 245, 250, 255));
    painter.restore();
}

// Byte at index of a BGRA row read as an RGBA-ordered pixel stream.
inline std::uint8_t rgba_byte(const std::uint8_t* const line, const std::size_t index) noexcept {
    const std::size_t channel = index % 4U;
    std::size_t source = index;
    if (channel != 1U && channel != 3U) source = index - channel + (2U - channel);
    return line[source];
}

// FNV-1a over rows of width * 4 bytes, in RGBA order.
inline std::uint64_t pixel_hash(const std::uint8_t* const bytes, const std::uint32_t width, const std::uint32_t height,
                                const std::size_t row_bytes) {
    std::uint64_t value = 1469598103934665603ULL;
    const std::size_t width_bytes = static_cast<std::size_t>(width) * 4U;
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::uint8_t* const line = bytes + static_cast<std::size_t>(row) * row_bytes;
        for (std::size_t column = 0; column < width_bytes; ++column) {
            value ^= rgba_byte(line, column);
            value *= 1099511628211ULL;
        }
    }
    return value;
}

// With GUI_FORMS_TEXT_IDENTITY_DUMP=<directory>, writes the raw rows of each
// case to <directory>/<case>-<scale x 10>.raw for byte comparison.
inline void dump_pixels(const std::uint8_t* const bytes, const std::uint32_t width, const std::uint32_t height,
                        const std::size_t row_bytes, const std::size_t kind, const std::size_t scale) {
    const char* const directory = std::getenv("GUI_FORMS_TEXT_IDENTITY_DUMP");
    if (directory == nullptr) return;
    const std::string path = std::string(directory) + "/" + case_names[kind] + "-" +
        std::to_string(static_cast<int>(scales[scale] * 10.0)) + ".raw";
    std::ofstream output(path, std::ios::binary);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::uint8_t* const line = bytes + static_cast<std::size_t>(row) * row_bytes;
        for (std::size_t column = 0; column < static_cast<std::size_t>(width) * 4U; ++column) {
            output.put(static_cast<char>(rgba_byte(line, column)));
        }
    }
}

} // namespace gui_forms::text_identity
