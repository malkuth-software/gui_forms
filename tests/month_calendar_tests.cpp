// MonthCalendar (stage W): days as an item system with selected, current,
// disabled and bolded states; pointer selection and dragging within
// max_selection_count; keyboard navigation; min and max dates; month
// scrolling; week numbers and the first day of the week; several months;
// semantics; and the published anatomy.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct RecordRange final {
    std::vector<CalendarDateRange>* seen{};
    void operator()(const CalendarDateRange& range) const { (*seen).push_back(range); }
};

constexpr DateTimeValue august(std::uint8_t day) {
    const DateTimeValue date{2026, 8U, day};
    return date;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

const MonthCalendar::Day* find_day(const std::vector<MonthCalendar::Day>& days, DateTimeValue date) {
    for (const MonthCalendar::Day& day : days) {
        if (day.date == date) return &day;
    }
    return nullptr;
}
const MonthCalendar::Day* find_day(std::vector<MonthCalendar::Day>&& days, DateTimeValue date) = delete;

Rect day_bounds(const MonthCalendar& calendar, DateTimeValue date) {
    const std::vector<MonthCalendar::Day> days = calendar.realized_days();
    const MonthCalendar::Day* day = find_day(days, date);
    require(day != nullptr, "the day is shown");
    return (*day).parts.day_text.bounds;
}

std::shared_ptr<MonthCalendar> make_calendar(const char* id) {
    std::shared_ptr<MonthCalendar> calendar = make_control<MonthCalendar>(StableId(std::string(id)));
    (*calendar).set_requested_bounds({0.0, 0.0, 520.0, 260.0});
    (*calendar).set_today_date(august(12U));
    (*calendar).set_date(august(12U));
    return calendar;
}

void days_and_anatomy() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.days");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*calendar).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "month_calendar" && anatomy.item_systems.size() == 4U,
            "MonthCalendar publishes a valid anatomy with four item systems");
    require((*calendar).realized_item_count() == 1U + 7U + 31U,
            "one month realizes its title, seven weekday names and its 31 days");
    const std::vector<MonthCalendar::Day> days = (*calendar).realized_days();
    require(days.size() == 31U && days.front().date == august(1U) && days.back().date == august(31U),
            "only the month's own days are shown");
    const MonthCalendar::Day* today = find_day(days, august(12U));
    require(today != nullptr && (*today).states.selected && (*today).states.current &&
                (*today).parts.day_marker.present &&
                (*today).parts.day_marker.states.contains(AnatomyState::current) &&
                (*today).stable_id == "calendar.days.day.2026-08-12",
            "today is selected, current and marked");
    require(!(*find_day(days, august(13U))).states.current && !(*find_day(days, august(13U))).parts.day_marker.present,
            "other days carry no today marker");
    // 1 August 2026 is a Saturday: with weeks from Sunday it sits last in
    // the first row, and the 2nd starts the second row.
    const Rect first = day_bounds(*calendar, august(1U));
    const Rect second = day_bounds(*calendar, august(2U));
    require(second.y > first.y && second.x < first.x, "days are laid out in weeks from Sunday");
    const MonthCalendar::Parts parts = (*calendar).parts();
    require(parts.previous_arrow.present && parts.next_glyph.present && parts.today_label.present &&
                !parts.previous_arrow.states.contains(AnatomyState::disabled),
            "the arrows and the today line are published");

    (*calendar).add_bolded_date(august(20U));
    (*calendar).add_annually_bolded_date({2000, 8U, 3U});
    (*calendar).add_monthly_bolded_date({2001, 1U, 28U});
    const std::vector<MonthCalendar::Day> bolded = (*calendar).realized_days();
    require((*find_day(bolded, august(20U))).states.bolded && (*find_day(bolded, august(3U))).states.bolded &&
                (*find_day(bolded, august(28U))).states.bolded && !(*find_day(bolded, august(21U))).states.bolded,
            "dated, annual and monthly bold days publish the bolded custom state");
    (*calendar).remove_annually_bolded_date({1999, 8U, 3U});
    require(!(*calendar).bolded(august(3U)) && (*calendar).bolded(august(20U)), "an annual bold date is removed by month and day");
}

// Replacing the annual and monthly bold lists: each replaces the whole list,
// shows in the realized days at once, and an invalid date anywhere in the
// list throws before anything changes.
bool replace_annual(MonthCalendar& calendar, std::vector<DateTimeValue> dates) {
    try {
        calendar.set_annually_bolded_dates(std::move(dates));
    } catch (const std::invalid_argument&) {
        return false;
    }
    return true;
}

bool replace_monthly(MonthCalendar& calendar, std::vector<DateTimeValue> dates) {
    try {
        calendar.set_monthly_bolded_dates(std::move(dates));
    } catch (const std::invalid_argument&) {
        return false;
    }
    return true;
}

