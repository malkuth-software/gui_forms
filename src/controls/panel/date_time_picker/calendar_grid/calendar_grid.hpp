#pragma once

#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The calendar model and layout DateTimePicker's drop-down calendar and
// MonthCalendar share: months and their arithmetic, the six-week page of
// dates a month shows, and the geometry of a page's day cells.
namespace gui_forms::calendar_detail {

// A month page is six weeks of seven days, so that every month fits.
inline constexpr std::size_t grid_columns = 7U;
inline constexpr std::size_t grid_rows = 6U;
inline constexpr std::size_t grid_cells = grid_columns * grid_rows;

// A year and a month (1 to 12).
struct CalendarMonth final {
    std::int32_t year{2000};
    std::uint8_t month{1U};

    friend constexpr bool operator==(CalendarMonth left, CalendarMonth right) noexcept {
        return left.year == right.year && left.month == right.month;
    }
};

[[nodiscard]] CalendarMonth month_of(DateTimeValue value) noexcept;
// The month `offset` months after (or, negative, before) `month`.
[[nodiscard]] CalendarMonth shifted_month(CalendarMonth month, std::int32_t offset) noexcept;
// Months from `from` to `to`; negative when `to` is earlier.
[[nodiscard]] std::int64_t months_between(CalendarMonth from, CalendarMonth to) noexcept;
// `value` moved into `month`, its day clamped to the month's length; the
// time of day is kept.
[[nodiscard]] DateTimeValue in_month(DateTimeValue value, CalendarMonth month) noexcept;
// The first day of `month` at midnight.
[[nodiscard]] DateTimeValue first_day(CalendarMonth month) noexcept;
// The same civil date, ignoring the time of day.
[[nodiscard]] bool same_day(DateTimeValue left, DateTimeValue right) noexcept;
// The date without its time of day.
[[nodiscard]] DateTimeValue civil_date(DateTimeValue value) noexcept;

// The 42 dates a month page shows: six weeks from the `first_day_of_week`
// (0 is Sunday, as day_of_week counts) on or before the month's first day.
// `month_start` is the month's first day; its time of day is kept on every
// date.
[[nodiscard]] std::array<DateTimeValue, grid_cells> month_page_dates(
    DateTimeValue month_start, std::uint8_t first_day_of_week) noexcept;
// The page cell (row * 7 + column) of `date` on the page of `month_start`;
// none when the page does not show it.
[[nodiscard]] std::optional<std::size_t> month_page_cell(
    DateTimeValue month_start, std::uint8_t first_day_of_week, DateTimeValue date) noexcept;

// Where a page's day cells sit, in logical units before the text scale:
// the first cell's corner, the distance from one cell to the next, and a
// cell's size (smaller than the pitch when cells have gaps between them).
struct MonthGridMetrics final {
    double cell_left{};
    double cell_top{};
    double column_pitch{};
    double row_pitch{};
    double cell_width{};
    double cell_height{};
};

// The bounds of page cell `cell`, scaled by `scale`.
[[nodiscard]] Rect month_grid_cell_bounds(const MonthGridMetrics& metrics, std::size_t cell,
                                          double scale) noexcept;
// The page cell whose pitch rectangle holds `point` (scaled coordinates);
// none outside the grid.
[[nodiscard]] std::optional<std::size_t> month_grid_cell_at(const MonthGridMetrics& metrics,
                                                            Point point, double scale) noexcept;

} // namespace gui_forms::calendar_detail
