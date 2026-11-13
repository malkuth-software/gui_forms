#pragma once

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

namespace gui_forms::detail {

// Assembles `destination` from `reference` along the plan (E7r): each row is
// the reference row the plan picks, and across a sliced axis its first
// `start` and last `end` pixels are copied and the run of `period` pixels
// from `start` (one pixel, unless a dither's period) repeated between them. The plan's lengths fit both rasters (slice_plan); the two
// rasters do not overlap.
void assemble_slices(const PartRaster& reference, const PrimitiveSlicePlan& plan,
                     const PartRasterView& destination) noexcept;

} // namespace gui_forms::detail
