#pragma once

#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <chrono>

namespace gui_forms {

// Scroll bar metrics shared by the standalone ScrollBar (its property
// defaults and ranges) and the scroll bars built into every
// ScrollableControl, so that the two kinds of bar size and repeat alike.

// The thumb's least extent along the bar, in logical units: the default,
// and the range ScrollBar::minimum_thumb_extent accepts. A track shorter
// than a bar's minimum thumb extent shows no thumb (its presses still
// page).
inline constexpr double scroll_thumb_extent_default = 18.0;
inline constexpr double scroll_thumb_extent_least = 8.0;
inline constexpr double scroll_thumb_extent_most = 128.0;

// Press-and-hold repeat of an arrow or the track (ADR-016): the first
// repeat after the delay, then one per interval, until release.
inline constexpr FrameInterval scroll_repeat_delay_default = std::chrono::milliseconds(400);
inline constexpr FrameInterval scroll_repeat_interval_default = std::chrono::milliseconds(60);

// The side of the square an arrow button's mark is drawn in; a button
// narrower or shorter than this draws no mark.
inline constexpr double scroll_arrow_mark_extent = 8.0;

} // namespace gui_forms
