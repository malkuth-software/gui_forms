#pragma once
#include <X11/Xlib.h>
namespace gui_forms::host::linux_detail {
// Which protocol errors a ForeignWindowScope absorbs; any other error goes
// to the handler that was in place before the scope.
enum class ForeignWindowErrors {
  // BadWindow naming the scope's window: a foreign selection or drop client
  // that disappeared between an event and our reply.
  bad_window,
  // BadWindow naming any window: a walk over other clients' windows
  // (XQueryTree, then a request on each child), any of which may be
  // destroyed meanwhile (A-gf-host-01-F2).
  any_bad_window,
  // XSetInputFocus refused with BadMatch because the window is not
  // viewable (hidden, iconified, or unmapped since our state last heard),
  // or BadWindow naming the scope's window (A-gf-host-01-F1).
  input_focus,
};

// A foreign selection/drop client can disappear between an event and our reply.
// The guard drains requests before restoring Xlib's process-wide handler.
// Xlib's default handler ends the process, so every request that can fail
// for a reason outside our control runs inside one of these.
class ForeignWindowScope final {
public:
  ForeignWindowScope(Display *display, ::Window window);
  ForeignWindowScope(Display *display, ::Window window, ForeignWindowErrors errors);
  ~ForeignWindowScope();
  ForeignWindowScope(const ForeignWindowScope &) = delete;
  ForeignWindowScope &operator=(const ForeignWindowScope &) = delete;

private:
  Display *display_;
  ::Window window_;
  ForeignWindowErrors errors_;
  XErrorHandler previous_;
  ForeignWindowScope *parent_;
  static thread_local ForeignWindowScope *active_;
  static int handle(Display *display, XErrorEvent *error) noexcept;
};
} // namespace gui_forms::host::linux_detail
