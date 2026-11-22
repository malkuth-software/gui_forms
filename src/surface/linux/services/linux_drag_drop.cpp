#include "../application/linux_host_internal.hpp"
#include "x11_foreign_window.hpp"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <poll.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace gui_forms::host::linux_detail {
namespace {
int digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace
DragFileListData local_files(const std::vector<std::byte> &data) {
  DragFileListData files;
  if (data.size() > DragLimits::maximum_total_bytes)
    return files;
  const std::string_view text(reinterpret_cast<const char *>(data.data()),
                              data.size());
  std::size_t start = 0;
  while (start < text.size() &&
         files.paths_utf8.size() < DragLimits::maximum_paths) {
    const std::size_t end = text.find_first_of("\r\n", start);
    std::string_view uri =
        text.substr(start, end == std::string_view::npos ? text.size() - start
                                                         : end - start);
    if (uri.starts_with("file://localhost/"))
      uri.remove_prefix(16);
    else if (uri.starts_with("file:///"))
      uri.remove_prefix(7);
    else
      uri = {};
    std::string path;
    bool valid = !uri.empty() && uri.front() == '/';
    for (std::size_t i = 0; valid && i < uri.size(); ++i) {
      char c = uri[i];
      if (c == '%') {
        if (i + 2 >= uri.size() || digit(uri[i + 1]) < 0 ||
            digit(uri[i + 2]) < 0) {
          valid = false;
          break;
        }
        c = static_cast<char>(digit(uri[i + 1]) * 16 + digit(uri[i + 2]));
        i += 2;
      }
      if (c == '\0' || path.size() >= DragLimits::maximum_path_bytes) {
        valid = false;
        break;
      }
      path += c;
    }
    if (valid)
      files.paths_utf8.push_back(std::move(path));
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return files;
}
namespace {
void reply(Runtime &runtime, ::Window source, ::Window target, const char *kind,
           bool accepted, bool position) {
  const ForeignWindowScope guard(runtime.display, source);
  XEvent event{};
  event.xclient.type = ClientMessage;
  event.xclient.display = runtime.display;
  event.xclient.window = source;
  event.xclient.message_type = runtime.atom(kind);
  event.xclient.format = 32;
  event.xclient.data.l[0] = static_cast<long>(target);
  event.xclient.data.l[1] = (accepted ? 1 : 0) | (position ? 2 : 0);
  event.xclient.data.l[position ? 4 : 2] =
      accepted ? static_cast<long>(runtime.atom("XdndActionCopy")) : 0;
  XSendEvent(runtime.display, source, False, NoEventMask, &event);
}
} // namespace
bool NativeWindow::drag_message(const XClientMessageEvent &message) {
  const Atom kind = message.message_type;
  if (kind == runtime.atom("XdndEnter")) {
    if (drag.session_id != 0) {
      drag.action = DragAction::leave;
      drag.accepted_effect = DragEffect::none;
      drag.handled = false;
      static_cast<void>(dispatch(drag));
    }
    drag = {};
    drag_source = static_cast<::Window>(message.data.l[0]);
    drag_type = None;
    const unsigned version =
        static_cast<unsigned long>(message.data.l[1]) >> 24U;
    if (version > 5)
      return true;
    const Atom uri = runtime.atom("text/uri-list");
    if ((message.data.l[1] & 1) != 0) {
      const ForeignWindowScope guard(runtime.display, drag_source);
      Atom type{};
      int format{};
      unsigned long count{}, remaining{};
      unsigned char *data = nullptr;
      if (XGetWindowProperty(runtime.display, drag_source,
                             runtime.atom("XdndTypeList"), 0, 1024, False,
                             XA_ATOM, &type, &format, &count, &remaining,
                             &data) == Success &&
          type == XA_ATOM && format == 32 && remaining == 0) {
        const Atom *atoms = reinterpret_cast<const Atom *>(data);
        for (unsigned long i = 0; i < count; ++i)
          if (atoms[i] == uri)
            drag_type = uri;
      }
      if (data != nullptr)
        XFree(data);
    } else {
      for (unsigned i = 2; i < 5; ++i)
        if (static_cast<Atom>(message.data.l[i]) == uri)
          drag_type = uri;
    }
    return true;
  }
  if (kind != runtime.atom("XdndPosition") &&
      kind != runtime.atom("XdndLeave") && kind != runtime.atom("XdndDrop"))
    return false;
  if (static_cast<::Window>(message.data.l[0]) != drag_source ||
      drag_source == 0)
    return true;
  if (kind == runtime.atom("XdndLeave")) {
    if (drag.session_id != 0) {
      drag.action = DragAction::leave;
      drag.accepted_effect = DragEffect::none;
      drag.handled = false;
      static_cast<void>(dispatch(drag));
    }
    drag = {};
    drag_source = 0;
    drag_type = None;
    return true;
  }
  if (kind == runtime.atom("XdndPosition")) {
    int x{}, y{};
    ::Window child{};
    const unsigned long packed = static_cast<unsigned long>(message.data.l[2]);
    XTranslateCoordinates(runtime.display, DefaultRootWindow(runtime.display),
                          xid, static_cast<short>(packed >> 16U),
                          static_cast<short>(packed & 0xffffU), &x, &y, &child);
    const bool entering = drag.session_id == 0;
    if (entering && drag_type != None && !modal) {
      DragFileListData files = local_files(runtime.read_selection(
          *this, runtime.atom("XdndSelection"), drag_type,
          static_cast<Time>(message.data.l[3]),
          DragLimits::maximum_total_bytes));
      if (!files.paths_utf8.empty()) {
        drag.session_id = ++drag_sequence;
        drag.allowed_effects = DragEffect::copy;
        drag.items.push_back(std::move(files));
      } else {
        // Tried once per XdndEnter (A-gf-host-01-F6): a source with no
        // local files, or one that never answers, is not asked again on
        // every position message, each read waiting up to 5 seconds. The
        // next XdndEnter sets drag_type again.
        drag_type = None;
      }
    }
    // Window device pixels to logical units, at the boundary.
    drag.position = {static_cast<double>(x) / scale, static_cast<double>(y) / scale};
    drag.action = entering ? DragAction::enter : DragAction::over;
    drag.accepted_effect = DragEffect::none;
    drag.handled = false;
    const bool accepted = !modal && drag.session_id != 0 &&
                          dispatch(drag).drag_effect == DragEffect::copy;
    drag.accepted_effect = accepted ? DragEffect::copy : DragEffect::none;
    reply(runtime, drag_source, xid, "XdndStatus", accepted, true);
    return true;
  }
  bool accepted = false;
  if (!modal && drag.session_id != 0 &&
      drag.accepted_effect == DragEffect::copy) {
    drag.action = DragAction::drop;
    drag.accepted_effect = DragEffect::none;
    drag.handled = false;
    accepted = dispatch(drag).drag_effect == DragEffect::copy;
  }
  reply(runtime, drag_source, xid, "XdndFinished", accepted, false);
  drag = {};
  drag_source = 0;
  drag_type = None;
  return true;
}
} // namespace gui_forms::host::linux_detail

// The XDND source: drags from the program to any XDND-aware window, this
// program's own included (freedesktop.org XDND, versions 3 to 5).
namespace gui_forms::host::linux_detail {
namespace {

// How deep the walk from the root to the window under the pointer goes; a
// deeper tree finds no target.
constexpr int maximum_target_depth = 32;
// How long a drop waits for XdndFinished.
constexpr std::chrono::seconds finish_wait{5};
// The newest XDND version spoken.
constexpr unsigned xdnd_version = 5U;
constexpr unsigned all_buttons = Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask;

bool unreserved_uri_byte(unsigned char byte) noexcept {
  const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
  const bool digit = byte >= '0' && byte <= '9';
  const bool mark = byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/';
  return letter || digit || mark;
}

// The key-button mask bit of X button `button` (1 to 5), or 0.
unsigned button_mask(unsigned button) noexcept {
  if (button < 1U || button > 5U)
    return 0U;
  const unsigned mask = Button1Mask << (button - 1U);
  return mask;
}

// An XDND-aware window: where messages go (its proxy, when it names one)
// and the version both sides speak.
struct XdndTarget final {
  ::Window window{};
  ::Window destination{};
  unsigned version{};
};

// One 32-bit property value of `window`, when it has one of `type`.
std::optional<unsigned long> first_property_value(Runtime &runtime, ::Window window, Atom property, Atom type) {
  std::optional<unsigned long> value;
  Atom actual{};
  int format{};
  unsigned long count{}, remaining{};
  unsigned char *data = nullptr;
  const int status = XGetWindowProperty(runtime.display, window, property, 0, 1, False, type, &actual, &format,
                                        &count, &remaining, &data);
  if (status == Success && actual == type && format == 32 && count >= 1U && data != nullptr)
    value = reinterpret_cast<const unsigned long *>(data)[0];
  if (data != nullptr)
    XFree(data);
  return value;
}

// `window` as a target when it (or the proxy it names, which names itself)
// announces XDND version 3 or later.
std::optional<XdndTarget> xdnd_target(Runtime &runtime, ::Window window) {
  std::optional<XdndTarget> target;
  ::Window destination = window;
  const std::optional<unsigned long> proxy =
      first_property_value(runtime, window, runtime.atom("XdndProxy"), XA_WINDOW);
  if (proxy.has_value() && *proxy != 0U) {
    const std::optional<unsigned long> confirmed =
        first_property_value(runtime, static_cast<::Window>(*proxy), runtime.atom("XdndProxy"), XA_WINDOW);
    if (confirmed.has_value() && *confirmed == *proxy)
      destination = static_cast<::Window>(*proxy);
  }
  const std::optional<unsigned long> version =
      first_property_value(runtime, destination, runtime.atom("XdndAware"), XA_ATOM);
  if (!version.has_value() || *version < 3U)
    return target;
  target = XdndTarget{window, destination, std::min(static_cast<unsigned>(*version), xdnd_version)};
  return target;
}

// The XDND-aware window under the root position, walking down from the
// root's child at that point (a window manager's frame) at most
// maximum_target_depth levels; none when no window there is aware.
std::optional<XdndTarget> target_at(Runtime &runtime, int root_x, int root_y) {
  // Any window of the walk may be destroyed meanwhile.
  const ForeignWindowScope guard(runtime.display, None, ForeignWindowErrors::any_bad_window);
  const ::Window root = DefaultRootWindow(runtime.display);
  ::Window current = root;
  for (int depth = 0; depth < maximum_target_depth; ++depth) {
    int x{}, y{};
    ::Window child{};
    if (XTranslateCoordinates(runtime.display, root, current, root_x, root_y, &x, &y, &child) == False ||
        child == None)
      break;
    const std::optional<XdndTarget> target = xdnd_target(runtime, child);
    if (target.has_value())
      return target;
    current = child;
  }
  return std::nullopt;
}

Atom action_atom(Runtime &runtime, DragEffect effect) {
  if (effect == DragEffect::move)
    return runtime.atom("XdndActionMove");
  if (effect == DragEffect::link)
    return runtime.atom("XdndActionLink");
  return runtime.atom("XdndActionCopy");
}

DragEffect action_effect(Runtime &runtime, Atom action) {
  if (action == runtime.atom("XdndActionCopy"))
    return DragEffect::copy;
  if (action == runtime.atom("XdndActionMove"))
    return DragEffect::move;
  if (action == runtime.atom("XdndActionLink"))
    return DragEffect::link;
  return DragEffect::none;
}

// The drag's targets and bytes: a file list as text/uri-list, text as
// UTF8_STRING and text/plain;charset=utf-8, bytes under their media type's
// atom; the first item of each target is offered.
std::vector<Runtime::DragOfferTarget> drag_offer(Runtime &runtime, const HostDragSourceRequest &request) {
  std::vector<Runtime::DragOfferTarget> offer;
  for (const DragDataItem &item : request.items) {
    std::vector<Runtime::DragOfferTarget> made;
    if (const DragFileListData *files = std::get_if<DragFileListData>(&item)) {
      made.push_back({runtime.atom("text/uri-list"), xdnd_uri_list(*files)});
    } else if (const DragTextData *text = std::get_if<DragTextData>(&item)) {
      std::vector<std::byte> bytes((*text).text_utf8.size());
      if (!bytes.empty())
        std::memcpy(bytes.data(), (*text).text_utf8.data(), bytes.size());
      made.push_back({runtime.atom("UTF8_STRING"), bytes});
      made.push_back({runtime.atom("text/plain;charset=utf-8"), std::move(bytes)});
    } else if (const DragBinaryData *binary = std::get_if<DragBinaryData>(&item)) {
      std::vector<std::byte> bytes((*binary).bytes.size());
      if (!bytes.empty())
        std::memcpy(bytes.data(), (*binary).bytes.data(), bytes.size());
      // HostServices::start_drag validated the media type: UTF-8, no NUL.
      made.push_back({runtime.atom((*binary).media_type.c_str()), std::move(bytes)});
    }
    for (Runtime::DragOfferTarget &target : made) {
      bool offered = false;
      for (const Runtime::DragOfferTarget &existing : offer)
        offered = offered || existing.target == target.target;
      if (!offered)
        offer.push_back(std::move(target));
    }
  }
  return offer;
}

// Holds a drag's grabs, selection and type list for its length, released
// on every exit: the pointer and keyboard ungrabbed, the offer cleared and
// XdndSelection given up when still the window's.
class XdndSourceScope final {
public:
  XdndSourceScope(Runtime &runtime, ::Window window) noexcept : runtime_(runtime), window_(window) {}
  ~XdndSourceScope() {
    if (keyboard_grabbed)
      XUngrabKeyboard(runtime_.display, CurrentTime);
    if (pointer_grabbed)
      XUngrabPointer(runtime_.display, CurrentTime);
    if (runtime_.drag_offer_owner == window_) {
      runtime_.drag_offer_owner = 0;
      runtime_.drag_offer.clear();
    }
    const Atom selection = runtime_.atom("XdndSelection");
    if (XGetSelectionOwner(runtime_.display, selection) == window_)
      XSetSelectionOwner(runtime_.display, selection, None, CurrentTime);
    XDeleteProperty(runtime_.display, window_, runtime_.atom("XdndTypeList"));
    XFlush(runtime_.display);
  }
  XdndSourceScope(const XdndSourceScope &) = delete;
  XdndSourceScope &operator=(const XdndSourceScope &) = delete;
  bool pointer_grabbed{};
  bool keyboard_grabbed{};

private:
  Runtime &runtime_;
  ::Window window_;
};

// Where a drag stands: following the pointer, waiting for XdndFinished
// after a drop, or ended.
enum class XdndPhase : std::uint8_t { dragging, dropping, ended };

// One drag from the program, run by run(): its event loop follows the
// pointer from target to target and ends at a drop, a cancel or the wait
// for XdndFinished running out.
class XdndSource final {
public:
  XdndSource(NativeWindow &window, const HostDragSourceRequest &request, unsigned buttons,
             std::vector<Atom> types) noexcept
      : window_(window), runtime_(window.runtime), allowed_(request.allowed_effects), buttons_(buttons),
        types_(std::move(types)) {}

