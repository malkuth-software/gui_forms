#include "gui_forms/controls/date_time_format_provider.hpp"

namespace gui_forms {

DateTimeFormatProvider DateTimeFormatProvider::english_united_states() {
    const DateTimeFormatProvider provider = from_locale(portable_host_locale());
    return provider;
}

DateTimeFormatProvider DateTimeFormatProvider::from_locale(const HostLocale& locale) {
    DateTimeFormatProvider provider;
    provider.month_names = locale.month_names;
    provider.abbreviated_month_names = locale.abbreviated_month_names;
    provider.day_names = locale.day_names;
    provider.abbreviated_day_names = locale.abbreviated_day_names;
    provider.long_date_pattern = locale.long_date_pattern;
    provider.short_date_pattern = locale.short_date_pattern;
    provider.time_pattern = locale.time_pattern;
    provider.am_designator = locale.am_designator;
    provider.pm_designator = locale.pm_designator;
    return provider;
}

} // namespace gui_forms
