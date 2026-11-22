#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include <span>

namespace gui_forms::engine_primitives {

// The ribbon button's cloth bulge, as the rendering reference specifies it
// (notes:planning/stages/T4-default-theme/RENDERING_REFERENCE.md §2 and §3; design
// language §3.5). The plane itself swells: a lit slope toward the upper-left
// light, a shaded slope away from it and a faint crown, each a field
// 1 − t^q over its own ellipse. There is no edge of any kind. `pressed`
// turns the bulge into a dent (swapped centres, reference §2 "Pressed is a
// dent"); `active_tool`, or the `gold` parameter at 0.5 or more, draws it in
// gold, the selected look (reference §2 "Active tool"), which the built-in
// rules give a checked ribbon command and a selected gallery item.
//
// Both primitives take `amplitude`, a number in [0, 1] (default 1) that
// multiplies the relief: 0 is the flat plane and draws nothing, 1 is the
// full bulge. It is the value the ribbon's rise animates while the button is
// hot (reference §2, "Hover is the rise"; T2 GIVEN G4-1 and D3-1), so it is
// declared `animated`: changing it never makes a preparation stale. It is a
// primitive parameter, not a vocabulary atom.
//
// Both primitives paint an overlay: they write premultiplied pixels that are
// transparent wherever the plane beneath should show. The one exception is
// the dithered form in gold, which draws the gold plate under its dots
// (below).

// The smooth form (reference §2): white, blue-grey and white layered over
// the plane in that order, each weighted by its field, its peak and the
// amplitude. It derives nothing in its preparation.
class BulgePrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;
    // At amplitude 0 the bulge is the flat plane and draws nothing.
    [[nodiscard]] bool blank(const PrimitiveParameterBlock& parameters) const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

// The dithered form (reference §3): the signed tone of the same fields,
// sampled on a lattice of dots, quantized onto three steps per side with an
// ordered 4×4 threshold. Between dots, and wherever the tone rounds to zero,
// the plane shows.
//
// Quantization is rescaled to the part's own tone range (T2 GIVEN G1-3): a
// dot whose tone at amplitude 1 is t quantizes a·t / P when t ≥ 0 and
// a·(−t) / N when t < 0, where P and N are the largest t and −t over the
// part's dots. A side whose extreme is 0 draws no dots. P and N (and the
// gold form's crown extreme) are the preparation's three values: computed
// once per part geometry, gains and pressed state, never per amplitude.
//
// The lattice is drawn at the nearest integer scale (T2 GIVEN G3-3): with
// k = max(1, floor(s + 0.5)), dot n is k × k device px at device (2n + 1)·k
// from the part's device origin, its fields are sampled at logical
// (2n + 1)·k / s, and its threshold is indexed by n. At integer scales this
// is the studies' rule exactly: 1-logical-px dots at odd logical positions.
//
// In gold the dots alone read as noise (owner, 2026-11-22), so the gold form
// draws the selected look's plate first (reference §1, "active tool"): a
// vertical gradient, #FFEDBA to #FFCF66 (pressed, the gold plate's pressed
// face), inside a one-keyline border #C49332 with corner radius 2; the
// gradient's ends and the border are the `gold_plate_*` parameters. Its dots shade the plate and stay inside the
// face; a dot that reaches a rounded corner is blended by the face's
// coverage. At amplitude 0 the gold form, like the others, draws nothing.
class DitheredBulgePrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;
    // At amplitude 0 every dot rounds to the plane: nothing is drawn.
    [[nodiscard]] bool blank(const PrimitiveParameterBlock& parameters) const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

} // namespace gui_forms::engine_primitives
