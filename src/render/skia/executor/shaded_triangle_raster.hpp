#pragma once

// The raster of GUI.Drawing's fill_triangles command (GraphicsRecorder::
// fill_triangles states the rules): shaded triangles drawn straight into a
// BGRA8 premultiplied target, as the executor's text composite is. It
// uses no Skia: the samples, the top-left ownership rule and the
// linear-light shading are GUI.Drawing's own, with the linear-light
// conversions of gui_forms/types/linear_light/linear_light.hpp, so every
// host gives the same bytes.

#include "skia_executor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_drawing::render {

// The farthest a corner may lie from the device origin, in device pixels,
// after the command's transform: corners are rounded to 1/16 pixel and the
// edge functions of a triangle's corners, summed over a pixel's 16
// samples, stay below 2^62 within it.
inline constexpr double maximum_shaded_triangle_coordinate = 8388608.0;

// The pixels a fill_triangles command draws into: `height` rows of `width`
// BGRA8 premultiplied pixels, `row_bytes` apart (a multiple of 4), owned by
// the caller and written in place.
struct ShadedTriangleTarget final {
    std::byte* pixels{};
    std::size_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// One triangle in device units (1/16 pixel), oriented so that its doubled
// signed area is positive, with what shading needs per corner.
struct ShadedTriangle final {
    // The corners, x then y, in 1/16 device pixel.
    std::array<std::int64_t, 3> x{};
    std::array<std::int64_t, 3> y{};
    // The doubled area in (1/16 pixel)^2, positive.
    std::int64_t area{};
    // Per edge (edge i lies opposite corner i): 0 when a sample on it
    // belongs to the triangle (a top or left edge), else 1, so that a
    // sample is inside when every edge function is at least its bias.
    std::array<std::int64_t, 3> bias{};
    // The bounding box of the corners, in 1/16 device pixel.
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
    // Per corner: its alpha (0 to 255) and its red, green and blue as
    // linear light (0 to 65535) times its alpha.
    std::array<std::uint64_t, 3> alpha{};
    std::array<std::uint64_t, 3> red{};
    std::array<std::uint64_t, 3> green{};
    std::array<std::uint64_t, 3> blue{};
};

// Storage reused by every fill_triangles command of one execution: the
// command's triangles, their order by top edge, the triangles crossing the
// current band of rows and, per sample of that band, the number of the
// triangle that owns it (its index plus one; 0 for none). Cleared, never
// released, between commands.
struct ShadedTriangleScratch final {
    std::vector<ShadedTriangle> triangles;
    std::vector<std::uint32_t> order;
    std::vector<std::uint32_t> active;
    std::vector<std::uint32_t> owners;
};

// The error drawing `command` (a fill_triangles command) would end with,
// found without drawing: a malformed command (counts that differ or are
// not whole triangles) is invalid_argument, and a corner that is not
// finite or lies beyond maximum_shaded_triangle_coordinate after the
// transform is dimension_limit_exceeded.
[[nodiscard]] RasterError shaded_triangles_refusal(const DrawingCommand& command) noexcept;

// Draws `command` (a fill_triangles command) into `target` under its
// state's transform, clip, smoothing and compositing. Refuses as
// shaded_triangles_refusal does, and a target wider or taller than
// maximum_shaded_triangle_coordinate is dimension_limit_exceeded; a refusal
// writes no pixel. Fills `scratch`, whose storage grows to the command's
// triangles and to one band of samples across the clipped target; throws
// std::bad_alloc when it cannot, after which the target is unchanged.
[[nodiscard]] RasterError draw_shaded_triangles(const DrawingCommand& command, ShadedTriangleTarget target,
                                                ShadedTriangleScratch& scratch);

} // namespace gui_drawing::render
