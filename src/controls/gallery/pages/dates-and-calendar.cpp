#include "pages.hpp"
#include "gui_forms/controls/calendar_values.hpp"
#include "gui_forms/controls/command_bar.hpp"
#include "gui_forms/controls/date_time_picker.hpp"
#include "gui_forms/controls/date_time_picker_values.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/month_calendar.hpp"

#include "dates-and-calendar_form.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// Owner of the DateTimePicker specimens: the controls
// demo/pages/dates-and-calendar.form describes, and their behaviour. The
// lifecycle actions are three application Commands shown through one
// CommandBar; their enabled state follows the bounded appointment date and
// the open calendar, and the picker's state events report to the status
// label through named members.
class DatesPage final : public gui_forms::Component {
public:
    explicit DatesPage(const std::shared_ptr<Surface>& body) {
        document_.initialize_component(*this);
        mount_document(body);
        build_formats();
        build_modes();
        build_lifecycle();
        build_month_calendar();
        on((*primary_).value_changed(), *this, &DatesPage::value_committed);
        on((*primary_).drop_down_changed(), *this, &DatesPage::drop_down_changed);
        on((*optional_).checked_changed(), *this, &DatesPage::optional_checked);
        on(previous_day_.invoked(), *this, &DatesPage::previous_day);
        on(next_day_.invoked(), *this, &DatesPage::next_day);
        on(open_calendar_.invoked(), *this, &DatesPage::open_calendar);
        project_commands();
    }

private:
    static constexpr DateTimeValue specimen{2026, 8U, 5U, 14U, 7U, 9U, 125U};
    static constexpr int command_previous = 1;
    static constexpr int command_next = 2;
    static constexpr int command_open = 3;

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_date_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        optional_ = document_.gallery_date_optional;
        primary_ = document_.gallery_date_primary;
        commands_ = document_.gallery_date_commands;
        status_ = document_.gallery_date_status;
        month_ = document_.gallery_date_month_calendar;
        month_status_ = document_.gallery_date_month_status;
    }

    // Every picker starts at the specimen value.
    static void prepare_picker(DateTimePicker& picker) {
        picker.value = specimen;
    }

    void build_formats() {
        prepare_picker(*document_.gallery_date_format_0);
        DateTimePicker& short_date = *document_.gallery_date_format_1;
        prepare_picker(short_date);
        short_date.format = DateTimePickerFormat::short_date;
        DateTimePicker& time = *document_.gallery_date_format_2;
        prepare_picker(time);
        time.format = DateTimePickerFormat::time;
        DateTimePicker& custom = *document_.gallery_date_format_3;
        prepare_picker(custom);
        custom.custom_format = "yyyy-MM-dd '·' HH:mm";
        custom.format = DateTimePickerFormat::custom;
    }

    void build_modes() {
        prepare_picker(*optional_);
        (*optional_).format = DateTimePickerFormat::short_date;
        (*optional_).show_check_box = true;
        (*optional_).checked = false;

        DateTimePicker& spinner = *document_.gallery_date_spinner;
        prepare_picker(spinner);
        spinner.format = DateTimePickerFormat::short_date;
        spinner.show_up_down = true;

        DateTimePicker& provider = *document_.gallery_date_provider;
        prepare_picker(provider);
        provider.format_provider = french_date_provider();

        DateTimePicker& disabled = *document_.gallery_date_disabled;
        prepare_picker(disabled);
        disabled.custom_format = "MMM d, yyyy";
        disabled.format = DateTimePickerFormat::custom;
    }

    void build_lifecycle() {
        prepare_picker(*primary_);
        (*primary_).set_range({2026, 8U, 1U}, {2026, 8U, 31U, 23U, 59U, 59U, 999U});

        previous_day_.set_text("Previous day");
        previous_day_.set_description("Move the appointment one day earlier");
        next_day_.set_text("Next day");
        next_day_.set_description("Move the appointment one day later");
        open_calendar_.set_text("Open calendar");
        open_calendar_.set_description("Drop the retained calendar overlay down");
        (*commands_).set_items({
            {.id = command_previous, .label = "Previous day",
             .accessible_name = "Previous day", .command = &previous_day_},
            {.id = command_next, .label = "Next day",
             .accessible_name = "Next day", .command = &next_day_},
            {.id = command_open, .label = "Open calendar",
             .accessible_name = "Open calendar", .command = &open_calendar_}});
        (*commands_).item(2).visual_style = ButtonVisualStyle::accent;
    }

    // A two-month MonthCalendar with week numbers, bold dates and a bounded
    // range; its selection reports to its own status label.
    void build_month_calendar() {
        (*month_).set_today_date(specimen);
        (*month_).set_range({2026, 8U, 1U}, {2026, 12U, 31U});
        (*month_).calendar_dimensions = {2U, 1U};
        (*month_).show_week_numbers = true;
        (*month_).set_bolded_dates({{2026, 8U, 21U}, {2026, 9U, 7U}});
        (*month_).add_monthly_bolded_date({2026, 1U, 1U});
        (*month_).set_date({2026, 8U, 12U});
        on((*month_).date_changed(), *this, &DatesPage::month_changed);
    }

    void month_changed(const CalendarDateRange& range) {
        std::string text = "Selected " + std::to_string(range.start.month) + "/" + std::to_string(range.start.day);
        if (!(range.start == range.end)) {
            text += " to " + std::to_string(range.end.month) + "/" + std::to_string(range.end.day);
        }
        (*month_status_).text = text;
    }

    // The commands' enabled state is the single authority their buttons show.
    void project_commands() {
        const DateTimeValue value = (*primary_).value.read();
        const DateTimeValue minimum = (*primary_).minimum.read();
        const DateTimeValue maximum = (*primary_).maximum.read();
        previous_day_.set_enabled(!(value.year == minimum.year && value.month == minimum.month &&
                                    value.day == minimum.day));
        next_day_.set_enabled(!(value.year == maximum.year && value.month == maximum.month &&
                                value.day == maximum.day));
        open_calendar_.set_enabled(!(*primary_).dropped_down.read());
    }

    void value_committed() {
        (*status_).text = "Committed · " + (*primary_).formatted_value() +
                            " · retained focus restored";
        project_commands();
    }

    void drop_down_changed(const bool expanded) {
        (*status_).text = expanded
            ? "Calendar open · arrows navigate · Enter commits · Esc cancels"
            : "Calendar closed · overlay and focus scope revoked";
        project_commands();
    }

    void optional_checked(const bool checked) {
        (*status_).text = checked ? "Optional date enabled · value retained"
                                    : "Optional date unchecked · value retained but inactive";
    }

    void previous_day() { (*primary_).value = add_days((*primary_).value.read(), -1); }
    void next_day() { (*primary_).value = add_days((*primary_).value.read(), 1); }
    void open_calendar() { (*primary_).dropped_down = true; }

    Command previous_day_{};
    Command next_day_{};
    Command open_calendar_{};
    std::shared_ptr<DateTimePicker> optional_;
    std::shared_ptr<DateTimePicker> primary_;
    std::shared_ptr<CommandBar> commands_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<MonthCalendar> month_;
    std::shared_ptr<Label> month_status_;
    // Declared after the Commands its bar borrows, so it is released first.
    DatesDocument<DatesPage> document_{};
};

} // namespace

void add_dates_and_calendar(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<DatesPage>(page));
}

} // namespace gui_forms::gallery::pages
