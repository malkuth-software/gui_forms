#include "date_time_utilities.hpp"

#include "gui_forms/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace gui_forms::date_time_detail {

std::int64_t days_from_civil(std::int64_t year, unsigned month,
                             unsigned day) noexcept {
    year -= month <= 2U;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        (153U * (month > 2U ? month - 3U : month + 9U) + 2U) / 5U + day - 1U;
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
        year_of_era / 100U + day_of_year;
    return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

DateTimeValue civil_from_days(std::int64_t serial, DateTimeValue time) noexcept {
    serial += 719468;
    const std::int64_t era = (serial >= 0 ? serial : serial - 146096) / 146097;
    const unsigned day_of_era = static_cast<unsigned>(serial - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) / 365U;
    std::int64_t year = static_cast<std::int64_t>(year_of_era) + era * 400;
    const unsigned day_of_year = day_of_era -
        (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    const unsigned month_prime = (5U * day_of_year + 2U) / 153U;
    const unsigned day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    const unsigned month = month_prime < 10U ? month_prime + 3U : month_prime - 9U;
    year += month <= 2U;
    if (year < std::numeric_limits<std::int32_t>::min() ||
        year > std::numeric_limits<std::int32_t>::max()) return time;
    time.year = static_cast<std::int32_t>(year);
    time.month = static_cast<std::uint8_t>(month);
    time.day = static_cast<std::uint8_t>(day);
    return time;
}

[[nodiscard]] bool includes(Modifier value, Modifier requested) noexcept {
    return (static_cast<std::uint8_t>(value) &
            static_cast<std::uint8_t>(requested)) != 0U;
}

[[nodiscard]] constexpr DateTimeValue civil_part(DateTimeValue value) noexcept {
    value.hour = 0U;
    value.minute = 0U;
    value.second = 0U;
    value.millisecond = 0U;
    return value;
}

bool civil_in_range(DateTimeValue value, DateTimeValue minimum,
                    DateTimeValue maximum) noexcept {
    const DateTimeValue date = civil_part(value);
    return date >= civil_part(minimum) && date <= civil_part(maximum);
}

[[nodiscard]] std::string padded(unsigned value, unsigned digits) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), digits == 2U ? "%02u" : "%04u", value);
    return buffer;
}

[[nodiscard]] std::string format_date_time(
    DateTimeValue value, std::string_view pattern,
    const DateTimeFormatProvider& provider) {
    std::string result;
    bool quoted = false;
    for (std::size_t index = 0U; index < pattern.size();) {
        if (pattern[index] == '\'') {
            quoted = !quoted;
            ++index;
            continue;
        }
        if (pattern[index] == '\\' && index + 1U < pattern.size()) {
            result.push_back(pattern[index + 1U]);
            index += 2U;
            continue;
        }
        if (quoted || !std::isalpha(static_cast<unsigned char>(pattern[index]))) {
            result.push_back(pattern[index++]);
            continue;
        }
        const char token = pattern[index];
        std::size_t count = 1U;
        while (index + count < pattern.size() && pattern[index + count] == token) {
            ++count;
        }
        switch (token) {
        case 'y':
            result += count <= 2U
                ? padded(static_cast<unsigned>(std::abs(value.year) % 100), 2U)
                : padded(static_cast<unsigned>(std::abs(value.year)), 4U);
            break;
        case 'M':
            if (count >= 4U) result += provider.month_names[value.month - 1U];
            else if (count == 3U) result += provider.abbreviated_month_names[value.month - 1U];
            else if (count == 2U) result += padded(value.month, 2U);
            else result += std::to_string(value.month);
            break;
        case 'd':
            if (count >= 4U) result += provider.day_names[day_of_week(value)];
            else if (count == 3U) result += provider.abbreviated_day_names[day_of_week(value)];
            else if (count == 2U) result += padded(value.day, 2U);
            else result += std::to_string(value.day);
            break;
        case 'H':
            result += count >= 2U ? padded(value.hour, 2U)
                                  : std::to_string(value.hour);
            break;
        case 'h': {
            const unsigned hour = value.hour % 12U == 0U ? 12U : value.hour % 12U;
            result += count >= 2U ? padded(hour, 2U) : std::to_string(hour);
            break;
        }
        case 'm':
            result += count >= 2U ? padded(value.minute, 2U)
                                  : std::to_string(value.minute);
            break;
        case 's':
            result += count >= 2U ? padded(value.second, 2U)
                                  : std::to_string(value.second);
            break;
        case 't': {
            const std::string& designator = value.hour < 12U
                ? provider.am_designator : provider.pm_designator;
            result += count == 1U && !designator.empty()
                ? designator.substr(0U, 1U) : designator;
            break;
        }
        default:
            result.append(pattern.substr(index, count));
            break;
        }
        index += count;
    }
    return result;
}

[[nodiscard]] bool valid_provider_text(const std::string& value) {
    return !value.empty() && validate_utf8(value).valid();
}

const DateTimeFormatProvider& locale_format_provider(const Control& control,
                                                     std::optional<std::uint64_t>& generation,
                                                     DateTimeFormatProvider& cache) {
    const std::uint64_t current = control.effective_locale_generation();
    if (generation.has_value() && *generation == current) return cache;
    cache = DateTimeFormatProvider::from_locale(control.effective_locale());
    generation = current;
    return cache;
}

void validate_provider(const DateTimeFormatProvider& provider) {
    for (const std::string& value : provider.month_names) {
        if (!valid_provider_text(value))
            throw std::invalid_argument("DateTime month name");
    }
    for (const std::string& value : provider.abbreviated_month_names) {
        if (!valid_provider_text(value))
            throw std::invalid_argument("DateTime abbreviated month name");
    }
    for (const std::string& value : provider.day_names) {
        if (!valid_provider_text(value))
            throw std::invalid_argument("DateTime day name");
    }
    for (const std::string& value : provider.abbreviated_day_names) {
        if (!valid_provider_text(value))
            throw std::invalid_argument("DateTime abbreviated day name");
    }
    if (!valid_provider_text(provider.long_date_pattern) ||
        !valid_provider_text(provider.short_date_pattern) ||
        !valid_provider_text(provider.time_pattern) ||
        !validate_utf8(provider.am_designator).valid() ||
        !validate_utf8(provider.pm_designator).valid()) {
        throw std::invalid_argument("DateTime format provider text");
    }
}

bool parse_iso_date(std::string_view text, DateTimeValue& value) {
    if (text.size() != 10U || text[4] != '-' || text[7] != '-') return false;
    int year{};
    unsigned month{};
    unsigned day{};
    const std::from_chars_result year_result =
        std::from_chars(text.data(), text.data() + 4U, year);
    const std::from_chars_result month_result =
        std::from_chars(text.data() + 5U, text.data() + 7U, month);
    const std::from_chars_result day_result =
        std::from_chars(text.data() + 8U, text.data() + 10U, day);
    if (year_result.ec != std::errc{} ||
        month_result.ec != std::errc{} ||
        day_result.ec != std::errc{}) return false;
    value.year = year;
    value.month = static_cast<std::uint8_t>(month);
    value.day = static_cast<std::uint8_t>(day);
    return valid_date_time(value);
}

} // namespace gui_forms::date_time_detail
