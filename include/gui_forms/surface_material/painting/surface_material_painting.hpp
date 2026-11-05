#pragma once

#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <optional>
#include <span>

namespace gui_forms {

[[nodiscard]] bool valid_surface_material(
    const SurfaceMaterial& material) noexcept;
void paint_surface_material(Painter& painter, Rect bounds,
                            const SurfaceMaterial& material);
[[nodiscard]] Insets surface_material_visual_outsets(
    const SurfaceMaterial& material) noexcept;
// Whether paint_surface_material leaves the whole of its bounds opaque,
// whatever lay beneath: a valid, unfinished material with square corners, no
// visual outsets, filter or mask, and an opaque solid fill drawn normally,
// over which everything else it draws composites.
[[nodiscard]] bool surface_material_opaque(
    const SurfaceMaterial& material) noexcept;
// Whether paint_surface_material draws each pixel of its bounds the same
// whatever the bounds' size: a valid, unfinished material with square
// corners, no border, shadow, visual outset, filter, mask or reflection,
// whose fills are all solid. Its pixels inside an old extent then stay
// right when the extent changes (Panel::paint_depends_on_size).
[[nodiscard]] bool surface_material_size_invariant(
    const SurfaceMaterial& material) noexcept;
// How far in from the bounds' right and bottom edges a size change can alter
// what paint_surface_material draws, in logical units, when nothing else
// moves: 0 for a size-invariant material; for one whose only size-dependent
// parts are square or rounded corners and a solid border of one width on
// every edge, the larger of the corner radius and the border width. Empty
// for any other material (a gradient, an image, shadows, outsets, filters,
// masks, reflections, per-edge borders), which a size change alters
// anywhere.
[[nodiscard]] std::optional<double> surface_material_resize_band(
    const SurfaceMaterial& material) noexcept;
// How far the outer (not inset) shadows among `shadows` reach outside the
// rect that casts them: three blur radii plus any positive spread, shifted
// by each offset.
[[nodiscard]] Insets box_shadow_visual_outsets(
    std::span<const MaterialShadow> shadows) noexcept;

} // namespace gui_forms
