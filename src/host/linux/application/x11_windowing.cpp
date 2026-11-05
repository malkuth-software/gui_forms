// Application-scope windowing on X11 (stage DK phase W0, decisions 6 and 7).
// Desktop logical units are X pixels: this host runs at scale 1.
#include "linux_host_internal.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cmath>
#include <stdexcept>

namespace gui_forms::host::linux_detail {
namespace {
// One of the run's windows by the root child that holds it.
struct OwnTopLevel final {
  ::Window top_level{};
  HostWindowId window{};
  const NativeWindow *native{};
};
} // namespace

X11Windowing::X11Windowing(Runtime &runtime)
    : HostWindowing(linux_capabilities()), runtime_(runtime) {}

NativeWindow *X11Windowing::native(HostWindowId window) noexcept {
  for (std::size_t i = 0; i < runtime_.windows.size(); ++i) {
    NativeWindow &candidate = *runtime_.windows[i];
    if (!candidate.closed && candidate.window_id == window)
      return &candidate;
  }
  return nullptr;
}

HostWindowAdmission X11Windowing::open(LinuxApplicationWindow entry,
                                       bool runtime) {
  HostWindowAdmission admission;
  if (!entry.model || entry.stable_id.empty()) {
    admission.status.error = HostServiceError::invalid_argument;
    return admission;
  }
  HostWindowId owner_id{};
  ::Window owner_xid = 0;
  if (!entry.owner_id.empty()) {
    const std::optional<HostWindowId> found = find(entry.owner_id);
    NativeWindow *owner = found.has_value() ? native(*found) : nullptr;
    if (owner == nullptr) {
      admission.status.error = HostServiceError::invalid_argument;
      return admission;
    }
    owner_id = *found;
    owner_xid = (*owner).xid;
  }
  const HostWindowKind kind =
      host_window_kind(owner_id.valid(), entry.tool_window, entry.floating, false);
  const bool floating = host_window_kind_traits(kind).floating;
  HostWindowDescription description;
  description.stable_id = entry.stable_id;
  description.owner = owner_id;
  description.kind = kind;
  admission = admit(description);
  if (!admission.status.accepted())
    return admission;
  const HostWindowId id = admission.window;
  // A floating window is created unmapped; the floating policy maps it.
  const bool initially_visible = entry.options.initially_visible;
  if (floating)
    entry.options.initially_visible = false;
  std::shared_ptr<NativeWindow> window;
  try {
    window = runtime_.add(std::move(entry), owner_xid, id, !runtime);
  } catch (...) {
    retire(id);
    if (!runtime)
      throw;
    admission.status.error = HostServiceError::backend_failure;
    admission.window = {};
    return admission;
  }
  if (!runtime) {
    window_created(id, initially_visible);
    return admission;
  }
  (*window).ready();
  report_native_frame(*window);
  window_created(id, initially_visible);
  return admission;
}

HostServiceStatus X11Windowing::show_impl(HostWindowId window) {
  NativeWindow *found = native(window);
  if (found == nullptr)
    return {HostServiceError::after_shutdown};
  (*found).action(ActionKind::show);
  // Put a window the policy hid back where it was.
  if ((*found).hidden_content.has_value()) {
    const Rect content = *(*found).hidden_content;
    (*found).hidden_content.reset();
    static_cast<void>((*found).request_frame(content));
  }
  return {};
}

HostServiceStatus X11Windowing::hide_impl(HostWindowId window) {
  NativeWindow *found = native(window);
  if (found == nullptr)
    return {HostServiceError::after_shutdown};
  if ((*found).visible)
    (*found).hidden_content = (*found).frame().content;
  (*found).action(ActionKind::hide);
  return {};
}

void X11Windowing::report_native_frame(NativeWindow &window) {
  if (window.closed || !live(window.window_id))
    return;
  report_frame(window.window_id, window.frame());
}

HostWindowFrameResult X11Windowing::frame_impl(HostWindowId window) {
  HostWindowFrameResult result;
  NativeWindow *found = native(window);
  if (found == nullptr) {
    result.status.error = HostServiceError::after_shutdown;
    return result;
  }
  result.frame = (*found).frame();
  return result;
}

HostServiceStatus X11Windowing::request_frame_impl(HostWindowId window,
                                                   Rect content) {
  NativeWindow *found = native(window);
  if (found == nullptr)
    return {HostServiceError::after_shutdown};
  // The window manager answers with ConfigureNotify on a later event, which
  // reports the frame it actually chose.
  const HostServiceStatus status = (*found).request_frame(content);
  return status;
}

HostScreenPointResult X11Windowing::pointer_position_impl() {
  HostScreenPointResult result;
  Display *display = runtime_.display;
  ::Window root_return{};
  ::Window child{};
  int root_x{}, root_y{}, window_x{}, window_y{};
  unsigned mask{};
  const Bool same_screen =
      XQueryPointer(display, DefaultRootWindow(display), &root_return, &child,
                    &root_x, &root_y, &window_x, &window_y, &mask);
  if (!same_screen) {
    // The pointer is on another X screen; this host uses the default one.
    result.status.error = HostServiceError::unsupported;
    return result;
  }
  result.position = {static_cast<double>(root_x), static_cast<double>(root_y)};
  return result;
}

::Window X11Windowing::top_level(::Window window) const {
  Display *display = runtime_.display;
  const ::Window root = DefaultRootWindow(display);
  ::Window current = window;
  // Window managers reparent at most a few levels deep; the bound stops a
  // malformed tree from looping.
  for (unsigned depth = 0; depth < 16U; ++depth) {
    ::Window root_return{};
    ::Window parent{};
    ::Window *children = nullptr;
    unsigned count{};
    if (!XQueryTree(display, current, &root_return, &parent, &children, &count))
      return current;
    if (children != nullptr)
      XFree(children);
    if (parent == root || parent == 0)
      return current;
    current = parent;
  }
  return current;
}

HostWindowAtResult X11Windowing::window_at_impl(DesktopLogicalPoint point) {
  HostWindowAtResult result;
  Display *display = runtime_.display;
  const ::Window root = DefaultRootWindow(display);
  // The run's windows by the root child that holds each (bounded by the
  // windowing table).
  std::vector<OwnTopLevel> own;
  own.reserve(HostWindowing::maximum_windows);
  for (std::size_t i = 0; i < runtime_.windows.size(); ++i) {
    const NativeWindow &candidate = *runtime_.windows[i];
    if (candidate.closed || !live(candidate.window_id))
      continue;
    own.push_back({top_level(candidate.xid), candidate.window_id, &candidate});
  }
  ::Window root_return{};
  ::Window parent{};
  ::Window *children = nullptr;
  unsigned count{};
  if (!XQueryTree(display, root, &root_return, &parent, &children, &count)) {
    result.status.error = HostServiceError::backend_failure;
    return result;
  }
  // XQueryTree lists the root's children from the bottom of the stack to
  // the top; the first viewable one containing the point decides.
  for (unsigned index = count; index > 0; --index) {
    const ::Window child = children[index - 1U];
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display, child, &attributes))
      continue;
    if (attributes.map_state != IsViewable || attributes.c_class == InputOnly)
      continue;
    const Rect bounds{static_cast<double>(attributes.x),
                      static_cast<double>(attributes.y),
                      static_cast<double>(attributes.width + 2 * attributes.border_width),
                      static_cast<double>(attributes.height + 2 * attributes.border_width)};
    // Logical units are X pixels here (scale 1).
    if (!bounds.contains(Point{point.x, point.y}))
      continue;
    for (std::size_t i = 0; i < own.size(); ++i) {
      if (own[i].top_level == child) {
        result.window = own[i].window;
        break;
      }
    }
    break;
  }
  if (children != nullptr)
    XFree(children);
  return result;
}

