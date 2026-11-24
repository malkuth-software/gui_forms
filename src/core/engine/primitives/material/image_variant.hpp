#pragma once

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"

#include <memory>

// An image face's sized variant (owner, 2026-10-22): CONV generates
// face sizes from the reference image as a preparation step, never inside a
// bake. The engine makes each variant once, here, keeps it in its shared
// cache (EnginePartTable), and lends it to the bakes that composite it.
//
// The resampling is the internal interpolation seam's, in binary64, over
// premultiplied colour in [0, 1], each axis on its own (the face study's
// choice, tools/resampler_face_study):
//
//   - shrinking an axis: pixel-area (stxlib::conv's canon, through the
//     interpolation seam), each variant pixel the source line's average
//     over its footprint, so nothing aliases;
//   - enlarging an axis: the CONV line field evaluated at each variant
//     pixel's centre, mapped to source position (o + 0.5) × s / t − 0.5 and
//     clamped to the first and last source pixel;
//   - an axis of unchanged length is copied.
//
// The vertical axis goes first when the height shrinks, else the
// horizontal. A nine-patch's nine regions are resampled apart, each from
// its own source pixels, so its fixed corners and edges keep their pixels.
// The seam clamps each channel to [0, 1]; the colour channels are then
// clamped to alpha, keeping the colour premultiplied, and each channel is
// quantized once (to_byte).

namespace gui_forms::engine_primitives {

// The variant `request` asks for, made from `registry`'s image: null when
// the image is not in the registry at the request's source size or does not
// decode, the extent is empty or larger than a raster may be, storage
// cannot be allocated, or the seam refuses. Synchronous; it allocates its
// source, working and result storage once.
[[nodiscard]] std::shared_ptr<PartRaster> make_image_variant(const ImageRegistry& registry,
                                                             const ImageVariantRequest& request) noexcept;

} // namespace gui_forms::engine_primitives
