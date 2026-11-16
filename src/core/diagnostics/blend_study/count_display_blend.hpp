#pragma once

#include "gui_forms/diagnostics/blend_study/blend_study.hpp"
#include "../../display/chunk/display_chunk.hpp"

namespace gui_forms::detail {

// The blend study's walk (blend_study.hpp): charges each drawing command of
// `chunk` to `planes`, between their begin_frame and end_frame, as
// replay_display_chunk_occluded draws it into `painter` under `clip` (the
// window's logical coordinates) at device `scale`. The painter is asked, in
// its current (base) state, what it hides under the chunk's occluder, which
// part of a live surface it would cover opaquely, and how it draws each
// image (Painter::image_draw_opacity); text runs are measured through it.
// Nothing is drawn. The walk keeps one stack of saved states, reserved once
// per call: the only allocation outside the planes.
void count_display_blend(const DisplayChunk& chunk, Painter& painter, Rect clip, double scale,
                         BlendStudyPlanes& planes);

// The device rectangles of logical `area` at `scale`: outward (every pixel
// it touches), inward (every pixel it covers whole, possibly empty) and
// nearest (edges rounded to the nearest device edge, as the CPU painter
// snaps rectangle fills). An empty or non-finite area gives an empty one.
[[nodiscard]] DevicePixelRect device_outward(Rect area, double scale) noexcept;
[[nodiscard]] DevicePixelRect device_inward(Rect area, double scale) noexcept;
[[nodiscard]] DevicePixelRect device_nearest(Rect area, double scale) noexcept;

} // namespace gui_forms::detail
