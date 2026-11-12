#include "gui_forms/controls/date_time_value.hpp"

#include "../date_time_utilities.hpp"

#include <array>
#include <limits>

namespace gui_forms {
using namespace date_time_detail;

std::uint8_t days_in_month(std::int32_t year, std::uint8_t month) noexcept {
    if (month < 1U || month > 12U) return 0U;
    constexpr std::array<std::uint8_t, 12> days{
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if (month != 2U) return days[month - 1U];
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return static_cast<std::uint8_t>(leap ? 29U : 28U);
}

bool valid_date_time(DateTimeValue value) noexcept {
    return value.month >= 1U && value.month <= 12U && value.day >= 1U &&
           value.day <= days_in_month(value.year, value.month) &&
           value.hour <= 23U && value.minute <= 59U && value.second <= 59U &&
           value.millisecond <= 999U;
}

DateTimeValue add_days(DateTimeValue value, std::int64_t days) noexcept {
    if (!valid_date_time(value)) return value;
    const std::int64_t serial = days_from_civil(value.year, value.month, value.day);
    if ((days > 0 && serial > std::numeric_limits<std::int64_t>::max() - days) ||
        (days < 0 && serial < std::numeric_limits<std::int64_t>::min() - days)) {
        return value;
    }
    return civil_from_days(serial + days, value);
}

std::uint8_t day_of_week(DateTimeValue value) noexcept {
    if (!valid_date_time(value)) return 0U;
    std::int64_t weekday = (days_from_civil(value.year, value.month, value.day) + 4) % 7;
    if (weekday < 0) weekday += 7;
    return static_cast<std::uint8_t>(weekday);
}

} // namespace gui_forms
