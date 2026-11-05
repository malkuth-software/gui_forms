#include "gui_forms/controls/panel/month_calendar/month_calendar.hpp"

#include "../date_time_picker/calendar_grid/calendar_grid.hpp"
#include "../date_time_picker/date_time_utilities.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace calendar_detail;

namespace {

// One month block in logical units at text scale 1: a margin, the title
// row, the weekday row, six rows of day cells, and an optional week-number
// column before the days. The "Today:" line runs below all blocks.
constexpr double block_margin = 6.0;
constexpr double title_height = 26.0;
constexpr double weekday_height = 20.0;
constexpr double cell_width = 30.0;
constexpr double cell_height = 22.0;
constexpr double week_column_width = 24.0;
constexpr double today_height = 22.0;
constexpr double arrow_size = 22.0;
constexpr double glyph_size = 7.0;
constexpr std::uint32_t maximum_months = 12U;

// One month block's size, unscaled: the margins, the week-number column
// when shown and seven day columns across; the margins, the title and
// weekday rows and six rows of days down.
Size month_block_size(bool week_numbers) noexcept {
    const double week = week_numbers ? week_column_width : 0.0;
    const double width = block_margin * 2.0 + week + cell_width * 7.0;
    const double height = block_margin * 2.0 + title_height + weekday_height + cell_height * 6.0;
    const Size result{width, height};
    return result;
}

Rect scaled(Rect bounds, double scale) noexcept {
    const Rect result{bounds.x * scale, bounds.y * scale, bounds.width * scale, bounds.height * scale};
    return result;
}

Rect inset(Rect bounds, double amount) noexcept {
    const Rect result{bounds.x + amount, bounds.y + amount, std::max(0.0, bounds.width - amount * 2.0),
                      std::max(0.0, bounds.height - amount * 2.0)};
    return result;
}

// A square glyph of `side` centred in `area`, on whole units.
Rect centred_glyph(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect glyph{std::floor(area.x + (area.width - side) * 0.5),
                     std::floor(area.y + (area.height - side) * 0.5), side, side};
    return glyph;
}

std::int64_t serial_day(DateTimeValue date) noexcept {
    const std::int64_t result = date_time_detail::days_from_civil(date.year, date.month, date.day);
    return result;
}

std::string iso_date(DateTimeValue date) {
    const std::string result = date_time_detail::padded(static_cast<unsigned>(date.year), 4U) + "-" +
                               date_time_detail::padded(date.month, 2U) + "-" +
                               date_time_detail::padded(date.day, 2U);
    return result;
}

// The system clock's current date in UTC.
DateTimeValue system_date() noexcept {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::sys_days day = std::chrono::floor<std::chrono::days>(now);
    const std::int64_t serial = day.time_since_epoch().count();
    const DateTimeValue result = date_time_detail::civil_from_days(serial, DateTimeValue{});
    return result;
}

void erase_day(std::vector<DateTimeValue>& dates, DateTimeValue date) {
    for (std::size_t index = dates.size(); index > 0U; --index) {
        if (same_day(dates[index - 1U], date)) {
            dates.erase(dates.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        }
    }
}

} // namespace

MonthCalendar::MonthCalendar(StableId stable_id) : Control(std::move(stable_id)) {
    set_focusable(true);
    set_cursor(CursorKind::arrow);
    set_paint_plane(PaintPlane::control);
    system_today_ = system_date();
    selection_ = CalendarDateRange{system_today_, system_today_};
    anchor_ = system_today_;
    focus_date_ = system_today_;
    first_year_ = system_today_.year;
    first_month_ = system_today_.month;
}

// -- model ---------------------------------------------------------------------

std::size_t MonthCalendar::month_count() const noexcept {
    const std::size_t result = static_cast<std::size_t>(dimensions_.columns) * dimensions_.rows;
    return result;
}

DateTimeValue MonthCalendar::month_start(std::size_t month) const noexcept {
    const CalendarMonth shown = shifted_month(CalendarMonth{first_year_, first_month_},
                                              static_cast<std::int32_t>(month));
    return first_day(shown);
}

DateTimeValue MonthCalendar::first_displayed_month() const noexcept {
    return month_start(0U);
}

bool MonthCalendar::in_range(DateTimeValue date) const noexcept {
    const bool result = date_time_detail::civil_in_range(date, minimum_, maximum_);
    return result;
}

CalendarDateRange MonthCalendar::bounded_range(DateTimeValue anchor, DateTimeValue target) const noexcept {
    const std::int64_t span = serial_day(target) - serial_day(anchor);
    const std::int64_t limit = static_cast<std::int64_t>(max_selection_count_) - 1;
    DateTimeValue reach = target;
    if (span > limit) reach = add_days(anchor, limit);
    if (span < -limit) reach = add_days(anchor, -limit);
    CalendarDateRange range{anchor, reach};
    if (reach < anchor) range = CalendarDateRange{reach, anchor};
    return range;
}

void MonthCalendar::select(CalendarDateRange range, DateTimeValue focus) {
    focus_date_ = focus;
    ensure_visible(focus);
    invalidate(Dirty::paint | Dirty::semantics);
    if (range == selection_) return;
    selection_ = range;
    publish_change(date_changed_, selection_);
}

void MonthCalendar::set_selection_range(DateTimeValue first, DateTimeValue last) {
    require_mutable();
    if (!valid_date_time(first) || !valid_date_time(last) || !in_range(first) || !in_range(last)) {
        throw std::out_of_range("MonthCalendar selection must be valid dates within min_date and max_date");
    }
    DateTimeValue start = civil_date(first);
    DateTimeValue end = civil_date(last);
    if (end < start) std::swap(start, end);
    const CalendarDateRange range = bounded_range(start, end);
    anchor_ = range.start;
    select(range, range.end);
}

void MonthCalendar::set_date(DateTimeValue date) {
    set_selection_range(date, date);
}

void MonthCalendar::set_max_selection_count(std::uint32_t count) {
    require_mutable();
    if (count == 0U) throw std::out_of_range("MonthCalendar max selection count must be at least one");
    if (max_selection_count_ == count) return;
    max_selection_count_ = count;
    const CalendarDateRange range = bounded_range(selection_.start, selection_.end);
    anchor_ = range.start;
    select(range, range.end);
}

void MonthCalendar::set_range(DateTimeValue minimum, DateTimeValue maximum) {
    require_mutable();
    if (!valid_date_time(minimum) || !valid_date_time(maximum) || maximum < minimum) {
        throw std::invalid_argument("MonthCalendar requires a valid ordered range");
    }
    minimum_ = civil_date(minimum);
    maximum_ = civil_date(maximum);
    const DateTimeValue start = std::clamp(selection_.start, minimum_, maximum_);
    const DateTimeValue end = std::clamp(selection_.end, minimum_, maximum_);
    anchor_ = start;
    clamp_first_month();
    select(CalendarDateRange{start, end}, end);
}

void MonthCalendar::set_today_date(DateTimeValue date) {
    require_mutable();
    if (!valid_date_time(date)) throw std::invalid_argument("MonthCalendar today date is invalid");
    set_today_ = civil_date(date);
    invalidate(Dirty::paint | Dirty::semantics);
}

void MonthCalendar::set_show_today(bool show) {
    require_mutable();
    if (show_today_ == show) return;
    show_today_ = show;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MonthCalendar::set_show_today_circle(bool show) {
    require_mutable();
    if (show_today_circle_ == show) return;
    show_today_circle_ = show;
    invalidate(Dirty::paint | Dirty::semantics);
}

void MonthCalendar::set_show_week_numbers(bool show) {
    require_mutable();
    if (show_week_numbers_ == show) return;
    show_week_numbers_ = show;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MonthCalendar::set_first_day_of_week(CalendarWeekday day) {
    require_mutable();
    if (static_cast<std::uint8_t>(day) > static_cast<std::uint8_t>(CalendarWeekday::saturday)) {
        throw std::invalid_argument("MonthCalendar first day of week is invalid");
    }
    if (first_day_of_week_ == day) return;
    first_day_of_week_ = day;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MonthCalendar::set_calendar_dimensions(CalendarDimensions dimensions) {
    require_mutable();
    if (dimensions.columns == 0U || dimensions.rows == 0U || dimensions.columns > maximum_months ||
        dimensions.rows > maximum_months || dimensions.columns * dimensions.rows > maximum_months) {
        throw std::out_of_range("MonthCalendar shows one to twelve months");
    }
    if (dimensions_ == dimensions) return;
    dimensions_ = dimensions;
    clamp_first_month();
    ensure_visible(focus_date_);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MonthCalendar::set_scroll_change(std::uint32_t months) {
    require_mutable();
    if (months > 20000U) throw std::out_of_range("MonthCalendar scroll change is at most 20000 months");
    scroll_change_ = months;
}

void MonthCalendar::set_format_provider(DateTimeFormatProvider provider) {
    require_mutable();
    date_time_detail::validate_provider(provider);
    provider_ = std::move(provider);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void MonthCalendar::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("MonthCalendar font specification is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint);
}

// -- bolded dates ----------------------------------------------------------------

void MonthCalendar::validate_bolded(DateTimeValue date) const {
    if (!valid_date_time(date)) throw std::invalid_argument("MonthCalendar bolded date is invalid");
}

void MonthCalendar::bolded_changed() {
    invalidate(Dirty::paint | Dirty::semantics);
}

void MonthCalendar::set_bolded_dates(std::vector<DateTimeValue> dates) {
    require_mutable();
    for (const DateTimeValue date : dates) validate_bolded(date);
    bolded_ = std::move(dates);
    bolded_changed();
}

void MonthCalendar::add_bolded_date(DateTimeValue date) {
    require_mutable();
    validate_bolded(date);
    bolded_.push_back(date);
    bolded_changed();
}

void MonthCalendar::remove_bolded_date(DateTimeValue date) {
    require_mutable();
    erase_day(bolded_, date);
    bolded_changed();
}

void MonthCalendar::remove_all_bolded_dates() {
    require_mutable();
    bolded_.clear();
    bolded_changed();
}

void MonthCalendar::set_annually_bolded_dates(std::vector<DateTimeValue> dates) {
    require_mutable();
    for (const DateTimeValue date : dates) validate_bolded(date);
    annually_bolded_ = std::move(dates);
    bolded_changed();
}

void MonthCalendar::add_annually_bolded_date(DateTimeValue date) {
    require_mutable();
    validate_bolded(date);
    annually_bolded_.push_back(date);
    bolded_changed();
}

void MonthCalendar::remove_annually_bolded_date(DateTimeValue date) {
    require_mutable();
    for (std::size_t index = annually_bolded_.size(); index > 0U; --index) {
        const DateTimeValue held = annually_bolded_[index - 1U];
        if (held.month == date.month && held.day == date.day) {
            annually_bolded_.erase(annually_bolded_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        }
    }
    bolded_changed();
}

void MonthCalendar::remove_all_annually_bolded_dates() {
    require_mutable();
    annually_bolded_.clear();
    bolded_changed();
}

void MonthCalendar::set_monthly_bolded_dates(std::vector<DateTimeValue> dates) {
    require_mutable();
    for (const DateTimeValue date : dates) validate_bolded(date);
    monthly_bolded_ = std::move(dates);
    bolded_changed();
}

void MonthCalendar::add_monthly_bolded_date(DateTimeValue date) {
    require_mutable();
    validate_bolded(date);
    monthly_bolded_.push_back(date);
    bolded_changed();
}

void MonthCalendar::remove_monthly_bolded_date(DateTimeValue date) {
    require_mutable();
    for (std::size_t index = monthly_bolded_.size(); index > 0U; --index) {
        if (monthly_bolded_[index - 1U].day == date.day) {
            monthly_bolded_.erase(monthly_bolded_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        }
    }
    bolded_changed();
}

void MonthCalendar::remove_all_monthly_bolded_dates() {
    require_mutable();
    monthly_bolded_.clear();
    bolded_changed();
}

bool MonthCalendar::bolded(DateTimeValue date) const noexcept {
    for (const DateTimeValue held : bolded_) {
        if (same_day(held, date)) return true;
    }
    for (const DateTimeValue held : annually_bolded_) {
        if (held.month == date.month && held.day == date.day) return true;
    }
    for (const DateTimeValue held : monthly_bolded_) {
        if (held.day == date.day) return true;
    }
    return false;
}

// -- display -----------------------------------------------------------------------

std::int32_t MonthCalendar::scroll_step() const noexcept {
    const std::uint32_t months = scroll_change_ == 0U ? static_cast<std::uint32_t>(month_count()) : scroll_change_;
    return static_cast<std::int32_t>(months);
}

bool MonthCalendar::can_scroll(std::int32_t direction) const noexcept {
    const CalendarMonth first{first_year_, first_month_};
    if (direction < 0) return months_between(month_of(minimum_), first) > 0;
    const CalendarMonth last = shifted_month(first, static_cast<std::int32_t>(month_count()) - 1);
    return months_between(last, month_of(maximum_)) > 0;
}

// Keeps the months shown within min_date..max_date where they fit.
void MonthCalendar::clamp_first_month() {
    CalendarMonth first{first_year_, first_month_};
    const std::int32_t shown = static_cast<std::int32_t>(month_count());
    const CalendarMonth latest = shifted_month(month_of(maximum_), 1 - shown);
    if (months_between(latest, first) > 0) first = latest;
    if (months_between(first, month_of(minimum_)) > 0) first = month_of(minimum_);
    first_year_ = first.year;
    first_month_ = first.month;
}

bool MonthCalendar::scroll_months(std::int32_t months) {
    require_mutable();
    const CalendarMonth before{first_year_, first_month_};
    const CalendarMonth target = shifted_month(before, months);
    first_year_ = target.year;
    first_month_ = target.month;
    clamp_first_month();
    const bool changed = !(CalendarMonth{first_year_, first_month_} == before);
    if (changed) invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return changed;
}

void MonthCalendar::ensure_visible(DateTimeValue date) {
    const CalendarMonth first{first_year_, first_month_};
    const CalendarMonth month = month_of(date);
    const std::int64_t offset = months_between(first, month);
    const std::int64_t shown = static_cast<std::int64_t>(month_count());
    if (offset >= 0 && offset < shown) return;
    const CalendarMonth target = offset < 0 ? month : shifted_month(month, static_cast<std::int32_t>(1 - shown));
    first_year_ = target.year;
    first_month_ = target.month;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- layout --------------------------------------------------------------------------

Size MonthCalendar::measure(Size available) {
    const double scale = effective_text_scale();
    const Size block = month_block_size(show_week_numbers_);
    const double width = block.width * static_cast<double>(dimensions_.columns) * scale;
    double height = block.height * static_cast<double>(dimensions_.rows) * scale;
    if (show_today_) height += today_height * scale;
    const Size result{std::min(available.width, width), std::min(available.height, height)};
    return result;
}

// A month's block, unscaled.
Rect MonthCalendar::month_block(std::size_t month) const noexcept {
    const Size size = month_block_size(show_week_numbers_);
    const double column = static_cast<double>(month % dimensions_.columns);
    const double row = static_cast<double>(month / dimensions_.columns);
    const Rect block{column * size.width, row * size.height, size.width, size.height};
    return block;
}

namespace {

// The day grid of the block at `block`, unscaled: gapless cells.
MonthGridMetrics day_grid(Rect block, bool week_numbers) noexcept {
    const double week = week_numbers ? week_column_width : 0.0;
    const MonthGridMetrics metrics{.cell_left = block.x + block_margin + week,
                                   .cell_top = block.y + block_margin + title_height + weekday_height,
                                   .column_pitch = cell_width,
                                   .row_pitch = cell_height,
                                   .cell_width = cell_width,
                                   .cell_height = cell_height};
    return metrics;
}

} // namespace

Rect MonthCalendar::day_cell(std::size_t month, std::size_t day_index) const noexcept {
    const DateTimeValue start = month_start(month);
    const DateTimeValue date = add_days(start, static_cast<std::int64_t>(day_index));
    const std::uint8_t first_day_number = static_cast<std::uint8_t>(first_day_of_week_);
    const std::optional<std::size_t> cell = month_page_cell(start, first_day_number, date);
    if (!cell) return {};
    const MonthGridMetrics metrics = day_grid(month_block(month), show_week_numbers_);
    return month_grid_cell_bounds(metrics, *cell, effective_text_scale());
}

std::size_t MonthCalendar::week_rows(std::size_t month) const noexcept {
    const DateTimeValue start = month_start(month);
    const std::uint8_t days = days_in_month(start.year, start.month);
    const DateTimeValue last = add_days(start, static_cast<std::int64_t>(days) - 1);
    const std::optional<std::size_t> cell =
        month_page_cell(start, static_cast<std::uint8_t>(first_day_of_week_), last);
    const std::size_t rows = cell ? *cell / grid_columns + 1U : grid_rows;
    return rows;
}

MonthCalendar::Parts MonthCalendar::control_parts() const {
    const double scale = effective_text_scale();
    const Rect surface = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(surface);
    record.face = PartGeometry::from_bounds(surface);
    record.focus = PartGeometry::from_bounds(surface);
    const Rect first = month_block(0U);
    const Rect last = month_block(static_cast<std::size_t>(dimensions_.columns) - 1U);
    const Rect previous{first.x + block_margin, first.y + block_margin + (title_height - arrow_size) * 0.5,
                        arrow_size, arrow_size};
    const Rect next{last.x + last.width - block_margin - arrow_size, previous.y, arrow_size, arrow_size};
    record.previous_arrow = PartGeometry::from_bounds(scaled(previous, scale));
    record.next_arrow = PartGeometry::from_bounds(scaled(next, scale));
    record.previous_glyph = PartGeometry::from_bounds(centred_glyph(record.previous_arrow.bounds, glyph_size * scale));
    record.next_glyph = PartGeometry::from_bounds(centred_glyph(record.next_arrow.bounds, glyph_size * scale));
    const bool previous_enabled = can_scroll(-1) && effectively_enabled();
    const bool next_enabled = can_scroll(1) && effectively_enabled();
    AnatomyStateSet previous_states;
    AnatomyStateSet next_states;
    if (hot_zone_ == Zone::previous && previous_enabled) previous_states.insert(AnatomyState::hot);
    if (hot_zone_ == Zone::next && next_enabled) next_states.insert(AnatomyState::hot);
    if (pressed_zone_ == Zone::previous && previous_enabled) previous_states.insert(AnatomyState::pressed);
    if (pressed_zone_ == Zone::next && next_enabled) next_states.insert(AnatomyState::pressed);
    if (!previous_enabled) previous_states.insert(AnatomyState::disabled);
    if (!next_enabled) next_states.insert(AnatomyState::disabled);
    record.previous_arrow.states = previous_states;
    record.previous_glyph.states = previous_states;
    record.next_arrow.states = next_states;
    record.next_glyph.states = next_states;
    if (show_today_) {
        const double top = month_block(0U).height * static_cast<double>(dimensions_.rows);
        const double width = month_block(0U).width * static_cast<double>(dimensions_.columns);
        const Rect line{block_margin, top, std::max(0.0, width - block_margin * 2.0), today_height};
        record.today_label = PartGeometry::from_bounds(scaled(line, scale));
    }
    return record;
}

MonthCalendar::MonthParts MonthCalendar::month_parts(std::size_t month) const {
    const double scale = effective_text_scale();
    const Rect block = month_block(month);
    const double week = show_week_numbers_ ? week_column_width : 0.0;
    // The title leaves room for the arrows at both ends.
    const Rect title{block.x + block_margin + arrow_size, block.y + block_margin,
                     std::max(0.0, block.width - (block_margin + arrow_size) * 2.0), title_height};
    const Rect rule{block.x + block_margin + week, block.y + block_margin + title_height + weekday_height - 1.0,
                    cell_width * 7.0, 1.0};
    MonthParts record;
    record.month_title = PartGeometry::from_bounds(scaled(title, scale));
    record.weekday_rule = PartGeometry::from_bounds(scaled(rule, scale));
    return record;
}

MonthCalendar::WeekdayParts MonthCalendar::weekday_parts(std::size_t month, std::size_t column) const {
    const Rect block = month_block(month);
    const double week = show_week_numbers_ ? week_column_width : 0.0;
    const Rect label{block.x + block_margin + week + cell_width * static_cast<double>(column),
                     block.y + block_margin + title_height, cell_width, weekday_height - 1.0};
    WeekdayParts record;
    record.weekday_label = PartGeometry::from_bounds(scaled(label, effective_text_scale()));
    return record;
}

MonthCalendar::WeekParts MonthCalendar::week_parts(std::size_t month, std::size_t row) const {
    const MonthGridMetrics metrics = day_grid(month_block(month), show_week_numbers_);
    const Rect label{metrics.cell_left - week_column_width, metrics.cell_top + cell_height * static_cast<double>(row),
                     week_column_width, cell_height};
    WeekParts record;
    record.week_label = PartGeometry::from_bounds(scaled(label, effective_text_scale()));
    return record;
}

MonthCalendar::DayParts MonthCalendar::day_parts(std::size_t month, std::size_t day_index) const {
    const Rect cell = day_cell(month, day_index);
    const DateTimeValue date = add_days(month_start(month), static_cast<std::int64_t>(day_index));
    DayParts record;
    record.day_face = PartGeometry::from_bounds(inset(cell, 1.0));
    record.day_text = PartGeometry::from_bounds(cell);
    record.day_focus = PartGeometry::from_bounds(cell);
    // The today marker is a ring around the cell (the perimeter role).
    if (show_today_circle_ && same_day(date, today_date())) {
        record.day_marker = PartGeometry::from_bounds(cell);
        record.day_marker.states.insert(AnatomyState::current);
    }
    return record;
}

namespace {

// The bolded custom state is the day system's first custom state.
constexpr CustomStateIndex bolded_state{0U};

} // namespace

AnatomyStateSet MonthCalendar::day_states(DateTimeValue date) const {
    AnatomyStateSet states;
    const bool enabled = in_range(date) && effectively_enabled();
    if (enabled && hot_date_ && same_day(*hot_date_, date)) states.insert(AnatomyState::hot);
    if (enabled && dragging_ && hot_date_ && same_day(*hot_date_, date)) states.insert(AnatomyState::pressed);
    const std::int64_t day = serial_day(date);
    if (day >= serial_day(selection_.start) && day <= serial_day(selection_.end)) {
        states.insert(AnatomyState::selected);
    }
    if (show_today_circle_ && same_day(date, today_date())) states.insert(AnatomyState::current);
    if (!in_range(date)) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (focused_ && same_day(date, focus_date_) && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    if (bolded(date)) states.insert(bolded_state);
    return states;
}

// -- text ----------------------------------------------------------------------------

std::string MonthCalendar::weekday_text(std::size_t column) const {
    const std::size_t day = (static_cast<std::size_t>(first_day_of_week_) + column) % 7U;
    return provider_.abbreviated_day_names[day];
}

std::string MonthCalendar::week_text(std::size_t month, std::size_t row) const {
    // The row's last day decides its week: a row that holds 1 January ends
    // in the new year and is week 1.
    const DateTimeValue start = month_start(month);
    const std::array<DateTimeValue, grid_cells> dates =
        month_page_dates(start, static_cast<std::uint8_t>(first_day_of_week_));
    const DateTimeValue last = dates[row * grid_columns + grid_columns - 1U];
    const DateTimeValue january{last.year, 1U, 1U};
    const int offset = (static_cast<int>(day_of_week(january)) -
                        static_cast<int>(first_day_of_week_) + 7) % 7;
    const std::int64_t day_of_year = serial_day(last) - serial_day(january);
    const std::int64_t week = (day_of_year + offset) / 7 + 1;
    return std::to_string(week);
}

std::string MonthCalendar::month_title_text(std::size_t month) const {
    const DateTimeValue start = month_start(month);
    const std::string title = provider_.month_names[start.month - 1U] + " " + std::to_string(start.year);
    return title;
}

std::string MonthCalendar::today_text() const {
    const std::string text = "Today: " + date_time_detail::format_date_time(
                                             today_date(), provider_.short_date_pattern, provider_);
    return text;
}

std::string MonthCalendar::day_stable_id(DateTimeValue date) const {
    const std::string id = std::string(stable_id().value()) + ".day." + iso_date(date);
    return id;
}

std::string MonthCalendar::month_stable_id(std::size_t month) const {
    const DateTimeValue start = month_start(month);
    const std::string id = std::string(stable_id().value()) + ".month." +
                           date_time_detail::padded(static_cast<unsigned>(start.year), 4U) + "-" +
                           date_time_detail::padded(start.month, 2U);
    return id;
}

// -- paint ---------------------------------------------------------------------------

void MonthCalendar::paint_day(Painter& painter, std::size_t month, std::size_t day_index) const {
    const DateTimeValue date = add_days(month_start(month), static_cast<std::int64_t>(day_index));
    const DayParts parts = day_parts(month, day_index);
    const AnatomyStateSet states = day_states(date);
    PartPaint part;
    part.family = ControlVisualRole::menu_item;
    part.states = states;
    part.role = PartRole::face;
    part.bounds = parts.day_face.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::content;
    part.bounds = parts.day_text.bounds;
    FontSpec font = text_font(font_);
    // A bolded day's number is set in bold.
    if (states.contains(bolded_state)) font.weight = 700;
    part.font = font;
    part.alignment = PartTextAlignment::center;
    const std::string number = std::to_string(date.day);
    part.text = number;
    paint_part(painter, *this, part);
    part.text = {};
    if (parts.day_marker.present) {
        part.role = PartRole::perimeter;
        part.bounds = parts.day_marker.bounds;
        paint_part(painter, *this, part);
    }
    part.role = PartRole::focus;
    part.bounds = parts.day_focus.bounds;
    paint_part(painter, *this, part);
}

void MonthCalendar::on_paint(Painter& painter, Rect) {
    const Parts control = control_parts();
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = control.face.bounds;
    paint_part(painter, *this, part);
    const FontSpec font = text_font(font_);
    FontSpec title_font = font;
    title_font.weight = 700;
    for (std::size_t month = 0U; month < month_count(); ++month) {
        const MonthParts parts = month_parts(month);
        PartPaint label;
        label.family = ControlVisualRole::panel;
        label.role = PartRole::label;
        label.alignment = PartTextAlignment::center;
        label.font = title_font;
        label.bounds = parts.month_title.bounds;
        const std::string title = month_title_text(month);
        label.text = title;
        paint_part(painter, *this, label);
        label.font = font;
        for (std::size_t column = 0U; column < grid_columns; ++column) {
            label.bounds = weekday_parts(month, column).weekday_label.bounds;
            const std::string name = weekday_text(column);
            label.text = name;
            paint_part(painter, *this, label);
        }
        label.text = {};
        PartPaint rule;
        rule.family = ControlVisualRole::panel;
        rule.role = PartRole::divider;
        rule.bounds = parts.weekday_rule.bounds;
        paint_part(painter, *this, rule);
        if (show_week_numbers_) {
            for (std::size_t row = 0U; row < week_rows(month); ++row) {
                label.bounds = week_parts(month, row).week_label.bounds;
                const std::string week = week_text(month, row);
                label.text = week;
                paint_part(painter, *this, label);
            }
            label.text = {};
        }
        const DateTimeValue start = month_start(month);
        const std::size_t days = days_in_month(start.year, start.month);
        for (std::size_t day_index = 0U; day_index < days; ++day_index) {
            paint_day(painter, month, day_index);
        }
    }
    PartPaint arrow;
    arrow.family = ControlVisualRole::menu_item;
    arrow.role = PartRole::actuator;
    arrow.bounds = control.previous_arrow.bounds;
    arrow.states = control.previous_arrow.states;
    paint_part(painter, *this, arrow);
    arrow.role = PartRole::glyph;
    arrow.mark = PartMark::arrow_left;
    arrow.bounds = control.previous_glyph.bounds;
    paint_part(painter, *this, arrow);
    arrow.role = PartRole::actuator;
    arrow.mark = PartMark::none;
    arrow.bounds = control.next_arrow.bounds;
    arrow.states = control.next_arrow.states;
    paint_part(painter, *this, arrow);
    arrow.role = PartRole::glyph;
    arrow.mark = PartMark::arrow_right;
    arrow.bounds = control.next_glyph.bounds;
    paint_part(painter, *this, arrow);
    if (control.today_label.present) {
        PartPaint today;
        today.family = ControlVisualRole::panel;
        today.role = PartRole::label;
        today.font = font;
        today.bounds = control.today_label.bounds;
        const std::string text = today_text();
        today.text = text;
        paint_part(painter, *this, today);
    }
}

// -- input ---------------------------------------------------------------------------

MonthCalendar::Zone MonthCalendar::zone_at(Point local, DateTimeValue& date) const {
    const Parts control = control_parts();
    if (control.previous_arrow.bounds.contains(local)) return Zone::previous;
    if (control.next_arrow.bounds.contains(local)) return Zone::next;
    const double scale = effective_text_scale();
    const std::uint8_t first_day_number = static_cast<std::uint8_t>(first_day_of_week_);
    for (std::size_t month = 0U; month < month_count(); ++month) {
        const MonthGridMetrics metrics = day_grid(month_block(month), show_week_numbers_);
        const std::optional<std::size_t> cell = month_grid_cell_at(metrics, local, scale);
        if (!cell) continue;
        const DateTimeValue start = month_start(month);
        const DateTimeValue candidate = month_page_dates(start, first_day_number)[*cell];
        // Cells of neighbouring months are blank.
        if (candidate.month != start.month) return Zone::none;
        date = candidate;
        return Zone::day;
    }
    return Zone::none;
}

void MonthCalendar::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (event.action == PointerAction::leave) {
        hot_zone_ = Zone::none;
        if (!dragging_) hot_date_.reset();
        invalidate(Dirty::paint);
        return;
    }
    DateTimeValue date{};
    const Zone zone = zone_at(local, date);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        hot_zone_ = zone;
        if (zone == Zone::day && in_range(date)) {
            hot_date_ = date;
            if (dragging_) select(bounded_range(anchor_, date), date);
        } else if (!dragging_) {
            hot_date_.reset();
        }
        invalidate(Dirty::paint);
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(shared_from_this()));
        if (zone == Zone::previous || zone == Zone::next) {
            const std::int32_t direction = zone == Zone::previous ? -1 : 1;
            pressed_zone_ = zone;
            if (can_scroll(direction)) static_cast<void>(scroll_months(direction * scroll_step()));
            invalidate(Dirty::paint);
            event.handled = true;
            return;
        }
        if (zone != Zone::day || !in_range(date)) return;
        if (!has_modifier(event.modifiers, Modifier::shift)) anchor_ = date;
        hot_date_ = date;
        dragging_ = true;
        set_pointer_capture(true);
        select(bounded_range(anchor_, date), date);
        event.handled = true;
    } else if (event.action == PointerAction::up) {
        const bool arrow = pressed_zone_ != Zone::none;
        pressed_zone_ = Zone::none;
        if (arrow) {
            invalidate(Dirty::paint);
            event.handled = true;
            return;
        }
        if (!dragging_) return;
        dragging_ = false;
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        date_selected_.emit(selection_);
        event.handled = true;
    }
}

void MonthCalendar::move_focus(DateTimeValue target, bool extend) {
    const DateTimeValue reached = std::clamp(civil_date(target), minimum_, maximum_);
    if (!extend) anchor_ = reached;
    select(bounded_range(anchor_, reached), reached);
}

void MonthCalendar::on_key(KeyEvent& event) {
    if (!focused_ || !effectively_enabled() || event.action != KeyAction::down) return;
    const bool extend = has_modifier(event.modifiers, Modifier::shift);
    const std::uint32_t key = event.physical_key;
    const DateTimeValue focus = focus_date_;
    if (key == PhysicalKey::left || key == PhysicalKey::right) {
        move_focus(add_days(focus, key == PhysicalKey::left ? -1 : 1), extend);
    } else if (key == PhysicalKey::up || key == PhysicalKey::down) {
        move_focus(add_days(focus, key == PhysicalKey::up ? -7 : 7), extend);
    } else if (key == PhysicalKey::page_up || key == PhysicalKey::page_down) {
        const std::int32_t direction = key == PhysicalKey::page_up ? -1 : 1;
        move_focus(in_month(focus, shifted_month(month_of(focus), direction)), extend);
    } else if (key == PhysicalKey::home) {
        move_focus(first_day(month_of(focus)), extend);
    } else if (key == PhysicalKey::end) {
        const DateTimeValue last{focus.year, focus.month, days_in_month(focus.year, focus.month)};
        move_focus(last, extend);
    } else {
        return;
    }
    event.handled = true;
}

void MonthCalendar::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -----------------------------------------------------------------------

SemanticDescriptor MonthCalendar::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::calendar;
    descriptor.name = accessible_name().empty() ? month_title_text(0U) : accessible_name();
    std::string value = date_time_detail::format_date_time(selection_.start, provider_.long_date_pattern, provider_);
    if (!same_day(selection_.start, selection_.end)) {
        value += " to " + date_time_detail::format_date_time(selection_.end, provider_.long_date_pattern, provider_);
    }
    descriptor.value = value;
    descriptor.description = accessible_description();
    descriptor.actions = {SemanticAction::focus, SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> MonthCalendar::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Rect absolute = absolute_bounds();
    const Parts control = control_parts();
    const std::string prefix(stable_id().value());
    const std::array<int, 2> directions{-1, 1};
    for (const int direction : directions) {
        SemanticNode node{};
        node.stable_id = prefix + (direction < 0 ? ".previous-month" : ".next-month");
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = direction < 0 ? "Previous month" : "Next month";
        const Rect bounds = direction < 0 ? control.previous_arrow.bounds : control.next_arrow.bounds;
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (can_scroll(direction) && effectively_enabled()) {
            node.states |= SemanticState::enabled;
            node.actions = {SemanticAction::press};
        }
        nodes.push_back(std::move(node));
    }
    for (std::size_t month = 0U; month < month_count(); ++month) {
        const DateTimeValue start = month_start(month);
        const std::size_t days = days_in_month(start.year, start.month);
        for (std::size_t day_index = 0U; day_index < days; ++day_index) {
            const DateTimeValue date = add_days(start, static_cast<std::int64_t>(day_index));
            const Rect bounds = day_cell(month, day_index);
            const AnatomyStateSet states = day_states(date);
            SemanticNode node{};
            node.stable_id = day_stable_id(date);
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::date_cell;
            node.name = date_time_detail::format_date_time(date, provider_.long_date_pattern, provider_);
            node.value = iso_date(date);
            node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
            node.states = SemanticState::visible | SemanticState::focusable;
            if (!states.contains(AnatomyState::disabled) && effectively_enabled()) {
                node.states |= SemanticState::enabled;
                node.actions = {SemanticAction::select, SemanticAction::press};
            }
            if (states.contains(AnatomyState::selected)) node.states |= SemanticState::selected;
            if (focused_ && same_day(date, focus_date_)) node.states |= SemanticState::focused;
            nodes.push_back(std::move(node));
        }
    }
    return nodes;
}

bool MonthCalendar::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::set_value) {
        DateTimeValue parsed{};
        if (!date_time_detail::parse_iso_date(value, parsed) || !in_range(parsed)) return false;
        anchor_ = parsed;
        select(CalendarDateRange{parsed, parsed}, parsed);
        return true;
    }
    return Control::on_semantic_action(action, value);
}

bool MonthCalendar::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (!effectively_enabled()) return false;
    const std::string prefix(stable_id().value());
    const std::string previous_id = prefix + ".previous-month";
    const std::string next_id = prefix + ".next-month";
    if (action == SemanticAction::press && (id == previous_id || id == next_id)) {
        const std::int32_t direction = id == previous_id ? -1 : 1;
        if (!can_scroll(direction)) return false;
        const bool scrolled = scroll_months(direction * scroll_step());
        return scrolled;
    }
    if (action != SemanticAction::select && action != SemanticAction::press) return false;
    const std::string day_prefix = prefix + ".day.";
    if (id.substr(0U, day_prefix.size()) != day_prefix) return false;
    DateTimeValue parsed{};
    if (!date_time_detail::parse_iso_date(id.substr(day_prefix.size()), parsed) || !in_range(parsed)) {
        return false;
    }
    anchor_ = parsed;
    select(CalendarDateRange{parsed, parsed}, parsed);
    date_selected_.emit(selection_);
    return true;
}

// -- anatomy -------------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 3> arrow_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::disabled}};
constexpr std::array<AnatomyState, 1> marker_states{{AnatomyState::current}};

constexpr std::array<PartField<MonthCalendar::Parts>, 8> month_calendar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &MonthCalendar::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &MonthCalendar::Parts::face},
    {.declaration = {.name = "previous_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &MonthCalendar::Parts::previous_arrow},
    {.declaration = {.name = "previous_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = arrow_states},
     .member = &MonthCalendar::Parts::previous_glyph},
    {.declaration = {.name = "next_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &MonthCalendar::Parts::next_arrow},
    {.declaration = {.name = "next_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = arrow_states},
     .member = &MonthCalendar::Parts::next_glyph},
    {.declaration = {.name = "today_label", .role = PartRole::label, .composed_from = {}},
     .member = &MonthCalendar::Parts::today_label},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &MonthCalendar::Parts::focus},
}};

constexpr std::array<PartField<MonthCalendar::MonthParts>, 2> month_part_fields{{
    {.declaration = {.name = "month_title", .role = PartRole::label, .composed_from = {}},
     .member = &MonthCalendar::MonthParts::month_title},
    {.declaration = {.name = "weekday_rule", .role = PartRole::divider, .composed_from = {}},
     .member = &MonthCalendar::MonthParts::weekday_rule},
}};

constexpr std::array<PartField<MonthCalendar::WeekdayParts>, 1> weekday_part_fields{{
    {.declaration = {.name = "weekday_label", .role = PartRole::label, .composed_from = {}},
     .member = &MonthCalendar::WeekdayParts::weekday_label},
}};

constexpr std::array<PartField<MonthCalendar::WeekParts>, 1> week_part_fields{{
    {.declaration = {.name = "week_label", .role = PartRole::label, .composed_from = {}},
     .member = &MonthCalendar::WeekParts::week_label},
}};

// The today marker is a ring around today's cell: the vocabulary has no
// marker role, and perimeter ("second outer ring") is the nearest.
constexpr std::array<PartField<MonthCalendar::DayParts>, 4> day_part_fields{{
    {.declaration = {.name = "day_face", .role = PartRole::face, .composed_from = {}},
     .member = &MonthCalendar::DayParts::day_face},
    {.declaration = {.name = "day_text", .role = PartRole::content, .composed_from = {}},
     .member = &MonthCalendar::DayParts::day_text},
    {.declaration = {.name = "day_marker", .role = PartRole::perimeter, .composed_from = {},
                     .states = marker_states},
     .member = &MonthCalendar::DayParts::day_marker},
    {.declaration = {.name = "day_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &MonthCalendar::DayParts::day_focus},
}};

constexpr std::array<StateField<MonthCalendar::DayStates>, 6> day_state_fields{{
    {.state = AnatomyState::hot, .member = &MonthCalendar::DayStates::hot},
    {.state = AnatomyState::pressed, .member = &MonthCalendar::DayStates::pressed},
    {.state = AnatomyState::selected, .member = &MonthCalendar::DayStates::selected},
    {.state = AnatomyState::current, .member = &MonthCalendar::DayStates::current},
    {.state = AnatomyState::disabled, .member = &MonthCalendar::DayStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &MonthCalendar::DayStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 8> month_calendar_parts = part_declarations(month_calendar_part_fields);
constexpr std::array<PartDeclaration, 2> month_parts_declared = part_declarations(month_part_fields);
constexpr std::array<PartDeclaration, 1> weekday_parts_declared = part_declarations(weekday_part_fields);
constexpr std::array<PartDeclaration, 1> week_parts_declared = part_declarations(week_part_fields);
constexpr std::array<PartDeclaration, 4> day_parts_declared = part_declarations(day_part_fields);
constexpr std::array<AnatomyState, 6> day_states_declared = state_declarations(day_state_fields);
// A bolded day (Windows Forms' BoldedDates). The vocabulary has no state for
// emphasis, so the day system declares its own (design language §12.6).
constexpr std::array<CustomStateDeclaration, 1> day_custom_states{{{.name = "bolded"}}};
constexpr std::array<AnatomyState, 5> month_calendar_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr std::array<ItemSystemDeclaration, 4> month_calendar_item_systems{{
    {.name = "month", .parts = month_parts_declared, .states = {}, .custom_states = {}},
    {.name = "weekday", .parts = weekday_parts_declared, .states = {}, .custom_states = {}},
    {.name = "week", .parts = week_parts_declared, .states = {}, .custom_states = {}},
    {.name = "day", .parts = day_parts_declared, .states = day_states_declared,
     .custom_states = day_custom_states},
}};

constexpr ControlAnatomy month_calendar_anatomy{
    .control_id = "month_calendar",
    .parts = month_calendar_parts,
    .states = month_calendar_states,
    .custom_states = {},
    .item_systems = month_calendar_item_systems,
};

} // namespace

const ControlAnatomy& MonthCalendar::anatomy() const noexcept {
    return month_calendar_anatomy;
}

void MonthCalendar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = control_parts();
    write_parts(anatomy(), month_calendar_part_fields, record, geometry);
}

MonthCalendar::Parts MonthCalendar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), month_calendar_part_fields, published.parts(), record);
    return record;
}

MonthCalendar::ItemReference MonthCalendar::locate(std::size_t item) const {
    const std::size_t months = month_count();
    std::size_t rest = item;
    if (rest < months) return ItemReference{ItemKind::month, rest, 0U};
    rest -= months;
    if (rest < months * grid_columns) {
        return ItemReference{ItemKind::weekday, rest / grid_columns, rest % grid_columns};
    }
    rest -= months * grid_columns;
    if (show_week_numbers_) {
        for (std::size_t month = 0U; month < months; ++month) {
            const std::size_t rows = week_rows(month);
            if (rest < rows) return ItemReference{ItemKind::week, month, rest};
            rest -= rows;
        }
    }
    for (std::size_t month = 0U; month < months; ++month) {
        const DateTimeValue start = month_start(month);
        const std::size_t days = days_in_month(start.year, start.month);
        if (rest < days) return ItemReference{ItemKind::day, month, rest};
        rest -= days;
    }
    throw std::out_of_range("MonthCalendar item index is past the realized items");
}

std::size_t MonthCalendar::realized_item_count() const {
    const std::size_t months = month_count();
    std::size_t count = months + months * grid_columns;
    for (std::size_t month = 0U; month < months; ++month) {
        if (show_week_numbers_) count += week_rows(month);
        const DateTimeValue start = month_start(month);
        count += days_in_month(start.year, start.month);
    }
    return count;
}

std::string MonthCalendar::realized_item_stable_id(std::size_t item) const {
    const ItemReference reference = locate(item);
    switch (reference.kind) {
    case ItemKind::month:
        return month_stable_id(reference.month);
    case ItemKind::weekday:
        return month_stable_id(reference.month) + ".weekday." + std::to_string(reference.index);
    case ItemKind::week:
        return month_stable_id(reference.month) + ".week." + std::to_string(reference.index);
    case ItemKind::day: {
        const DateTimeValue date = add_days(month_start(reference.month), static_cast<std::int64_t>(reference.index));
        return day_stable_id(date);
    }
    }
    // A kind outside the enumeration names no item.
    return {};
}

ItemSystemIndex MonthCalendar::publish_realized_item_system(std::size_t item) const {
    const ItemReference reference = locate(item);
    const ItemSystemIndex system{static_cast<std::uint8_t>(reference.kind)};
    return system;
}

void MonthCalendar::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const ItemReference reference = locate(item);
    const std::span<const ItemSystemDeclaration> systems = anatomy().item_systems;
    const ItemSystemDeclaration& system = systems[static_cast<std::size_t>(reference.kind)];
    switch (reference.kind) {
    case ItemKind::month: {
        const MonthParts record = month_parts(reference.month);
        write_item_parts(system, month_part_fields, record, geometry);
        return;
    }
    case ItemKind::weekday: {
        const WeekdayParts record = weekday_parts(reference.month, reference.index);
        write_item_parts(system, weekday_part_fields, record, geometry);
        return;
    }
    case ItemKind::week: {
        const WeekParts record = week_parts(reference.month, reference.index);
        write_item_parts(system, week_part_fields, record, geometry);
        return;
    }
    case ItemKind::day: {
        const DayParts record = day_parts(reference.month, reference.index);
        write_item_parts(system, day_part_fields, record, geometry);
        return;
    }
    }
    // A kind outside the enumeration publishes no parts.
    return;
}

AnatomyStateSet MonthCalendar::realized_item_anatomy_states(std::size_t item) const {
    const ItemReference reference = locate(item);
    if (reference.kind != ItemKind::day) return AnatomyStateSet{};
    const DateTimeValue date = add_days(month_start(reference.month), static_cast<std::int64_t>(reference.index));
    return day_states(date);
}

std::vector<MonthCalendar::Day> MonthCalendar::realized_days() const {
    std::vector<Day> days;
    const std::size_t count = realized_item_count();
    const ItemSystemDeclaration& system = anatomy().item_systems[static_cast<std::size_t>(ItemKind::day)];
    for (std::size_t item = 0U; item < count; ++item) {
        const ItemReference reference = locate(item);
        if (reference.kind != ItemKind::day) continue;
        Day record;
        record.date = add_days(month_start(reference.month), static_cast<std::int64_t>(reference.index));
        record.stable_id = day_stable_id(record.date);
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        read_item_parts(system, day_part_fields, published.parts(), record.parts);
        const AnatomyStateSet states = realized_item_anatomy_states(item);
        read_states(day_state_fields, states, record.states);
        record.states.bolded = states.contains(bolded_state);
        days.push_back(record);
    }
    return days;
}

namespace {

// The events MonthCalendar adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> month_calendar_interactions{{
    {.name = "date_changed", .payload = "CalendarDateRange", .mutable_payload = false},
    {.name = "date_selected", .payload = "CalendarDateRange", .mutable_payload = false},
}};

} // namespace

void MonthCalendar::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), month_calendar_interactions.begin(),
                        month_calendar_interactions.end());
}

} // namespace gui_forms
