#pragma once

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"

#include <span>

namespace gui_forms::engine_primitives {

// The light map (owner, 2026-11-22; skeudoindustrial surfaces, phase 2): a
// part drawn from a baked light map, as any program's theme can ask for
// it (`primitive: light-map; bake: dialog-ok`). The bake is one of the
// stores the library ships (engine_light::shipped_light_plane_store): grey
// planes of the room's light, of each lamp's and of the key's coverage,
// baked without colour. The lamp's colour is set when the part is drawn,
// so one bake serves every colour, and the room's light and the lamps'
// are composited as engine_light::composite_light_planes does: stretched
// over the part, toned, the key's coverage as alpha.
//
// Parameters:
// - `bake`: which shipped bake, by its stem: dialog_ok (clear over a
//   frosted back) or dialog_cancel (white plastic over red lamps), each
//   shipped up and down (<stem>_up, <stem>_down);
// - `lamp_colour` (default white): the lamps' colour, as sRGB; its alpha
//   scales their power;
// - `lamp_power` (0 to 2, default 0): the lamps' power, 1 the baked unit;
// - `press` (0 to 1, default 0): how far the key is down, mixing the up
//   bake toward the down one;
// - `room` (0 to 2, default 1): the room light's gain.
// A theme sets the lit and pressed looks per state, naming the primitive
// in each rule (a parameter cascades among the rules that select it):
//   .lit-ok::part(face) { primitive: light-map; bake: dialog-ok;
//                         lamp-colour: #6fff89; lamp-power: 1; }
//   .lit-ok.lamp-off::part(face) { primitive: light-map; lamp-power: 0; }
//   .lit-ok:active::part(face) { primitive: light-map; press: 1; }
//
// None of its parameters is animated: a change of state switches the
// look at once. A key 180 units wide composited every frame at scale 2
// would cost past the frame budget (maximum_animated_operations_per_frame),
// so the press and the strike are not transitions here; a control that
// animates its lamp (IndicatorLamp) draws its own bake.
//
// Only the whole layout (engine_light::LightShape::whole): the jewel and
// the pinpoint read 3 x 3 toned samples a pixel, past the
// 512-operations-a-pixel budget, so they stay with the controls that
// draw them (IndicatorLamp, RadioButton's lamp look).
//
// It reads no states and no inputs. Where the shipped store cannot be
// read (memory exhausted), it draws nothing.
class LightMapPrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override;

private:
    void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                std::span<double> values) const noexcept override;
    void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
              std::span<const double> prepared, const PartRasterView& destination) const noexcept override;
};

} // namespace gui_forms::engine_primitives
