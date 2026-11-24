#pragma once

// Rotation tier 1 (claude/geometry-1): before a part raster is drawn through
// a turning or skewing map, it is area-resampled (stxlib::conv's canon, as
// draw_image below 1x) down to the map's minimum scale, so the bilinear
// draw through the map never minifies and the turned raster does not alias.
// Private to src/render/skia; pure arithmetic, no Skia type.

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gui_forms::skia_map_scale {

// The least a unit length is stretched in any direction by the linear map
// x' = xx x + xy y, y' = yx x + yy y: its smaller singular value. Computed
// as |determinant| / largest singular value, which has no cancellation; a
// rotation by any angle gives 1 up to the rounding of its sine and cosine.
// Zero for a degenerate map or one with a value that is not finite.
[[nodiscard]] inline double minimum_map_scale(double xx, double xy, double yx, double yy) noexcept {
    const double sum = xx * xx + xy * xy + yx * yx + yy * yy;
    const double determinant = xx * yy - xy * yx;
    if (!std::isfinite(sum) || !std::isfinite(determinant) || !(sum > 0.0)) return 0.0;
    const double gap_squared = sum * sum - 4.0 * determinant * determinant;
    const double gap = std::sqrt(std::max(gap_squared, 0.0));
    const double largest = std::sqrt((sum + gap) * 0.5);
    if (!(largest > 0.0)) return 0.0;
    const double smallest = std::abs(determinant) / largest;
    return smallest;
}

// The pixel size a raster of `source` pixels is reduced to before it is
// drawn over `destination` (logical units) through a map whose minimum
// scale is `minimum_scale` (device pixels per logical unit): on each axis
// the destination's device length at that scale, rounded up, at least one
// and never more than the source's. `reduced` is false when neither axis
// shrinks (nothing to resample) or the inputs are not usable.
struct ReducedExtent final {
    DeviceExtent extent{};
    bool reduced{};
};

[[nodiscard]] inline std::uint32_t reduced_side(std::uint32_t source, double logical, double scale) noexcept {
    const double device = std::ceil(logical * scale);
    if (!(device < static_cast<double>(source))) return source;
    const double kept = std::max(device, 1.0);
    return static_cast<std::uint32_t>(kept);
}

[[nodiscard]] inline ReducedExtent reduced_part_extent(DeviceExtent source, Size destination,
                                                       double minimum_scale) noexcept {
    ReducedExtent result{};
    result.extent = source;
    const bool usable = source.width > 0U && source.height > 0U && std::isfinite(minimum_scale) &&
                        minimum_scale > 0.0 && std::isfinite(destination.width) && destination.width > 0.0 &&
                        std::isfinite(destination.height) && destination.height > 0.0;
    if (!usable) return result;
    result.extent.width = reduced_side(source.width, destination.width, minimum_scale);
    result.extent.height = reduced_side(source.height, destination.height, minimum_scale);
    result.reduced = result.extent.width < source.width || result.extent.height < source.height;
    return result;
}

} // namespace gui_forms::skia_map_scale
