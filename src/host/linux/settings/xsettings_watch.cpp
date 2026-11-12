#include "xsettings_watch.hpp"

#include "../services/x11_foreign_window.hpp"
#include "xsettings_parse.hpp"

#include <X11/Xatom.h>

#include <cstddef>
#include <span>
#include <string>

namespace gui_forms::host::linux_detail {
namespace {

// The property is read whole up to this many 32-bit units (256 KiB); a
// larger one is not used.
constexpr long maximum_property_units = 65536L;

} // namespace

XSettingsWatch::XSettingsWatch(Display *display, int screen)
    : display_(display), root_(RootWindow(display, screen)) {
  const std::string name = "_XSETTINGS_S" + std::to_string(screen);
  selection_ = XInternAtom(display_, name.c_str(), False);
  settings_ = XInternAtom(display_, "_XSETTINGS_SETTINGS", False);
  manager_ = XInternAtom(display_, "MANAGER", False);
  // A new manager announces itself to StructureNotify on the root; the
  // root's other selections, made by this client, are kept.
  XWindowAttributes attributes{};
  long mask = StructureNotifyMask;
  if (XGetWindowAttributes(display_, root_, &attributes) != 0)
    mask |= attributes.your_event_mask;
  XSelectInput(display_, root_, mask);
  acquire_manager();
}

bool XSettingsWatch::settings_event(const XEvent &event) {
  if (event.type == ClientMessage && event.xclient.window == root_ &&
      event.xclient.message_type == manager_ && event.xclient.format == 32 &&
      static_cast<Atom>(event.xclient.data.l[1]) == selection_) {
    acquire_manager();
    return true;
  }
  if (owner_ == None)
    return false;
  if (event.type == PropertyNotify && event.xproperty.window == owner_ &&
      event.xproperty.atom == settings_) {
    read_settings();
    return true;
  }
  if (event.type == DestroyNotify && event.xdestroywindow.window == owner_) {
    owner_ = None;
    // A replacement may already own the selection; else the default.
    acquire_manager();
    return true;
  }
  return false;
}

void XSettingsWatch::acquire_manager() {
  // Grabbed, so that the owner cannot vanish between finding it and
  // selecting its events (XSETTINGS specification, "Reading settings").
  XGrabServer(display_);
  const ::Window owner = XGetSelectionOwner(display_, selection_);
  if (owner != None)
    XSelectInput(display_, owner, StructureNotifyMask | PropertyChangeMask);
  XUngrabServer(display_);
  XFlush(display_);
  owner_ = owner;
  read_settings();
}

void XSettingsWatch::read_settings() {
  double_click_milliseconds_ = default_double_click_milliseconds;
  double_click_origin_ = SettingOrigin::default_value;
  double_click_distance_ = default_double_click_distance;
  pointer_settings_ = PointerSettings{};
  caret_settings_ = CaretSettings{};
  if (owner_ == None)
    return;
  Atom type = None;
  int format = 0;
  unsigned long items = 0UL;
  unsigned long after = 0UL;
  unsigned char *data = nullptr;
  int status = BadWindow;
  {
    // The manager may exit at any moment: its window is foreign.
    const ForeignWindowScope foreign(display_, owner_);
    status = XGetWindowProperty(display_, owner_, settings_, 0L, maximum_property_units, False,
                                settings_, &type, &format, &items, &after, &data);
  }
  const bool usable = status == Success && data != nullptr && type == settings_ &&
                      format == 8 && after == 0UL;
  XSettingsInteger interval{};
  XSettingsInteger distance{};
  XSettingsInteger drag{};
  XSettingsInteger blink{};
  XSettingsInteger blink_cycle{};
  if (usable) {
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t *>(data),
                                              static_cast<std::size_t>(items));
    interval = find_xsettings_integer(bytes, "Net/DoubleClickTime");
    distance = find_xsettings_integer(bytes, "Net/DoubleClickDistance");
    drag = find_xsettings_integer(bytes, "Net/DndDragThreshold");
    blink = find_xsettings_integer(bytes, "Net/CursorBlink");
    blink_cycle = find_xsettings_integer(bytes, "Net/CursorBlinkTime");
  }
  if (data != nullptr)
    XFree(data);
  const bool interval_usable =
      interval.lookup == XSettingsLookup::found && interval.value > 0 &&
      static_cast<std::uint32_t>(interval.value) <= maximum_double_click_milliseconds;
  if (interval_usable) {
    double_click_milliseconds_ = static_cast<std::uint32_t>(interval.value);
    double_click_origin_ = SettingOrigin::host;
  }
  const bool distance_usable = distance.lookup == XSettingsLookup::found && distance.value >= 0 &&
                               static_cast<double>(distance.value) <= maximum_pointer_distance;
  if (distance_usable)
    double_click_distance_ = static_cast<double>(distance.value);
  // A threshold of n pixels is the rectangle reaching n each way.
  const bool drag_usable = drag.lookup == XSettingsLookup::found && drag.value >= 0 &&
                           2.0 * static_cast<double>(drag.value) <= maximum_pointer_distance;
  if (drag_usable) {
    pointer_settings_.drag_width = 2.0 * static_cast<double>(drag.value);
    pointer_settings_.drag_height = pointer_settings_.drag_width;
    pointer_settings_.drag_origin = SettingOrigin::host;
  }
  caret_settings_ = xsettings_caret(blink, blink_cycle);
}

} // namespace gui_forms::host::linux_detail
