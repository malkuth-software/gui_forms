#pragma once
// What the X11 host computes because its platform does not report it
// (owner's mouse-event rule, 2026-11-08): the click count of each press,
// from the desktop's double-click time and distance, and which keys ask
// for a context menu. Plain values in, no Xlib, so the rules are tested on
// every platform.
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>

namespace gui_forms::host::linux_detail {

// The double-click distance the host uses where the desktop offers none
// (no XSETTINGS Net/DoubleClickDistance): Windows' SM_CXDOUBLECLK of 4
// pixels, a square reaching 2 pixels each way from the first press.
inline constexpr double default_double_click_distance = 2.0;

// The last press the click count compares the next one against.
struct LinuxClickHistory final {
  std::uint32_t count{};
  unsigned button{};
  // The X server time of the press (milliseconds; it wraps at 2^32).
  std::uint32_t time{};
  Point position{};
};

// The click count of a press of X button `button` at server time `time`
// and window position `position`, updated into `history`: one more than
// the last press's (up to 3, then 1 again, as GTK counts) when it is the
// same button, within `interval_milliseconds` of the last press, and
// within `distance` pixels of it along each axis; otherwise 1. Wrapped
// server times compare by their difference.
[[nodiscard]] std::uint32_t next_click_count(LinuxClickHistory &history, unsigned button,
                                             std::uint32_t time, Point position,
                                             std::uint32_t interval_milliseconds,
                                             double distance) noexcept;

// Whether a key press asks for a context menu, as Windows' Shift+F10 and
// the Menu key do: the context-menu key with no modifier, or F10 with
// Shift alone. The host asks only after the window left the key
// unhandled.
[[nodiscard]] bool requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept;

} // namespace gui_forms::host::linux_detail
