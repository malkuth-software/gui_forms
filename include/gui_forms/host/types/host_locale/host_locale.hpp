#pragma once

#include "gui_forms/export/application_export.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace gui_forms {

// A day of the week, Sunday first (the order of DateTimeFormatProvider's day
// names and of std::tm::tm_wday).
enum class HostWeekday : std::uint8_t { sunday, monday, tuesday, wednesday, thursday, friday, saturday };

// The host's locale details GUI.Forms formats with (exhibit gap E11; owner,
// 2026-11-13: GUI.Forms defers to the host for the date and time and, where
// possible, the locale details). Host data, not core policy: a host fills
// what its platform says (HostCapabilities::locale, reported with
// HostCapability::locale) and leaves the rest at the portable default.
//
// The patterns use the DateTimePicker's custom-format letters (d, dd, ddd,
// dddd, M to MMMM, yy, yyyy, h, hh, H, HH, m, mm, s, ss, t, tt, text in
// single quotes); a host converts its platform's pattern language to them.
// The names run January first and Sunday first. Every text is UTF-8; the
// names and patterns are never empty (valid_host_locale).
struct GUI_FORMS_API HostLocale final {
    // The platform's name for the locale, a BCP 47 tag where the platform
    // gives one ("en-GB", "de-DE"), else as the platform writes it; "und"
    // (undetermined) for the portable default.
    std::string name{"und"};
    std::array<std::string, 12> month_names{"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
    std::array<std::string, 12> abbreviated_month_names{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::array<std::string, 7> day_names{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                         "Saturday"};
    std::array<std::string, 7> abbreviated_day_names{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::string long_date_pattern{"dddd, MMMM d, yyyy"};
    std::string short_date_pattern{"M/d/yyyy"};
    std::string time_pattern{"h:mm tt"};
    std::string am_designator{"AM"};
    std::string pm_designator{"PM"};
    // The separators of formatted numbers: between the integer and the
    // fraction, and between digit groups of the integer.
    std::string decimal_separator{"."};
    std::string group_separator{","};
    HostWeekday first_day_of_week{HostWeekday::sunday};

    friend bool operator==(const HostLocale& left, const HostLocale& right) {
        const bool names = left.name == right.name && left.month_names == right.month_names &&
                           left.abbreviated_month_names == right.abbreviated_month_names &&
                           left.day_names == right.day_names && left.abbreviated_day_names == right.abbreviated_day_names;
        const bool dates = left.long_date_pattern == right.long_date_pattern &&
                           left.short_date_pattern == right.short_date_pattern &&
                           left.time_pattern == right.time_pattern && left.am_designator == right.am_designator &&
                           left.pm_designator == right.pm_designator;
        const bool numbers = left.decimal_separator == right.decimal_separator &&
                             left.group_separator == right.group_separator &&
                             left.first_day_of_week == right.first_day_of_week;
        return names && dates && numbers;
    }
};

// The fixed portable default, used where a host cannot say: HostLocale's
// member defaults. They are the patterns, names and separators the
// DateTimePicker and NumericUpDown drew before GUI.Forms read a host locale
// (English names, month-day-year dates, a twelve-hour clock, "." and ","),
// kept so a host that reports nothing draws as before; a choice, not a
// claim about any user's locale.
[[nodiscard]] GUI_FORMS_API HostLocale portable_host_locale();

// Whether `locale` can be formatted with: every name and pattern non-empty
// valid UTF-8, the designators and separators valid UTF-8 (they may be
// empty), the decimal separator non-empty and different from the group
// separator, and a known first day.
[[nodiscard]] GUI_FORMS_API bool valid_host_locale(const HostLocale& locale);

// `locale` as one JSON object (HostCapabilities::to_json writes it under
// "locale").
[[nodiscard]] GUI_FORMS_API std::string host_locale_json(const HostLocale& locale);

} // namespace gui_forms