HostServiceStatus X11Windowing::bring_to_front_impl(HostWindowId window) {
  NativeWindow *target = native(window);
  if (target == nullptr)
    return {HostServiceError::after_shutdown};
  Display *display = runtime_.display;
  const ::Window root = DefaultRootWindow(display);
  const ::Window target_top = top_level((*target).xid);
  std::vector<OwnTopLevel> others;
  others.reserve(HostWindowing::maximum_windows);
  for (std::size_t i = 0; i < runtime_.windows.size(); ++i) {
    const NativeWindow &candidate = *runtime_.windows[i];
    if (candidate.closed || &candidate == target)
      continue;
    others.push_back({top_level(candidate.xid), candidate.window_id, &candidate});
  }
  // The run's topmost other window, by the stacking order of root children.
  ::Window root_return{};
  ::Window parent{};
  ::Window *children = nullptr;
  unsigned count{};
  if (!XQueryTree(display, root, &root_return, &parent, &children, &count))
    return {HostServiceError::backend_failure};
  ::Window sibling = 0;
  bool target_is_topmost = false;
  for (unsigned index = count; index > 0 && sibling == 0 && !target_is_topmost;
       --index) {
    const ::Window child = children[index - 1U];
    if (child == target_top) {
      target_is_topmost = true;
      break;
    }
    for (std::size_t i = 0; i < others.size(); ++i) {
      if (others[i].top_level == child) {
        sibling = (*others[i].native).xid;
        break;
      }
    }
  }
  if (children != nullptr)
    XFree(children);
  if (target_is_topmost || sibling == 0)
    return {};
  // ICCCM 4.1.5: a restack of a top-level window goes through the window
  // manager; XReconfigureWMWindow sends the synthetic request when the
  // window is reparented. The window manager may decline it.
  XWindowChanges changes{};
  changes.sibling = sibling;
  changes.stack_mode = Above;
  if (!XReconfigureWMWindow(display, (*target).xid, DefaultScreen(display),
                            CWSibling | CWStackMode, &changes))
    return {HostServiceError::backend_failure};
  XFlush(display);
  return {};
}

