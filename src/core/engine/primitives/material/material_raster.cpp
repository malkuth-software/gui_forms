#include "material_raster.hpp"

#include "../../part_table/slice_assembly.hpp"
#include "material_bake_policy.hpp"
#include "material_primitive.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gui_forms::detail {
namespace {

// One side of the raster: the logical length times the scale, rounded up,
// as the theme engine sizes a part's raster; zero when it does not fit.
std::uint32_t raster_side(double logical, double scale) noexcept {
    const double device = std::ceil(logical * scale);
    if (!(device > 0.0) || device > static_cast<double>(maximum_part_raster_side)) return 0U;
    const std::uint32_t side = static_cast<std::uint32_t>(device);
    return side;
}

// The material's box, `outset` inside the raster, less its largest corner
// radius on every side, in whole device pixels inward: what an opaque
// material leaves opaque (blend study, 2026-11-16). The raster checks it
// (PartRaster::record_opaque_interior), so a translucent material, a
// keyline or a finish that is not opaque there simply records none.
void record_material_interior(PartRaster& raster, const SurfaceMaterial& material, const Size logical_size,
                              const double scale, const double outset) noexcept {
    double radius = std::max(0.0, material.corner_radius);
    if (material.corner_radii.present()) {
        const MaterialCornerRadii& radii = *material.corner_radii.value();
        radius = std::max({radius, radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left});
    }
    const double left = std::ceil((outset + radius) * scale);
    const double top = std::ceil((outset + radius) * scale);
    const double right = std::floor((outset + logical_size.width - radius) * scale);
    const double bottom = std::floor((outset + logical_size.height - radius) * scale);
    const DeviceExtent extent = raster.extent();
    const bool usable = std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
                        std::isfinite(bottom) && left >= 0.0 && top >= 0.0 && right > left && bottom > top &&
                        right <= static_cast<double>(extent.width) && bottom <= static_cast<double>(extent.height);
    if (!usable) return;
    PartRasterInterior interior{};
    interior.left = static_cast<std::uint32_t>(left);
    interior.top = static_cast<std::uint32_t>(top);
    interior.right = static_cast<std::uint32_t>(right);
    interior.bottom = static_cast<std::uint32_t>(bottom);
    raster.record_opaque_interior(interior);
}

// Whether every fill of `material` is one the kernel draws from the
// material alone: solid and gradient fills, not images or patterns.
bool fills_need_no_resources(const SurfaceMaterial& material) noexcept {
    for (const MaterialFillLayer& layer : material.fills) {
        if (layer.kind == MaterialFillKind::image || layer.kind == MaterialFillKind::procedural) return false;
    }
    return true;
}

// Whether `material` is a lone solid fill with square corners and nothing
// over or around it: one fill_rect.
bool lone_square_fill(const SurfaceMaterial& material) noexcept {
    const bool lone = material.fills.size() == 1U && material.fills[0].kind == MaterialFillKind::solid &&
                      material.fills[0].blend == MaterialBlendMode::normal;
    const bool bare = material.shadows.empty() && material.glows.empty() && material.border.absent() &&
                      material.border_edges.top.absent() && material.border_edges.right.absent() &&
                      material.border_edges.bottom.absent() && material.border_edges.left.absent() &&
                          material.keylines.empty();
    const bool square = material.corner_radius == 0.0;
    return lone && bare && square;
}

} // namespace

MaterialPainting material_painting(const SurfaceMaterial& material) noexcept {
    if (!fills_need_no_resources(material)) return MaterialPainting::layers;
    if (lone_square_fill(material)) return MaterialPainting::layers;
    return MaterialPainting::kernel;
}

std::optional<SurfaceMaterial> material_as_painted(const SurfaceMaterial& material) {
    const bool unpainted = material.corner_radii.present() || material.colour_filter ||
                           material.drop_shadow.present() || material.filter_blur != 0.0 || !material.masks.empty() ||
                           material.reflection.present();
    if (!unpainted) return std::nullopt;
    std::optional<SurfaceMaterial> painted{material};
    (*painted).corner_radii.reset();
    (*painted).colour_filter.reset();
    (*painted).drop_shadow.reset();
    (*painted).filter_blur = 0.0;
    (*painted).masks.clear();
    (*painted).reflection.reset();
    return painted;
}

double material_raster_outset(const SurfaceMaterial& material) noexcept {
    const Insets reach = surface_material_visual_outsets(material);
    const double farthest = std::max({reach.left, reach.top, reach.right, reach.bottom});
    const double outset = std::max(engine_primitives::material_outset, std::ceil(farthest));
    return outset;
}

std::shared_ptr<const PartRaster> bake_material_raster(const SurfaceMaterial& authored, Size logical_size,
                                                       DeviceScale scale, double outset) {
    const double value = scale.value;
    if (!(value >= minimum_device_scale && value <= maximum_device_scale)) return nullptr;
    if (!(outset >= 0.0) || !std::isfinite(outset)) return nullptr;
    const double grown = outset * 2.0;
    const DeviceExtent extent{raster_side(logical_size.width + grown, value),
                              raster_side(logical_size.height + grown, value)};
    if (extent.width == 0U || extent.height == 0U) return nullptr;
    PrimitiveTarget target{};
    target.logical_size = logical_size;
    target.scale = scale;
    // A dither gives way before the material does (as the engine lends a
    // part's, EnginePrimitive::affordable_dither): baked with the strongest
    // dither within the budget, so the raster keeps its material. Copied
    // only when the dither is lowered.
    const MaterialDither affordable =
        engine_primitives::material_affordable_dither(authored, target, extent, outset);
    std::optional<SurfaceMaterial> lowered{};
    if (affordable != authored.dither) {
        lowered.emplace(authored);
        (*lowered).dither = affordable;
    }
    const SurfaceMaterial& material = lowered ? *lowered : authored;
    // A part assembled from slices bakes only its reference, and the budget
    // bounds that bake (as MaterialPrimitive::accepts does).
    const PrimitiveSlicePlan plan = engine_primitives::material_slice_plan(material, target, extent, outset);
    const bool sliced =
        plan.across.outcome == SliceAxisOutcome::sliced || plan.down.outcome == SliceAxisOutcome::sliced;
    PrimitiveTarget baked = target;
    DeviceExtent baked_extent = extent;
    if (sliced) {
        baked.logical_size = plan.reference_size;
        baked_extent = plan.reference_extent;
    }
    const engine_primitives::BakeEffort effort = engine_primitives::material_bake_effort(material, baked, outset);
    if (effort > engine_primitives::maximum_material_bake_effort) return nullptr;
    const std::shared_ptr<PartRaster> drawn = PartRaster::create(baked_extent, scale);
    if (!drawn) return nullptr;
    engine_primitives::bake_material(material, baked, (*drawn).view(), outset);
    if (!sliced) {
        record_material_interior(*drawn, material, logical_size, value, outset);
        return drawn;
    }
    const std::shared_ptr<PartRaster> assembled = PartRaster::create(extent, scale);
    if (!assembled) return nullptr;
    assemble_slices(*drawn, plan, (*assembled).view());
    record_material_interior(*assembled, material, logical_size, value, outset);
    return assembled;
}

} // namespace gui_forms::detail
