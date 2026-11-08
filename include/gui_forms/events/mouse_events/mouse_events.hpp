#pragma once
// The WinForms mouse event records a control raises (owner's mouse-event
// rule, 2026-11-08): MouseEventArgs for MouseDown, MouseUp, MouseMove,
// MouseWheel, MouseClick and MouseDoubleClick, and ContextMenuRequest for
// ContextMenuRequested. The click count comes from the host (Windows and
// macOS report it; the Linux host computes it from the desktop's
// double-click time and distance); the core counts no clicks.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>

namespace gui_forms {

// One mouse event as WinForms' MouseEventArgs gives it: the button (none
// for a move or wheel without one), the click count the host reported for
// the press (1, 2 for the second press of a double-click, and so on; 0 for
// a move or wheel), the position in the control's own coordinates and in
// the window's, the wheel delta (logical pixels, positive away from the
// user and to the right) and the modifiers held.
struct MouseEventArgs final {
    PointerButton button{PointerButton::none};
    std::uint32_t clicks{};
    Point location{};
    Point window_position{};
    Point wheel_delta{};
    Modifier modifiers{Modifier::none};
};

// Where a context-menu request came from: the pointer (a right press or
// release, a Control-click on macOS) or the keyboard (Shift+F10 or the
// Menu key, where the host's platform reports or emulates them).
enum class ContextRequestOrigin : std::uint8_t {
    pointer,
    keyboard,
};

// A request for a context menu, raised on the control under the pointer
// (or the focused control for a keyboard request) and then on each
// ancestor until a handler sets `handled`. `location` is in the control's
// coordinates and `window_position` in the window's; a keyboard request
// has no pointer position and carries the control's own origin, so the
// control chooses its anchor (its selection, its keyboard item).
struct ContextMenuRequest final {
    ContextRequestOrigin origin{ContextRequestOrigin::pointer};
    Point location{};
    Point window_position{};
    bool handled{};
};

} // namespace gui_forms
