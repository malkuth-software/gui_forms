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
// on Windows x64 (clang 22.1.8), 2026-11-02 (SK5). Re-recorded 2026-11-16 for
// linear light and STXlib's glyph engine (macOS arm64 and Linux agree).
inline constexpr std::array<std::array<std::uint64_t, 3>, 5> expected{{
    {0xca59944b23da4969ULL, 0x4e4c3c7667a6adadULL, 0xd633022fc72f9becULL},
    {0x2d698dee7a545a9eULL, 0x0c295c29408d56a8ULL, 0xe3e5923d4c3e1790ULL},
    {0xde9b0ac8f06ada18ULL, 0xd3886fe48a03a201ULL, 0x4e508ef96ea3de7eULL},
    {0xf7c0e368ed01a5e5ULL, 0x05a9d2c2ae59ccd6ULL, 0x797b030fe335370dULL},
    {0xca95b97d51d4b3f5ULL, 0xa996cf71e56f58f6ULL, 0x1e9ddcfb2a0a7aa7ULL},
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
