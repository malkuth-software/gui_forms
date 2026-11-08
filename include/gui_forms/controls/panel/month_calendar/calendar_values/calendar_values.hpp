#pragma once

// MonthCalendar's value types, apart from the control so its capabilities
// header can name them.

#include "gui_forms/controls/panel/date_time_picker/date_time_value/date_time_value.hpp"

#include <cstdint>

namespace gui_forms {

// A day of the week, numbered as day_of_week numbers them (Sunday is 0).
enum class CalendarWeekday : std::uint8_t {
    sunday,
    monday,
    tuesday,
    wednesday,
    thursday,
    friday,
    saturday,
};

// A run of whole days, first to last inclusive; the time of day is zero.
struct CalendarDateRange final {
    DateTimeValue start{};
    DateTimeValue end{};

    friend constexpr bool operator==(const CalendarDateRange& left,
                                     const CalendarDateRange& right) noexcept {
        return left.start == right.start && left.end == right.end;
    }
};

// How many months the calendar shows: columns by rows, at most twelve.
struct CalendarDimensions final {
    std::uint32_t columns{1U};
    std::uint32_t rows{1U};

    friend constexpr bool operator==(CalendarDimensions left, CalendarDimensions right) noexcept {
        return left.columns == right.columns && left.rows == right.rows;
    }
};

} // namespace gui_forms
