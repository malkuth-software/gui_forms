#pragma once
// The Windows host's locale (HostCapabilities::locale, reported with
// HostCapability::locale): the user's locale read with GetLocaleInfoEx.

#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include <optional>
#include <string_view>

namespace gui_forms::host {

// The user's locale (LOCALE_NAME_USER_DEFAULT, with the user's own
// overrides from the Region settings): its name (LOCALE_SNAME, a BCP 47
// tag such as "en-GB"), the short and long date and the short
// time (LOCALE_SSHORTDATE, LOCALE_SLONGDATE, LOCALE_SSHORTTIME, converted
// by picker_pattern_from_windows; one that converts to nothing keeps the
// portable default), the month and weekday names and their abbreviations
// (LOCALE_SMONTHNAME1.., LOCALE_SABBREVMONTHNAME1.., LOCALE_SDAYNAME1..
// and LOCALE_SABBREVDAYNAME1.., whose weekdays run Monday first), AM and PM
// (LOCALE_S1159, LOCALE_S2359), the separators (LOCALE_SDECIMAL,
// LOCALE_STHOUSAND) and the first day of the week (LOCALE_IFIRSTDAYOFWEEK,
// 0 for Monday). Empty when a value cannot be read or converted to UTF-8,
// or when the record read is not valid_host_locale; the host then does not
// report HostCapability::locale. Called on the host thread when the
// capabilities are built.
[[nodiscard]] std::optional<HostLocale> read_windows_locale();

// The same record for the locale named `name` (a BCP 47 tag of at most 84
// characters, LOCALE_NAME_MAX_LENGTH less the terminator), from the
// locale's own data without the user's overrides (LOCALE_NOUSEROVERRIDE),
// so that a test can pin known values. Empty for a name Windows does not
// know, and as read_windows_locale otherwise.
[[nodiscard]] std::optional<HostLocale> read_windows_locale_defaults(std::string_view name);

} // namespace gui_forms::host
