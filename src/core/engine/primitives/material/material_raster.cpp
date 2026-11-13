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
    const bool bare = material.shadows.empty() && material.glows.empty() && !material.border &&
                      !material.border_edges.top && !material.border_edges.right &&
                      !material.border_edges.bottom && !material.border_edges.left && material.keylines.empty();
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
    const bool unpainted = material.corner_radii || material.colour_filter || material.drop_shadow ||
                           material.filter_blur != 0.0 || material.mask || material.reflection;
    if (!unpainted) return std::nullopt;
    std::optional<SurfaceMaterial> painted{material};
    (*painted).corner_radii.reset();
    (*painted).colour_filter.reset();
    (*painted).drop_shadow.reset();
    (*painted).filter_blur = 0.0;
    (*painted).mask.reset();
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
    if (!sliced) return drawn;
    const std::shared_ptr<PartRaster> assembled = PartRaster::create(extent, scale);
    if (!assembled) return nullptr;
    assemble_slices(*drawn, plan, (*assembled).view());
    return assembled;
}

} // namespace gui_forms::detail
