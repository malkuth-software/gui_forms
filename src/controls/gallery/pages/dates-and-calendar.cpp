#include "pages.hpp"

namespace gui_forms::gallery::pages {

// Owner of the DateTimePicker specimens. The lifecycle actions are three
// application Commands shown through one CommandBar; their enabled state
// follows the bounded appointment date and the open calendar, and the
// picker's state events report to the status label through named members.
class DatesPage final : public gui_forms::Component {
public:
    explicit DatesPage(const std::shared_ptr<Surface>& body) {
        (*body).add_at(label("gallery.date.heading", "DATES AND CALENDAR", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
        (*body).add_at(label(
            "gallery.date.lead",
            "Validated civil time, caller-owned formats, nullable and spinner modes, retained overlay focus, and semantic date cells.",
            12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
        build_formats(body);
        build_modes(body);
        build_lifecycle(body);
        build_month_calendar(body);
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
    static constexpr std::array<std::string_view, 4> captions{
        "Long date", "Short date", "Time", "Custom"};
    static constexpr int command_previous = 1;
    static constexpr int command_next = 2;
    static constexpr int command_open = 3;

    [[nodiscard]] static std::shared_ptr<DateTimePicker> make_picker(
        std::string id, std::string accessible_name) {
        std::shared_ptr<DateTimePicker> picker =
            make_control<DateTimePicker>(StableId(std::move(id)));
        (*picker).set_style(page_style());
        (*picker).set_value(specimen);
        (*picker).accessible_name = std::move(accessible_name);
        return picker;
    }

    void build_formats(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> formats = group(
            "gallery.date.formats", "Formatting · four modes and an owned provider",
            {930.0, 180.0});
        (*body).add_at(formats, {24.0, 92.0, 930.0, 180.0});
        for (std::size_t index = 0U; index < captions.size(); ++index) {
            const double column = index % 2U == 0U ? 22.0 : 474.0;
            const double row = index < 2U ? 40.0 : 102.0;
            (*formats).add_at(label("gallery.date.format.caption." + std::to_string(index),
                                    std::string(captions[index]), 10.0, 700, shell_blue_dark),
                              {column, row, 86.0, 22.0});
            std::shared_ptr<DateTimePicker> picker = make_picker(
                "gallery.date.format." + std::to_string(index),
                std::string(captions[index]) + " picker");
            if (index == 1U) {
                (*picker).set_format(DateTimePickerFormat::short_date);
            } else if (index == 2U) {
                (*picker).set_format(DateTimePickerFormat::time);
            } else if (index == 3U) {
                (*picker).set_custom_format("yyyy-MM-dd '·' HH:mm");
                (*picker).set_format(DateTimePickerFormat::custom);
            }
            (*formats).add_at(picker, {column + 92.0, row - 5.0, 320.0, 30.0});
        }
    }

    void build_modes(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> modes = group(
            "gallery.date.modes", "Modes and states · nullable, spinner, provider, disabled",
            {930.0, 180.0});
        (*body).add_at(modes, {24.0, 290.0, 930.0, 180.0});

        optional_ = make_picker("gallery.date.optional", "Optional unchecked date");
        (*optional_).set_format(DateTimePickerFormat::short_date);
        (*optional_).set_show_check_box(true);
        (*optional_).set_checked(false);
        (*modes).add_at(label("gallery.date.optional.caption", "Optional value", 10.0, 700,
                              shell_blue_dark), {22.0, 40.0, 106.0, 22.0});
        (*modes).add_at(optional_, {132.0, 35.0, 284.0, 30.0});

        std::shared_ptr<DateTimePicker> spinner = make_picker("gallery.date.spinner", "Date spinner");
        (*spinner).set_format(DateTimePickerFormat::short_date);
        (*spinner).set_show_up_down(true);
        (*modes).add_at(label("gallery.date.spinner.caption", "Up/down date", 10.0, 700,
                              shell_blue_dark), {474.0, 40.0, 106.0, 22.0});
        (*modes).add_at(spinner, {584.0, 35.0, 320.0, 30.0});

        std::shared_ptr<DateTimePicker> provider = make_picker(
            "gallery.date.provider", "Caller-owned French date provider");
        (*provider).set_format_provider(french_date_provider());
        (*modes).add_at(label("gallery.date.provider.caption", "Owned provider", 10.0, 700,
                              shell_blue_dark), {22.0, 102.0, 106.0, 22.0});
        (*modes).add_at(provider, {132.0, 97.0, 284.0, 30.0});

        std::shared_ptr<DateTimePicker> disabled = make_picker(
            "gallery.date.disabled", "Disabled date picker");
        (*disabled).set_custom_format("MMM d, yyyy");
        (*disabled).set_format(DateTimePickerFormat::custom);
        (*disabled).enabled = false;
        (*modes).add_at(label("gallery.date.disabled.caption", "Disabled state", 10.0, 700,
                              shell_blue_dark), {474.0, 102.0, 106.0, 22.0});
        (*modes).add_at(disabled, {584.0, 97.0, 320.0, 30.0});
    }

    void build_lifecycle(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> lifecycle = group(
            "gallery.date.lifecycle", "Overlay lifecycle · commit, cancel, bounds, semantics",
            {930.0, 138.0});
        (*body).add_at(lifecycle, {24.0, 488.0, 930.0, 138.0});
        primary_ = make_picker("gallery.date.primary", "Bounded appointment date");
        (*primary_).set_range({2026, 8U, 1U}, {2026, 8U, 31U, 23U, 59U, 59U, 999U});
        (*lifecycle).add_at(primary_, {22.0, 38.0, 310.0, 31.0});

        previous_day_.set_text("Previous day");
        previous_day_.set_description("Move the appointment one day earlier");
        next_day_.set_text("Next day");
        next_day_.set_description("Move the appointment one day later");
        open_calendar_.set_text("Open calendar");
        open_calendar_.set_description("Drop the retained calendar overlay down");
        commands_ = make_control<CommandBar>(StableId("gallery.date.commands"));
        (*commands_).accessible_name = "Appointment commands";
        (*commands_).set_items({
            {.id = command_previous, .label = "Previous day",
             .accessible_name = "Previous day", .command = &previous_day_},
            {.id = command_next, .label = "Next day",
             .accessible_name = "Next day", .command = &next_day_},
            {.id = command_open, .label = "Open calendar",
             .accessible_name = "Open calendar", .command = &open_calendar_}});
        (*commands_).item(2).set_visual_style(ButtonVisualStyle::accent);
        (*lifecycle).add_at(commands_, {352.0, 38.0, 454.0, 32.0});
        status_ = label("gallery.date.status",
                        "Ready · August 1–31, 2026 · F4 or Alt+Down opens", 10.0, 600, green);
        (*status_).set_text_wrapping(TextWrapping::word);
        (*status_).set_vertical_alignment(VerticalAlignment::near);
        (*lifecycle).add_at(status_, {22.0, 86.0, 884.0, 34.0});
    }

    // A two-month MonthCalendar with week numbers, bold dates and a bounded
    // range; its selection reports to its own status label.
    void build_month_calendar(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> months = group(
            "gallery.date.month", "MonthCalendar · two months, week numbers, bold dates, bounded range",
            {930.0, 280.0});
        (*body).add_at(months, {24.0, 644.0, 930.0, 280.0});
        month_ = make_control<MonthCalendar>(StableId("gallery.date.month.calendar"));
        (*month_).accessible_name = "Appointment calendar";
        (*month_).set_today_date(specimen);
        (*month_).set_range({2026, 8U, 1U}, {2026, 12U, 31U});
        (*month_).set_calendar_dimensions({2U, 1U});
        (*month_).set_show_week_numbers(true);
        (*month_).set_bolded_dates({{2026, 8U, 21U}, {2026, 9U, 7U}});
        (*month_).add_monthly_bolded_date({2026, 1U, 1U});
        (*month_).set_date({2026, 8U, 12U});
        (*months).add_at(month_, {22.0, 38.0, 500.0, 216.0});
        month_status_ = label("gallery.date.month.status",
                              "Drag to select up to seven days · arrows, Page Up/Down, Home, End", 10.0, 600, green);
        (*month_status_).set_text_wrapping(TextWrapping::word);
        (*month_status_).set_vertical_alignment(VerticalAlignment::near);
        (*months).add_at(month_status_, {546.0, 44.0, 360.0, 60.0});
        on((*month_).date_changed(), *this, &DatesPage::month_changed);
    }

    void month_changed(const CalendarDateRange& range) {
        std::string text = "Selected " + std::to_string(range.start.month) + "/" + std::to_string(range.start.day);
        if (!(range.start == range.end)) {
            text += " to " + std::to_string(range.end.month) + "/" + std::to_string(range.end.day);
        }
        (*month_status_).set_text(text);
    }

    // The commands' enabled state is the single authority their buttons show.
    void project_commands() {
        const DateTimeValue value = (*primary_).value();
        const DateTimeValue minimum = (*primary_).minimum();
        const DateTimeValue maximum = (*primary_).maximum();
        previous_day_.set_enabled(!(value.year == minimum.year && value.month == minimum.month &&
                                    value.day == minimum.day));
        next_day_.set_enabled(!(value.year == maximum.year && value.month == maximum.month &&
                                value.day == maximum.day));
        open_calendar_.set_enabled(!(*primary_).dropped_down());
    }

    void value_committed() {
        (*status_).set_text("Committed · " + (*primary_).formatted_value() +
                            " · retained focus restored");
        project_commands();
    }

    void drop_down_changed(const bool expanded) {
        (*status_).set_text(expanded
            ? "Calendar open · arrows navigate · Enter commits · Esc cancels"
            : "Calendar closed · overlay and focus scope revoked");
        project_commands();
    }

    void optional_checked(const bool checked) {
        (*status_).set_text(checked ? "Optional date enabled · value retained"
                                    : "Optional date unchecked · value retained but inactive");
    }

    void previous_day() { (*primary_).set_value(add_days((*primary_).value(), -1)); }
    void next_day() { (*primary_).set_value(add_days((*primary_).value(), 1)); }
    void open_calendar() { (*primary_).set_dropped_down(true); }

    Command previous_day_{};
    Command next_day_{};
    Command open_calendar_{};
    std::shared_ptr<DateTimePicker> optional_;
    std::shared_ptr<DateTimePicker> primary_;
    std::shared_ptr<CommandBar> commands_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<MonthCalendar> month_;
    std::shared_ptr<Label> month_status_;
};

void add_dates_and_calendar(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<DatesPage>(page));
}

} // namespace gui_forms::gallery::pages