  // Runs the loop until the drag ends; the outcome's status is accepted.
  XdndDragOutcome run(int root_x, int root_y, unsigned state) {
    moved(root_x, root_y, state, CurrentTime);
    std::chrono::steady_clock::time_point deadline{};
    while (phase_ != XdndPhase::ended) {
      while (phase_ != XdndPhase::ended && XPending(runtime_.display) > 0) {
        XEvent event{};
        XNextEvent(runtime_.display, &event);
        const bool was_dragging = phase_ == XdndPhase::dragging;
        handle(event);
        if (was_dragging && phase_ == XdndPhase::dropping)
          deadline = std::chrono::steady_clock::now() + finish_wait;
      }
      if (phase_ == XdndPhase::dropping && std::chrono::steady_clock::now() >= deadline) {
        // No XdndFinished: a version 3 or 4 target (which sends none
        // before version 5's fields) took the effect its status accepted.
        outcome_.performed_effect = target_.has_value() && (*target_).version < 5U
                                        ? action_effect(runtime_, accepted_action_)
                                        : DragEffect::none;
        phase_ = XdndPhase::ended;
      }
      if (phase_ == XdndPhase::ended)
        break;
      pollfd descriptor{ConnectionNumber(runtime_.display), POLLIN, 0};
      static_cast<void>(poll(&descriptor, 1, 25));
    }
    return std::move(outcome_);
  }

private:
  void handle(XEvent &event) {
    if (event.type == MotionNotify) {
      if (phase_ == XdndPhase::dragging)
        moved(event.xmotion.x_root, event.xmotion.y_root, event.xmotion.state, event.xmotion.time);
      return;
    }
    if (event.type == ButtonRelease) {
      const unsigned held = (event.xbutton.state & all_buttons) & ~button_mask(event.xbutton.button);
      const bool started_button = (button_mask(event.xbutton.button) & buttons_) != 0U;
      if (phase_ == XdndPhase::dragging && started_button && (held & buttons_) == 0U) {
        outcome_.release = event;
        dropped(event.xbutton.time);
      }
      return;
    }
    if (event.type == ButtonPress || event.type == KeyRelease)
      return;
    if (event.type == KeyPress) {
      const KeySym key = XLookupKeysym(&event.xkey, 0);
      if (key == XK_Escape && phase_ == XdndPhase::dragging)
        cancelled();
      return;
    }
    if (event.type == ClientMessage && event.xclient.window == window_.xid) {
      const Atom kind = event.xclient.message_type;
      if (kind == runtime_.atom("XdndStatus")) {
        status(event.xclient);
        return;
      }
      if (kind == runtime_.atom("XdndFinished")) {
        finished(event.xclient);
        return;
      }
    }
    // Everything else as the run loop would: paints, selection requests
    // (this drag's data among them) and the program's own windows taking
    // part as destinations.
    runtime_.dispatch(event);
  }