HostServiceStatus X11Windowing::destroy_window_impl(HostWindowId window) {
  NativeWindow *found = native(window);
  if (found == nullptr)
    return {HostServiceError::after_shutdown};
  // A forced close publishes the closed event and retires the identity; the
  // native window is released by Runtime::reclaim after this turn.
  (*found).close(HostCloseReason::application, true);
  return {};
}

HostMonitorResult X11Windowing::query_monitors_impl() {
  HostMonitorResult result;
  Display *display = runtime_.display;
  const int screen = DefaultScreen(display);
  const Rect frame{0.0, 0.0, static_cast<double>(DisplayWidth(display, screen)),
                   static_cast<double>(DisplayHeight(display, screen))};
  Rect work_area = frame;
  // _NET_WORKAREA: x, y, width, height per desktop (EWMH); the first
  // desktop's area is used.
  Atom type{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char *data = nullptr;
  if (XGetWindowProperty(display, DefaultRootWindow(display),
                         runtime_.atom("_NET_WORKAREA"), 0, 4, False,
                         XA_CARDINAL, &type, &format, &count, &remaining,
                         &data) == Success &&
      type == XA_CARDINAL && format == 32 && count >= 4 && data != nullptr) {
    const long *values = reinterpret_cast<const long *>(data);
    const Rect published{static_cast<double>(values[0]),
                         static_cast<double>(values[1]),
                         static_cast<double>(values[2]),
                         static_cast<double>(values[3])};
    if (!published.empty())
      work_area = published;
  }
  if (data != nullptr)
    XFree(data);
  result.monitors.push_back({"x11-screen", frame, work_area, 1.0, true});
  return result;
}

HostWindowAdmission RunOpen::operator()(LinuxApplicationWindow entry) const {
  HostWindowAdmission refused;
  const std::shared_ptr<RunOpenState> state = owner.lock();
  if (!state || (*state).windowing == nullptr) {
    refused.status.error = HostServiceError::after_shutdown;
    return refused;
  }
  if ((*state).thread != std::this_thread::get_id()) {
    refused.status.error = HostServiceError::wrong_thread;
    return refused;
  }
  const HostWindowAdmission admission =
      (*(*state).windowing).open(std::move(entry), true);
  return admission;
}

WindowingEndGuard::~WindowingEndGuard() {
  (*state_).windowing = nullptr;
  try {
    if ((*options_).windowing_ended)
      (*options_).windowing_ended();
  } catch (...) {
    // The runtime's handler does not throw; a program cannot reach here.
  }
  (*windowing_).shutdown();
}

} // namespace gui_forms::host::linux_detail
