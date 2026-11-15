#pragma once
// Converting a Windows date or time format picture (GetLocaleInfoEx's
// LOCALE_SSHORTDATE, LOCALE_SLONGDATE and LOCALE_SSHORTTIME, converted to
// UTF-8) to the DateTimePicker's custom-format letters that HostLocale's
// patterns use. Plain text in and out, no Win32 types, so the conversion is
// tested on every platform.

#include <string>
#include <string_view>

namespace gui_forms::host {

// `pattern` in the picker's letters (HostLocale). The Windows pictures
// ("Day, Month, Year, and Era Format Pictures" and "Hour, Minute, and
// Second Format Pictures" in Microsoft's National Language Support
// documentation) are the letters the picker's custom format took from
// Windows Forms, so most fields stay as they are:
// - d, dd, ddd, dddd: as they are (longer runs as dddd);
// - M, MM, MMM, MMMM: as they are (longer runs as MMMM);
// - y (the year without century and without a leading zero) and yy: yy;
//   yyy or longer: yyyy;
// - h, hh, H, HH, m, mm, s, ss: as they are (longer runs as two letters);
// - t, tt: as they are (longer runs as tt);
// - g and gg (the era): dropped, and the spaces and commas left at either
//   end of the result trimmed;
// - text in single quotes, and '' (an apostrophe): picker text in single
//   quotes, with ' and \ escaped by a backslash;
// - any other ASCII letter: quoted, so the picker draws it as text;
// - everything else (punctuation, spaces, non-ASCII text) as it is, a
//   backslash escaped.
// Returns an empty string when nothing is left (the caller keeps its default).
[[nodiscard]] std::string picker_pattern_from_windows(std::string_view pattern);

} // namespace gui_forms::host