  void moved(int root_x, int root_y, unsigned state, Time time) {
    root_x_ = root_x;
    root_y_ = root_y;
    state_ = state;
    time_ = time;
    const std::optional<XdndTarget> under = target_at(runtime_, root_x, root_y);
    const bool changed = under.has_value() != target_.has_value() ||
                         (under.has_value() && (*under).window != (*target_).window);
    if (changed) {
      if (target_.has_value())
        send(*target_, "XdndLeave", 0, 0, 0, 0);
      target_ = under;
      waiting_status_ = false;
      position_pending_ = false;
      accepted_ = false;
      accepted_action_ = None;
      if (target_.has_value())
        entered(*target_);
    }
    if (!target_.has_value())
      return;
    if (waiting_status_) {
      // One position at a time: the newest is sent when the status comes.
      position_pending_ = true;
      return;
    }
    send_position();
  }

  void entered(const XdndTarget &target) {
    const long more = types_.size() > 3U ? 1L : 0L;
    const long version = static_cast<long>(static_cast<unsigned long>(target.version) << 24U);
    long listed[3]{0, 0, 0};
    for (std::size_t i = 0; i < types_.size() && i < 3U; ++i)
      listed[i] = static_cast<long>(types_[i]);
    send(target, "XdndEnter", version | more, listed[0], listed[1], listed[2]);
  }

