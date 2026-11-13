#pragma once
// The Windows host's locale (HostCapabilities::locale): an interface not yet
// reported. The Windows hand-back fills it from the user's locale
// (GetUserDefaultLocaleName and GetLocaleInfoEx: LOCALE_SSHORTDATE,
// LOCALE_SLONGDATE, LOCALE_STIMEFORMAT or LOCALE_SSHORTTIME,
// LOCALE_SMONTHNAME1.., LOCALE_SABBREVMONTHNAME1.., LOCALE_SDAYNAME1..,
// LOCALE_S1159 and LOCALE_S2359, LOCALE_SDECIMAL, LOCALE_STHOUSAND and
// LOCALE_IFIRSTDAYOFWEEK, whose Monday-first count differs from
// HostWeekday's). Until then the host reports no HostCapability::locale and
// GUI.Forms uses portable_host_locale().

#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include <optional>

namespace gui_forms::host {

[[nodiscard]] inline std::optional<HostLocale> read_windows_locale() {
    return std::nullopt;
}

} // namespace gui_forms::host
