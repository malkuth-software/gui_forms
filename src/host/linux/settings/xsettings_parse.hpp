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

} // namespace gui_forms::host::linux_detail