  void send_position() {
    const XdndTarget &target = *target_;
    const unsigned long packed = (static_cast<unsigned long>(root_x_ & 0xffff) << 16U) |
                                 static_cast<unsigned long>(root_y_ & 0xffff);
    const DragEffect asked = xdnd_requested_effect(state_, allowed_);
    send(target, "XdndPosition", 0, static_cast<long>(packed), static_cast<long>(time_),
         static_cast<long>(action_atom(runtime_, asked)));
    waiting_status_ = true;
    position_pending_ = false;
  }

  void status(const XClientMessageEvent &message) {
    if (!target_.has_value() || static_cast<::Window>(message.data.l[0]) != (*target_).window)
      return;
    accepted_ = (message.data.l[1] & 1L) != 0;
    // Version 2 and later name the accepted action; earlier, copy.
    accepted_action_ = accepted_ ? static_cast<Atom>(message.data.l[4]) : None;
    if (accepted_ && accepted_action_ == None)
      accepted_action_ = runtime_.atom("XdndActionCopy");
    // An action outside the allowed effects is not accepted.
    const DragEffect effect = action_effect(runtime_, accepted_action_);
    if (accepted_ && (effect == DragEffect::none || !has_drag_effect(allowed_, effect))) {
      accepted_ = false;
      accepted_action_ = None;
    }
    waiting_status_ = false;
    if (position_pending_ && phase_ == XdndPhase::dragging)
      send_position();
  }

