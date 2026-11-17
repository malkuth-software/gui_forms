#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <cstddef>
#include <vector>

namespace gui_forms {

// A paint group's effect stage (capability review decision 1, owner
// 2026-11-16): the filter and mask of a part surface's material, applied
// to a paint group's image (PaintGroup::effect), so that content drawn by
// many commands (a control's face and what it paints inside it) is
// filtered and masked as one, through the same kernels a material uses.
//
// In order, over the group's premultiplied image:
// 1. `colour_filter`: each pixel through the colour stages (matrix, then
//    table), as a material's colour filter is (pixel_kernel's
//    colour_staged), rounded once to bytes.
// 2. `blur`: the engine's integer three-box blur (box_blur), radius in
//    logical units as a material's filter_blur (2σ), 0 to
//    maximum_paint_group_effect_blur. The image spreads past the group's
//    bounds by up to paint_group_effect_reach logical units: the group draws
//    there too, beneath its content's own limit.
// 3. `masks`: the mask layers of a material (SurfaceMaterial::masks), laid
//    over the group's bounds as a material's masks are over its box (mask
//    positions and sizes are relative to the bounds), each pixel's alpha
//    weighted by the joined mask. Image masks are refused: a painter holds
//    no window resources. Beyond the bounds the mask is 0.
// Then the image is composited by the group's opacity and composite.
//
// A painter that cannot isolate the group, or whose mask bake exceeds the
// material bake budget, draws the group without its effect and counts a
// group refusal (Painter::begin_group).
struct GUI_FORMS_API PaintGroupEffect final {
    MaterialColourFilter colour_filter;
    double blur{};
    // Up to SurfaceMaterial::maximum_mask_layers layers, bottom first.
    std::vector<MaterialMaskLayer> masks{};

    friend bool operator==(const PaintGroupEffect& left, const PaintGroupEffect& right) noexcept {
        return left.colour_filter == right.colour_filter && left.blur == right.blur && left.masks == right.masks;
    }
    friend bool operator!=(const PaintGroupEffect& left, const PaintGroupEffect& right) noexcept {
        return !(left == right);
    }
};

// The largest effect blur, in logical units (glass's cap of 8 doubled for
// a group's larger content).
inline constexpr double maximum_paint_group_effect_blur = 16.0;

// Whether `effect` is valid: a finite blur from 0 to the cap, and colour
// stages and masks a material could hold (valid_surface_material), with no
// image mask layer.
[[nodiscard]] GUI_FORMS_API bool valid_paint_group_effect(const PaintGroupEffect& effect) noexcept;

// How far, in logical units on every side, a valid effect's image reaches
// past the group's bounds: 1.5 × blur rounded up, plus 3, or 0 without a
// blur. Three boxes of half width h reach 3h device pixels, and h, rounded
// from √(blur² × scale² + 1) / 2 − 1/2, is at most blur × scale / 2 + 1/2;
// the 3 covers that half pixel's 1.5 device pixels at any scale from
// minimum_device_scale (1/2). A control that paints an effect group grants that
// overflow and invalidates its changed content grown by it, so that damage
// covers the blur's spread.
[[nodiscard]] GUI_FORMS_API double paint_group_effect_reach(const PaintGroupEffect& effect) noexcept;

// The material whose baked alpha is `effect`'s joined mask over a box: one
// opaque white fill under `effect.masks`. Throws std::bad_alloc when it
// cannot be held.
[[nodiscard]] GUI_FORMS_API SurfaceMaterial paint_group_effect_mask_material(const PaintGroupEffect& effect);

} // namespace gui_forms
