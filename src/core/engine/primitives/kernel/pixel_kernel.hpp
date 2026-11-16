#pragma once

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>

// The pixel arithmetic the engine's primitive kernels share (T2 plan,
// "Numerics and identical pixels"): premultiplied colour in double,
// source-over, one quantization to 8 bits, and pixel-interval coverage.
// Defined in pixel_kernel.cpp, compiled without fused multiply-add, so the
// same inputs give the same bytes on every host.

namespace gui_forms::engine_primitives {

// A premultiplied colour in [0, 1] per channel.
struct Unit final {
    double red{};
    double green{};
    double blue{};
    double alpha{};
};

// `colour` (straight alpha) at `coverage`, premultiplied.
[[nodiscard]] Unit premultiply(Color colour, double coverage) noexcept;

// Source-over of `source` onto `target`, both premultiplied.
void blend_over(Unit& target, Unit source) noexcept;

// `source` joined to `target` by multiply or screen (E9.6), both
// premultiplied, as CSS's separable blend modes composite:
// multiply gives s × (1 − αd) + d × (1 − αs) + s × d per colour channel,
// screen gives s + d − s × d per channel; alpha is αs + αd − αs × αd.
void blend_multiply(Unit& target, Unit source) noexcept;
void blend_screen(Unit& target, Unit source) noexcept;
// `source` joined to `target` by one of the other separable modes
// (MaterialBlendMode overlay to exclusion; capability review, owner
// 2026-11-16), both premultiplied: each colour channel
// s × (1 − αd) + d × (1 − αs) + αs × αd × B(d / αd, s / αs), with B the
// mode's function of the straight colours (Compositing and Blending
// Level 1; soft-light's root is sqrt, exact in IEEE arithmetic), and alpha
// αs + αd − αs × αd. A straight colour of a transparent pixel is 0. Normal,
// multiply and screen join as blend_over, blend_multiply and blend_screen.
void blend_separable(Unit& target, Unit source, MaterialBlendMode mode) noexcept;
// B(backdrop, source) of blend_separable's `mode` on straight colours in
// [0, 1]: normal gives source, multiply and screen their products.
[[nodiscard]] double separable_blend_function(double backdrop, double source, MaterialBlendMode mode) noexcept;

// `pixel` (premultiplied) through `matrix` (E9.6): unpremultiplied, each
// row applied and clamped to [0, 1], and premultiplied again. A pixel of
// alpha 0 is read as transparent black.
[[nodiscard]] Unit colour_filtered(const MaterialColourMatrix& matrix, Unit pixel) noexcept;
// `pixel` through `matrix`, then, when `table` is not null, each clamped
// straight channel quantized once to 8 bits and looked up in `table`
// (MaterialColourTable), before premultiplying again. With a null table,
// colour_filtered's bits.
[[nodiscard]] Unit colour_staged(const MaterialColourMatrix& matrix, const MaterialColourTable* table,
                                 Unit pixel) noexcept;

// One channel quantized once: floor(clamp(v, 0, 1) × 255 + 0.5).
[[nodiscard]] std::uint8_t to_byte(double value) noexcept;

// Writes `unit` as BGRA8 at byte `offset` of `destination`, which the
// caller has validated.
void store(const PartRasterView& destination, std::size_t offset, const Unit& unit) noexcept;

// The width of a line in device pixels at `scale` (design language §3.6,
// proposed): max(1, round(min(scale, 2))).
[[nodiscard]] double keyline_width(double scale) noexcept;

// How much of the pixel interval [pixel, pixel + 1) lies in [start, end).
[[nodiscard]] double interval_overlap(double pixel, double start, double end) noexcept;

} // namespace gui_forms::engine_primitives