  void dropped(Time time) {
    if (!target_.has_value() || !accepted_) {
      cancelled();
      return;
    }
    send(*target_, "XdndDrop", 0, static_cast<long>(time), 0, 0);
    phase_ = XdndPhase::dropping;
  }

  void cancelled() {
    if (target_.has_value())
      send(*target_, "XdndLeave", 0, 0, 0, 0);
    outcome_.performed_effect = DragEffect::none;
    phase_ = XdndPhase::ended;
  }

  void finished(const XClientMessageEvent &message) {
    if (phase_ != XdndPhase::dropping || !target_.has_value() ||
        static_cast<::Window>(message.data.l[0]) != (*target_).window)
      return;
    DragEffect performed = action_effect(runtime_, accepted_action_);
    if ((*target_).version >= 5U) {
      const bool succeeded = (message.data.l[1] & 1L) != 0;
      performed = succeeded ? action_effect(runtime_, static_cast<Atom>(message.data.l[2])) : DragEffect::none;
    }
    outcome_.performed_effect = performed;
    phase_ = XdndPhase::ended;
  }

  // Sends one XDND message about the drag to `target` (data.l[0] is this
  // source window; l[1] to l[4] as given).
  void send(const XdndTarget &target, const char *kind, long l1, long l2, long l3, long l4) {
    const ForeignWindowScope guard(runtime_.display, target.destination);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = runtime_.display;
    event.xclient.window = target.window;
    event.xclient.message_type = runtime_.atom(kind);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(window_.xid);
    event.xclient.data.l[1] = l1;
    event.xclient.data.l[2] = l2;
    event.xclient.data.l[3] = l3;
    event.xclient.data.l[4] = l4;
    XSendEvent(runtime_.display, target.destination, False, NoEventMask, &event);
    XFlush(runtime_.display);
  }

