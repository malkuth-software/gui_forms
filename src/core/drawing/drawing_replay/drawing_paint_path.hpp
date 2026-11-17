#pragma once

// GUI.Drawing figures as Painter paths (paint_path.hpp), with the raster
// executor's figure rules: shared by drawing replay (draw_path, fill_path,
// the ellipses) and the Skia executor, which cuts a pen's trimmed figures
// with trim_paint_path in the core (Pen::set_trim). Private to the
// library; defined in drawing_replay.cpp.

#include "gui_forms/drawing/graphics_path/graphics_path.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"

namespace gui_forms {

// Appends the ellipse inscribed in `bounds` to `path` as a closed contour
// of its own (a move to its rightmost point, a full-turn arc, a close).
// An ellipse with no area appends nothing, as the executor draws none.
void append_drawing_ellipse(gui_drawing::RectF bounds, PaintPath& path);

// Clears `path` and writes `source` into it with the executor's figure
// rules (to_sk_path). False for an element outside the enumeration (the
// executor refuses the path); `path` then holds the elements before it.
[[nodiscard]] bool write_drawing_paint_path(const gui_drawing::PathSnapshot& source, PaintPath& path);

} // namespace gui_forms
