#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/graphics_recorder/graphics_recorder.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <cstddef>
#include <span>

namespace gui_forms {

// What replay_drawing_commands drew and what it left out (the plot
// demonstrator's gap G6). Every command is counted once.
struct GUI_FORMS_API DrawingReplayReport final {
    // Commands drawn through the painter.
    std::size_t drawn{};
    // State commands (save, restore, translate, set_transform, set_clip,
    // reset_clip, set_quality): they draw nothing, and every drawing
    // command carries the state they made.
    std::size_t state{};
    // Commands left out because their transform is not a translation: the
    // Painter vocabulary has no scale, rotation or shear.
    std::size_t transformed{};
    // Commands left out because the Painter has no equivalent for their
    // kind, brush or pen (see replay_drawing_commands).
    std::size_t unsupported{};

    friend bool operator==(const DrawingReplayReport& left, const DrawingReplayReport& right) noexcept {
        return left.drawn == right.drawn && left.state == right.state &&
               left.transformed == right.transformed && left.unsupported == right.unsupported;
    }
};

// Replays a GUI.Drawing recording (GraphicsRecorder::commands()) onto a
// Painter, in order, in the painter's current coordinates: the recording's
// origin is the painter's. Each drawing command is drawn inside a save and
// restore of its own, under its state's clip (already in recording
// coordinates) and its transform's translation, so the painter's state is
// as it was when the call returns.
//
// Drawn:
// - fill_rectangle, fill_rectangles: fill_rect, fill_rects;
// - fill_polygon: fill_polygon (alternate is even-odd, winding nonzero);
// - fill_proportion: the covered part under clip_rounded_rect_corners;
// - fill_ellipse, fill_path: fill_path (a path's figures as the raster
//   executor builds them: a line or curve not starting at the current
//   point starts a figure, rectangles and ellipses are closed figures,
//   an arc extends its figure);
// - draw_rectangle: stroke_rect for a solid pen, stroke_polyline round
//   the rectangle for a dashed one;
// - draw_line, draw_lines: stroke_polyline;
// - draw_line_segments: stroke_segments;
// - draw_ellipse, draw_path: stroke_path;
// - draw_markers, fill_grid_pattern: draw_markers, fill_grid_pattern.
// A fill whose brush is not solid draws its shape as a clip (clip_rect for
// rectangles, clip_path for polygons, ellipses and paths) and the brush's
// paint over the shape's bounds: a linear gradient brush as
// fill_linear_gradient_spread on the executor's line, a path gradient as
// fill_radial_gradient_spread on the executor's circle, a conic gradient
// as fill_conic_gradient, a checker as fill_checker and a hatch as
// fill_hatch, each anchored at the recording's origin; the wrap mode is the
// spread (tile repeats, the flips reflect, clamp pads). A gradient whose
// colours and positions are not valid gradient stops fills with its
// primary colour, as the executor does.
// A pen maps to a StrokeStyle as the raster executor draws it: preset
// dashes in multiples of the width, a custom pattern as given (at most
// maximum_dash_intervals entries, an even count of 2 or more).
//
// Left out (counted unsupported): clear (the control paints its own
// background), draw_string (FontSnapshot to FontSpec waits for the font
// identity work), images and texture brushes (their pixels have no image
// identity in a painter), a path with an element outside the enumeration,
// and any command whose custom dash pattern does not fit a StrokeStyle.
//
// Reads `commands` during the call only; allocates scratch storage once
// per call, reused across commands.
[[nodiscard]] GUI_FORMS_API DrawingReplayReport replay_drawing_commands(
    std::span<const gui_drawing::DrawingCommand> commands, Painter& painter);

} // namespace gui_forms
