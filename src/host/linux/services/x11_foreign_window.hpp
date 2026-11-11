#pragma once
#include <X11/Xlib.h>
namespace gui_forms::host::linux_detail {
// A foreign selection/drop client can disappear between an event and our reply.
// The guard drains requests before restoring Xlib's process-wide handler.
class ForeignWindowScope final {
public:
  ForeignWindowScope(Display *display, ::Window window);
  ~ForeignWindowScope();
  ForeignWindowScope(const ForeignWindowScope &) = delete;
  ForeignWindowScope &operator=(const ForeignWindowScope &) = delete;

private:
  Display *display_;
  ::Window window_;
  XErrorHandler previous_;
  ForeignWindowScope *parent_;
  static thread_local ForeignWindowScope *active_;
  static int handle(Display *display, XErrorEvent *error) noexcept;
};
} // namespace gui_forms::host::linux_detail
