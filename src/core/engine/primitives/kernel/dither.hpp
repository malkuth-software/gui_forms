#pragma once

#include "pixel_kernel.hpp"

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// Gradient dithering (MaterialDither): the material kernel's one
// quantization to 8 bits with a threshold per device pixel instead of the
// half that rounds. A pixel's channel v in [0, 1] becomes
//
//     floor(v × 255 + (rank + 1/2) / levels)
//
// where rank is the pixel's entry in the mode's table (0 to levels − 1,
// each once per period square) and levels a power of two, so the threshold
// is exact in double and lies strictly inside (0, 1). Its mean is 1/2, the
// rounding threshold, so a region's mean byte keeps the exact value's
// within 1 / levels plus the table's spatial imbalance; an exact byte value
// k / 255 stays k (the threshold never reaches 0 or 1, and the product
// k / 255 × 255 is within an ulp of k). All four channels of a pixel take
// the same threshold: the dither is grey, never coloured, and a
// premultiplied colour channel no larger than alpha stays no larger after
// quantization (floor is monotonic).
//
// - ordered: the 8 × 8 Bayer matrix (64 levels), generated in code by bit
//   interleaving; a period of 8 pixels.
// - blue_noise: a 64 × 64 void-and-cluster table (4096 levels),
//   blue_noise_ranks.inc, written in integers by tools/dither_tables.py; a
//   period of 64 pixels.
//
// Anchoring. A pixel's table cell is its device offset from the box's
// first device pixel, (x − anchor_x, y − anchor_y) modulo the period, where
// the anchor is the box's top-left corner rounded to the nearest pixel (as
// the finish kernel anchors, finish_surface). The pattern belongs to the
// surface: a raster that scrolls carries its pattern with it, so nothing
// swims against the gradient, and a region bake of the same box at the same
// scale (bake_material_region, shifted by whole pixels) gives the whole
// bake's bytes, so any repaint rectangle matches a full repaint.
//
// No libm, no clock, no host state: integer tables, an exact threshold and
// one floor, compiled without fused multiply-add (dither.cpp), so the same
// inputs give the same bytes on every host.

namespace gui_forms::engine_primitives {

// The pixels along one axis after which a mode's pattern repeats: 1 for
// none (rounding has no pattern), 8 for ordered, 64 for blue noise. Slices
// and repeated spans copy whole periods so the pattern is kept (slice plan,
// SliceAxis::period).
[[nodiscard]] std::uint32_t dither_period(MaterialDither mode) noexcept;

// The ordered table (64 ranks, row-major 8 × 8) and the blue-noise table
// (4096 ranks, row-major 64 × 64). Static storage, read-only.
[[nodiscard]] std::span<const std::uint16_t> ordered_dither_ranks() noexcept;
[[nodiscard]] std::span<const std::uint16_t> blue_noise_dither_ranks() noexcept;

// A mode's table anchored to a box, chosen once before a bake's loops.
// `ranks` is null for none: the caller stores with plain rounding.
struct DitherPlane final {
    const std::uint16_t* ranks{};
    // The period (a power of two) and period − 1, the mask of a cell's
    // coordinate.
    std::uint32_t side{1U};
    std::uint32_t mask{};
    // 1 / levels: the threshold of rank r is (r + 1/2) × step.
    double step{};
    // The box's first device pixel, in the destination's pixels.
    std::int64_t anchor_x{};
    std::int64_t anchor_y{};
};

// The plane for `mode`, anchored at the destination pixel (anchor_x,
// anchor_y). A mode outside the enumeration dithers nothing.
[[nodiscard]] DitherPlane dither_plane(MaterialDither mode, std::int64_t anchor_x, std::int64_t anchor_y) noexcept;

// The plane's cell coordinate for destination coordinate `position` along
// an axis anchored at `anchor`: (position − anchor) modulo the period,
// negative offsets included.
[[nodiscard]] std::uint32_t dither_cell(const DitherPlane& plane, std::int64_t position,
                                        std::int64_t anchor) noexcept;

// The threshold of `rank` in `plane`: (rank + 1/2) × step, in (0, 1).
[[nodiscard]] double dither_threshold(const DitherPlane& plane, std::uint16_t rank) noexcept;

// One channel quantized with `threshold`: floor(clamp(v, 0, 1) × 255 +
// threshold). With a threshold of exactly 1/2 it is to_byte.
[[nodiscard]] std::uint8_t to_byte_dithered(double value, double threshold) noexcept;

// Writes `unit` as BGRA8 at byte `offset` of `destination`, which the
// caller has validated, every channel with `threshold`.
void store_dithered(const PartRasterView& destination, std::size_t offset, const Unit& unit,
                    double threshold) noexcept;

} // namespace gui_forms::engine_primitives
