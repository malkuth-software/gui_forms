#include "gui_forms/controls/date_time_format_provider.hpp"

#include <cstddef>

namespace gui_forms {

DateTimeFormatProvider DateTimeFormatProvider::english_united_states() {
    DateTimeFormatProvider provider;
    provider.month_names = {"January", "February", "March", "April", "May", "June",
                            "July", "August", "September", "October", "November",
                            "December"};
    provider.abbreviated_month_names = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    provider.day_names = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                          "Friday", "Saturday"};
    provider.abbreviated_day_names = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return provider;
}

DateTimeFormatProvider DateTimeFormatProvider::english_united_kingdom() {
    // The names are English's; the patterns are the United Kingdom's, as
    // Windows' en-GB culture writes them, with lower-case designators for a
    // twelve-hour custom format.
    DateTimeFormatProvider provider = english_united_states();
    provider.long_date_pattern = "dddd d MMMM yyyy";
    provider.short_date_pattern = "dd/MM/yyyy";
    provider.time_pattern = "HH:mm";
    provider.am_designator = "am";
    provider.pm_designator = "pm";
    return provider;
}

namespace {

// Whether `name` is `expected` with ASCII letters compared without case.
bool same_culture_name(std::string_view name, std::string_view expected) noexcept {
    if (name.size() != expected.size()) return false;
    for (std::size_t index = 0U; index < name.size(); ++index) {
        char left = name[index];
        char right = expected[index];
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}

} // namespace

std::optional<DateTimeFormatProvider> DateTimeFormatProvider::for_culture(std::string_view name) {
    if (same_culture_name(name, "en-US")) return english_united_states();
    if (same_culture_name(name, "en-GB")) return english_united_kingdom();
    return std::nullopt;
}

} // namespace gui_forms
