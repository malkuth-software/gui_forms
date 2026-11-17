#include "windows_locale.hpp"

#include "windows_date_pattern.hpp"

#include <windows.h>

#include <array>
#include <climits>
#include <cstddef>
#include <string>

namespace gui_forms::host {
namespace {

// The longest locale value read, in UTF-16 units with the terminator. The
// values read here are far shorter (Microsoft documents at most 80
// characters for the date and time pictures and the names); GetLocaleInfoEx
// refuses a longer one (ERROR_INSUFFICIENT_BUFFER), and so does the reader.
constexpr int maximum_locale_value = 256;

// Where values are read from: the locale name (null for the user's
// default, LOCALE_NAME_USER_DEFAULT) and the flags added to every LCTYPE.
// `name` borrows a string that outlives the read.
struct LocaleSource final {
    const wchar_t* name{};
    LCTYPE flags{};
};

// The weekday name LCTYPEs in HostLocale's order, Sunday first; Windows
// numbers its names Monday first (LOCALE_SDAYNAME1 is Monday).
constexpr std::array<LCTYPE, 7> day_name_types{LOCALE_SDAYNAME7, LOCALE_SDAYNAME1, LOCALE_SDAYNAME2,
                                               LOCALE_SDAYNAME3, LOCALE_SDAYNAME4, LOCALE_SDAYNAME5,
                                               LOCALE_SDAYNAME6};
constexpr std::array<LCTYPE, 7> abbreviated_day_name_types{
    LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1, LOCALE_SABBREVDAYNAME2, LOCALE_SABBREVDAYNAME3,
    LOCALE_SABBREVDAYNAME4, LOCALE_SABBREVDAYNAME5, LOCALE_SABBREVDAYNAME6};
constexpr std::array<LCTYPE, 12> month_name_types{
    LOCALE_SMONTHNAME1, LOCALE_SMONTHNAME2, LOCALE_SMONTHNAME3,  LOCALE_SMONTHNAME4,
    LOCALE_SMONTHNAME5, LOCALE_SMONTHNAME6, LOCALE_SMONTHNAME7,  LOCALE_SMONTHNAME8,
    LOCALE_SMONTHNAME9, LOCALE_SMONTHNAME10, LOCALE_SMONTHNAME11, LOCALE_SMONTHNAME12};
constexpr std::array<LCTYPE, 12> abbreviated_month_name_types{
    LOCALE_SABBREVMONTHNAME1, LOCALE_SABBREVMONTHNAME2,  LOCALE_SABBREVMONTHNAME3,  LOCALE_SABBREVMONTHNAME4,
    LOCALE_SABBREVMONTHNAME5, LOCALE_SABBREVMONTHNAME6,  LOCALE_SABBREVMONTHNAME7,  LOCALE_SABBREVMONTHNAME8,
    LOCALE_SABBREVMONTHNAME9, LOCALE_SABBREVMONTHNAME10, LOCALE_SABBREVMONTHNAME11, LOCALE_SABBREVMONTHNAME12};

// `text` as UTF-8; empty optional when it holds an unpaired surrogate.
std::optional<std::string> utf8_from_wide(const std::wstring_view text) {
    std::string converted;
    if (text.empty()) return converted;
    if (text.size() > static_cast<std::size_t>(INT_MAX)) return std::nullopt;
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::nullopt;
    converted.resize(static_cast<std::size_t>(length));
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            converted.data(), length, nullptr, nullptr);
    if (written != length) return std::nullopt;
    return converted;
}

// The locale's text value `type` as UTF-8 (possibly empty); empty optional
// when it cannot be read, is too long, or cannot be converted.
std::optional<std::string> locale_text(const LocaleSource& source, const LCTYPE type) {
    std::array<wchar_t, maximum_locale_value> buffer{};
    const int written = GetLocaleInfoEx(source.name, type | source.flags, buffer.data(), maximum_locale_value);
    // The count includes the terminator.
    if (written <= 0) return std::nullopt;
    const std::wstring_view text(buffer.data(), static_cast<std::size_t>(written - 1));
    const std::optional<std::string> converted = utf8_from_wide(text);
    return converted;
}

// Reads the names `types` into `names`, all or none: false, with `names`
// unchanged, when one cannot be read.
template <std::size_t Count>
bool fill_names(const LocaleSource& source, const std::array<LCTYPE, Count>& types,
                std::array<std::string, Count>& names) {
    std::array<std::string, Count> read{};
    for (std::size_t index = 0U; index < Count; ++index) {
        const std::optional<std::string> name = locale_text(source, types[index]);
        if (!name.has_value()) return false;
        read[index] = *name;
    }
    names = read;
    return true;
}

// Replaces `pattern` with the locale's picture `type` converted, when it
// reads and converts to something; false when it cannot be read.
bool fill_pattern(const LocaleSource& source, const LCTYPE type, std::string& pattern) {
    const std::optional<std::string> picture = locale_text(source, type);
    if (!picture.has_value()) return false;
    const std::string converted = picker_pattern_from_windows(*picture);
    if (!converted.empty()) pattern = converted;
    return true;
}

// The record for `source`; empty as read_windows_locale says.
std::optional<HostLocale> read_locale(const LocaleSource& source) {
    HostLocale record = portable_host_locale();
    const std::optional<std::string> name = locale_text(source, LOCALE_SNAME);
    if (!name.has_value() || (*name).empty()) return std::nullopt;
    record.name = *name;
    if (!fill_pattern(source, LOCALE_SSHORTDATE, record.short_date_pattern)) return std::nullopt;
    if (!fill_pattern(source, LOCALE_SLONGDATE, record.long_date_pattern)) return std::nullopt;
    if (!fill_pattern(source, LOCALE_SSHORTTIME, record.time_pattern)) return std::nullopt;
    if (!fill_names(source, month_name_types, record.month_names)) return std::nullopt;
    if (!fill_names(source, abbreviated_month_name_types, record.abbreviated_month_names)) return std::nullopt;
    if (!fill_names(source, day_name_types, record.day_names)) return std::nullopt;
    if (!fill_names(source, abbreviated_day_name_types, record.abbreviated_day_names)) return std::nullopt;
    const std::optional<std::string> am = locale_text(source, LOCALE_S1159);
    const std::optional<std::string> pm = locale_text(source, LOCALE_S2359);
    const std::optional<std::string> decimal = locale_text(source, LOCALE_SDECIMAL);
    const std::optional<std::string> group = locale_text(source, LOCALE_STHOUSAND);
    if (!am.has_value() || !pm.has_value() || !decimal.has_value() || !group.has_value()) return std::nullopt;
    record.am_designator = *am;
    record.pm_designator = *pm;
    record.decimal_separator = *decimal;
    record.group_separator = *group;
    // LOCALE_RETURN_NUMBER writes a DWORD into the buffer, whose size is
    // given in UTF-16 units.
    DWORD first_day = 0U;
    const int number = GetLocaleInfoEx(source.name, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER | source.flags,
                                       reinterpret_cast<LPWSTR>(&first_day),
                                       static_cast<int>(sizeof(DWORD) / sizeof(wchar_t)));
    // Windows counts Monday as 0; HostWeekday counts Sunday as 0.
    if (number <= 0 || first_day > 6U) return std::nullopt;
    record.first_day_of_week = static_cast<HostWeekday>((first_day + 1U) % 7U);
    if (!valid_host_locale(record)) return std::nullopt;
    return record;
}

} // namespace

std::optional<HostLocale> read_windows_locale() {
    const LocaleSource source{LOCALE_NAME_USER_DEFAULT, 0U};
    const std::optional<HostLocale> record = read_locale(source);
    return record;
}

std::optional<HostLocale> read_windows_locale_defaults(const std::string_view name) {
    if (name.empty() || name.size() >= static_cast<std::size_t>(LOCALE_NAME_MAX_LENGTH)) return std::nullopt;
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> wide{};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
                                           wide.data(), LOCALE_NAME_MAX_LENGTH - 1);
    // The array starts zeroed, so the name stays terminated.
    if (length <= 0) return std::nullopt;
    if (IsValidLocaleName(wide.data()) == FALSE) return std::nullopt;
    const LocaleSource source{wide.data(), LOCALE_NOUSEROVERRIDE};
    const std::optional<HostLocale> record = read_locale(source);
    return record;
}

} // namespace gui_forms::host
