#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace gui_forms::engine_primitives {

// Glass (T2 plan, phase E9; design language §4.6): the window's style
// backplane behind the part, blurred, under a tint, within the part's
// rounded shape. It samples the backplane alone, never the control plane
// (§4.1 rule 3): the engine lends the backplane image (E9-B, owner
// 2026-10-22; PrimitiveTarget::backplane), and without one the glass is the
// tint alone.
//
// The blur. Three box filters along each axis, in integers, approximate a
// Gaussian of σ = blur × scale / 2 device pixels: each box is w = 2h + 1
// pixels wide, w the odd integer nearest √(4σ² + 1), so that the three
// boxes' variance 3(w² − 1)/12 is closest to σ². The passes read h pixels
// past each side, 3h in all, which is the backplane margin the glass asks
// for (EnginePrimitive::backplane_margin); every output pixel therefore
// averages a full window and no edge is extended. Each pass rounds its
// average once, (sum + h) / w; premultiplied colour stays within its alpha,
// since a sum of colours never exceeds the sum of their alphas. The cost
// per pixel does not depend on the radius: a running sum adds one pixel and
// drops one.
//
// Parameters: `blur`, the radius in logical units, 0 to 8 (default 6; the
// cap is §4.6's 8 logical px, and a rule past it is rejected and falls
// back); `tint`, composited over the blurred backplane (default white at
// alpha 64); `corner`, the shape's corner radius in logical units (0 to
// 64, default 0), replaced by the part's own when the part sets one;
// `recipe` (E9.6), `none` (the default) or a recipe role, whose material's
// colour filter (SurfaceMaterial::colour_filter) glass applies to the
// blurred backplane before the tint: CSS `backdrop-filter` colour
// functions, composed into one matrix in source order (coordinator,
// 2026-10-22); `layers` (the glass recipe, T4's phase 7 gap 1), `filter`
// (the default: the colour filter alone, under the tint) or `material`:
// with a recipe named, the material's fills (gradients included), inset
// shadows, over glows, border, single edges and keylines are drawn over
// the filtered backdrop in place of the tint, and the backdrop takes the
// material's corner radius (the part's own when it sets one). Glass has no
// outsets, so what lies past the box (outer shadows, under glows, the drop
// shadow, the reflection) is not drawn, and the material's filter blur is
// not applied. With `material`, glass refuses (accepts) a material that is
// not lent or not valid, holds an image or procedural fill or an image
// mask, or would bake past the material primitive's bound. Under reduced
// transparency the flattened backdrop is opaque, so the material over it
// is too. It reads no states, no inputs and no time.
class GlassPrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;
    [[nodiscard]] PartShape shape(const PrimitiveParameterBlock& parameters) const noexcept override;
    [[nodiscard]] std::optional<ControlVisualRole> theme_material(const PrimitiveParameterBlock& parameters,
                                                                  ControlVisualRole family) const noexcept override;
    [[nodiscard]] std::optional<std::uint32_t> backplane_margin(const PrimitiveParameterBlock& parameters,
                                                                DeviceScale scale) const noexcept override;
    [[nodiscard]] bool accepts(const PrimitiveParameterBlock& parameters,
                               const PrimitiveTarget& target) const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

// The half width h of glass's box filter for a blur of `blur` logical units
// at display `scale` (see GlassPrimitive): 0 for no blur.
[[nodiscard]] std::uint32_t glass_box_half(double blur, double scale) noexcept;

} // namespace gui_forms::engine_primitives
