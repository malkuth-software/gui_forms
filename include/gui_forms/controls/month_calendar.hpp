#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/date_time_format_provider.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/calendar_values.hpp"
#include "gui_forms/controls/month_calendar_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// A month calendar (Windows Forms' MonthCalendar): one or more month pages
// on which the user selects a date or a run of dates. Each month shows its
// title, the weekday names, optionally week numbers, and its own days (days
// of neighbouring months are not shown). The arrows beside the first and
// last titles scroll the months. Days outside min_date..max_date are
// disabled. Months, weekday names, week numbers and days are its four item
// systems, and every part is painted through the by-role paint path.
//
// Dates are civil days: the time of day of every date the calendar takes is
// dropped, and every date it reports is at midnight.
class GUI_FORMS_API MonthCalendar final : public Control, public MonthCalendarCapabilities<MonthCalendar> {
public:
    explicit MonthCalendar(StableId stable_id);

    // The selected run of days. set_selection_range orders its ends, keeps at
    // most max_selection_count days from the first, and shows the last.
    // Throws std::out_of_range for an invalid date or one outside
    // min_date..max_date.
    [[nodiscard]] CalendarDateRange selection_range() const noexcept { return selection_; }
    [[nodiscard]] DateTimeValue selection_start() const noexcept { return selection_.start; }
    [[nodiscard]] DateTimeValue selection_end() const noexcept { return selection_.end; }
    void set_selection_range(DateTimeValue first, DateTimeValue last);
    // Selects one day.
    void set_date(DateTimeValue date);
    [[nodiscard]] DateTimeValue min_date() const noexcept { return minimum_; }
    [[nodiscard]] DateTimeValue max_date() const noexcept { return maximum_; }
    // Throws std::invalid_argument for an invalid or reversed range. The
    // selection is moved into the new range.
    void set_range(DateTimeValue minimum, DateTimeValue maximum);

    // Today's date: the system clock's date in UTC (GUI.Forms has no
    // time-zone service) until set_today_date sets it.
    [[nodiscard]] DateTimeValue today_date() const noexcept { return set_today_.value_or(system_today_); }
    void set_today_date(DateTimeValue date);
    [[nodiscard]] bool today_date_set() const noexcept { return set_today_.has_value(); }
    // The first day of the first month shown.
    [[nodiscard]] DateTimeValue first_displayed_month() const noexcept;
    // Scrolls the months shown by `months` (negative: back), within
    // min_date..max_date. Returns whether the months changed.
    bool scroll_months(std::int32_t months);

    // Days shown in bold: on one date, on a month and day every year, or on
    // a day of every month (only the day of a monthly date counts, and only
    // the month and day of an annual one). Invalid dates throw
    // std::invalid_argument.
    [[nodiscard]] std::span<const DateTimeValue> bolded_dates() const& noexcept { return bolded_; }
    std::span<const DateTimeValue> bolded_dates() const&& = delete;
    void set_bolded_dates(std::vector<DateTimeValue> dates);
    void add_bolded_date(DateTimeValue date);
    void remove_bolded_date(DateTimeValue date);
    void remove_all_bolded_dates();
    [[nodiscard]] std::span<const DateTimeValue> annually_bolded_dates() const& noexcept { return annually_bolded_; }
    std::span<const DateTimeValue> annually_bolded_dates() const&& = delete;
    void set_annually_bolded_dates(std::vector<DateTimeValue> dates);
    void add_annually_bolded_date(DateTimeValue date);
    void remove_annually_bolded_date(DateTimeValue date);
    void remove_all_annually_bolded_dates();
    [[nodiscard]] std::span<const DateTimeValue> monthly_bolded_dates() const& noexcept { return monthly_bolded_; }
    std::span<const DateTimeValue> monthly_bolded_dates() const&& = delete;
    void set_monthly_bolded_dates(std::vector<DateTimeValue> dates);
    void add_monthly_bolded_date(DateTimeValue date);
    void remove_monthly_bolded_date(DateTimeValue date);
    void remove_all_monthly_bolded_dates();
    [[nodiscard]] bool bolded(DateTimeValue date) const noexcept;

