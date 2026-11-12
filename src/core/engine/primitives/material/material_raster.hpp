#pragma once

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <memory>
#include <optional>

namespace gui_forms::detail {

// A material painted outside the theme engine (paint_surface_material) baked
// by the engine's material kernel (stage T4, owner 2026-11-07: one
// rasteriser), so that it has the pixels it would have as a part's face.

// How paint_surface_material draws `material`, which is valid and
// unfinished:
// - layers: as painter commands (paint_material_layers). A lone solid fill
//   with square corners, nothing else, is one fill_rect, the kernel's
//   pixels on device-aligned bounds; and an image or procedural fill needs
//   the window's resources, which the kernel reads only through the theme
//   engine.
// - kernel: baked by the material kernel.
enum class MaterialPainting : std::uint8_t { layers, kernel };
[[nodiscard]] MaterialPainting material_painting(const SurfaceMaterial& material) noexcept;

// The material the kernel bakes for paint_surface_material: `material`
// without what paint_surface_material has never drawn (its per-corner
// radii, part filters, mask and reflection; surface_material_types.hpp), so
// both draw the same material. Empty when `material` has none of them.
[[nodiscard]] std::optional<SurfaceMaterial> material_as_painted(const SurfaceMaterial& material);

// How far the raster reaches outside the box, in logical units, on every
// side: the material primitive's outset (material_outset), so the box sits
// in the raster as it does in a part's, or more where the outer shadows
// and glows reach further (surface_material_visual_outsets), rounded up.
[[nodiscard]] double material_raster_outset(const SurfaceMaterial& material) noexcept;

// A raster of `material` (MaterialPainting::kernel, as painted) over a box
// of `logical_size` at `scale`, covering the box grown by `outset` on every
// side: assembled from a shorter reference where the material's slice plan
// allows (material_slice_plan), else baked whole. A dithered material is
// baked with the strongest dither within the bake budget
// (material_affordable_dither), so a dither alone never nulls it. Null
// when the raster
// would not fit (maximum_part_raster_side), the scale is outside
// [minimum_device_scale, maximum_device_scale], or the bake would take more
// than the bake budget (maximum_material_bake_effort): the caller then
// draws the layers. A new raster, shared read-only once returned.
[[nodiscard]] std::shared_ptr<const PartRaster> bake_material_raster(const SurfaceMaterial& material,
                                                                     Size logical_size, DeviceScale scale,
                                                                     double outset);

} // namespace gui_forms::detail
