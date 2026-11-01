#pragma once

// The pointer and stamp mechanics the framework's drags share (owner
// decision DK-36, K2-1): DockSite's docking drag and ToolStripContainer's
// strip drag. A press arms a drag that starts once the pointer has moved the
// threshold (4 units, D5-1) on either axis; Ctrl held suppresses the targets
// (a release then drops nowhere); Escape cancels. The stamp starts as the
// source's rectangle and shrinks, as its owner's motion value runs from 0 to
// 1 over 220 ms (smoothstep), to a miniature whose longer side is about 100
// units (scaled with the text), held where the pointer grabbed the source;
// at the end it settles from where the drag stopped into the subject's new
// place along the same curve. These are geometry and input rules only: each
// owner keeps its own phase, motion value, targets and drop.

#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <chrono>

namespace gui_forms::drag_gesture {

inline constexpr double default_threshold = 4.0;
// A host may widen the threshold (DockSite::set_drag_threshold), within this.
inline constexpr double minimum_threshold = 1.0;
inline constexpr double maximum_threshold = 64.0;
// The miniature's longer side in logical units at text scale 1.
inline constexpr double stamp_side = 100.0;
inline constexpr std::chrono::milliseconds stamp_duration{220};

// The curve an owner's stamp motion value runs on (0 to 1).
[[nodiscard]] MotionCurve stamp_curve() noexcept;
// Whether `threshold` is finite and lies in the admitted range.
[[nodiscard]] bool valid_threshold(double threshold) noexcept;
// Whether an armed drag pressed at `press` starts with the pointer at `now`.
[[nodiscard]] bool past_threshold(Point press, Point now, double threshold) noexcept;
// Whether the modifiers held suppress the drag's targets (Ctrl).
[[nodiscard]] bool suppresses_targets(Modifier modifiers) noexcept;
// Whether a key event cancels the drag (Escape pressed).
[[nodiscard]] bool cancels(const KeyEvent& event) noexcept;
// Where `press` holds `source`, as fractions of its width and height
// clamped to [0, 1]; the centre for an empty source.
[[nodiscard]] Point grab_of(Rect source, Point press) noexcept;
// The stamp at `progress` (0: the source itself; 1: the miniature at the
// pointer, grabbed at `grab`).
[[nodiscard]] Rect stamp_at(Rect source, Point pointer, Point grab, double progress, double text_scale) noexcept;
// The rectangle `progress` of the way from `from` to `to` (settling).
[[nodiscard]] Rect interpolate(Rect from, Rect to, double progress) noexcept;

} // namespace gui_forms::drag_gesture
