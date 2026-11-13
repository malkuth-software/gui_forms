#pragma once
// The X11 host's input settings (HostInputSettingsEvent): the double-click
// interval and distance, the drag threshold and the caret blink from the
// screen's XSETTINGS manager (Net/DoubleClickTime, Net/DoubleClickDistance,
// Net/DndDragThreshold, Net/CursorBlink, Net/CursorBlinkTime), or the
// Windows-like defaults where no manager runs
// or it has no such setting; and the desktop's device scale from Xft.dpi
// (the manager's Xft/DPI, then the root window's RESOURCE_MANAGER; audit
// N66, option a). Xlib types stay here and in src/host/linux/.
#include "../application/linux_input_rules.hpp"
#include "gui_forms/window/presentation/input_settings.hpp"
#include "xsettings_parse.hpp"

#include <X11/Xlib.h>

#include <cstdint>
#include <optional>

namespace gui_forms::host::linux_detail {

// Follows the screen's XSETTINGS manager for one run, as the specification
// describes: the owner of _XSETTINGS_S<screen>, found with the server
// grabbed; its _XSETTINGS_SETTINGS property, read again on each
// PropertyNotify; a new manager, announced by a MANAGER client message on
// the root window; and the manager's window being destroyed, after which
// the setting falls back to the default. Made after the display opens and
// destroyed before it closes; UI thread only.
class XSettingsWatch final {
public:
  XSettingsWatch(Display *display, int screen);
  XSettingsWatch(const XSettingsWatch &) = delete;
  XSettingsWatch &operator=(const XSettingsWatch &) = delete;

  // Takes `event` when it concerns the settings: a MANAGER announcement
  // for this screen, a change of the manager's settings property, the
  // manager window's destruction, or a change of the root window's
  // RESOURCE_MANAGER. The watch has then read the settings again, and the
  // caller reports them (and applies scale()). False for every other event.
  [[nodiscard]] bool settings_event(const XEvent &event);

  // The interval to report: the manager's Net/DoubleClickTime when it has
  // a usable one (an integer between 1 and
  // maximum_double_click_milliseconds), with the origin `host`; otherwise
  // default_double_click_milliseconds with the origin `default_value`.
  [[nodiscard]] std::uint32_t double_click_milliseconds() const noexcept {
    return double_click_milliseconds_;
  }
  [[nodiscard]] SettingOrigin double_click_origin() const noexcept {
    return double_click_origin_;
  }
  // How far, in pixels along each axis, a second press may land from the
  // first and still count toward a double-click: the manager's
  // Net/DoubleClickDistance when it has a usable one (0 to
  // maximum_pointer_distance), otherwise default_double_click_distance.
  [[nodiscard]] double double_click_distance() const noexcept { return double_click_distance_; }
  // The pointer settings to report: the drag rectangle from the manager's
  // Net/DndDragThreshold (a threshold of n is a rectangle 2n wide, origin
  // host) when it has a usable one, otherwise the defaults. X11 reports no
  // hover, so the window times it.
  [[nodiscard]] const PointerSettings &pointer_settings() const noexcept { return pointer_settings_; }
  // The caret settings to report (xsettings_caret over the manager's
  // Net/CursorBlink and Net/CursorBlinkTime), otherwise the default.
  [[nodiscard]] const CaretSettings &caret_settings() const noexcept { return caret_settings_; }
  // The desktop's device scale (x11_desktop_scale): the manager's Xft/DPI
  // when usable, else the resource database's Xft.dpi, over 96, bounded to
  // [1, maximum_x11_scale]; 1 with neither.
  [[nodiscard]] double scale() const noexcept { return scale_; }

private:
  // Finds the manager with the server grabbed, selects its property and
  // structure events, and reads the settings.
  void acquire_manager();
  // Reads the manager's property; the default without a manager, or when
  // the property is missing or malformed.
  void read_settings();
  // Reads the root window's RESOURCE_MANAGER into resource_dpi_; nullopt
  // when it is missing, not a string, or has no usable Xft.dpi.
  void read_resources();
  // scale_ from the manager's density and resource_dpi_.
  void update_scale() noexcept;

  // Borrowed from the runtime, which outlives this watch.
  Display *display_{};
  ::Window root_{};
  Atom selection_{};
  Atom settings_{};
  Atom manager_{};
  Atom resource_manager_{};
  // The manager's selection window; None without a manager.
  ::Window owner_{};
  std::uint32_t double_click_milliseconds_{default_double_click_milliseconds};
  SettingOrigin double_click_origin_{SettingOrigin::default_value};
  double double_click_distance_{default_double_click_distance};
  PointerSettings pointer_settings_{};
  CaretSettings caret_settings_{};
  // The manager's Xft/DPI as read (absent without a manager) and the
  // resource database's density.
  XSettingsInteger xft_dpi_{};
  std::optional<double> resource_dpi_{};
  double scale_{1.0};
};

} // namespace gui_forms::host::linux_detail
