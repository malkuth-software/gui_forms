#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "material_bake_policy.hpp"

#include <span>

namespace gui_forms::engine_primitives {

// The material primitive (T2 plan, phase E8; owner decision E8-1 (a)): a
// deterministic engine kernel that rasterizes the theme's SurfaceMaterial
// for one of its recipe roles. Its `recipe` choice names the role
// (ControlVisualRole, in enum order: window, panel, card, button,
// accent_button, command_button, choice, editor, menu_item, selection,
// progress, tool_tip, link, visited_link, notice, badge), or `family`, the part's own
// family's and the default; the engine resolves that role's recipe for the
// part's state and lends the recipe's material in the target. The kernel
// draws the material's layers in the order paint_surface_material does:
//
// 1. outer shadows, under the box;
// 2. fills, clipped to the box (rounded by the material's corner radius, or
//    the part's own when it sets one): solid; linear (endpoints, or a CSS
//    angle through engine_sine_cosine), padded, repeated or reflected;
//    radial, padded; images (see material_image.hpp) and procedural
//    patterns (E10, procedural_pattern.hpp: a tile repeated from the box's
//    first device pixel, or the fresco over the box; the fill's ground
//    until the engine lends the pattern), at their opacity;
// 3. inset shadows, inside the box;
// 4. the border, a ring inside the box's edge;
// 5. the border's single edges;
// 6. keylines, clipped to the box when it is rounded.
//
// Every layer is anti-aliased by coverage in device pixels and blended
// premultiplied in double; each pixel is quantized once. Gradient colours
// are interpolated in straight colour, then premultiplied. A shadow of blur
// b at scale s is a Gaussian of standard deviation σ = b / 2 × s device
// pixels, widened by the pixel's own box filter, σ' = sqrt(σ² + 1/12); its
// coverage is the smaller of the Gaussian integral over the shadow's
// rectangle and the integral across its rounded edge, Φ(−d / σ'), where d is
// the pixel centre's signed distance to the rounded shadow shape and Φ the
// polynomial normal integral of gaussian_integral (error at most 4e-4).
// The rectangle's integral is separable, so an outer shadow evaluates it
// once per column and once per row. The rounded edge's integral is taken
// only within the shape's corner squares and outward from them, where a
// centre lies within the radius of both axes' ends or beyond them:
// elsewhere the rounded shape is its rectangle, and a blurred rectangle
// never exceeds the integral across its nearest edge (E9).
//
// Outsets. The primitive draws up to 8 logical units outside the part's box
// on every side (its descriptor's outsets): the destination covers the box
// grown by them, and the box starts 8 × s device pixels in. An outer shadow
// that reaches further is clipped there: the rest of it is not drawn.
//
// The hit region is the part's box, not the rounded shape: the corner
// radius is the theme material's, which the primitive's parameters do not
// carry. No text slot either (content_insets is none).
//
// Refusals (accepts). The primitive draws nothing, and the engine draws the
// part with the built-in recipe and counts the refusal, when the target
// lends no material, the material is not valid, an image fill's image is
// not in the window's resources with its declared pixel size, a
// procedural fill's pattern would take more effort to generate than that
// budget or hold more than maximum_procedural_pattern_bytes, or the
// material's bake effort exceeds the budget of material_bake_policy.hpp.
// An image fill whose pixels cannot be decoded or stored at bake time is
// drawn as nothing; the other layers are drawn.
// How far the material primitive draws outside the part's box, in logical
// units, on every side: room for the outer shadows, which are clipped
// beyond it. The kernel functions below take the outset they draw within,
// so that the overlay shadow (overlay_shadow_primitive.hpp) can reach
// further.
inline constexpr double material_outset = 8.0;

class MaterialPrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;
    [[nodiscard]] std::optional<ControlVisualRole> theme_material(const PrimitiveParameterBlock& parameters,
                                                                  ControlVisualRole family) const noexcept override;
    [[nodiscard]] PartShape shape(const PrimitiveParameterBlock& parameters) const noexcept override;
    [[nodiscard]] bool accepts(const PrimitiveParameterBlock& parameters,
                               const PrimitiveTarget& target) const noexcept override;
    // One variant per image fill, of the material's box (material_image.hpp).
    [[nodiscard]] std::size_t image_variants(const PrimitiveParameterBlock& parameters,
                                             const PrimitiveTarget& target,
                                             std::span<ImageVariantRequest> requests) const noexcept override;
    [[nodiscard]] PrimitiveSlicePlan slice_plan(const PrimitiveParameterBlock& parameters,
                                                const PrimitiveTarget& target,
                                                DeviceExtent extent) const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

// The recipe roles' names, the `recipe` choices, in ControlVisualRole order.
[[nodiscard]] std::span<const std::string_view> material_recipe_names() noexcept;

// The bake effort of `material` for `target` (material_bake_policy.hpp),
// into a destination of the box grown by `outset` logical units on every
// side, drawn with the material primitive's `shape` (E9.6). Every other
// caller draws the material's rounded rectangle, the default.
[[nodiscard]] BakeEffort material_bake_effort(const SurfaceMaterial& material, const PrimitiveTarget& target,
                                              double outset = material_outset,
                                              PartShapeKind shape = PartShapeKind::rounded_rectangle) noexcept;

// How a raster of `extent` for `target` is assembled from slices (E7r): an
// axis is sliced unless a gradient runs along it (varies), the box does
// not start and end on device pixels (fractional), or the part is no
// longer than its reference (too_small). A radial gradient or an image
// fill varies along both axes. The corners reach as far in as any layer's
// edge changes: the box's arcs, the border's inner edge, the keylines and
// single edges, and each shadow's offset, spread, arc and 3.5 σ', where Φ
// is exactly 0 or 1.
[[nodiscard]] PrimitiveSlicePlan material_slice_plan(const SurfaceMaterial& material, const PrimitiveTarget& target,
                                                     DeviceExtent extent, double outset = material_outset,
                                                     PartShapeKind shape = PartShapeKind::rounded_rectangle) noexcept;

// The kernel alone: draws `material` for `target` into every pixel of
// `destination`, without accepts()'s checks or bound. The caller has
// validated the destination (validate_raster_view), the target and the
// material (valid_surface_material); target.material is not read. The box
// starts `outset` logical units in. The bake measurement uses it to time
// materials past the bound. Where the look does not vary across (or down)
// its interior, it bakes the pixels near the box's edges and one interior
// column (or row) and copies it over the rest, to the same bytes;
// bake_material_region bakes every pixel and is its reference.
void bake_material(const SurfaceMaterial& material, const PrimitiveTarget& target,
                   const PartRasterView& destination, double outset = material_outset,
                   PartShapeKind shape = PartShapeKind::rounded_rectangle) noexcept;

// The kernel over one region of a box: `material` for a box of
// target.logical_size at the target's scale whose top-left corner is the
// device origin, drawn into `destination`, whose top-left pixel lies at
// `device_origin` (device pixels, possibly fractional). The window's
// backplane image (E9-B) bakes the window's material this way, a part's
// region of it at a time. The same preconditions as bake_material. Of the
// part filters (E9.6) it draws the colour filter, which is per pixel; the
// drop shadow and the blur read the whole raster, so it draws neither.
void bake_material_region(const SurfaceMaterial& material, const PrimitiveTarget& target, Point device_origin,
                          const PartRasterView& destination) noexcept;

// Φ, the standard normal integral, by a polynomial: deterministic, absolute
// error at most 4e-4, exactly 0 and 1 from ±3.5 on.
[[nodiscard]] double gaussian_integral(double u) noexcept;

} // namespace gui_forms::engine_primitives
