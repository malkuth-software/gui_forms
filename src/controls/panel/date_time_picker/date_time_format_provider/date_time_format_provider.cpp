#include "gui_forms/controls/date_time_format_provider.hpp"

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

} // namespace gui_forms
