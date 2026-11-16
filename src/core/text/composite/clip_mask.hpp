#pragma once

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::detail {

// Path clip coverage for the shared composite (BLENDING_RULE.md section 3):
// a device-space coverage mask that composite_text_coverage and
// composite_part_raster multiply into their clip coverage
// (TextCompositeClip::mask), so text and part rasters follow a clip_path
// through the same integer blend instead of giving way to Skia draws.
//
// The rasterizer takes a path already flattened and placed in device
// pixels. Every vertex is rounded once to 1/256 of a pixel; after that all
// arithmetic is integer, so the same path gives the same mask on every
// machine. Across a row, coverage is the exact covered length in 1/256 of
// a pixel; down a pixel, the inside is sampled on clip_mask_sub_rows rows
// at the centres of equal bands (a horizontal edge at a multiple of 1/16
// of a pixel, and every edge whose crossings are linear across the pixel,
// such as a diagonal through its corners, come out exact). A pixel's
// coverage is the rounded mean of its rows, 0..256.

// Rows sampled down one pixel. The vertical precision is 1/16 of a pixel;
// raise it for a finer edge at a proportional cost per row.
inline constexpr std::int64_t clip_mask_sub_rows = 16;
// Device coordinates beyond this many pixels from the origin are refused,
// so that every product of two coordinate differences in 1/256-pixel
// fixed point stays inside 64-bit integers.
inline constexpr double clip_mask_coordinate_limit = 4'000'000.0;
// The most pixels across or down a mask.
inline constexpr std::uint32_t maximum_clip_mask_extent = 32768U;

// The device pixels a mask covers: its top-left pixel and its size. A pixel
// outside them has coverage 0.
struct ClipMaskBounds final {
    std::int64_t left{};
    std::int64_t top{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// One edge of the flattened path in 1/256-pixel fixed point, top end first;
// winding is +1 for an edge drawn downward and -1 for one drawn upward.
struct ClipMaskEdge final {
    std::int64_t top_x{};
    std::int64_t top_y{};
    std::int64_t bottom_x{};
    std::int64_t bottom_y{};
    std::int32_t winding{};
};

// Where one edge crosses one sample row, in 1/256-pixel fixed point.
struct ClipMaskCrossing final {
    std::int64_t x{};
    std::int32_t winding{};
};

// Storage the rasterizer reuses from call to call: it grows to the largest
// path and mask seen and is never shrunk. One workspace per execution
// context; a call borrows it for its duration only.
struct ClipMaskWorkspace final {
    std::vector<ClipMaskEdge> edges;
    // Indices into edges of the edges that reach the current row.
    std::vector<std::size_t> active;
    std::vector<ClipMaskCrossing> crossings;
    // Per pixel of one row, summed over its sample rows: the covered length
    // of pixels a span ends in (width + 1 entries; a span may end exactly
    // at the right edge), and the change in the count of wholly covered
    // pixels at each pixel.
    std::vector<std::int32_t> partial;
    std::vector<std::int32_t> whole_changes;
};

enum class ClipMaskStatus : std::uint8_t {
    success,
    // An unknown rule, contour ends that do not fit the points, a mask
    // larger than maximum_clip_mask_extent or than `coverage`, or a point
    // that is not finite.
    invalid_geometry,
    // A point beyond clip_mask_coordinate_limit.
    out_of_range
};

// Fills `coverage` (bounds.width * bounds.height entries, row-major with
// stride bounds.width) with the coverage, 0..256, of the device-space
// polylines `points` and `contours` (as flatten_paint_path writes them)
// filled by `rule`. An open contour is closed by a straight edge, as a fill
// closes it. Validates everything before writing; on failure `coverage` is
// left as it was. Grows `workspace` when it is too small, the only
// allocation, before anything is written: std::bad_alloc from that growth
// leaves `coverage` as it was. `points` and `coverage` cannot overlap
// (different types).
[[nodiscard]] ClipMaskStatus rasterize_clip_mask(std::span<const Point> points,
                                                 std::span<const FlattenedContour> contours, PolygonFillRule rule,
                                                 ClipMaskBounds bounds, ClipMaskWorkspace& workspace,
                                                 std::span<std::uint16_t> coverage);

// Multiplies `coverage` (the mask at `bounds`, as rasterize_clip_mask
// fills it) by an enclosing mask's coverage at the same device pixels,
// (c * e + 128) / 256 per pixel, 0 where the enclosing mask does not reach:
// the intersection of two path clips. `enclosing_bounds` and
// `enclosing` describe the enclosing mask, which must not overlap
// `coverage`. False, with nothing written, when either mask's entries are
// fewer than its bounds need.
[[nodiscard]] bool intersect_clip_mask(ClipMaskBounds enclosing_bounds, std::span<const std::uint16_t> enclosing,
                                       ClipMaskBounds bounds, std::span<std::uint16_t> coverage) noexcept;

} // namespace gui_forms::detail
