#pragma once

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/finish_fill/finish_fill.hpp"
#include "../material/material_bake_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// The surface finishes' kernel (skeudoindustrial surfaces, phase 1): a port
// of the industrial-surfaces study's finish kernel
// (notes:experiments/industrial-surfaces/cpp/surface_kernels.cpp, itself a
// port of finish_kernel.js), painted straight into a destination.
//
// Every device pixel is a pure function of its position relative to the
// surface's own origin, the display scale and the fill (FinishFill).
// Nothing is baked or tiled: repainting any damage rectangle writes exactly
// the bytes a whole repaint writes (damage invariance). The only working
// storage is two rows of samples the damage's width plus one, which the
// caller supplies and reuses (FinishScratch).
//
// The byte reference is the JavaScript kernel kept beside its generator,
// tools/industrial_surfaces/finish_kernel.js: gui_forms_engine_finish_tests
// checks this kernel's bytes against hashes of the reference's.
//
// Identical bytes on every host. The kernel uses doubles, as the
// reference does, with only operations IEEE 754 rounds exactly alike
// everywhere: + − × ÷, square root and floor. Contraction into fused
// multiply-adds is off (the pragma in the source; the reference, being
// JavaScript, never fuses). Where the study called libm (the sRGB tables,
// the light's cosine and sine, the sheen's power) the reference itself now
// reads tables generated from it (finish_tables.inc) and raises to a whole
// power by repeated squaring, so the two compute the same sequence of
// operations.

namespace gui_forms::engine_primitives {

// One sample of a finish's row: its height (logical units), albedo
// multiplier, mix towards the second colour, and specular multiplier.
// Stored as float, as the reference stores them; read back as doubles.
struct FinishSample final {
    float height{};
    float albedo{};
    float mix{};
    float specular{};
};

// The two rows the kernel works in: the row it shades and the one below
// it (for the slope down), each the clipped damage's width plus one (for
// the slope across). Reserve once for the widest damage, reuse.
struct FinishScratch final {
    std::vector<FinishSample> upper;
    std::vector<FinishSample> lower;
};

// A rectangle of device pixels. Signed, since a surface may begin left of
// or above the destination (its origin anchors the finish).
struct FinishRect final {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t width{};
    std::int64_t height{};
};

// Sizes both rows for damage `width` pixels wide (reset-size to width + 1,
// values unspecified). Throws std::bad_alloc when storage cannot be had,
// before anything is painted.
void reserve_finish_scratch(FinishScratch& scratch, std::uint32_t width);

// Paints the part of `damage` that lies inside both `surface` and
// `destination` (premultiplied BGRA8; every pixel written is opaque) with
// `fill` at `scale` device pixels per logical unit, anchored to
// `surface`'s origin. Returns the pixels written: none for an empty
// intersection, and none (writing nothing) when `scratch` holds fewer
// than the clipped width plus one samples (the glint reads no scratch, but
// is held to the same rule). The fill is valid
// (valid_surface_material) and the destination validated by the caller.
// Bytes outside the clipped damage are left as they are.
std::size_t paint_finish(const FinishFill& fill, double scale, FinishRect surface, FinishRect damage,
                         const PartRasterView& destination, FinishScratch& scratch) noexcept;

// Linear light to an sRGB byte through the reference's 4097-step table
// (entry i the byte for i / 4096; values outside [0, 1] clamped), which the
// light planes' composite shares (panel_lamps.html builds the same table).
[[nodiscard]] std::uint8_t encode_linear(double linear) noexcept;

// The finishes' effort weights, in effort units per painted device pixel
// (material_bake_policy.hpp), on the material weights' scale. A material's
// finish layer is painted during its bake, so its weight is charged per
// destination pixel with the layer's composite (MaterialEffortWeights's
// image weight).
//
// Measured (2026-11-12, the integrator, Apple M4, Release, LLVM 22):
// gui_forms_engine_finish_tests --measure, the median of nine paints of a
// 256 × 256 surface at scale 1, two runs averaged, in nanoseconds per
// device pixel: dusty matte 25.6, orange peel 12.2, hammertone 111,
// wrinkle 34.3, bead blast 12.8, sand cast 63.4, flat 5.7, cloth weave
// 13.5, glint 276. They are scaled by the budget's own calibration
// (maximum_material_bake_effort's 20,000,000 units for the 4 ms bound: 5
// units a nanosecond) and rounded up to a multiple of five. The solid
// layer's ratio could not set the scale on that machine (it measures
// 0.003 ns past the base, within noise); the flat finish's 30 beside the
// solid layer's 25 agrees with the scale chosen. Rerun --measure and
// rescale when a kernel changes.
struct FinishEffortWeights final {
    std::uint32_t dusty_matte{};
    std::uint32_t orange_peel{};
    std::uint32_t hammertone{};
    std::uint32_t wrinkle{};
    std::uint32_t bead_blast{};
    std::uint32_t sand_cast{};
    std::uint32_t flat{};
    std::uint32_t cloth_weave{};
    std::uint32_t glint{};
};

inline constexpr FinishEffortWeights finish_effort_weights{
    .dusty_matte = 130U,
    .orange_peel = 65U,
    .hammertone = 555U,
    .wrinkle = 175U,
    .bead_blast = 65U,
    .sand_cast = 320U,
    .flat = 30U,
    .cloth_weave = 70U,
    // About six speck points in reach of each pixel, each four bins of an
    // exponential, three hashes and a Poisson draw: the dearest finish.
    // With the base and the image composite, over the budget past about
    // 13,000 device pixels (a 115 x 115 face), where the material is
    // refused and the built-in recipe draws: the glint's cost bound.
    .glint = 1380U,
};

// `kind`'s weight per painted device pixel; past the budget for a kind
// outside the enumeration (never accepted by valid_surface_material).
[[nodiscard]] std::uint64_t finish_effort_weight(FinishKind kind) noexcept;

} // namespace gui_forms::engine_primitives
