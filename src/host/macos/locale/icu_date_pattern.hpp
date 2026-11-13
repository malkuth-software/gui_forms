#pragma once
// Converting a Unicode date pattern (UTS #35, the language of ICU, CFDateFormatter
// and NSDateFormatter) to the DateTimePicker's custom-format letters that
// HostLocale's patterns use. Plain text in and out, no CoreFoundation, so the
// conversion is tested on every platform.

#include <string>
#include <string_view>

namespace gui_forms::host {

// `pattern` in the picker's letters (HostLocale):
// - y and yyy or longer: yyyy; yy: yy (u is read as y);
// - M or L, 1 to 4: M, MM, MMM, MMMM; 5 (narrow): MMM;
// - d, dd: as they are;
// - E or c, 1 to 3 and 5 or more: ddd; 4: dddd (c alone is a weekday
//   number in UTS #35 and is read as ddd too);
// - h, hh, H, HH: as they are; K and k (0-11 and 1-24) as h and H;
// - m, mm, s, ss: as they are (longer runs as two letters);
// - a, b, B: tt;
// - text in single quotes, and '' (an apostrophe): picker text in single
//   quotes, with ' and \ escaped by a backslash;
// - any other letter (era, zone, week, quarter, fractions): dropped, and
//   the spaces and commas left at either end of the result trimmed;
// - everything else as it is, a backslash escaped.
// Returns an empty string when nothing is left (the caller keeps its default).
[[nodiscard]] std::string picker_pattern_from_icu(std::string_view pattern);

} // namespace gui_forms::host