void bold_date_lists() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.bold-lists");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    (*calendar).add_annually_bolded_date({2000, 8U, 9U});
    require(replace_annual(*calendar, {{1990, 8U, 3U}, {1991, 12U, 25U}}), "a valid annual list is taken");
    require((*calendar).annually_bolded_dates().size() == 2U &&
                (*calendar).annually_bolded_dates()[0] == DateTimeValue{1990, 8U, 3U} &&
                (*calendar).annually_bolded_dates()[1] == DateTimeValue{1991, 12U, 25U},
            "the annual list reads back as given, replacing the earlier entry");
    const std::vector<MonthCalendar::Day> annual = (*calendar).realized_days();
    require((*find_day(annual, august(3U))).states.bolded && !(*find_day(annual, august(9U))).states.bolded,
            "the annual list bolds its month and day in any year, and the replaced entry no longer shows");

    require(replace_monthly(*calendar, {{2001, 1U, 15U}, {1999, 3U, 30U}}), "a valid monthly list is taken");
    require((*calendar).monthly_bolded_dates().size() == 2U &&
                (*calendar).monthly_bolded_dates()[0] == DateTimeValue{2001, 1U, 15U},
            "the monthly list reads back as given");
    const std::vector<MonthCalendar::Day> monthly = (*calendar).realized_days();
    require((*find_day(monthly, august(15U))).states.bolded && (*find_day(monthly, august(30U))).states.bolded &&
                !(*find_day(monthly, august(16U))).states.bolded,
            "the monthly list bolds its days in every month");

    require(!replace_annual(*calendar, {{2000, 5U, 1U}, {2000, 2U, 30U}}) &&
                (*calendar).annually_bolded_dates().size() == 2U &&
                (*calendar).annually_bolded_dates()[0] == DateTimeValue{1990, 8U, 3U},
            "an invalid date in an annual list throws and keeps the old list");
    require(!replace_monthly(*calendar, {{2000, 13U, 1U}}) && (*calendar).monthly_bolded_dates().size() == 2U,
            "an invalid date in a monthly list throws and keeps the old list");

    require(replace_annual(*calendar, {}) && replace_monthly(*calendar, {}) &&
                (*calendar).annually_bolded_dates().empty() && (*calendar).monthly_bolded_dates().empty(),
            "empty lists clear both");
    const std::vector<MonthCalendar::Day> cleared = (*calendar).realized_days();
    require(!(*find_day(cleared, august(3U))).states.bolded && !(*find_day(cleared, august(15U))).states.bolded,
            "cleared lists bold nothing");
}

void pointer_selection() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.pointer");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    std::vector<CalendarDateRange> changed;
    std::vector<CalendarDateRange> selected;
    SubscriptionToken changes = (*calendar).date_changed().subscribe(RecordRange{&changed});
    SubscriptionToken selections = (*calendar).date_selected().subscribe(RecordRange{&selected});
    const Point third = centre(day_bounds(*calendar, august(3U)));
    const Point fifth = centre(day_bounds(*calendar, august(5U)));
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, third}),
            "a day takes the press");
    require((*calendar).selection_start() == august(3U) && changed.size() == 1U && selected.empty(),
            "pressing a day selects it; the user's selection is reported on release");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, fifth}));
    require((*calendar).selection_range() == CalendarDateRange{august(3U), august(5U)},
            "dragging extends the selection");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, fifth}));
    require(selected.size() == 1U && selected[0] == CalendarDateRange{august(3U), august(5U)},
            "date_selected reports the dragged range");

    (*calendar).max_selection_count = 3U;
    const Point tenth = centre(day_bounds(*calendar, august(10U)));
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, third}));
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, tenth}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, tenth}));
    require((*calendar).selection_range() == CalendarDateRange{august(3U), august(5U)},
            "a drag stops at max_selection_count days");

    const Point previous = centre((*calendar).parts().previous_arrow.bounds);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, previous}) &&
                (*calendar).first_displayed_month() == DateTimeValue{2026, 7U, 1U},
            "the previous arrow scrolls back a month");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, previous}));
    bool threw = false;
    try {
        (*calendar).max_selection_count = 0U;
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "a selection count of zero is refused");
}

void keyboard_navigation() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.keys");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    require(window.request_focus(calendar), "the calendar takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
                (*calendar).selection_start() == august(13U),
            "Right moves to the next day");
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::right, Modifier::shift}));
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::right, Modifier::shift}));
    require((*calendar).selection_range() == CalendarDateRange{august(13U), august(15U)},
            "Shift extends the selection");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::down}) &&
                (*calendar).selection_range() == CalendarDateRange{august(22U), august(22U)},
            "Down moves a week and selects one day");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::page_down}) &&
                (*calendar).selection_start() == DateTimeValue{2026, 9U, 22U} &&
                (*calendar).first_displayed_month() == DateTimeValue{2026, 9U, 1U},
            "Page Down moves a month and shows it");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::home}) &&
                (*calendar).selection_start() == DateTimeValue{2026, 9U, 1U},
            "Home moves to the month's first day");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::end}) &&
                (*calendar).selection_start() == DateTimeValue{2026, 9U, 30U},
            "End moves to the month's last day");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::page_up}) &&
                (*calendar).selection_start() == DateTimeValue{2026, 8U, 30U},
            "Page Up moves back a month, clamping nothing in a longer month");
    const std::vector<MonthCalendar::Day> days = (*calendar).realized_days();
    require((*find_day(days, august(30U))).states.focus_visible || !window.focus_cue_visible(),
            "the focused day shows the focus cue when cues are visible");
}

