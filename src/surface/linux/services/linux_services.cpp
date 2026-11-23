#include "../../../core/host/image/clipboard_image_wire.hpp"
#include "../../../core/host/image/clipboard_png.hpp"
#include "../../../core/host/image/host_image_pixels.hpp"
#include "../application/linux_host_internal.hpp"
#include "x11_foreign_window.hpp"
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#if defined(GUI_FORMS_HAVE_XI2)
#include <X11/extensions/XInput2.h>
#endif
#ifdef GUI_FORMS_HAS_XCURSOR
#include "x11_cursor.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <cerrno>
#include <spawn.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// The process environment, handed to the address opener (posix_spawn). A
// high-integrity build has no address opener (cmake/HighIntegrity.cmake).
extern char** environ;
#endif

namespace gui_forms::host::linux_detail {
namespace {
// The X11 host shows notification-area icons only where a system tray owned
// the tray selection when the window's services were made.
HostCapabilities with_tray(HostCapabilities capabilities, Display *display) {
  if (X11TrayIcons::tray_present(display))
    capabilities.available =
        capabilities.available | HostCapability::notification_area;
  return capabilities;
}

// Desktop notifications where the session bus has the freedesktop
// notification service (asked once per process). A high-integrity build has
// none (cmake/HighIntegrity.cmake).
HostCapabilities with_notifications(HostCapabilities capabilities) {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  if (LinuxDesktopNotifications::service_present())
    capabilities.available = capabilities.available | HostCapability::desktop_notifications;
#endif
  return capabilities;
}

// Relative pointer mode needs the server's XI 2 raw motion: without it
// (no libXi in the build, or a server without XI 2.0) the report drops
// relative_pointer and raw_pointer_motion, and the window derives deltas
// from positions.
HostCapabilities with_raw_motion(HostCapabilities capabilities, const Runtime &runtime) {
  if (runtime.xi_opcode < 0) {
    const std::uint64_t dropped = static_cast<std::uint64_t>(HostCapability::relative_pointer) |
                                  static_cast<std::uint64_t>(HostCapability::raw_pointer_motion);
    capabilities.available =
        static_cast<HostCapability>(static_cast<std::uint64_t>(capabilities.available) & ~dropped);
  }
  return capabilities;
}

#if defined(GUI_FORMS_HAVE_XI2)
// Selects (or, with `on` false, deselects) XI_RawMotion from every master
// device on the root window.
void select_raw_motion(Display *display, bool on) {
  std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> bits{};
  if (on)
    XISetMask(bits.data(), XI_RawMotion);
  XIEventMask mask{};
  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = static_cast<int>(bits.size());
  mask.mask = bits.data();
  XISelectEvents(display, DefaultRootWindow(display), &mask, 1);
}
#endif

// The X cursor-font shape for a cursor kind.
unsigned font_cursor_shape(CursorKind cursor) noexcept {
  switch (cursor) {
  case CursorKind::arrow:
    return XC_left_ptr;
  case CursorKind::text:
    return XC_xterm;
  case CursorKind::hand:
    return XC_hand2;
  case CursorKind::crosshair:
    return XC_crosshair;
  case CursorKind::resize_horizontal:
    return XC_sb_h_double_arrow;
  case CursorKind::resize_vertical:
    return XC_sb_v_double_arrow;
  case CursorKind::resize_diagonal_down:
    return XC_bottom_right_corner;
  case CursorKind::resize_diagonal_up:
    return XC_top_right_corner;
  case CursorKind::wait:
    return XC_watch;
  case CursorKind::forbidden:
    return XC_X_cursor;
  case CursorKind::help:
    return XC_question_arrow;
  }
  // A kind outside the enumeration shows the arrow.
  return XC_left_ptr;
}
} // namespace
Services::Services(NativeWindow &window)
    : HostServices(with_notifications(
          with_raw_motion(with_tray(linux_capabilities(), window.runtime.display), window.runtime))),
      window_(window) {}
Services::~Services() { shutdown(); }
bool Services::tray_event(const XEvent &event) {
  if (!tray_)
    return false;
  return (*tray_).handle_event(event);
}
HostServiceStatus Services::add_tray_icon_impl(HostTrayIconId icon_id,
                                               const HostTrayIconRequest &request) {
  if (!tray_)
    tray_ = std::make_unique<X11TrayIcons>(
        window_.runtime.display,
        Delegate<const HostTrayIconEvent &>::bind<Services, &Services::forward_tray_event>(*this));
  return (*tray_).add(icon_id, request);
}
HostServiceStatus Services::update_tray_icon_impl(HostTrayIconId icon_id,
                                                  const HostTrayIconRequest &request) {
  if (!tray_)
    return {HostServiceError::backend_failure};
  return (*tray_).update(icon_id, request);
}
void Services::remove_tray_icon_impl(HostTrayIconId icon_id) noexcept {
  if (tray_)
    (*tray_).remove(icon_id);
}
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
HostServiceStatus Services::show_desktop_notification_impl(HostDesktopNotificationId notification_id,
                                                           const HostDesktopNotificationRequest &request) {
  if (!notifications_)
    notifications_ = std::make_unique<LinuxDesktopNotifications>(
        Delegate<const HostDesktopNotificationEvent &>::bind<Services, &Services::forward_notification_event>(
            *this));
  return (*notifications_).show(notification_id, request);
}
void Services::withdraw_desktop_notification_impl(HostDesktopNotificationId notification_id) noexcept {
  if (notifications_)
    (*notifications_).withdraw(notification_id);
}
#endif
void Services::shutdown_impl() noexcept {
  tray_.reset();
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  notifications_.reset();
#endif
  for (const CursorEntry& entry : cursors_) XFreeCursor(window_.runtime.display, entry.native);
  cursors_.clear();
}
HostServiceStatus Services::set_custom_cursor_impl(const CursorImagesPtr& images, const CursorImage& image) {
#ifdef GUI_FORMS_HAS_XCURSOR
  for (const CursorEntry& entry : cursors_) {
    if (entry.images == images && entry.scale == image.scale) {
      XDefineCursor(window_.runtime.display, window_.xid, entry.native);
      window_.cursor_set = false;
      return {};
    }
  }
  if (!XcursorSupportsARGB(window_.runtime.display)) return {HostServiceError::unsupported};
  if (cursors_.capacity() < 32) cursors_.reserve(32);
  const ::Cursor cursor = create_image_cursor(window_.runtime.display, *images, image);
  if (cursor == None) return {HostServiceError::backend_failure};
  XDefineCursor(window_.runtime.display, window_.xid, cursor);
  window_.cursor_set = false;
  if (cursors_.size() >= 32) {
    XFreeCursor(window_.runtime.display, cursors_.front().native);
    cursors_.erase(cursors_.begin());
  }
  cursors_.push_back({images, image.scale, cursor});
  return {};
#else
  static_cast<void>(images); static_cast<void>(image);
  return {HostServiceError::unsupported};
#endif
}
HostMonitorResult Services::query_monitors_impl() {
  HostMonitorResult result;
  query_x11_monitors(window_.runtime, result);
  return result;
}
HostServiceStatus Services::apply_window_backdrop_impl(WindowBackdrop backdrop) {
  // The grant change repaints the whole window, so the next present
  // follows the new alpha rule.
  if (backdrop == WindowBackdrop::opaque) {
    window_.present_alpha = false;
    return {};
  }
  if (backdrop == WindowBackdrop::transparent && window_.argb) {
    window_.present_alpha = true;
    return {};
  }
  return {HostServiceError::backend_failure};
}
namespace {

// _NET_WM_STATE actions (EWMH): remove, add.
constexpr long wm_state_remove = 0;
constexpr long wm_state_add = 1;

// Asks the window manager to add or remove one or two states of a mapped
// window (EWMH _NET_WM_STATE client message; source indication 1, an
// application).
void send_wm_state(NativeWindow &window, long action, Atom first, Atom second) {
  XEvent event{};
  event.xclient.type = ClientMessage;
  event.xclient.window = window.xid;
  event.xclient.message_type = window.runtime.atom("_NET_WM_STATE");
  event.xclient.format = 32;
  event.xclient.data.l[0] = action;
  event.xclient.data.l[1] = static_cast<long>(first);
  event.xclient.data.l[2] = static_cast<long>(second);
  event.xclient.data.l[3] = 1;
  XSendEvent(window.runtime.display, DefaultRootWindow(window.runtime.display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

// The Motif window manager hints (_MOTIF_WM_HINTS, five longs): which
// decorations and functions the frame offers.
struct MotifHints final {
  unsigned long flags{};
  unsigned long functions{};
  unsigned long decorations{};
  long input_mode{};
  unsigned long status{};
};
constexpr unsigned long motif_functions_flag = 1UL;
constexpr unsigned long motif_decorations_flag = 2UL;
constexpr unsigned long motif_function_resize = 2UL;
constexpr unsigned long motif_function_move = 4UL;
constexpr unsigned long motif_function_minimize = 8UL;
constexpr unsigned long motif_function_maximize = 16UL;
constexpr unsigned long motif_function_close = 32UL;
constexpr unsigned long motif_decoration_border = 2UL;
constexpr unsigned long motif_decoration_resize = 4UL;
constexpr unsigned long motif_decoration_title = 8UL;
constexpr unsigned long motif_decoration_menu = 16UL;
constexpr unsigned long motif_decoration_minimize = 32UL;
constexpr unsigned long motif_decoration_maximize = 64UL;

// The frame and boxes as Motif hints. The window can always be moved and
// closed (Alt+F4 closes a Windows Forms window without a control box too).
MotifHints motif_hints_for(const WindowChrome &chrome) noexcept {
  MotifHints hints{};
  hints.flags = motif_functions_flag | motif_decorations_flag;
  const bool sizable = form_border_style_is_sizable(chrome.form_border_style);
  const bool minimize = chrome.control_box && chrome.minimize_box;
  const bool maximize = chrome.control_box && chrome.maximize_box;
  hints.functions = motif_function_move | motif_function_close;
  if (sizable) hints.functions = hints.functions | motif_function_resize;
  if (minimize) hints.functions = hints.functions | motif_function_minimize;
  if (maximize) hints.functions = hints.functions | motif_function_maximize;
  if (chrome.form_border_style == FormBorderStyle::none) return hints;
  hints.decorations = motif_decoration_border | motif_decoration_title;
  if (sizable) hints.decorations = hints.decorations | motif_decoration_resize;
  if (chrome.control_box) hints.decorations = hints.decorations | motif_decoration_menu;
  if (minimize) hints.decorations = hints.decorations | motif_decoration_minimize;
  if (maximize) hints.decorations = hints.decorations | motif_decoration_maximize;
  return hints;
}

// _NET_WM_ICON: width, height, then one straight-alpha ARGB pixel per
// CARDINAL. Xlib passes format-32 data as longs, so each element is a long
// even where long is 64 bits.
void write_wm_icon(NativeWindow &window, const HostImage &image) {
  const Atom property = window.runtime.atom("_NET_WM_ICON");
  if (image.width == 0U) {
    XDeleteProperty(window.runtime.display, window.xid, property);
    return;
  }
  const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
  std::vector<unsigned long> data(2U + pixel_count);
  data[0] = image.width;
  data[1] = image.height;
  const std::size_t row_length = static_cast<std::size_t>(image.width) * 4U;
  std::vector<std::byte> straight(row_length);
  for (std::uint32_t row = 0U; row < image.height; ++row) {
    const std::span<const std::byte> source(image.pixels.data() + image.row_bytes * row, row_length);
    gui_forms::detail::premultiplied_bgra_to_straight_bgra(source, straight, image.width);
    for (std::uint32_t column = 0U; column < image.width; ++column) {
      const std::size_t at = static_cast<std::size_t>(column) * 4U;
      const unsigned long blue = std::to_integer<unsigned long>(straight[at]);
      const unsigned long green = std::to_integer<unsigned long>(straight[at + 1U]);
      const unsigned long red = std::to_integer<unsigned long>(straight[at + 2U]);
      const unsigned long alpha = std::to_integer<unsigned long>(straight[at + 3U]);
      data[2U + static_cast<std::size_t>(row) * image.width + column] =
          (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
    }
  }
  // XChangeProperty counts elements in an int; HostImage's byte limit keeps
  // an icon far below it, and a larger one is not published.
  if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return;
  XChangeProperty(window.runtime.display, window.xid, property, XA_CARDINAL, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(data.data()), static_cast<int>(data.size()));
}

// Moves an unmapped window so its client area is centred over its owner
// (WM_TRANSIENT_FOR, center_parent) or the screen; the size hints say the
// program chose the position. Device pixels throughout: the window's device
// size against its owner's or the screen's.
void place_for_start_position(NativeWindow &window, FormStartPosition position) {
  if (position != FormStartPosition::center_screen && position != FormStartPosition::center_parent) return;
  Display *display = window.runtime.display;
  const int screen = DefaultScreen(display);
  int around_x = 0;
  int around_y = 0;
  int around_width = DisplayWidth(display, screen);
  int around_height = DisplayHeight(display, screen);
  ::Window parent{};
  if (position == FormStartPosition::center_parent && XGetTransientForHint(display, window.xid, &parent) != 0 &&
      parent != None) {
    XWindowAttributes attributes{};
    ::Window child{};
    if (XGetWindowAttributes(display, parent, &attributes) != 0 &&
        XTranslateCoordinates(display, parent, DefaultRootWindow(display), 0, 0, &around_x, &around_y, &child) != 0) {
      around_width = attributes.width;
      around_height = attributes.height;
    }
  }
  const int width = window.device_width;
  const int height = window.device_height;
  const int x = around_x + (around_width - width) / 2;
  const int y = around_y + (around_height - height) / 2;
  XSizeHints hints{};
  long supplied{};
  XGetWMNormalHints(display, window.xid, &hints, &supplied);
  hints.flags = hints.flags | USPosition;
  hints.x = x;
  hints.y = y;
  XSetWMNormalHints(display, window.xid, &hints);
  XMoveWindow(display, window.xid, x, y);
}

// Asks for normal, minimized or maximized. Before the first map the state
// is the initial one (WM_HINTS initial_state, _NET_WM_STATE at the map);
// afterwards iconify and _NET_WM_STATE requests.
void apply_window_state(NativeWindow &window, FormWindowState state) {
  Display *display = window.runtime.display;
  window.chrome_window_state = state;
  const Atom vertical = window.runtime.atom("_NET_WM_STATE_MAXIMIZED_VERT");
  const Atom horizontal = window.runtime.atom("_NET_WM_STATE_MAXIMIZED_HORZ");
  if (!window.visible) {
    XWMHints *current = XGetWMHints(display, window.xid);
    XWMHints hints{};
    if (current != nullptr) {
      hints = *current;
      XFree(current);
    }
    hints.flags = hints.flags | StateHint;
    hints.initial_state = state == FormWindowState::minimized ? IconicState : NormalState;
    XSetWMHints(display, window.xid, &hints);
    return;
  }
  if (state == FormWindowState::minimized) {
    XIconifyWindow(display, window.xid, DefaultScreen(display));
    return;
  }
  // Mapping an iconified window again restores it.
  XMapRaised(display, window.xid);
  const long action = state == FormWindowState::maximized ? wm_state_add : wm_state_remove;
  send_wm_state(window, action, vertical, horizontal);
}

} // namespace

WindowChromeFields Services::supported_window_chrome() const noexcept {
  const WindowChromeFields all{WindowChromeFields::all_bits};
  return all;
}

HostWindowChromeResult Services::apply_window_chrome_impl(const WindowChrome &chrome, WindowChromeFields fields) {
  HostWindowChromeResult result{};
  NativeWindow &window = window_;
  if (window.closed || window.xid == 0) {
    result.status = HostServiceStatus{HostServiceError::after_shutdown};
    return result;
  }
  Display *display = window.runtime.display;
  try {
    if (fields.contains(WindowChromeField::text)) {
      if (window.accessibility) (*window.accessibility).set_title(chrome.text);
      XStoreName(display, window.xid, chrome.text.c_str());
      XChangeProperty(display, window.xid, window.runtime.atom("_NET_WM_NAME"), window.runtime.atom("UTF8_STRING"),
                      8, PropModeReplace, reinterpret_cast<const unsigned char *>(chrome.text.data()),
                      static_cast<int>(chrome.text.size()));
      result.applied.add(WindowChromeField::text);
    }
    if (fields.contains(WindowChromeField::icon)) {
      write_wm_icon(window, chrome.icon);
      result.applied.add(WindowChromeField::icon);
    }
    const std::uint32_t frame_bits = static_cast<std::uint32_t>(WindowChromeField::form_border_style) |
                                     static_cast<std::uint32_t>(WindowChromeField::minimize_box) |
                                     static_cast<std::uint32_t>(WindowChromeField::maximize_box) |
                                     static_cast<std::uint32_t>(WindowChromeField::control_box);
    if ((fields.bits & frame_bits) != 0U) {
      const MotifHints hints = motif_hints_for(chrome);
      const Atom property = window.runtime.atom("_MOTIF_WM_HINTS");
      XChangeProperty(display, window.xid, property, property, 32, PropModeReplace,
                      reinterpret_cast<const unsigned char *>(&hints), 5);
      result.applied.bits = result.applied.bits | (fields.bits & frame_bits);
    }
    if (fields.contains(WindowChromeField::top_most)) {
      window.chrome_top_most = chrome.top_most;
      if (window.visible) {
        send_wm_state(window, chrome.top_most ? wm_state_add : wm_state_remove,
                      window.runtime.atom("_NET_WM_STATE_ABOVE"), None);
      }
      result.applied.add(WindowChromeField::top_most);
    }
    if (fields.contains(WindowChromeField::show_in_taskbar)) {
      window.chrome_skip_taskbar = !chrome.show_in_taskbar;
      // A utility window stays off the taskbar (decision 11).
      if (window.visible && !window.utility) {
        send_wm_state(window, chrome.show_in_taskbar ? wm_state_remove : wm_state_add,
                      window.runtime.atom("_NET_WM_STATE_SKIP_TASKBAR"), None);
      }
      if (!window.utility || !chrome.show_in_taskbar) result.applied.add(WindowChromeField::show_in_taskbar);
    }
    if (fields.contains(WindowChromeField::opacity)) {
      const Atom property = window.runtime.atom("_NET_WM_WINDOW_OPACITY");
      if (chrome.opacity >= 1.0) {
        XDeleteProperty(display, window.xid, property);
      } else {
        // EWMH: a CARDINAL from 0 (transparent) to 0xffffffff (opaque).
        const unsigned long value = static_cast<unsigned long>(std::llround(chrome.opacity * 4294967295.0));
        XChangeProperty(display, window.xid, property, XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(&value), 1);
      }
      result.applied.add(WindowChromeField::opacity);
    }
    if (fields.contains(WindowChromeField::minimum_size)) {
      window.chrome_minimum_size = chrome.minimum_size;
      // Logical units, written at the window's scale.
      window.write_minimum_hint();
      result.applied.add(WindowChromeField::minimum_size);
    }
    if (fields.contains(WindowChromeField::start_position) && !window.shown_once) {
      place_for_start_position(window, chrome.start_position);
      result.applied.add(WindowChromeField::start_position);
    }
    if (fields.contains(WindowChromeField::window_state)) {
      apply_window_state(window, chrome.window_state);
      result.applied.add(WindowChromeField::window_state);
    }
    XFlush(display);
  } catch (...) {
    // Only the allocations (the icon's pixels) throw; what was applied
    // before stays applied.
    result.status = HostServiceStatus{HostServiceError::backend_failure};
  }
  return result;
}

HostServiceStatus Services::set_cursor_impl(CursorKind cursor) {
  if (window_.cursor_set && window_.current_cursor == cursor)
    return {};
  const unsigned shape = font_cursor_shape(cursor);
  const Cursor native = XCreateFontCursor(window_.runtime.display, shape);
  if (native == None)
    return {HostServiceError::backend_failure};
  XDefineCursor(window_.runtime.display, window_.xid, native);
  window_.current_cursor = cursor;
  window_.cursor_set = true;
  XFreeCursor(window_.runtime.display, native);
  return {};
}
HostServiceStatus Services::set_pointer_capture_impl(bool captured,
                                                     std::uint64_t) {
  if (!captured) {
    XUngrabPointer(window_.runtime.display, CurrentTime);
    return {};
  }
  const int status =
      XGrabPointer(window_.runtime.display, window_.xid, False,
                   ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                   GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
  return {status == GrabSuccess ? HostServiceError::none
                                : HostServiceError::backend_failure};
}
HostServiceStatus Services::set_relative_pointer_impl(const HostRelativePointerRequest &request) {
  if (relative_.active)
    release_relative_pointer();
  if (!request.active)
    return {};
#if defined(GUI_FORMS_HAVE_XI2)
  Display *const display = window_.runtime.display;
  if (window_.runtime.xi_opcode < 0)
    return {HostServiceError::unsupported};
  select_raw_motion(display, true);
  relative_ = request;
  if (request.mode == RelativePointerMode::free) {
    XFlush(display);
    return {};
  }
  const double scale = window_.scale;
  ::Window confine_to = window_.xid;
  if (request.mode == RelativePointerMode::confined) {
    const int x = device_coordinate(std::floor(request.confine.x * scale));
    const int y = device_coordinate(std::floor(request.confine.y * scale));
    const unsigned width = device_extent(request.confine.width, scale);
    const unsigned height = device_extent(request.confine.height, scale);
    confine_window_ = XCreateWindow(display, window_.xid, x, y, width, height, 0, CopyFromParent, InputOnly,
                                    CopyFromParent, 0, nullptr);
    XMapWindow(display, confine_window_);
    confine_to = confine_window_;
  } else {
    // Held where it is: each sample warps it back here.
    ::Window root_return{};
    ::Window child_return{};
    int root_x = 0;
    int root_y = 0;
    unsigned mask_return = 0;
    static_cast<void>(XQueryPointer(display, window_.xid, &root_return, &child_return, &root_x, &root_y,
                                    &lock_x_, &lock_y_, &mask_return));
  }
  if (request.hide_cursor || request.mode == RelativePointerMode::locked) {
    XFixesHideCursor(display, window_.xid);
    relative_cursor_hidden_ = true;
  }
  const int grabbed = XGrabPointer(display, window_.xid, False,
                                   ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                                   GrabModeAsync, confine_to, None, CurrentTime);
  if (grabbed != GrabSuccess) {
    release_relative_pointer();
    return {HostServiceError::backend_failure};
  }
  XFlush(display);
  return {};
#else
  return {HostServiceError::unsupported};
#endif
}

void Services::release_relative_pointer() noexcept {
  Display *const display = window_.runtime.display;
  if (relative_.mode != RelativePointerMode::free)
    XUngrabPointer(display, CurrentTime);
  if (confine_window_ != 0) {
    XDestroyWindow(display, confine_window_);
    confine_window_ = 0;
  }
  if (relative_cursor_hidden_) {
    XFixesShowCursor(display, window_.xid);
    relative_cursor_hidden_ = false;
  }
#if defined(GUI_FORMS_HAVE_XI2)
  if (window_.runtime.xi_opcode >= 0)
    select_raw_motion(display, false);
#endif
  relative_ = {};
  XFlush(display);
}

HostServiceStatus Services::warp_pointer_impl(Point position) {
  Display *const display = window_.runtime.display;
  const double scale = window_.scale;
  const int x = device_coordinate(std::round(position.x * scale));
  const int y = device_coordinate(std::round(position.y * scale));
  XWarpPointer(display, None, window_.xid, 0, 0, 0, 0, x, y);
  if (relative_.active && relative_.mode == RelativePointerMode::locked) {
    lock_x_ = x;
    lock_y_ = y;
  }
  XFlush(display);
  return {};
}

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).

// open_url: the desktop's address opener, xdg-open, found on PATH. A
// shell starts it in the background and exits at once, and that shell is
// reaped here, so the opener is never a child of this process and nothing
// waits for the browser. The address is the shell's first argument,
// quoted in the script, so it is never parsed as shell text (open_url has
// already refused spaces and control characters). Exit 127 from the shell:
// no xdg-open on PATH.
HostServiceStatus Services::open_url_impl(std::string_view url_utf8) {
  char shell_path[] = "/bin/sh";
  char command_flag[] = "-c";
  char script[] = "command -v xdg-open >/dev/null 2>&1 || exit 127; "
                  "xdg-open \"$1\" </dev/null >/dev/null 2>&1 & exit 0";
  char script_name[] = "gui-forms-open-url";
  std::vector<char> address(url_utf8.begin(), url_utf8.end());
  address.push_back('\0');
  char *const arguments[] = {shell_path, command_flag, script, script_name, address.data(), nullptr};
  pid_t child = 0;
  const int spawned = posix_spawn(&child, shell_path, nullptr, nullptr, arguments, environ);
  if (spawned != 0)
    return {HostServiceError::backend_failure};
  int status = 0;
  pid_t waited = -1;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  const bool started = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!started)
    return {HostServiceError::backend_failure};
  return {};
}
#endif

void Services::relative_raw_motion(Point raw, Point accelerated, std::uint64_t timestamp) {
  if (!relative_.active)
    return;
  const double scale = window_.scale;
  const Point device = relative_.raw_motion ? raw : accelerated;
  HostRelativeMotionEvent motion{};
  motion.delta = Point{device.x / scale, device.y / scale};
  motion.unaccelerated = relative_.raw_motion;
  motion.timestamp_nanoseconds = timestamp;
  // No position: the window keeps it from the last absolute one and the
  // deltas (a query per sample would be a round trip).
  const bool locked = relative_.mode == RelativePointerMode::locked;
  static_cast<void>(window_.dispatch(motion));
  // The dispatch may have ended the mode (a handler released it).
  if (locked && relative_.active && relative_.mode == RelativePointerMode::locked) {
    XWarpPointer(window_.runtime.display, None, window_.xid, 0, 0, 0, 0, lock_x_, lock_y_);
  }
}

#if !defined(GUI_FORMS_HIGH_INTEGRITY) || defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
HostClipboardTextResult Services::read_clipboard_text_impl() {
  Runtime &runtime = window_.runtime;
  HostClipboardTextResult result;
  result.generation = runtime.clipboard_generation;
  const std::vector<std::byte> bytes = runtime.read_selection(
      window_, runtime.atom("CLIPBOARD"), runtime.atom("UTF8_STRING"));
  if (!bytes.empty()) {
    result.text_utf8.assign(reinterpret_cast<const char *>(bytes.data()),
                            bytes.size());
    result.has_text = true;
  }
  return result;
}
HostServiceStatus Services::write_clipboard_text_impl(std::string_view text) {
  Runtime &runtime = window_.runtime;
  runtime.clipboard_text = text;
  runtime.clipboard_image = {};
  runtime.clipboard_png.clear();
  runtime.clipboard_owner = window_.xid;
  ++runtime.clipboard_generation;
  XSetSelectionOwner(runtime.display, runtime.atom("CLIPBOARD"), window_.xid,
                     CurrentTime);
  XFlush(runtime.display);
  return {XGetSelectionOwner(runtime.display, runtime.atom("CLIPBOARD")) ==
                  window_.xid
              ? HostServiceError::none
              : HostServiceError::backend_failure};
}
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
HostClipboardFilesResult Services::read_clipboard_files_impl() {
  Runtime& runtime = window_.runtime;
  std::vector<std::byte> bytes = runtime.read_selection(
      window_, runtime.atom("CLIPBOARD"), runtime.atom("text/uri-list"));
  if (bytes.empty()) {
    bytes = runtime.read_selection(window_, runtime.atom("CLIPBOARD"),
                                   runtime.atom("x-special/gnome-copied-files"));
  }
  HostClipboardFilesResult result;
  result.paths_utf8 = local_files(bytes).paths_utf8;
  result.generation = runtime.clipboard_generation;
  return result;
}
HostClipboardImageResult Services::read_clipboard_image_impl() {
  Runtime &runtime = window_.runtime;
  // The private target offered (GFIM version 2, premultiplied BGRA8), then
  // the one offered before SK2 (version 1, straight RGBA8), then PNG.
  std::vector<std::byte> bytes =
      runtime.read_selection(window_, runtime.atom("CLIPBOARD"),
                             runtime.atom("application/x-gui-forms-bgra"));
  if (bytes.empty())
    bytes = runtime.read_selection(window_, runtime.atom("CLIPBOARD"),
                                   runtime.atom("application/x-gui-forms-rgba"));
  if (!bytes.empty())
    return detail::decode_clipboard_image(bytes, runtime.clipboard_generation,
                                          false);
  bytes = runtime.read_selection(window_, runtime.atom("CLIPBOARD"),
                                 runtime.atom("image/png"));
  if (!bytes.empty())
    return detail::decode_clipboard_png(bytes, runtime.clipboard_generation);
  return HostClipboardImageResult::nothing_read(runtime.clipboard_generation);
}
HostServiceStatus Services::write_clipboard_image_impl(HostImageView image) {
  Runtime &runtime = window_.runtime;
  std::vector<std::byte> png = detail::encode_clipboard_png(image);
  if (png.empty())
    return {HostServiceError::backend_failure};
  runtime.clipboard_image = detail::copy_host_image(image);
  runtime.clipboard_png = std::move(png);
  runtime.clipboard_text.clear();
  runtime.clipboard_owner = window_.xid;
  ++runtime.clipboard_generation;
  XSetSelectionOwner(runtime.display, runtime.atom("CLIPBOARD"), window_.xid,
                     CurrentTime);
  XFlush(runtime.display);
  return {XGetSelectionOwner(runtime.display, runtime.atom("CLIPBOARD")) ==
                  window_.xid
              ? HostServiceError::none
              : HostServiceError::backend_failure};
}
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
HostServiceStatus Services::start_drag_impl(std::uint64_t session_id, const HostDragSourceRequest &request) {
  XdndDragOutcome outcome = run_xdnd_drag(window_, request);
  if (!outcome.status.accepted())
    return outcome.status;
  publish_drag_source_end(HostDragSourceEnd{session_id, outcome.performed_effect});
  // The grab took the release from the window: it gets it now, so its
  // pressed and capture state end as after any press.
  if (outcome.release.has_value())
    window_.event(*outcome.release);
  return {};
}
#endif
HostDialogResult Services::show_dialog_impl(const HostDialogRequest &request) {
  return window_.dialog(request);
}
HostServiceStatus
Services::play_sound_cue_impl(const HostSoundCueRequest &request) {
  XBell(window_.runtime.display, static_cast<int>(request.gain * 100.0) - 100);
  return {};
}
namespace {
// text as ISO Latin-1 for the STRING target: each code point up to U+00FF
// as its byte, anything above (or a malformed sequence) as '?'.
std::vector<std::byte> latin1_from_utf8(std::string_view text) {
  std::vector<std::byte> latin1;
  latin1.reserve(text.size());
  std::size_t index = 0U;
  while (index < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80U) {
      latin1.push_back(static_cast<std::byte>(lead));
      ++index;
      continue;
    }
    std::size_t length = 1U;
    if ((lead & 0xE0U) == 0xC0U)
      length = 2U;
    else if ((lead & 0xF0U) == 0xE0U)
      length = 3U;
    else if ((lead & 0xF8U) == 0xF0U)
      length = 4U;
    const bool whole = length > 1U && length <= text.size() - index;
    if (whole && length == 2U) {
      const unsigned char trail = static_cast<unsigned char>(text[index + 1U]);
      const unsigned code = ((lead & 0x1FU) << 6U) | (trail & 0x3FU);
      const bool latin = (trail & 0xC0U) == 0x80U && code <= 0xFFU;
      latin1.push_back(latin ? static_cast<std::byte>(code) : std::byte{'?'});
    } else {
      latin1.push_back(std::byte{'?'});
    }
    index += whole ? length : 1U;
  }
  return latin1;
}
} // namespace
bool Runtime::write_selection_reply(const XSelectionRequestEvent &request, Atom property,
                                    std::vector<std::byte> bytes) {
  if (bytes.size() > 64U * 1024U) {
    if (transfers.size() >= 8)
      return false;
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, request.requestor, &attributes) == 0)
      return false;
    XSelectInput(display, request.requestor,
                 attributes.your_event_mask | PropertyChangeMask | StructureNotifyMask);
    const unsigned long count = static_cast<unsigned long>(bytes.size());
    XChangeProperty(display, request.requestor, property, atom("INCR"), 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&count), 1);
    transfers.push_back({request.requestor, property, request.target, std::move(bytes), 0,
                         std::chrono::steady_clock::now() + std::chrono::seconds(30)});
  } else {
    XChangeProperty(display, request.requestor, property, request.target, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<int>(bytes.size()));
  }
  return true;
}
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
const std::vector<std::byte> *Runtime::drag_offer_bytes(Atom target) const noexcept {
  for (const DragOfferTarget &offered : drag_offer)
    if (offered.target == target)
      return &offered.bytes;
  return nullptr;
}
#endif
void Runtime::selection_request(const XSelectionRequestEvent &request) {
  const ForeignWindowScope guard(display, request.requestor, ForeignWindowErrors::selection_reply);
  XEvent response{};
  response.xselection.type = SelectionNotify;
  response.xselection.display = display;
  response.xselection.requestor = request.requestor;
  response.xselection.selection = request.selection;
  response.xselection.target = request.target;
  response.xselection.time = request.time;
  response.xselection.property = None;
  const Atom property =
      request.property == None ? request.target : request.property;
  // What the request is answered with: a target list, or the target's
  // bytes; neither refuses it.
  std::vector<Atom> targets;
  std::vector<std::byte> bytes;
  bool answered = false;
  if (request.selection == atom("CLIPBOARD") &&
      request.owner == clipboard_owner && clipboard_owner != 0) {
    if (request.target == atom("TARGETS")) {
      targets.push_back(atom("TARGETS"));
      if (clipboard_image.width != 0) {
        targets.push_back(atom("image/png"));
        targets.push_back(atom("application/x-gui-forms-bgra"));
      } else {
        targets.push_back(atom("UTF8_STRING"));
        targets.push_back(XA_STRING);
      }
    } else {
      // A text target is answered whenever TARGETS offers text, so an empty
      // clipboard text is pasted as empty rather than refused; STRING is
      // ISO Latin-1 by ICCCM, so it is converted (A-gf-host-01-F10).
      if (request.target == atom("UTF8_STRING") &&
          clipboard_image.width == 0) {
        bytes.resize(clipboard_text.size());
        std::memcpy(bytes.data(), clipboard_text.data(), bytes.size());
        answered = true;
      } else if (request.target == XA_STRING && clipboard_image.width == 0) {
        bytes = latin1_from_utf8(clipboard_text);
        answered = true;
      } else if (request.target == atom("image/png"))
        bytes = clipboard_png;
      else if (request.target == atom("application/x-gui-forms-bgra") &&
               clipboard_image.width != 0)
        bytes = detail::encode_clipboard_image(clipboard_image.view());
      if (!bytes.empty())
        answered = true;
    }
  }
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // A running drag's data (run_xdnd_drag): its targets, each answered
  // even when empty (an empty text).
  if (request.selection == atom("XdndSelection") &&
      request.owner == drag_offer_owner && drag_offer_owner != 0) {
    if (request.target == atom("TARGETS")) {
      targets.push_back(atom("TARGETS"));
      for (const DragOfferTarget &offered : drag_offer)
        targets.push_back(offered.target);
    } else if (const std::vector<std::byte> *offered = drag_offer_bytes(request.target)) {
      bytes = *offered;
      answered = true;
    }
  }
#endif
  if (!targets.empty()) {
    XChangeProperty(display, request.requestor, property, XA_ATOM, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(targets.data()),
                    static_cast<int>(targets.size()));
    response.xselection.property = property;
  } else if (answered) {
    if (!write_selection_reply(request, property, std::move(bytes))) {
      XSendEvent(display, request.requestor, False, 0, &response);
      XFlush(display);
      return;
    }
    response.xselection.property = property;
  }
  XSendEvent(display, request.requestor, False, 0, &response);
  XFlush(display);
}
void Runtime::selection_property(const XPropertyEvent &event) {
  if (event.state != PropertyDelete)
    return;
  for (std::size_t i = 0; i < transfers.size(); ++i) {
    SelectionTransfer &transfer = transfers[i];
    if (transfer.requestor != event.window || transfer.property != event.atom)
      continue;
    const ForeignWindowScope guard(display, transfer.requestor);
    transfer.deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const std::size_t count = std::min<std::size_t>(
        64U * 1024U, transfer.bytes.size() - transfer.offset);
    XChangeProperty(display, transfer.requestor, transfer.property,
                    transfer.target, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(
                        transfer.bytes.data() + transfer.offset),
                    static_cast<int>(count));
    transfer.offset += count;
    if (count == 0)
      transfers.erase(transfers.begin() + static_cast<std::ptrdiff_t>(i));
    XFlush(display);
    break;
  }
}
namespace {
class DeferredSelectionInput final {
public:
  explicit DeferredSelectionInput(Display *display) : display_(display) {}
  ~DeferredSelectionInput() {
    // Reinsert backwards because XPutBackEvent prepends to the event queue.
    for (std::size_t index = events.size(); index > 0; --index)
      XPutBackEvent(display_, &events[index - 1]);
  }
  std::vector<XEvent> events;

private:
  Display *display_;
};
} // namespace
std::vector<std::byte> Runtime::read_selection(NativeWindow &window,
                                               Atom selection, Atom target,
                                               Time time, std::size_t limit) {
  if (XGetSelectionOwner(display, selection) == None)
    return {};
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // A drop of this program's own drag reads the offer, without a round
  // trip through the server.
  if (selection == atom("XdndSelection") && drag_offer_owner != 0 &&
      XGetSelectionOwner(display, selection) == drag_offer_owner) {
    const std::vector<std::byte> *offered = drag_offer_bytes(target);
    if (offered == nullptr || (*offered).size() > limit)
      return {};
    return *offered;
  }
#endif
  if (selection == atom("CLIPBOARD") &&
      XGetSelectionOwner(display, selection) == clipboard_owner) {
    if (target == atom("image/png"))
      return clipboard_png;
    if (target == atom("application/x-gui-forms-bgra") &&
        clipboard_image.width != 0)
      return detail::encode_clipboard_image(clipboard_image.view());
    if (target == atom("UTF8_STRING")) {
      std::vector<std::byte> result(clipboard_text.size());
      std::memcpy(result.data(), clipboard_text.data(), result.size());
      return result;
    }
    return {};
  }
  const Atom property = atom("GUI_FORMS_SELECTION");
  XDeleteProperty(display, window.xid, property);
  XConvertSelection(display, selection, target, property, window.xid, time);
  XFlush(display);
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  DeferredSelectionInput deferred(display);
  bool incremental = false;
  std::vector<std::byte> result;
  while (std::chrono::steady_clock::now() < deadline) {
    while (XPending(display) > 0) {
      XEvent event{};
      XNextEvent(display, &event);
      const bool notify = event.type == SelectionNotify &&
                          event.xselection.requestor == window.xid &&
                          event.xselection.selection == selection;
      const bool chunk = incremental && event.type == PropertyNotify &&
                         event.xproperty.window == window.xid &&
                         event.xproperty.atom == property &&
                         event.xproperty.state == PropertyNewValue;
      if (notify || chunk) {
        if (notify && event.xselection.property == None)
          return {};
        Atom type{};
        int format{};
        unsigned long count{}, remaining{};
        unsigned char *data = nullptr;
        const int status = XGetWindowProperty(
            display, window.xid, property, 0,
            static_cast<long>(limit / 4U + 4U), True, AnyPropertyType, &type,
            &format, &count, &remaining, &data);
        if (status != Success || remaining != 0) {
          if (data != nullptr)
            XFree(data);
          return {};
        }
        if (type == atom("INCR")) {
          incremental = true;
          if (data != nullptr)
            XFree(data);
          XFlush(display);
          continue;
        }
        if (type != target || format != 8 || count > limit - result.size()) {
          if (data != nullptr)
            XFree(data);
          return {};
        }
        if (count > 0) {
          const std::byte *start = reinterpret_cast<const std::byte *>(data);
          result.insert(result.end(), start, start + count);
        }
        if (data != nullptr)
          XFree(data);
        XFlush(display);
        if (!incremental || count == 0)
          return result;
      } else if (event.type == SelectionRequest ||
                 event.type == SelectionClear || event.type == PropertyNotify ||
                 event.type == Expose || event.type == ConfigureNotify)
        dispatch(event);
      else
        deferred.events.push_back(event);
    }
    pollfd descriptor{ConnectionNumber(display), POLLIN, 0};
    static_cast<void>(poll(&descriptor, 1, 25));
  }
  return {};
}
} // namespace gui_forms::host::linux_detail
