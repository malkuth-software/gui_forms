#pragma once
// The macOS host's locale (HostCapabilities::locale): the user's current
// locale and calendar as CoreFoundation reports them, including the formats
// chosen in System Settings.

#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include <optional>

namespace gui_forms::host {

// The current locale's date and time patterns (CFDateFormatter's full and
// short date styles and short time style, converted by
// picker_pattern_from_icu), month and weekday names, AM and PM symbols,
// decimal and grouping separators (CFLocale) and first weekday
// (CFCalendarCopyCurrent). A detail CoreFoundation does not give keeps the
// portable default. Empty when the record read is not valid_host_locale (the
// host then does not report HostCapability::locale). Reads process-wide
// state; called on the main thread when the capabilities are built.
[[nodiscard]] std::optional<HostLocale> read_macos_locale();

} // namespace gui_forms::host
