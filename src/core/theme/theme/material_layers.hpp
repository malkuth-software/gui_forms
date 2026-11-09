#pragma once

#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/painter/painter.hpp"

namespace gui_forms::detail {

// Draws a valid, unfinished `material` over `bounds` as painter commands:
// box shadows, fills, strokes and lines (Painter::draw_surface_material's
// base implementation). A minimal painter's fallback and the path for a
// material the kernel does not bake (material_raster.hpp): only the
// recording and Skia painters bake, and only what material_bakes admits.
void paint_material_layers(Painter& painter, Rect bounds, const SurfaceMaterial& material);

} // namespace gui_forms::detail
