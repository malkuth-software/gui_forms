#pragma once

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/surface_material/procedural_fill/procedural_fill.hpp"
#include "gui_forms/types.hpp"
#include "../material/material_bake_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// The procedural patterns' generators (T2 plan, E10). A procedural fill is
// generated per display scale, never inside a paint: the engine queues its
// pattern when a bake first needs it, generates it in an idle frame, keeps
// it in its shared cache, and lends it to the bakes that composite it, as
// it does an image's sized variant (image_variant.hpp). Until it is
// ready, the fill draws its ground as a solid.
//
// Identical bytes on every host. The generators read only integers: each
// lattice value is a 32-bit hash of the seed and the cell (lowbias32,
// below), positions are 16.16 fixed point, and every blend is an integer
// weight in [0, 65536]. The doubles in a fill (tile, strength) are read
// once each, before the pixel loops, by one IEEE operation and a
// conversion, which every host rounds alike. No libm function is called.
//
// Per display scale, not resampled. A tiled pattern's tile is
// round(tile × scale) device pixels square, and its lattices have a whole
// number of cells across it, each a fixed number of logical units, so a
// pattern has the same features in logical units at every scale and each
// scale's tile is made from the lattice, not from another scale's pixels.
// Every lattice wraps at the tile's edges, so the tile repeats seamlessly.
// The fresco is made at the box's own device size instead.

namespace gui_forms::engine_primitives {

// The device extent `fill`'s pattern is generated at, for a box of `box`
// device pixels at `scale`: a tile's round(tile × scale) square (at least
// one pixel), or for the fresco the box's lengths rounded, at least one.
[[nodiscard]] DeviceExtent procedural_extent(const ProceduralFill& fill, Rect box, double scale) noexcept;

// The generators' effort weights, in effort units per generated device
// pixel (material_bake_policy.hpp): dimensionless, fixed in code, on the
// material weights' scale. Their relative sizes come from
// gui_forms_engine_procedural_tests --measure: the median of nine
// generations of a 256 × 256 pattern at scale 1 (a 256-unit tile; the
// fresco over a 256 × 256 box), per pixel; each the median of seven runs,
// rounded up and scaled by five, as the material layers' are
// (2026-10-22, with other builds running; the seven runs agreed within
// 3 % but for one felt run). The runs: felt 28.30-32.71, wood grain
// 13.21-13.75, paper fibre 66.57-68.74, pinstripe 0.69-0.70, brushed
// metal 23.29-23.81, fresco 8.56-8.73.
struct ProceduralEffortWeights final {
    std::uint32_t felt{};
    std::uint32_t wood_grain{};
    std::uint32_t paper_fibre{};
    std::uint32_t pinstripe{};
    std::uint32_t brushed_metal{};
    std::uint32_t fresco{};
    std::uint32_t checker{};
};

inline constexpr ProceduralEffortWeights procedural_effort_weights{
    .felt = 145U,
    .wood_grain = 70U,
    .paper_fibre = 335U,
    .pinstripe = 5U,
    .brushed_metal = 120U,
    .fresco = 45U,
    .checker = 5U,
};

// The effort of generating `fill`'s pattern at `extent`. A generation is
// an idle frame's whole work, so it is held to the bake budget
// (maximum_material_bake_effort): a material with a fill whose generation
// would exceed it is refused, and the part is drawn by the built-in recipe
// (the cost-bound fallback).
[[nodiscard]] BakeEffort procedural_generation_effort(const ProceduralFill& fill, DeviceExtent extent) noexcept;

// The most bytes one pattern may hold: half the engine's shared budget
// (shared_bake_budget_bytes), so that one pattern never displaces the
// rest. A material with a larger pattern is refused, as one past the budget
// is.
inline constexpr std::size_t maximum_procedural_pattern_bytes = 2U * 1024U * 1024U;

// Whether `fill`'s pattern at `extent` is within the bake budget and
// maximum_procedural_pattern_bytes.
[[nodiscard]] bool procedural_pattern_bounded(const ProceduralFill& fill, DeviceExtent extent) noexcept;

// The working storage one generation reuses, filled by the generator
// (reset-size) before its pixel loop, so that the loop allocates nothing:
// per-axis lattice cells and weights; each octave's lattice values along
// the two cell rows the current pixel row lies between; and the paper's
// fibres, four values each.
struct ProceduralScratch final {
    std::vector<std::uint32_t> across_cells;
    std::vector<std::uint32_t> across_weights;
    std::vector<std::uint32_t> down_cells;
    std::vector<std::uint32_t> down_weights;
    std::vector<std::uint32_t> lattice_rows;
    std::vector<std::uint32_t> fibres;
};

// Generates `fill`'s pattern at `scale` into every pixel of `destination`
// (premultiplied BGRA8), whose extent is procedural_extent's. The caller
// has validated the destination (validate_raster_view) and the fill
// (valid_surface_material). `scratch` is reset-size; storage that cannot be
// allocated throws std::bad_alloc before any pixel is written.
void generate_procedural(const ProceduralFill& fill, double scale, const PartRasterView& destination,
                         ProceduralScratch& scratch);

// `fill`'s pattern at `scale` and `extent` in a raster of its own: null
// when the extent is empty or larger than a raster may be, or storage
// cannot be allocated. Synchronous.
[[nodiscard]] std::shared_ptr<PartRaster> make_procedural_pattern(const ProceduralFill& fill, double scale,
                                                                  DeviceExtent extent) noexcept;

// Fills `map` (reset-size to `outputs`) with the tile column (or row) each
// destination pixel along an axis shows, for a tile of `tile_length`
// pixels whose first pixel lies at destination pixel floor(`start`): the
// tile repeats in both directions. `tile_length` is at least 1.
void map_procedural_tile(double start, std::uint32_t tile_length, std::uint32_t outputs,
                         std::vector<std::uint32_t>& map);

} // namespace gui_forms::engine_primitives
