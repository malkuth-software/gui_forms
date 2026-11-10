#pragma once

#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {
// Declared in gui_forms/types/painter/painter.hpp.
enum class PaintGroupComposite : std::uint8_t;
} // namespace gui_forms

namespace gui_forms::detail {

// ADR-004 shared text blending. Every host composites text coverage through
// this one integer arithmetic, so the same coverage, colour, clip and
// destination give the same bytes on every platform and architecture.

// A target's pixels are premultiplied BGRA8 (blue, green, red, alpha in
// memory); the hosts are little-endian.
struct TextCompositeTarget final {
    std::span<std::uint32_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    // Pixels per row, at least width.
    std::size_t stride{};
    // True for surfaces whose alpha channel is not maintained (GDI DIBs):
    // the result is written fully opaque, as before ADR-004.
    bool opaque{};
};

// Clip in device pixels. Edges at fractional positions give partial coverage
// equal to the covered area of the pixel; a rounded clip's corners give
// coverage from the pixel centre's signed distance to the arc. All coverage
// is computed in 1/256-pixel fixed point.
struct TextCompositeClip final {
    Rect rect{};
    bool rounded{};
    Rect rounded_rect{};
    double radius{};
};

// Grayscale coverage, 0..255, row-major with stride == width, placed with its
// top-left pixel at (left, top) in the target's device pixels.
struct TextCoverage final {
    std::span<const std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t left{};
    std::int64_t top{};
};

enum class TextCompositeStatus : std::uint8_t { success, invalid_geometry };

// source_over blends colour (alpha included) over the destination. source
// (GDI+ SourceCopy) replaces the destination with the premultiplied colour,
// alpha included, in proportion to coverage x clip. The two agree for opaque
// colours.
enum class TextCompositeMode : std::uint8_t { source_over, source };

// Blends colour over the target with alpha = coverage * colour.alpha * clip,
// rounded once, and each channel rounded once:
//   out = (source * alpha + destination * (255 - alpha) + 127) / 255.
// Validates everything before writing; no allocation, no partial write on
// failure. Coverage and target must not overlap.
[[nodiscard]] TextCompositeStatus composite_text_coverage(TextCoverage coverage, TextCompositeTarget target,
                                                          TextCompositeClip clip, Color color,
                                                          TextCompositeMode mode = TextCompositeMode::source_over) noexcept;

// Clip coverage of one device pixel, 0..256. Exposed for tests.
[[nodiscard]] std::uint32_t text_clip_coverage(TextCompositeClip clip, std::int64_t x, std::int64_t y) noexcept;

// How composite_part_raster joins a weighted source pixel to the target
// pixel: premultiplied source-over; plus (each channel added, clamped at
// 255; PaintGroupComposite::plus); or multiply or screen
// (PaintGroupComposite, E9.6), in bytes, each product rounded once:
//   multiply: (s × (255 − αd) + d × (255 − αs) + s × d + 127) / 255,
//   screen:   s + d − (s × d + 127) / 255,
//   alpha:    αs + αd − (αs × αd + 127) / 255 (255 on an opaque target);
// or copy (PaintGroupComposite::copy, stage T5): the target replaced by the
// weighted source, transparent source pixels included, each channel
// (s × w + 32640) / 65280 + (d × (65280 − w) + 32640) / 65280, clamped at
// 255, so that full weight writes the source exactly (alpha 255 on an
// opaque target).
enum class PartCompositeBlend : std::uint8_t { source_over, plus, multiply, screen, copy };

// The same blend for the theme engine's part rasters (T2 plan, "One blend";
// GIVEN A2-2 and C1-1). A part raster is premultiplied BGRA8, memory order
// blue, green, red, alpha (PartRasterView), as is a paint group's image
// (Painter::begin_group), placed with its top-left pixel
// at (left, top) in the target's device pixels, one raster pixel per device
// pixel.
struct PartRasterSource final {
    std::span<const std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    // Bytes from one row to the next, at least width * 4.
    std::size_t row_bytes{};
    std::int64_t left{};
    std::int64_t top{};
    // The device pixels the source is shown across and down, from (left,
    // top): zero for its own width and height, one raster pixel per device
    // pixel. Another size shows it scaled (Painter::draw_part_raster_scaled,
    // E7r): each device pixel's centre is mapped onto the source and sampled
    // bilinearly, in premultiplied colour with 8-bit weights, transparent
    // beyond the source's edges.
    std::uint32_t shown_width{};
    std::uint32_t shown_height{};
    // How the weighted source joins the target (PartCompositeBlend). Copy
    // writes every target pixel within the source's extent and the clip.
    PartCompositeBlend blend{PartCompositeBlend::source_over};
};

// A part opacity in [0, 1] as the byte the blend reads: floor(o * 255 + 0.5),
// the engine's one quantization rule. NaN is 0; values outside [0, 1] are
// clamped.
[[nodiscard]] std::uint8_t part_composite_opacity(double opacity) noexcept;

// Premultiplied source-over of the raster onto the target, weighted by
// opacity (0..255) times clip coverage (0..256). With w that weight and s a
// source channel (alpha included), each channel is rounded twice:
//   s' = (s * w + 32640) / 65280
//   out = s' + (destination * (255 - alpha') + 127) / 255
// where alpha' is s' of the source alpha; an opaque target is written with
// alpha 255. The plus blend writes min(255, s' + destination) instead; copy
// writes as PartCompositeBlend states, transparent source pixels included.
// A fully opaque raster at full opacity and full coverage is copied
// exactly. The result never exceeds 255 for a premultiplied source,
// and is clamped for one that is not. Validates everything before writing;
// no allocation, no partial write on failure. Source and target must not
// overlap.
[[nodiscard]] TextCompositeStatus composite_part_raster(PartRasterSource source, TextCompositeTarget target,
                                                        TextCompositeClip clip, std::uint8_t opacity) noexcept;

// (value + 127) / 255 without a division, for value in 0..65535: the
// blends' rounding of a product of two bytes (and of a sum of two such
// products that stays within 65535). Exact over that whole range, which a
// test checks value by value.
[[nodiscard]] constexpr unsigned rounded_quotient_by_255(const unsigned value) noexcept {
    const unsigned biased = value + 128U;
    const unsigned quotient = (biased + (biased >> 8U)) >> 8U;
    return quotient;
}

// The blend a paint group's composite composites through, for the hosts'
// painters. A value outside the enumeration is source-over.
[[nodiscard]] PartCompositeBlend part_composite_blend(PaintGroupComposite composite) noexcept;

} // namespace gui_forms::detail
