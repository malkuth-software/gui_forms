#include "glass_primitive.hpp"

#include "../kernel/box_blur.hpp"
#include "../kernel/pixel_kernel.hpp"
#include "../material/material_bake_policy.hpp"
#include "../material/material_primitive.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

const PartRole glass_roles[] = {PartRole::region, PartRole::face, PartRole::well, PartRole::plot};

constexpr std::size_t glass_blur = 0U;
constexpr std::size_t glass_tint = 1U;
constexpr std::size_t glass_corner = 2U;
constexpr std::size_t glass_recipe = 3U;
constexpr std::size_t glass_layers = 4U;

// `none`, then the theme's recipe roles in enum order (E9.6): the recipe
// whose material's colour filter glass applies to its backdrop.
constexpr std::array<std::string_view, 17> glass_recipe_names{
    "none", "window", "panel", "card", "button", "accent_button", "command_button",
    "choice", "editor", "menu_item", "selection", "progress", "tool_tip",
    "link", "visited_link", "notice", "badge"};
static_assert(glass_recipe_names.size() == static_cast<std::size_t>(ControlVisualRole::count) + 1U,
              "none, then one choice per recipe role");

// What glass draws of its recipe's material (the glass recipe): its colour
// filter alone, under the tint (`filter`, the default), or the material's
// layers in place of the tint (`material`).
constexpr std::array<std::string_view, 2> glass_layer_names{"filter", "material"};

const PrimitiveParameterDeclaration glass_parameters[] = {
    {"blur", PrimitiveParameterKind::number, 0.0, 8.0, 6.0, {}},
    {"tint", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 64}},
    {"corner", PrimitiveParameterKind::number, 0.0, 64.0, 0.0, {}},
    {"recipe", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, glass_recipe_names},
    {"layers", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, glass_layer_names},
};

const PrimitiveDescriptor glass_descriptor{
    "glass", EngineVersion{0, 2}, glass_roles, {}, glass_parameters, PrimitiveCost{48U, 64U, 0U}, Insets{}, 0U};

constexpr std::size_t lanes = box_blur_lanes;

// The blurred backplane under the destination: its pixels from the start
// of `pixels` (the work's first image), rows `stride` elements apart.
struct Blurred final {
    std::span<const std::uint32_t> pixels{};
    std::size_t stride{};
};

// Blurs `backplane` (the destination grown by 3 × half on every side)
// through `work` (prepared for the backplane's pixels, four lanes), and
// says where the result lies.
Blurred blur_backplane(const PartRaster& backplane, std::uint32_t half, BoxBlurWork& work) noexcept {
    const std::span<const std::uint8_t> bytes = backplane.pixels();
    const std::size_t columns = backplane.extent().width;
    const std::size_t rows = backplane.extent().height;
    const std::size_t stride = columns * lanes;
    for (std::size_t row = 0U; row < rows; ++row) {
        const std::size_t read = row * backplane.row_bytes();
        const std::size_t write = row * stride;
        for (std::size_t element = 0U; element < stride; ++element) {
            work.first[write + element] = bytes[read + element];
        }
    }
    blur_three_boxes(work, columns, rows, lanes, half);
    return Blurred{work.first, stride};
}

double unit_lane(std::uint32_t value) noexcept {
    const double unit = static_cast<double>(value) / 255.0;
    return unit;
}

// The backplane's own pixel under destination pixel (column, row), unblurred.
Unit backplane_pixel(const PartRaster& backplane, std::size_t margin, std::size_t column, std::size_t row) noexcept {
    const std::span<const std::uint8_t> bytes = backplane.pixels();
    const std::size_t at = (row + margin) * backplane.row_bytes() + (column + margin) * lanes;
    const Unit pixel{unit_lane(bytes[at + 2U]), unit_lane(bytes[at + 1U]), unit_lane(bytes[at]),
                     unit_lane(bytes[at + 3U])};
    return pixel;
}

// A flattened pixel made opaque: its colour unpremultiplied at alpha 1.
// The backplane under flattened glass is the window recipe's material,
// which is opaque in every built-in theme; this keeps the promise for a
// theme whose window material is not. A pixel with no alpha left is black.
Unit opaque_of(Unit pixel) noexcept {
    if (!(pixel.alpha > 0.0)) return Unit{0.0, 0.0, 0.0, 1.0};
    const Unit opaque{pixel.red / pixel.alpha, pixel.green / pixel.alpha, pixel.blue / pixel.alpha, 1.0};
    return opaque;
}