    // Raised after the selection changes, whatever the cause.
    [[nodiscard]] Event<const CalendarDateRange&>& date_changed() noexcept { return date_changed_; }
    // Raised when the user selects with the pointer (on release) or through
    // assistive technology, after date_changed.
    [[nodiscard]] Event<const CalendarDateRange&>& date_selected() noexcept { return date_selected_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; the control's states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry previous_arrow;
        PartGeometry previous_glyph;
        PartGeometry next_arrow;
        PartGeometry next_glyph;
        PartGeometry today_label;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    // Item part names are unique within the control, so each names its
    // system.
    struct GUI_FORMS_API MonthParts final {
        PartGeometry month_title;
        PartGeometry weekday_rule;
    };
    struct GUI_FORMS_API WeekdayParts final {
        PartGeometry weekday_label;
    };
    struct GUI_FORMS_API WeekParts final {
        PartGeometry week_label;
    };
    struct GUI_FORMS_API DayParts final {
        PartGeometry day_face;
        PartGeometry day_text;
        PartGeometry day_marker;
        PartGeometry day_focus;
    };
    struct GUI_FORMS_API DayStates final {
        bool hot{};
        bool pressed{};
        bool selected{};
        bool current{};
        bool disabled{};
        bool focus_visible{};
        bool bolded{};
    };
    struct GUI_FORMS_API Day final {
        DateTimeValue date{};
        std::string stable_id;
        DayParts parts;
        DayStates states;
    };
    // The days shown, month by month.
    [[nodiscard]] std::vector<Day> realized_days() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // The four item systems, in declaration and paint order.
    enum class ItemKind : std::uint8_t { month, weekday, week, day };
    // One realized item: its system, the month it belongs to (0 is the first
    // shown), and its index within the month (weekday column, page row, or
    // day of the month less one).
    struct ItemReference final {
        ItemKind kind{ItemKind::month};
        std::size_t month{};
        std::size_t index{};
    };
    // What a point is over.
    enum class Zone : std::uint8_t { none, previous, next, day };

    [[nodiscard]] std::size_t month_count() const noexcept;
    [[nodiscard]] DateTimeValue month_start(std::size_t month) const noexcept;
    [[nodiscard]] std::size_t week_rows(std::size_t month) const noexcept;
    [[nodiscard]] ItemReference locate(std::size_t item) const;
    // Geometry, in local coordinates at the current text scale.
    [[nodiscard]] Rect month_block(std::size_t month) const noexcept;
    [[nodiscard]] Rect day_cell(std::size_t month, std::size_t day_index) const noexcept;
    [[nodiscard]] Parts control_parts() const;
    [[nodiscard]] MonthParts month_parts(std::size_t month) const;
    [[nodiscard]] WeekdayParts weekday_parts(std::size_t month, std::size_t column) const;
    [[nodiscard]] WeekParts week_parts(std::size_t month, std::size_t row) const;
    [[nodiscard]] DayParts day_parts(std::size_t month, std::size_t day_index) const;
    [[nodiscard]] AnatomyStateSet day_states(DateTimeValue date) const;
    [[nodiscard]] std::string weekday_text(std::size_t column) const;
    [[nodiscard]] std::string week_text(std::size_t month, std::size_t row) const;
    [[nodiscard]] std::string month_title_text(std::size_t month) const;
    [[nodiscard]] std::string today_text() const;
    [[nodiscard]] std::string day_stable_id(DateTimeValue date) const;
    [[nodiscard]] std::string month_stable_id(std::size_t month) const;
    [[nodiscard]] bool in_range(DateTimeValue date) const noexcept;
    [[nodiscard]] bool can_scroll(std::int32_t direction) const noexcept;
    [[nodiscard]] std::int32_t scroll_step() const noexcept;
    [[nodiscard]] Zone zone_at(Point local, DateTimeValue& date) const;
    // The run from `anchor` toward `target`, at most max_selection_count days.
    [[nodiscard]] CalendarDateRange bounded_range(DateTimeValue anchor, DateTimeValue target) const noexcept;
    void select(CalendarDateRange range, DateTimeValue focus);
    void move_focus(DateTimeValue target, bool extend);
    void ensure_visible(DateTimeValue date);
    void clamp_first_month();
    void paint_day(Painter& painter, std::size_t month, std::size_t day_index) const;
    void validate_bolded(DateTimeValue date) const;
    void bolded_changed();

    CalendarDateRange selection_{};
    DateTimeValue anchor_{};
    DateTimeValue focus_date_{};
    DateTimeValue minimum_{1753, 1U, 1U};
    DateTimeValue maximum_{9998, 12U, 31U};
    // The system clock's date, read once at construction; set_today_ replaces
    // it once set_today_date has run, so the two cannot disagree.
    DateTimeValue system_today_{};
    std::optional<DateTimeValue> set_today_{};
    std::int32_t first_year_{2000};
    std::uint8_t first_month_{1U};
    std::vector<DateTimeValue> bolded_{};
    std::vector<DateTimeValue> annually_bolded_{};
    std::vector<DateTimeValue> monthly_bolded_{};
    std::optional<DateTimeValue> hot_date_{};
    Zone hot_zone_{Zone::none};
    Zone pressed_zone_{Zone::none};
    bool dragging_{};
    bool focused_{};
    Event<const CalendarDateRange&> date_changed_{};
    Event<const CalendarDateRange&> date_selected_{};
    // property_codemod begin MonthCalendar (tools/property_families/month_calendar.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct ShowTodayCapability<MonthCalendar>;
    friend struct ShowTodayCircleCapability<MonthCalendar>;
    friend struct ShowWeekNumbersCapability<MonthCalendar>;
    friend struct FirstDayOfWeekCapability<MonthCalendar>;
    friend struct CalendarDimensionsCapability<MonthCalendar>;
    friend struct MaxSelectionCountCapability<MonthCalendar>;
    friend struct ScrollChangeCapability<MonthCalendar>;
    friend struct MonthCalendarFormatProviderCapability<MonthCalendar>;
    friend struct FontCapability<MonthCalendar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(show_today)& cell, bool& incoming);
    void written(const decltype(show_today)& cell);
    void prepare_write(const decltype(show_today_circle)& cell, bool& incoming);
    void written(const decltype(show_today_circle)& cell);
    void prepare_write(const decltype(show_week_numbers)& cell, bool& incoming);
    void written(const decltype(show_week_numbers)& cell);
    void prepare_write(const decltype(first_day_of_week)& cell, CalendarWeekday& incoming);
    void written(const decltype(first_day_of_week)& cell);
    void prepare_write(const decltype(calendar_dimensions)& cell, CalendarDimensions& incoming);
    void written(const decltype(calendar_dimensions)& cell);
    void prepare_write(const decltype(max_selection_count)& cell, std::uint32_t& incoming);
    void written(const decltype(max_selection_count)& cell);
    void prepare_write(const decltype(scroll_change)& cell, std::uint32_t& incoming);
    void written(const decltype(scroll_change)& cell);
    void prepare_write(const decltype(format_provider)& cell, DateTimeFormatProvider& incoming);
    void written(const decltype(format_provider)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    // property_codemod end MonthCalendar
};

} // namespace gui_forms
