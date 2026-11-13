// The host locale record (include/gui_forms/host/types/host_locale) and the
// hosts' pattern conversions: UTS #35 date patterns for macOS
// (src/host/macos/locale/icu_date_pattern.hpp) and strftime patterns for
// Linux (src/host/linux/locale/strftime_date_pattern.hpp), which take plain
// text and are tested on every platform; and the platform's own reader
// where it is compiled in.
#include "../src/host/linux/locale/strftime_date_pattern.hpp"
#include "../src/host/macos/locale/icu_date_pattern.hpp"
#include "gui_forms/host/types/host_locale/host_locale.hpp"

#if defined(GUI_FORMS_TEST_MACOS_LOCALE)
#include "../src/host/macos/locale/macos_locale.hpp"
#endif
#if defined(GUI_FORMS_TEST_POSIX_LOCALE)
#include "../src/host/linux/locale/posix_locale.hpp"
#endif

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using gui_forms::HostLocale;
using gui_forms::HostWeekday;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_text(std::string_view actual, std::string_view expected, const char* message) {
    if (actual != expected) {
        std::cerr << "  got \"" << actual << "\", expected \"" << expected << "\"\n";
        throw std::runtime_error(message);
    }
}

void test_portable_default() {
    const HostLocale locale = gui_forms::portable_host_locale();
    require(gui_forms::valid_host_locale(locale), "the portable default is valid");
    require_text(locale.name, "und", "the portable default is undetermined");
    require_text(locale.short_date_pattern, "M/d/yyyy", "the portable short date keeps the earlier drawing");
    require_text(locale.long_date_pattern, "dddd, MMMM d, yyyy", "the portable long date keeps the earlier drawing");
    require_text(locale.time_pattern, "h:mm tt", "the portable time keeps the earlier drawing");
    require(locale.first_day_of_week == HostWeekday::sunday, "the portable week starts on Sunday");
    require(locale == HostLocale{}, "the portable default is the member defaults");
}

void test_validation() {
    HostLocale locale = gui_forms::portable_host_locale();
    locale.decimal_separator = ",";
    require(!gui_forms::valid_host_locale(locale), "equal separators are refused");
    locale.group_separator = ".";
    require(gui_forms::valid_host_locale(locale), "swapped separators are accepted");
    locale.group_separator.clear();
    require(gui_forms::valid_host_locale(locale), "an empty group separator is accepted");
    locale.am_designator.clear();
    locale.pm_designator.clear();
    require(gui_forms::valid_host_locale(locale), "empty designators are accepted (a 24-hour clock)");
    HostLocale empty_month = gui_forms::portable_host_locale();
    empty_month.month_names[4].clear();
    require(!gui_forms::valid_host_locale(empty_month), "an empty month name is refused");
    HostLocale empty_pattern = gui_forms::portable_host_locale();
    empty_pattern.short_date_pattern.clear();
    require(!gui_forms::valid_host_locale(empty_pattern), "an empty pattern is refused");
    HostLocale bad_text = gui_forms::portable_host_locale();
    bad_text.day_names[0] = std::string{"\xC3\x28", 2U};
    require(!gui_forms::valid_host_locale(bad_text), "invalid UTF-8 is refused");
    HostLocale bad_day = gui_forms::portable_host_locale();
    bad_day.first_day_of_week = static_cast<HostWeekday>(7);
    require(!gui_forms::valid_host_locale(bad_day), "an unknown first day is refused");
}

void test_json() {
    HostLocale locale = gui_forms::portable_host_locale();
    locale.name = "de-DE";
    locale.short_date_pattern = "dd.MM.yyyy";
    locale.am_designator = "\"q\"";
    locale.first_day_of_week = HostWeekday::monday;
    const std::string json = gui_forms::host_locale_json(locale);
    require(json.find("\"name\":\"de-DE\"") != std::string::npos, "the JSON names the locale");
    require(json.find("\"short_date_pattern\":\"dd.MM.yyyy\"") != std::string::npos, "the JSON has the short date");
    require(json.find("\"am_designator\":\"\\\"q\\\"\"") != std::string::npos, "the JSON escapes quotes");
    require(json.find("\"first_day_of_week\":1}") != std::string::npos, "the JSON ends with the first day");
    require(json.front() == '{' && json.back() == '}', "the JSON is one object");
}

void test_icu_patterns() {
    using gui_forms::host::picker_pattern_from_icu;
    require_text(picker_pattern_from_icu("M/d/yy"), "M/d/yy", "en-US short date");
    require_text(picker_pattern_from_icu("dd/MM/y"), "dd/MM/yyyy", "en-GB short date (y is the full year)");
    require_text(picker_pattern_from_icu("dd.MM.yy"), "dd.MM.yy", "de-DE short date");
    require_text(picker_pattern_from_icu("EEEE, MMMM d, y"), "dddd, MMMM d, yyyy", "en-US full date");
    require_text(picker_pattern_from_icu("EEEE, d MMMM y"), "dddd, d MMMM yyyy", "en-GB full date");
    require_text(picker_pattern_from_icu("EEEE, d. MMMM y"), "dddd, d. MMMM yyyy", "de-DE full date");
    require_text(picker_pattern_from_icu("h:mm a"), "h:mm tt", "en-US short time");
    require_text(picker_pattern_from_icu("HH:mm"), "HH:mm", "de-DE short time");
    require_text(picker_pattern_from_icu("K:mm a"), "h:mm tt", "a 0-11 hour as h");
    require_text(picker_pattern_from_icu("EEE d LLL"), "ddd d MMM", "abbreviated and stand-alone names");
    require_text(picker_pattern_from_icu("d 'de' MMMM 'de' y"), "d 'de' MMMM 'de' yyyy", "quoted text stays text");
    require_text(picker_pattern_from_icu("h 'o''clock' a"), "h 'o\\'clock' tt", "an apostrophe in quotes");
    require_text(picker_pattern_from_icu("HH''mm"), "HH'\\''mm", "an apostrophe outside quotes");
    require_text(picker_pattern_from_icu("y. MM. dd."), "yyyy. MM. dd.", "a trailing period stays");
    require_text(picker_pattern_from_icu("G y"), "yyyy", "an era is dropped and the space trimmed");
    require_text(picker_pattern_from_icu("h:mm a zzzz"), "h:mm tt", "a zone is dropped");
    require_text(picker_pattern_from_icu("y\\M"), "yyyy\\\\M", "a backslash is escaped");
    require_text(picker_pattern_from_icu("GGGG"), "", "nothing left gives an empty pattern");
}

