#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include <stxlib/optional.hpp>
#include <optional>
#include <span>

namespace gui_forms::engine_primitives {

// The sheen (T2 plan, phase E9; design language §9.3 item 2): a specular
// band that follows the pointer across a part, a reflection and never a
// moving light (§2.6). It is the only primitive behind the overlay seam
// (owner E9-A, 2026-10-22; EnginePrimitive::pointer_overlay): it bakes a
// band of its `width` and the part's height, once per (size, state, scale)
// and never per pointer position, and the engine composites the band with
// its centre at the part-local pointer, clipped to the part's shape. While
// the pointer is not over the part, and always under reduced motion, the
// band rests at `rest` (a fraction of the part's width).
//
// The band. Across its width w, at t = 2x / w - 1 for a pixel centre x, its
// colour's alpha is weighted by (1 - t²)², which falls smoothly to zero at
// both edges; it is uniform down the part. No transcendental function is
// evaluated, so the bytes are the same on every host.
//
// Parameters: `colour` (default white at alpha 96), `width` in logical
// units (2 to 96, default 24) and `rest` (0 to 1, default 0.25: towards
// the light, upper left). It reads no states, no inputs and no time; the
// pointer places it but never changes its pixels.
class SheenPrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;
    [[nodiscard]] bool blank(const PrimitiveParameterBlock& parameters) const noexcept override;
    [[nodiscard]] stxlib::Optional<PointerOverlay> pointer_overlay(const PrimitiveParameterBlock& parameters,
                                                                Size part) const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

} // namespace gui_forms::engine_primitives
