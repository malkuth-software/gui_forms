#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"

#include <optional>
#include <span>

namespace gui_forms {

[[nodiscard]] GUI_FORMS_API bool valid_surface_material(
    const SurfaceMaterial& material) noexcept;
GUI_FORMS_API void paint_surface_material(Painter& painter, Rect bounds,
                            const SurfaceMaterial& material);
[[nodiscard]] GUI_FORMS_API Insets surface_material_visual_outsets(
    const SurfaceMaterial& material) noexcept;
// Whether paint_surface_material leaves the whole of its bounds opaque,
// whatever lay beneath: a valid, unfinished material with square corners, no
// visual outsets, filter or mask, and an opaque solid fill drawn normally,
// over which everything else it draws composites.
[[nodiscard]] GUI_FORMS_API bool surface_material_opaque(
    const SurfaceMaterial& material) noexcept;
// Whether paint_surface_material draws each pixel of its bounds the same
// whatever the bounds' size: a valid, unfinished material with square
// corners, no border, shadow, visual outset, filter, mask or reflection,
// whose fills are all solid. Its pixels inside an old extent then stay
// right when the extent changes (Panel::paint_depends_on_size).
[[nodiscard]] GUI_FORMS_API bool surface_material_size_invariant(
    const SurfaceMaterial& material) noexcept;
// How far in from the bounds' right and bottom edges a size change can alter
// what paint_surface_material draws, in logical units, when nothing else
// moves: 0 for a size-invariant material; for one whose only size-dependent
// parts are square or rounded corners and a solid border of one width on
// every edge, the larger of the corner radius and the border width. Empty
// for any other material (a gradient, an image, shadows, outsets, filters,
// masks, reflections, per-edge borders), which a size change alters
// anywhere.
[[nodiscard]] GUI_FORMS_API std::optional<double> surface_material_resize_band(
    const SurfaceMaterial& material) noexcept;
// How far the outer (not inset) shadows among `shadows` reach outside the
// rect that casts them: three blur radii plus any positive spread, shifted
// by each offset.
[[nodiscard]] GUI_FORMS_API Insets box_shadow_visual_outsets(
    std::span<const MaterialShadow> shadows) noexcept;
// Whether `material` is drawn through a shape: a transform that is not the
// identity (MaterialTransform) or a clip path (MaterialClipPath).
[[nodiscard]] GUI_FORMS_API bool surface_material_shaped(
    const SurfaceMaterial& material) noexcept;
// Writes `clip` placed in a box at `bounds` into `path` (cleared first).
// `clip` must be valid, as valid_surface_material holds it.
GUI_FORMS_API void resolve_material_clip_path(const MaterialClipPath& clip, Rect bounds, PaintPath& path);
// The fill rule a clip path's rule names.
[[nodiscard]] GUI_FORMS_API PolygonFillRule material_clip_fill_rule(MaterialClipRule rule) noexcept;
// Begins drawing a material of `bounds` through its shape, when it has one
// (surface_material_shaped); otherwise does nothing. In order: a save, a
// clip to `bounds` grown by the material's visual outsets, the transform
// group of its transform about the box's centre (bounded by the same grown
// rectangle), then a save and the clip path resolved into `scratch`
// inside the group. end_surface_material_shape, given the same material,
// undoes them in reverse order; draw the material between the two. The
// painter's state is as it was once both have run. `scratch` is storage
// the caller reuses; it is only read while the clip is set.
GUI_FORMS_API void begin_surface_material_shape(Painter& painter, Rect bounds, const SurfaceMaterial& material,
                                                PaintPath& scratch);
GUI_FORMS_API void end_surface_material_shape(Painter& painter, const SurfaceMaterial& material);

} // namespace gui_forms
