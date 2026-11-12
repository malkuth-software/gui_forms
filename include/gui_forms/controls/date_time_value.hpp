#pragma once

#include "gui_forms/export/application_export.hpp"
#include <compare>
#include <cstdint>

namespace gui_forms {

struct GUI_FORMS_API DateTimeValue final {
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

[[nodiscard]] GUI_FORMS_API bool valid_date_time(DateTimeValue value) noexcept;
[[nodiscard]] GUI_FORMS_API std::uint8_t days_in_month(std::int32_t year,
                                         std::uint8_t month) noexcept;
[[nodiscard]] GUI_FORMS_API DateTimeValue add_days(DateTimeValue value,
                                     std::int64_t days) noexcept;
[[nodiscard]] GUI_FORMS_API std::uint8_t day_of_week(DateTimeValue value) noexcept;

} // namespace gui_forms
