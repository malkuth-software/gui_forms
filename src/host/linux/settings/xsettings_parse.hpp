#pragma once
// Reading one setting from an XSETTINGS property (the freedesktop.org
// XSETTINGS specification, version 0.5): the _XSETTINGS_SETTINGS property
// the settings manager keeps on its selection window. Plain bytes in, no
// Xlib, so the parser is tested on every platform.
//
// The layout, every field in the byte order the first byte names (0 least
// significant first, 1 most significant first):
//   CARD8 byte order, 3 unused, CARD32 serial, CARD32 setting count;
//   then each setting: CARD8 type (0 integer, 1 string, 2 colour), 1 unused,
//   CARD16 name length, the name padded to a multiple of 4, CARD32 last
//   change serial, and the value: INT32 for an integer; CARD32 length and
//   the bytes padded to a multiple of 4 for a string; four CARD16 for a
//   colour.
#include "gui_forms/window/presentation/input_settings.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms::host::linux_detail {

enum class XSettingsLookup : std::uint8_t {
  // The setting is present, an integer; `value` holds it.
  found,
  // The property is well formed and has no such setting.
  absent,
  // The setting is present with another type.
  wrong_type,
  // The property is truncated or inconsistent; nothing in it is used.
  malformed,
};

struct XSettingsInteger final {
  XSettingsLookup lookup{XSettingsLookup::absent};
  std::int32_t value{};
};

// Finds the integer setting `name` in `property`. Every setting is walked
// and bounds-checked, so a malformed property is reported as malformed even
// when the setting comes before the fault. Reads `property` only.
[[nodiscard]] XSettingsInteger find_xsettings_integer(std::span<const std::uint8_t> property,
                                                    std::string_view name) noexcept;

// The caret settings from the manager's Net/CursorBlink and
// Net/CursorBlinkTime (audit N75): blinking off (Net/CursorBlink 0) is a
// phase of 0 with origin host; a usable Net/CursorBlinkTime (a positive
// integer, the whole on-and-off cycle in milliseconds) is halved into the
// phase, at least 1 and at most maximum_caret_blink_milliseconds, origin
// host; anything else leaves the default.
[[nodiscard]] CaretSettings xsettings_caret(XSettingsInteger blink, XSettingsInteger cycle) noexcept;

// The X11 host's device scale (audit N66, option a). An X11 desktop states
// its density once, as Xft.dpi: the settings manager's Xft/DPI first, then
// the resource database's Xft.dpi, else 96. The scale is that density over
// 96, never below 1 and at most maximum_x11_scale, as the Windows host
// divides a monitor's DPI by 96.
inline constexpr double x11_reference_dpi = 96.0;
inline constexpr double maximum_x11_scale = 4.0;
// The densest Xft.dpi accepted; a larger one is treated as unset.
inline constexpr double maximum_x11_dpi = 9600.0;

// The manager's Xft/DPI, an integer in 1024ths of a dot per inch: the
// density when it is a positive integer of at most maximum_x11_dpi dots;
// nullopt otherwise (GTK publishes -1 for "use the default").
[[nodiscard]] std::optional<double> xsettings_dpi(XSettingsInteger xft_dpi) noexcept;

// The Xft.dpi entry of a resource database string (the root window's
// RESOURCE_MANAGER property, as xrdb writes it): lines of "name: value",
// separated by newlines, '!' starting a comment line. The name must be
// exactly "Xft.dpi" after leading blanks, with blanks allowed before the
// colon and after it; the value is a decimal number (digits, an optional
// fraction), read until the line's trailing blanks. With several entries the
// last one counts, as xrdb's merge keeps the newest. Returns the density
// when the value is positive and at most maximum_x11_dpi; nullopt when there
// is no usable entry. Reads `resources` only.
[[nodiscard]] std::optional<double> resource_database_dpi(std::string_view resources) noexcept;

// The scale for a density: dpi / x11_reference_dpi bounded to
// [1, maximum_x11_scale]; 1 without a density.
[[nodiscard]] double x11_scale_for_dpi(std::optional<double> dpi) noexcept;

// The desktop's scale from both sources, in their order: the manager's
// density (xsettings_dpi) when it has one, else the resource database's
// (resource_database_dpi), else 1.
[[nodiscard]] double x11_desktop_scale(std::optional<double> manager_dpi,
                                       std::optional<double> resource_dpi) noexcept;

} // namespace gui_forms::host::linux_detail
