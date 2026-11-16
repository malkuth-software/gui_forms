// A paint group's effect stage: see paint_group_effect.hpp.

#include "gui_forms/surface_material/paint_group_effect/paint_group_effect.hpp"

#include "gui_forms/surface_material/painting/surface_material_painting.hpp"

#include <cmath>

namespace gui_forms {

bool valid_paint_group_effect(const PaintGroupEffect& effect) noexcept {
    if (!(effect.blur >= 0.0 && effect.blur <= maximum_paint_group_effect_blur)) return false;
    if (effect.masks.size() > SurfaceMaterial::maximum_mask_layers) return false;
    for (const MaterialMaskLayer& layer : effect.masks) {
        if (layer.source.kind == MaterialFillKind::image) return false;
    }
    if (effect.masks.empty() && !effect.colour_filter) return true;
    // The masks and colour stages as a material would hold them. Building
    // the probe may throw; an effect that cannot be held is not drawn
    // either.
    try {
        SurfaceMaterial probe = paint_group_effect_mask_material(effect);
        probe.colour_filter = effect.colour_filter;
        const bool valid = valid_surface_material(probe);
        return valid;
    } catch (...) {
        return false;
    }
}

double paint_group_effect_reach(const PaintGroupEffect& effect) noexcept {
    if (!(effect.blur > 0.0)) return 0.0;
    const double reach = std::ceil(1.5 * effect.blur) + 3.0;
    return reach;
}

SurfaceMaterial paint_group_effect_mask_material(const PaintGroupEffect& effect) {
    SurfaceMaterial material{};
    material.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(255, 255, 255)));
    material.masks = effect.masks;
    return material;
}

} // namespace gui_forms