  NativeWindow &window_;
  Runtime &runtime_;
  const DragEffect allowed_;
  // The buttons held when the drag started; it drops when all are up.
  const unsigned buttons_;
  const std::vector<Atom> types_;
  XdndPhase phase_{XdndPhase::dragging};
  std::optional<XdndTarget> target_;
  int root_x_{};
  int root_y_{};
  unsigned state_{};
  Time time_{CurrentTime};
  bool waiting_status_{};
  bool position_pending_{};
  bool accepted_{};
  Atom accepted_action_{None};
  XdndDragOutcome outcome_;
};

} // namespace

std::string xdnd_file_uri(std::string_view path) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string uri = "file://";
  uri.reserve(uri.size() + path.size() * 3U);
  for (const char character : path) {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (unreserved_uri_byte(byte)) {
      uri += character;
    } else {
      uri += '%';
      uri += hex[byte >> 4U];
      uri += hex[byte & 0x0fU];
    }
  }
  return uri;
}

std::vector<std::byte> xdnd_uri_list(const DragFileListData &files) {
  std::string list;
  for (const std::string &path : files.paths_utf8) {
    list += xdnd_file_uri(path);
    list += "\r\n";
  }
  std::vector<std::byte> bytes(list.size());
  if (!bytes.empty())
    std::memcpy(bytes.data(), list.data(), bytes.size());
  return bytes;
}

DragEffect xdnd_requested_effect(unsigned state, DragEffect allowed) noexcept {
  const bool shift = (state & ShiftMask) != 0U;
  const bool control = (state & ControlMask) != 0U;
  DragEffect asked = DragEffect::none;
  if (shift && control)
    asked = DragEffect::link;
  else if (shift)
    asked = DragEffect::move;
  else if (control)
    asked = DragEffect::copy;
  if (asked != DragEffect::none && has_drag_effect(allowed, asked))
    return asked;
  if (has_drag_effect(allowed, DragEffect::copy))
    return DragEffect::copy;
  if (has_drag_effect(allowed, DragEffect::move))
    return DragEffect::move;
  if (has_drag_effect(allowed, DragEffect::link))
    return DragEffect::link;
  return DragEffect::none;
}

XdndDragOutcome run_xdnd_drag(NativeWindow &window, const HostDragSourceRequest &request) {
  XdndDragOutcome refused;
  refused.status = {HostServiceError::backend_failure};
  Runtime &runtime = window.runtime;
  ::Window root{}, child{};
  int root_x{}, root_y{}, x{}, y{};
  unsigned state{};
  if (XQueryPointer(runtime.display, window.xid, &root, &child, &root_x, &root_y, &x, &y, &state) == False)
    return refused;
  const unsigned buttons = state & all_buttons;
  if (buttons == 0U)
    return refused;
  std::vector<Runtime::DragOfferTarget> offer = drag_offer(runtime, request);
  if (offer.empty())
    return refused;
  std::vector<Atom> types;
  types.reserve(offer.size());
  for (const Runtime::DragOfferTarget &offered : offer)
    types.push_back(offered.target);

  XdndSourceScope scope(runtime, window.xid);
  const unsigned int pointer_events = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
  scope.pointer_grabbed = XGrabPointer(runtime.display, window.xid, False, pointer_events, GrabModeAsync,
                                       GrabModeAsync, None, None, CurrentTime) == GrabSuccess;
  if (!scope.pointer_grabbed)
    return refused;
  // Escape cancels when the keyboard is ours; a drag without it still runs.
  scope.keyboard_grabbed =
      XGrabKeyboard(runtime.display, window.xid, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess;
  const Atom selection = runtime.atom("XdndSelection");
  XSetSelectionOwner(runtime.display, selection, window.xid, CurrentTime);
  if (XGetSelectionOwner(runtime.display, selection) != window.xid)
    return refused;
  runtime.drag_offer_owner = window.xid;
  runtime.drag_offer = std::move(offer);
  XChangeProperty(runtime.display, window.xid, runtime.atom("XdndTypeList"), XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(types.data()), static_cast<int>(types.size()));
  XFlush(runtime.display);

  XdndSource source(window, request, buttons, std::move(types));
  XdndDragOutcome outcome = source.run(root_x, root_y, state);
  return outcome;
}

} // namespace gui_forms::host::linux_detail
