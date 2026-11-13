#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/date_time_value.hpp"

#include <array>
#include <string>

namespace gui_forms {

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

    [[nodiscard]] static DateTimeFormatProvider english_united_states();

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
