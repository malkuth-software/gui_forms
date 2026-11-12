#pragma once

#include "finish_kernel.hpp"

#include <cstddef>
#include <cstdint>

// A surface finish over an unbounded world (cp31, the node canvas's finish
// texture): the finish kernel (finish_kernel.hpp) evaluated at world
// device pixels, so that any rectangle of the world, painted on its own,
// holds exactly the bytes a larger paint holds there. Tiles painted this
// way join with no seam: the kernel reads only a pixel's position, the
// scale and the fill, and the slopes at a tile's last row and column read
// the next world pixel, which is the neighbouring tile's first.

namespace gui_forms::engine_primitives {

// How far the world reaches from its origin, in device pixels, on every
// side: world pixels in [-finish_world_reach, finish_world_reach) are
// painted, others left as they are. The kernel's surface begins at
// -finish_world_reach, so its positions stay non-negative and below 2^21:
// the range its noise is defined and checked over.
inline constexpr std::int64_t finish_world_reach = std::int64_t{1} << 20;

// Paints `destination`, whose pixel (0, 0) is world device pixel
// (`origin_x`, `origin_y`), with `fill` at `scale` device pixels per world
// unit. Returns the pixels written: none when the destination lies outside
// the world's reach, or when `scratch` holds fewer samples than the
// destination's width plus one (reserve_finish_scratch). The fill is valid
// (valid_surface_material) and the destination validated by the caller.
std::size_t paint_world_finish(const FinishFill& fill, double scale, std::int64_t origin_x, std::int64_t origin_y,
                               const PartRasterView& destination, FinishScratch& scratch) noexcept;

} // namespace gui_forms::engine_primitives