// Whether `parameters` draw the recipe's material in place of the tint: the
// `material` layers with a recipe named.
bool draws_material(const PrimitiveParameterBlock& parameters) noexcept {
    const bool named = parameters.values[glass_recipe].number >= 1.0;
    const bool layers = parameters.values[glass_layers].number >= 1.0;
    return named && layers;
}

// The layers glass draws of `authored` over its backdrop: those inside the
// part's box. Glass has no outsets, so the outer shadows, the under glows,
// the drop shadow and the reflection, which lie past the box, are left out;
// so are the colour filter, which glass applies to the backdrop instead,
// and the filter blur, which would blur the material and not the backdrop.
void inside_layers(const SurfaceMaterial& authored, SurfaceMaterial& inside) {
    inside = authored;
    inside.shadows.clear();
    for (const MaterialShadow& shadow : authored.shadows) {
        if (shadow.inset) inside.shadows.push_back(shadow);
    }
    inside.glows.clear();
    for (const MaterialGlow& glow : authored.glows) {
        if (glow.placement == MaterialGlowPlacement::over) inside.glows.push_back(glow);
    }
    inside.colour_filter = MaterialColourFilter{};
    inside.drop_shadow.reset();
    inside.filter_blur = 0.0;
    inside.reflection.reset();
}

// A fill glass's material layers do not draw (they need image variants or a
// generated pattern the engine lends only to the material primitive, or,
// for a finish, the material bake's own painting).
bool lent_only_to_materials(const MaterialFillLayer& layer) noexcept {
    const bool lent = layer.kind == MaterialFillKind::image || layer.kind == MaterialFillKind::procedural ||
                      layer.kind == MaterialFillKind::finish;
    return lent;
}

// The pixel at byte `at` of `view`, premultiplied.
Unit view_pixel(const PartRasterView& view, std::size_t at) noexcept {
    const Unit pixel{unit_lane(view.pixels[at + 2U]), unit_lane(view.pixels[at + 1U]), unit_lane(view.pixels[at]),
                     unit_lane(view.pixels[at + 3U])};
    return pixel;
}

} // namespace

std::uint32_t glass_box_half(double blur, double scale) noexcept {
    const std::uint32_t half = box_blur_half(blur, scale);
    return half;
}

const PrimitiveDescriptor& GlassPrimitive::descriptor() const noexcept {
    return glass_descriptor;
}

PartShape GlassPrimitive::shape(const PrimitiveParameterBlock& parameters) const noexcept {
    PartShape rounded{};
    rounded.kind = PartShapeKind::rounded_rectangle;
    rounded.corner_radius = parameters.values[glass_corner].number;
    return rounded;
}

// Glass names its recipe or none; it never reads the part's family.
std::optional<ControlVisualRole> GlassPrimitive::theme_material(const PrimitiveParameterBlock& parameters,
                                                                ControlVisualRole) const noexcept {
    const double position = parameters.values[glass_recipe].number;
    if (!(position >= 1.0)) return std::nullopt;
    const ControlVisualRole role = static_cast<ControlVisualRole>(static_cast<std::uint8_t>(position - 1.0));
    return role;
}

std::optional<std::uint32_t> GlassPrimitive::backplane_margin(const PrimitiveParameterBlock& parameters,
                                                              DeviceScale scale) const noexcept {
    const std::uint32_t half = glass_box_half(parameters.values[glass_blur].number, scale.value);
    const std::uint32_t margin = half * 3U;
    return margin;
}

// With the material layers, the recipe's material must be lent and valid,
// hold no image or procedural fill or image mask, and bake within the
// material primitive's bound (material_bake_policy.hpp); otherwise the part
// falls back to the built-in recipe, as the material primitive's would.
bool GlassPrimitive::accepts(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target) const noexcept {
    if (!draws_material(parameters)) return true;
    if (target.material == nullptr) return false;
    const SurfaceMaterial& material = *target.material;
    if (!valid_surface_material(material)) return false;
    for (const MaterialFillLayer& layer : material.fills) {
        if (lent_only_to_materials(layer)) return false;
    }
    if (material.mask && lent_only_to_materials(*material.mask)) return false;
    // The authored material bounds the layers drawn, which are fewer.
    const BakeEffort effort = material_bake_effort(material, target, 0.0);
    const bool within = effort <= maximum_material_bake_effort;
    return within;
}

void GlassPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void GlassPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                          std::span<const double>, const PartRasterView& destination) const noexcept {
    const double scale = target.scale.value;
    const std::size_t width = destination.extent.width;
    const std::size_t height = destination.extent.height;
    const Color tint = parameters.values[glass_tint].colour;
    const SurfaceMaterial* const lent = target.material;
    // The material's layers replace the tint (the glass recipe); accepts()
    // has checked the material.
    const bool material_layers = draws_material(parameters) && lent != nullptr;
    PartShape shape = GlassPrimitive::shape(parameters);
    // With the material's layers the backdrop takes the material's corner,
    // as its fills do.
    if (material_layers) shape.corner_radius = (*lent).corner_radius;
    if (target.corner_radius) shape.corner_radius = std::max(0.0, *target.corner_radius);
    const Rect box{0.0, 0.0, target.logical_size.width, target.logical_size.height};

    // The blurred backplane, when one is lent at the margin this blur reads.
    // Under reduced transparency (stage T5, PLAN.md §4.5) glass draws
    // flattened instead: its tint over the backplane's own pixel, unblurred,
    // and opaque.
    const std::uint32_t half = glass_box_half(parameters.values[glass_blur].number, scale);
    const std::size_t margin = static_cast<std::size_t>(half) * 3U;
    const bool flattened = target.reduced_transparency;
    bool fits = false;
    if (target.backplane != nullptr && target.backplane_margin == margin) {
        const PartRaster& backplane = *target.backplane;
        fits = backplane.extent().width == width + margin * 2U && backplane.extent().height == height + margin * 2U;
    }
    BoxBlurWork work{};
    Blurred blurred{};
    bool sampled = false;
    if (fits && !flattened) {
        try {
            prepare_box_blur(work, (*target.backplane).extent().width, (*target.backplane).extent().height, lanes);
            blurred = blur_backplane(*target.backplane, half, work);
            sampled = true;
        } catch (const std::exception&) {
            // std::bad_alloc or std::length_error: no backdrop is sampled.
            sampled = false;
        }
    }
    const bool flattened_over_backplane = flattened && fits;

    // The material's layers, baked once into a raster of the destination's
    // extent and composited over the backdrop pixel by pixel. Without the
    // storage, the backdrop alone is drawn.
    std::vector<std::uint8_t> layer_pixels{};
    PartRasterView layers{};
    bool layered = false;
    if (material_layers) {
        try {
            SurfaceMaterial inside{};
            inside_layers(*lent, inside);
            const std::size_t row_bytes = width * part_raster_bytes_per_pixel;
            layer_pixels.assign(row_bytes * height, std::uint8_t{0});
            layers = PartRasterView{std::span<std::uint8_t>(layer_pixels), destination.extent, row_bytes};
            bake_material(inside, target, layers, 0.0);
            layered = true;
        } catch (const std::exception&) {
            // std::bad_alloc or std::length_error.
            layered = false;
        }
    }

    // The backdrop's colour filter, from the recipe's material (E9.6).
    const bool filtered = lent != nullptr && (*lent).colour_filter.has_value();
    const MaterialColourMatrix filter = filtered ? *(*lent).colour_filter : MaterialColourMatrix{};
    const Unit over = premultiply(tint, 1.0);
    for (std::size_t row = 0U; row < height; ++row) {
        const std::size_t start = row * destination.row_bytes;
        for (std::size_t column = 0U; column < width; ++column) {
            Unit pixel{};
            if (sampled) {
                const std::size_t at = row * blurred.stride + column * lanes;
                pixel = Unit{unit_lane(blurred.pixels[at + 2U]), unit_lane(blurred.pixels[at + 1U]),
                             unit_lane(blurred.pixels[at]), unit_lane(blurred.pixels[at + 3U])};
                if (filtered) pixel = colour_filtered(filter, pixel);
            }
            if (flattened_over_backplane) {
                pixel = backplane_pixel(*target.backplane, margin, column, row);
                if (filtered) pixel = colour_filtered(filter, pixel);
            }
            if (!material_layers) blend_over(pixel, over);
            if (flattened) pixel = opaque_of(pixel);
            const double coverage = shape_coverage(shape, box, scale, static_cast<std::int64_t>(column),
                                                   static_cast<std::int64_t>(row));
            Unit covered{pixel.red * coverage, pixel.green * coverage, pixel.blue * coverage,
                         pixel.alpha * coverage};
            if (layered) blend_over(covered, view_pixel(layers, row * layers.row_bytes + column * lanes));
            store(destination, start + column * part_raster_bytes_per_pixel, covered);
        }
    }
}

} // namespace gui_forms::engine_primitives
