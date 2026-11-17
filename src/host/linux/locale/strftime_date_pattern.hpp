#pragma once
// Converting a POSIX strftime pattern (nl_langinfo's D_FMT and T_FMT) to the
// DateTimePicker's custom-format letters that HostLocale's patterns use.
// Plain text in and out, no locale calls, so the conversion is tested on
// every platform.

#include <string>
#include <string_view>

namespace gui_forms::host {

// `pattern` in the picker's letters (HostLocale):
// - %d dd, %e d, %m MM, %b and %h MMM, %B MMMM, %y yy, %Y yyyy, %a ddd,
//   %A dddd, %H HH, %k H, %I hh, %l h, %M mm, %S ss, %p and %P tt;
// - %D MM/dd/yy, %F yyyy-MM-dd, %T HH:mm:ss, %R HH:mm, %r hh:mm:ss tt;
// - %n and %t a space, %% a percent sign; the E and O modifiers skipped;
// - any other conversion (zone, week, century, epoch): dropped, and the
//   spaces and commas left at either end of the result trimmed;
// - an ASCII letter outside a conversion: quoted ('...'), so the picker
//   draws it as text; ' and \ escaped by a backslash; everything else as
//   it is.
// Returns an empty string when nothing is left (the caller keeps its default).
[[nodiscard]] std::string picker_pattern_from_strftime(std::string_view pattern);

} // namespace gui_forms::host
