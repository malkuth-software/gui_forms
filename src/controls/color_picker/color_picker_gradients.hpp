#pragma once

#include "gui_forms/types.hpp"

namespace gui_forms {
class Painter;
}

namespace gui_forms::color_picker_detail {

// The ColorPicker's content gradients (design language 8.5, owner decision
// W-7: a picker's colours are data, drawn as given in every theme). Kept
// apart from the control so the paint census sees the content paint in one
// exempt file. Each fills `area` in the control's local coordinates.

// The saturation-brightness square for `hue` (degrees): white to the hue's
// pure colour left to right, then transparent to black top to bottom.
void paint_field(Painter& painter, Rect area, double hue);
// The hue spectrum from 0 degrees at the top to 360 at the bottom.
void paint_hue_track(Painter& painter, Rect area);
// A checkerboard of `cell` squares under `colour` from opaque at the top to
// transparent at the bottom.
void paint_alpha_track(Painter& painter, Rect area, Color colour, double cell);
// A checkerboard of `cell` squares under `colour`, flat.
void paint_swatch(Painter& painter, Rect area, Color colour, double cell);

} // namespace gui_forms::color_picker_detail
