#pragma once

#include <compare>
#include <cstdint>

namespace gui_forms {

struct DateTimeValue final {
    std::int32_t year{2000};
    std::uint8_t month{1U};
    std::uint8_t day{1U};
    std::uint8_t hour{};
    std::uint8_t minute{};
    std::uint8_t second{};
    std::uint16_t millisecond{};

    friend constexpr bool operator==(const DateTimeValue& left,
                                     const DateTimeValue& right) noexcept {
        return left.year == right.year && left.month == right.month &&
               left.day == right.day && left.hour == right.hour &&
               left.minute == right.minute && left.second == right.second &&
               left.millisecond == right.millisecond;
    }
    friend constexpr std::strong_ordering operator<=>(
        const DateTimeValue& left, const DateTimeValue& right) noexcept {
        std::strong_ordering order = left.year <=> right.year;
        if (order != 0) return order;
        order = left.month <=> right.month;
        if (order != 0) return order;
        order = left.day <=> right.day;
        if (order != 0) return order;
        order = left.hour <=> right.hour;
        if (order != 0) return order;
        order = left.minute <=> right.minute;
        if (order != 0) return order;
        order = left.second <=> right.second;
        return order != 0 ? order : left.millisecond <=> right.millisecond;
    }
};

[[nodiscard]] bool valid_date_time(DateTimeValue value) noexcept;
[[nodiscard]] std::uint8_t days_in_month(std::int32_t year,
                                         std::uint8_t month) noexcept;
[[nodiscard]] DateTimeValue add_days(DateTimeValue value,
                                     std::int64_t days) noexcept;
[[nodiscard]] std::uint8_t day_of_week(DateTimeValue value) noexcept;

} // namespace gui_forms