void test_strftime_patterns() {
    using gui_forms::host::picker_pattern_from_strftime;
    require_text(picker_pattern_from_strftime("%m/%d/%Y"), "MM/dd/yyyy", "en_US D_FMT");
    require_text(picker_pattern_from_strftime("%d/%m/%y"), "dd/MM/yy", "en_GB D_FMT");
    require_text(picker_pattern_from_strftime("%d.%m.%Y"), "dd.MM.yyyy", "de_DE D_FMT");
    require_text(picker_pattern_from_strftime("%r"), "hh:mm:ss tt", "en_US T_FMT");
    require_text(picker_pattern_from_strftime("%T"), "HH:mm:ss", "en_GB T_FMT");
    require_text(picker_pattern_from_strftime("%I:%M:%S %p"), "hh:mm:ss tt", "a spelled twelve-hour clock");
    require_text(picker_pattern_from_strftime("%e %B %Y"), "d MMMM yyyy", "unpadded day and month name");
    require_text(picker_pattern_from_strftime("%a %b %Ed"), "ddd MMM dd", "the E modifier is skipped");
    require_text(picker_pattern_from_strftime("%-d.%-m.%Y"), "d.M.yyyy", "glibc's no-padding flag");
    require_text(picker_pattern_from_strftime("%Y年%m月%d日"), "yyyy年MM月dd日", "non-ASCII text stays");
    require_text(picker_pattern_from_strftime("%d de %B"), "dd 'de' MMMM", "ASCII letters are quoted");
    require_text(picker_pattern_from_strftime("%H h %M"), "HH 'h' mm", "a letter between fields is quoted");
    require_text(picker_pattern_from_strftime("%H'%M"), "HH\\'mm", "an apostrophe is escaped");
    require_text(picker_pattern_from_strftime("%Y-%m-%d %Z"), "yyyy-MM-dd", "a zone is dropped and trimmed");
    require_text(picker_pattern_from_strftime("%F"), "yyyy-MM-dd", "%F");
    require_text(picker_pattern_from_strftime("100%%"), "100%", "a percent sign");
    require_text(picker_pattern_from_strftime("%Z"), "", "nothing left gives an empty pattern");
}

#if defined(GUI_FORMS_TEST_MACOS_LOCALE)
void test_platform_reader() {
    // The user's locale is not fixed on a build machine; what is fixed is
    // that whatever is read is a record GUI.Forms can format with.
    const std::optional<HostLocale> locale = gui_forms::host::read_macos_locale();
    require(locale.has_value(), "CoreFoundation gives a current locale");
    require(gui_forms::valid_host_locale(*locale), "the macOS record is valid");
    require((*locale).name.find('_') == std::string::npos, "the macOS name is a BCP 47 tag");
}
#endif

#if defined(GUI_FORMS_TEST_POSIX_LOCALE)
void test_platform_reader() {
    using gui_forms::host::posix_locale_name;
    require_text(posix_locale_name("", "", "en_GB.UTF-8"), "en-GB", "LANG with a codeset");
    require_text(posix_locale_name("", "de_DE.UTF-8@euro", "en_US.UTF-8"), "de-DE", "LC_TIME before LANG");
    require_text(posix_locale_name("fr_FR.UTF-8", "de_DE.UTF-8", "en_US.UTF-8"), "fr-FR", "LC_ALL first");
    require_text(posix_locale_name("", "", "C.UTF-8"), "", "the C locale names nothing");
    require_text(posix_locale_name("POSIX", "de_DE.UTF-8", ""), "", "LC_ALL=POSIX names nothing");
    require_text(posix_locale_name("", "", ""), "", "nothing set names nothing");
    // The environment's own locale may be anything; with LC_ALL=C the host
    // reports none.
    require(setenv("LC_ALL", "C", 1) == 0, "LC_ALL is set");
    require(!gui_forms::host::read_posix_locale().has_value(), "the C locale is not reported");
    require(setenv("LC_ALL", "C.UTF-8", 1) == 0, "LC_ALL is set");
    require(!gui_forms::host::read_posix_locale().has_value(), "the C.UTF-8 locale is not reported");
}
#endif

} // namespace

int main() {
    try {
        test_portable_default();
        test_validation();
        test_json();
        test_icu_patterns();
        test_strftime_patterns();
#if defined(GUI_FORMS_TEST_MACOS_LOCALE) || defined(GUI_FORMS_TEST_POSIX_LOCALE)
        test_platform_reader();
#endif
    } catch (const std::exception& error) {
        std::cerr << "host_locale_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "host_locale_tests: passed\n";
    return EXIT_SUCCESS;
}
