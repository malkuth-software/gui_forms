#pragma once
// The Linux host's locale (HostCapabilities::locale): the POSIX locale the
// environment selects (LC_ALL, then each LC_* category, then LANG), read
// with nl_langinfo_l.

#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::host {

// The locale name the environment selects for dates: LC_ALL, else LC_TIME,
// else LANG, each only when set and non-empty, with the codeset
// (".UTF-8") and modifier ("@euro") dropped and '_' as '-' ("en_GB.UTF-8"
// as "en-GB"). Empty when none is set, or for the C and POSIX locales.
// The three values are passed in, so the rule is tested without the
// environment.
[[nodiscard]] std::string posix_locale_name(std::string_view lc_all, std::string_view lc_time,
                                            std::string_view lang);

// The environment's locale (newlocale(LC_ALL_MASK, "", 0)): the short date
// and time (D_FMT and T_FMT, converted by picker_pattern_from_strftime), a
// long date of the weekday and the short date (POSIX has no long date), the
// month and weekday names, AM and PM, RADIXCHAR and THOUSEP, and with glibc
// the first weekday (_NL_TIME_WEEK_1STDAY and _NL_TIME_FIRST_WEEKDAY). Empty
// when the environment names no locale or the C or POSIX locale, when the
// locale cannot be loaded, or when the record read is not
// valid_host_locale (a codeset other than UTF-8 is refused that way); the
// host then does not report HostCapability::locale. Reads the process
// environment; called on the host thread when the capabilities are built.
[[nodiscard]] std::optional<HostLocale> read_posix_locale();

} // namespace gui_forms::host
