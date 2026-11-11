#include "../application/linux_host_internal.hpp"
#include "x11_foreign_window.hpp"
#include <X11/Xatom.h>
#include <string_view>
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
