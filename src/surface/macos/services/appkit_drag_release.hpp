#pragma once
// The button release a drag from the program takes from its window (AppKit's
// dragging session consumes the mouse-up): rebuilt when the session ends and
// sent to the view, so the window's pressed and capture state end as after
// any press (AppKitHostServices::finish_drag). Not compiled in a
// high-integrity build (cmake/HighIntegrity.cmake).

#import <AppKit/AppKit.h>

#include "gui_forms/host/services/host_services.hpp"

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

// The window of `services` (AppKitHostServices) closed: a drag from it ends
// now, reported cancelled (effect none). AppKit's session, which cannot be
// ended early, ends later and then reports nothing and sends no release to
// the view. Called by the view's notifyClosedWithReason:. UI thread.
void macos_drag_source_window_closed(HostServices& services);

// Test seam: starts a drag on `services` (AppKitHostServices) through
// start_drag, without the AppKit session (which needs a held button), and
// returns the drag's source; the test calls its
// draggingSession:endedAtPoint:operation: as AppKit would. Nil when refused.
[[nodiscard]] id<NSDraggingSource> macos_start_drag_for_testing(HostServices& services,
                                                                const HostDragSourceRequest& request);

#endif
} // namespace gui_forms::host::detail
