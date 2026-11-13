#include "posix_locale.hpp"

#include "strftime_date_pattern.hpp"

#include <langinfo.h>
#include <locale.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace gui_forms::host {
namespace {

// The environment variable `name`, or empty when unset.
std::string_view environment_text(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return {};
    const std::string_view text{value};
    return text;
}

// A locale loaded with newlocale, freed on destruction.
class OwnedLocale final {
public:
    explicit OwnedLocale(locale_t locale) noexcept : locale_(locale) {}
    OwnedLocale(const OwnedLocale&) = delete;
    OwnedLocale& operator=(const OwnedLocale&) = delete;
    ~OwnedLocale() {
        if (locale_ != static_cast<locale_t>(0)) freelocale(locale_);
    }
    [[nodiscard]] locale_t get() const noexcept { return locale_; }

private:
    locale_t locale_;
};

// nl_langinfo_l's text for `item`, copied (the locale owns the original);
// empty for a null result.
std::string item_text(locale_t locale, nl_item item) {
    const char* text = nl_langinfo_l(item, locale);
    std::string result;
    if (text != nullptr) result = text;
    return result;
}

template <std::size_t Count>
void fill_names(locale_t locale, const std::array<nl_item, Count>& items, std::array<std::string, Count>& names) {
    std::array<std::string, Count> read{};
    for (std::size_t index = 0U; index < Count; ++index) {
        read[index] = item_text(locale, items[index]);
        if (read[index].empty()) return;
    }
    names = read;
}

std::string converted_or(const std::string& strftime_pattern, const std::string& fallback) {
    const std::string converted = picker_pattern_from_strftime(strftime_pattern);
    return converted.empty() ? fallback : converted;
}

} // namespace

std::string posix_locale_name(const std::string_view lc_all, const std::string_view lc_time,
                              const std::string_view lang) {
    std::string_view chosen = lang;
    if (!lc_time.empty()) chosen = lc_time;
    if (!lc_all.empty()) chosen = lc_all;
    const std::size_t end = chosen.find_first_of(".@");
    std::string name{chosen.substr(0U, end)};
    if (name == "C" || name == "POSIX") name.clear();
    for (char& character : name) {
        if (character == '_') character = '-';
    }
    return name;
}

std::optional<HostLocale> read_posix_locale() {
    const std::string name =
        posix_locale_name(environment_text("LC_ALL"), environment_text("LC_TIME"), environment_text("LANG"));
    if (name.empty()) return std::nullopt;
    const OwnedLocale locale(newlocale(LC_ALL_MASK, "", static_cast<locale_t>(0)));
    if (locale.get() == static_cast<locale_t>(0)) return std::nullopt;

    HostLocale record = portable_host_locale();
    record.name = name;
    record.short_date_pattern = converted_or(item_text(locale.get(), D_FMT), record.short_date_pattern);
    record.time_pattern = converted_or(item_text(locale.get(), T_FMT), record.time_pattern);
    record.long_date_pattern = "dddd, " + record.short_date_pattern;

    static constexpr std::array<nl_item, 12> months{MON_1, MON_2, MON_3, MON_4,  MON_5,  MON_6,
                                                    MON_7, MON_8, MON_9, MON_10, MON_11, MON_12};
    static constexpr std::array<nl_item, 12> abbreviated_months{ABMON_1, ABMON_2, ABMON_3,  ABMON_4,
                                                                ABMON_5, ABMON_6, ABMON_7,  ABMON_8,
                                                                ABMON_9, ABMON_10, ABMON_11, ABMON_12};
    static constexpr std::array<nl_item, 7> days{DAY_1, DAY_2, DAY_3, DAY_4, DAY_5, DAY_6, DAY_7};
    static constexpr std::array<nl_item, 7> abbreviated_days{ABDAY_1, ABDAY_2, ABDAY_3, ABDAY_4,
                                                             ABDAY_5, ABDAY_6, ABDAY_7};
    fill_names(locale.get(), months, record.month_names);
    fill_names(locale.get(), abbreviated_months, record.abbreviated_month_names);
    fill_names(locale.get(), days, record.day_names);
    fill_names(locale.get(), abbreviated_days, record.abbreviated_day_names);
    // A locale with a 24-hour clock gives empty designators; they stay empty.
    record.am_designator = item_text(locale.get(), AM_STR);
    record.pm_designator = item_text(locale.get(), PM_STR);

    const std::string radix = item_text(locale.get(), RADIXCHAR);
    if (!radix.empty()) record.decimal_separator = radix;
    record.group_separator = item_text(locale.get(), THOUSEP);

#if defined(__GLIBC__)
    // glibc's week: _NL_TIME_WEEK_1STDAY is a date (YYYYMMDD) carried in the
    // pointer's value, 19971130 a Sunday and 19971201 a Monday; the first
    // byte of _NL_TIME_FIRST_WEEKDAY counts from that day, 1 being the day
    // itself.
    const std::uintptr_t week_start =
        reinterpret_cast<std::uintptr_t>(nl_langinfo_l(_NL_TIME_WEEK_1STDAY, locale.get()));
    const char* first_weekday = nl_langinfo_l(_NL_TIME_FIRST_WEEKDAY, locale.get());
    const unsigned base = week_start == 19971201U ? 1U : 0U;
    if (first_weekday != nullptr && first_weekday[0] >= 1 && first_weekday[0] <= 7) {
        const unsigned day = (base + static_cast<unsigned>(first_weekday[0]) - 1U) % 7U;
        record.first_day_of_week = static_cast<HostWeekday>(day);
    }
#endif

    if (!valid_host_locale(record)) return std::nullopt;
    return record;
}

} // namespace gui_forms::host
