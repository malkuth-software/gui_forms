#pragma once
// The button release a drag from the program takes from its window (AppKit's
// dragging session consumes the mouse-up): rebuilt when the session ends and
// sent to the view, so the window's pressed and capture state end as after
// any press (AppKitHostServices::finish_drag). Not compiled in a
// high-integrity build (cmake/HighIntegrity.cmake).

#import <AppKit/AppKit.h>

namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)

// The release of the button a drag started with: `start_type` is the press
// or move AppKit started the session from (left, right or other), the
// release lands at `screen_point` (the session's end, in screen
// coordinates) converted into `window`. Nil for a start type that is not a
// button's, or without a window.
[[nodiscard]] NSEvent* drag_release_event(NSWindow* window, NSEventType start_type, NSPoint screen_point);

// Sends drag_release_event for `view`'s window to `view` (mouseUp:,
// rightMouseUp: or otherMouseUp:). Nothing for a view without a window.
void deliver_drag_release(NSView* view, NSEventType start_type, NSPoint screen_point);

#endif
} // namespace gui_forms::host::detail
