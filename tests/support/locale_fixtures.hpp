#pragma once
// Host locale fixtures for tests (exhibit gap E11): records as a host would
// report them for en-US, en-GB and de-DE. They are test data, not a source
// of locale details: GUI.Forms reads the locale from the host
// (HostCapabilities::locale) and otherwise uses portable_host_locale().

#include "gui_forms/host/types/host_locale/host_locale.hpp"

namespace gui_forms::test_support {

inline HostLocale english_united_states_locale() {
    HostLocale locale = portable_host_locale();
    locale.name = "en-US";
    return locale;
}

inline HostLocale english_united_kingdom_locale() {
    HostLocale locale = portable_host_locale();
    locale.name = "en-GB";
    locale.long_date_pattern = "dddd d MMMM yyyy";
    locale.short_date_pattern = "dd/MM/yyyy";
    locale.time_pattern = "HH:mm";
    locale.am_designator = "am";
    locale.pm_designator = "pm";
    locale.first_day_of_week = HostWeekday::monday;
    return locale;
}

// German names in UTF-8 ("M\xC3\xA4rz" is Maerz with an a-umlaut).
inline HostLocale german_germany_locale() {
    HostLocale locale;
    locale.name = "de-DE";
    locale.month_names = {"Januar", "Februar", "M\xC3\xA4rz",  "April",   "Mai",      "Juni",
                          "Juli",   "August",  "September", "Oktober", "November", "Dezember"};
    locale.abbreviated_month_names = {"Jan.", "Feb.", "M\xC3\xA4rz", "Apr.", "Mai",  "Juni",
                                      "Juli", "Aug.", "Sept.",       "Okt.", "Nov.", "Dez."};
    locale.day_names = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    locale.abbreviated_day_names = {"So.", "Mo.", "Di.", "Mi.", "Do.", "Fr.", "Sa."};
    locale.long_date_pattern = "dddd, d. MMMM yyyy";
    locale.short_date_pattern = "dd.MM.yyyy";
    locale.time_pattern = "HH:mm";
    locale.am_designator = "";
    locale.pm_designator = "";
    locale.decimal_separator = ",";
    locale.group_separator = ".";
    locale.first_day_of_week = HostWeekday::monday;
    return locale;
}

} // namespace gui_forms::test_support
