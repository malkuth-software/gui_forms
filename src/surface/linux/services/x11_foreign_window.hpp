#pragma once
#include <X11/Xlib.h>
#include <array>
#include <cstddef>
#include <span>
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
  // A reply to a foreign selection request: BadWindow naming the scope's
  // window, or BadAtom or BadMatch, since the property and target atoms
  // (and so the property's type and format) are the requestor's choice
  // (A-gf-host-01-F3).
  selection_reply,
  // Any error of a request to one of the scope's extensions (their major
  // opcodes): the presentation direct path's MIT-SHM, Present and XFixes
  // requests, which a server may refuse (BadAlloc, BadMatch,
  // BadPixmap) for reasons outside our control (A-gf-present-01-F1). The
  // caller learns of it from synchronize() and falls back.
  extension_requests,
};

// A foreign selection/drop client can disappear between an event and our reply.
// The guard drains requests before restoring Xlib's process-wide handler.
// Xlib's default handler ends the process, so every request that can fail
// for a reason outside our control runs inside one of these.
class ForeignWindowScope final {
public:
  ForeignWindowScope(Display *display, ::Window window);
  ForeignWindowScope(Display *display, ::Window window, ForeignWindowErrors errors);
  // ForeignWindowErrors::extension_requests for the extensions whose major
  // opcodes are `opcodes` (at most maximum_opcodes; negative entries, an
  // extension the server lacks, are skipped).
  ForeignWindowScope(Display *display, ::Window window, std::span<const int> opcodes);
  ~ForeignWindowScope();
  ForeignWindowScope(const ForeignWindowScope &) = delete;
  ForeignWindowScope &operator=(const ForeignWindowScope &) = delete;

  // Waits until the server has answered every request sent so far, and
  // returns whether this scope absorbed an error since it began.
  [[nodiscard]] bool synchronize() noexcept;

  static constexpr std::size_t maximum_opcodes = 4U;

private:
  Display *display_;
  ::Window window_;
  ForeignWindowErrors errors_;
  std::array<int, maximum_opcodes> opcodes_{-1, -1, -1, -1};
  bool absorbed_{};
  XErrorHandler previous_;
  ForeignWindowScope *parent_;
  static thread_local ForeignWindowScope *active_;
  static int handle(Display *display, XErrorEvent *error) noexcept;
};
} // namespace gui_forms::host::linux_detail
