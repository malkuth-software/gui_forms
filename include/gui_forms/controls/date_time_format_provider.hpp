#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include <array>
#include <string>

namespace gui_forms {

// The month and day names and the patterns a DateTimePicker or MonthCalendar
// shows its value with: the culture setting. By default both follow their
// window's locale (Control::effective_locale: the host's, where the host
// reports one, else the portable default) through from_locale; a program
// that writes their format_provider property chooses its own instead
// (DateTimePicker::follows_host_locale).
struct GUI_FORMS_API DateTimeFormatProvider final {
    std::array<std::string, 12> month_names;
    std::array<std::string, 12> abbreviated_month_names;
    std::array<std::string, 7> day_names;
    std::array<std::string, 7> abbreviated_day_names;
    std::string long_date_pattern{"dddd, MMMM d, yyyy"};
    std::string short_date_pattern{"M/d/yyyy"};
    std::string time_pattern{"h:mm tt"};
    std::string am_designator{"AM"};
    std::string pm_designator{"PM"};

    // English (United States): "Wednesday, August 5, 2026", "8/5/2026",
    // "2:07 PM"; the portable default locale's (portable_host_locale).
    [[nodiscard]] static DateTimeFormatProvider english_united_states();
    // The names, patterns and designators of `locale`.
    [[nodiscard]] static DateTimeFormatProvider from_locale(const HostLocale& locale);

    // Field by field; a property cell compares before it stores.
    friend bool operator==(const DateTimeFormatProvider& left, const DateTimeFormatProvider& right) {
        const bool names = left.month_names == right.month_names &&
                           left.abbreviated_month_names == right.abbreviated_month_names &&
                           left.day_names == right.day_names && left.abbreviated_day_names == right.abbreviated_day_names;
        const bool patterns = left.long_date_pattern == right.long_date_pattern &&
                              left.short_date_pattern == right.short_date_pattern && left.time_pattern == right.time_pattern;
        const bool designators = left.am_designator == right.am_designator && left.pm_designator == right.pm_designator;
        return names && patterns && designators;
    }
};

} // namespace gui_forms
