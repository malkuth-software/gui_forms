#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/date_time_value.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

// The month and day names and the patterns a DateTimePicker shows its value
// with (its format_provider property): the culture setting. GUI.Forms has no
// culture service and reads no host locale; a program picks a culture by
// assigning a provider, one of the named ones below or its own.
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

    // English (United States), the default: "Wednesday, August 5, 2026",
    // "8/5/2026", "2:07 PM".
    [[nodiscard]] static DateTimeFormatProvider english_united_states();
    // English (United Kingdom): "Wednesday 5 August 2026", "05/08/2026",
    // "14:07" (exhibit gap E11).
    [[nodiscard]] static DateTimeFormatProvider english_united_kingdom();
    // The named provider for a BCP 47 culture name GUI.Forms carries
    // ("en-US", "en-GB"; ASCII case ignored), or nullopt for any other.
    [[nodiscard]] static std::optional<DateTimeFormatProvider> for_culture(std::string_view name);

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