void range_and_months() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.range");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    (*calendar).set_range(august(10U), august(20U));
    const std::vector<MonthCalendar::Day> days = (*calendar).realized_days();
    require((*find_day(days, august(9U))).states.disabled && !(*find_day(days, august(10U))).states.disabled,
            "days outside the range are disabled");
    require((*calendar).parts().previous_arrow.states.contains(AnatomyState::disabled) &&
                (*calendar).parts().next_arrow.states.contains(AnatomyState::disabled) &&
                !(*calendar).scroll_months(1),
            "a one-month range disables both arrows");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::down, PointerButton::primary, centre(day_bounds(*calendar, august(5U)))}));
    require((*calendar).selection_start() == august(12U), "a disabled day cannot be selected");
    bool threw = false;
    try {
        (*calendar).set_date(august(25U));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && (*calendar).selection_start() == august(12U), "a date outside the range is refused");
    (*calendar).set_range({2026, 1U, 1U}, {2026, 12U, 31U});
    require(window.request_focus(calendar), "the calendar takes focus");
    (*calendar).set_date(august(10U));
    for (int step = 0; step < 6; ++step) {
        static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::page_down}));
    }
    require((*calendar).selection_start() == DateTimeValue{2026, 12U, 31U},
            "keyboard movement stops at max_date");

    (*calendar).set_date(august(12U));
    (*calendar).calendar_dimensions = {2U, 1U};
    require((*calendar).realized_days().size() == 31U + 30U, "two months show both months' days");
    const MonthCalendar::Parts parts = (*calendar).parts();
    require(parts.next_arrow.bounds.x > 300.0, "the next arrow sits on the last month of the top row");
    bool too_many = false;
    try {
        (*calendar).calendar_dimensions = {5U, 3U};
    } catch (const std::out_of_range&) {
        too_many = true;
    }
    require(too_many && (*calendar).calendar_dimensions.read() == CalendarDimensions{2U, 1U},
            "more than twelve months are refused");

    (*calendar).calendar_dimensions = {1U, 1U};
    (*calendar).first_day_of_week = CalendarWeekday::monday;
    (*calendar).show_week_numbers = true;
    require((*calendar).realized_item_count() == 1U + 7U + 6U + 31U,
            "week numbers add one item per row that holds days");
    const Rect first = day_bounds(*calendar, august(1U));
    const Rect second = day_bounds(*calendar, august(2U));
    const Rect third = day_bounds(*calendar, august(3U));
    require(second.y == first.y && third.y > second.y, "with weeks from Monday, 2 August ends the first row");
}

void semantics() {
    std::shared_ptr<MonthCalendar> calendar = make_calendar("calendar.semantics");
    Window window(calendar, {520.0, 260.0});
    window.perform_layout();
    std::vector<CalendarDateRange> selected;
    SubscriptionToken selections = (*calendar).date_selected().subscribe(RecordRange{&selected});
    const SemanticDescriptor descriptor = (*calendar).semantic_descriptor();
    require(descriptor.role == SemanticRole::calendar && descriptor.value == "Wednesday, August 12, 2026",
            "the calendar names its selection");
    const std::vector<SemanticNode> children = (*calendar).semantic_virtual_children();
    bool found = false;
    for (const SemanticNode& node : children) {
        if (node.stable_id == "calendar.semantics.day.2026-08-12") {
            found = node.role == SemanticRole::date_cell &&
                    has_semantic_state(node.states, SemanticState::selected);
        }
    }
    require(found && children.size() == 2U + 31U, "each day is a date cell; the arrows are buttons");
    require((*calendar).on_semantic_child_action("calendar.semantics.day.2026-08-14", SemanticAction::select, {}) &&
                (*calendar).selection_start() == august(14U) && selected.size() == 1U,
            "selecting a date cell selects its day and reports it");
    require((*calendar).on_semantic_child_action("calendar.semantics.next-month", SemanticAction::press, {}) &&
                (*calendar).first_displayed_month() == DateTimeValue{2026, 9U, 1U},
            "the next-month button scrolls");
    require((*calendar).on_semantic_action(SemanticAction::set_value, "2026-08-20") &&
                (*calendar).selection_start() == august(20U) &&
                (*calendar).first_displayed_month() == DateTimeValue{2026, 8U, 1U},
            "set_value takes an ISO date and shows it");
    require(!(*calendar).on_semantic_action(SemanticAction::set_value, "2026-13-01"),
            "an invalid ISO date is refused");
}

} // namespace

int main() {
    days_and_anatomy();
    bold_date_lists();
    pointer_selection();
    keyboard_navigation();
    range_and_months();
    semantics();
    std::cout << "MonthCalendar: days, anatomy, bold dates, pointer, keyboard, range, months, "
                 "week numbers and semantics passed\n";
    return 0;
}
