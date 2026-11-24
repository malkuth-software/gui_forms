#include "overlay_shadow_primitive.hpp"

#include "material_bake_policy.hpp"
#include "material_primitive.hpp"

#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Identical bytes on every host: no fused multiply-add.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

const PartRole overlay_shadow_roles[] = {PartRole::shadow};

// The declared cost is the most the load check admits, as the material's;
// the real bake is bounded by its effort (accepts).
const PrimitiveDescriptor overlay_shadow_descriptor{
    "overlay_shadow",
    EngineVersion{0, 2},
    overlay_shadow_roles,
    {},
    {},
    PrimitiveCost{512U, 64U, 0U},
    Insets{overlay_shadow_outset, overlay_shadow_outset, overlay_shadow_outset, overlay_shadow_outset},
    0U};

// How far an outer shadow draws outside the box along its farthest side,
// in logical units: its spread and offset, and 3.5 σ' of its blur (σ' at
// most b / 2 widened by a device pixel's box filter, which adds less than
// one device pixel), plus a device pixel for coverage at the edge.
double shadow_extent(const MaterialShadow& shadow, double scale) noexcept {
    const double offset = std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y));
    const double pixel = 1.0 / scale;
    const double blur = 3.5 * shadow.blur_radius * 0.5 + pixel;
    const double extent = std::max(0.0, shadow.spread) + offset + blur + pixel;
    return extent;
}

} // namespace

const PrimitiveDescriptor& OverlayShadowPrimitive::descriptor() const noexcept {
    return overlay_shadow_descriptor;
}

bool OverlayShadowPrimitive::lends_overlay_shadow() const noexcept {
    return true;
}

bool OverlayShadowPrimitive::accepts(const PrimitiveParameterBlock&, const PrimitiveTarget& target) const noexcept {
    if (target.material == nullptr) return false;
    const SurfaceMaterial& material = *target.material;
    if (!valid_surface_material(material)) return false;
    const double scale = target.scale.value;
    for (const MaterialShadow& shadow : material.shadows) {
        if (shadow.inset) return false;
        if (!(shadow_extent(shadow, scale) <= overlay_shadow_outset)) return false;
    }
    // A shadow assembled from slices bakes only its reference.
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil(
                                  (target.logical_size.width + overlay_shadow_outset * 2.0) * scale)),
                              static_cast<std::uint32_t>(std::ceil(
                                  (target.logical_size.height + overlay_shadow_outset * 2.0) * scale))};
    const PrimitiveSlicePlan plan = material_slice_plan(material, target, extent, overlay_shadow_outset);
    PrimitiveTarget baked = target;
    if (plan.across.outcome == SliceAxisOutcome::sliced) baked.logical_size.width = plan.reference_size.width;
    if (plan.down.outcome == SliceAxisOutcome::sliced) baked.logical_size.height = plan.reference_size.height;
    const BakeEffort effort = material_bake_effort(material, baked, overlay_shadow_outset);
    const bool within = effort <= maximum_material_bake_effort;
    return within;
}

PrimitiveSlicePlan OverlayShadowPrimitive::slice_plan(const PrimitiveParameterBlock&, const PrimitiveTarget& target,
                                                      DeviceExtent extent) const noexcept {
    if (target.material == nullptr) return PrimitiveSlicePlan{};
    const PrimitiveSlicePlan plan = material_slice_plan(*target.material, target, extent, overlay_shadow_outset);
    return plan;
}

void OverlayShadowPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&,
                                    std::span<double>) const noexcept {}

void OverlayShadowPrimitive::fill(const PrimitiveParameterBlock&, const PrimitiveTarget& target,
                                  std::span<const double>, const PartRasterView& destination) const noexcept {
    // accepts() has passed: the target lends a valid material.
    bake_material(*target.material, target, destination, overlay_shadow_outset);
}

} // namespace gui_forms::engine_primitives
