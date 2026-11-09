#include "support.hpp"

namespace gallery_pages_tests {

void test_dates_and_calendar(gui_forms::Window& window) {
    select_page(window, "dates-and-calendar");

    const std::shared_ptr<gui_forms::DateTimePicker> long_date = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.format.0"));
    const std::shared_ptr<gui_forms::DateTimePicker> short_date = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.format.1"));
    const std::shared_ptr<gui_forms::DateTimePicker> time = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.format.2"));
    const std::shared_ptr<gui_forms::DateTimePicker> custom = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.format.3"));
    const std::shared_ptr<gui_forms::DateTimePicker> optional = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.optional"));
    const std::shared_ptr<gui_forms::DateTimePicker> spinner = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.spinner"));
    const std::shared_ptr<gui_forms::DateTimePicker> provider = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.provider"));
    const std::shared_ptr<gui_forms::DateTimePicker> disabled = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.disabled"));
    const std::shared_ptr<gui_forms::DateTimePicker> primary = std::dynamic_pointer_cast<DateTimePicker>(
        window.find("gallery.date.primary"));
    require(long_date && short_date && time && custom && optional && spinner &&
                provider && disabled && primary,
            "date board must use public DateTimePicker instances for every variant");
    require((*long_date).formatted_value() == "Wednesday, August 5, 2026" &&
                (*short_date).formatted_value() == "8/5/2026" &&
                (*time).formatted_value() == "2:07 PM" &&
                (*custom).formatted_value() == "2026-08-05 · 14:07" &&
                (*provider).formatted_value() == "mercredi 5 août 2026",
            "date board must visibly distinguish all format and provider paths");
    require((*optional).show_check_box.read() && !(*optional).checked.read() &&
                (*spinner).show_up_down.read() && !(*disabled).enabled.read(),
            "date board must retain nullable, spinner, and disabled states");

    const Rect optional_bounds = (*optional).absolute_bounds();
    const Point optional_check{optional_bounds.x + 12.0,
                               optional_bounds.y + optional_bounds.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     optional_check}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                         optional_check}) && (*optional).checked.read(),
            "optional date checkbox must toggle through its public hit region");
    require(window.perform_semantic_action(
                "gallery.date.optional", SemanticAction::press) &&
                !(*optional).checked.read() &&
                window.perform_semantic_action(
                    "gallery.date.optional", SemanticAction::set_value,
                    "2026-08-09") && (*optional).checked.read() &&
                (*optional).value.read().day == 9U,
            "optional date semantics must toggle and activate a supplied value");
    require(window.request_focus(spinner) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::up}) &&
                (*spinner).value.read().day == 6U,
            "date spinner must step its civil value through keyboard input");

    click(window, window.find("gallery.date.commands.item.3"));
    require((*primary).dropped_down.read() && window.focus_scope_depth() == 1U &&
                window.find("gallery.date.primary.overlay.calendar") &&
                window.semantic_snapshot().to_json().find(
                    "gallery.date.primary.overlay.calendar.day.2026-08-12") !=
                    std::string::npos,
            "date board must open one retained semantic calendar focus scope");
    require(window.perform_semantic_action(
                "gallery.date.primary.overlay.calendar.day.2026-08-12",
                SemanticAction::press) && (*primary).value.read().day == 12U &&
                !(*primary).dropped_down.read() && window.focus_scope_depth() == 0U,
            "semantic date-cell commit must share pointer commit and restore focus");

    click(window, window.find("gallery.date.commands.item.3"));
    const std::shared_ptr<gui_forms::CommandBar> commands = std::dynamic_pointer_cast<CommandBar>(
        window.find("gallery.date.commands"));
    require(commands && (*commands).items().size() == 3U && !(*commands).item(2).enabled.read(),
            "the open-calendar Command must disable its button while the calendar is open");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::left}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*primary).value.read().day == 12U && !(*primary).dropped_down.read() &&
                (*commands).item(2).enabled.read(),
            "calendar Escape must discard overlay-local navigation without mutation");

    // The previous/next Commands follow the bounded range; their buttons
    // project that enabled state and clicking one moves the date once.
    (*primary).value = DateTimeValue{2026, 8U, 31U};
    require(!(*commands).item(1).enabled.read() && (*commands).item(0).enabled.read(),
            "the next-day Command must disable at the range maximum");
    click(window, window.find("gallery.date.commands.item.1"));
    require((*primary).value.read().day == 30U && (*commands).item(1).enabled.read() &&
                (*std::dynamic_pointer_cast<Label>(window.find("gallery.date.status")))
                        .text.read().find("Committed") != std::string::npos,
            "the previous-day Command must move the bounded date and re-enable next day");
    (*primary).value = DateTimeValue{2026, 8U, 1U};
    require(!(*commands).item(0).enabled.read() && (*commands).item(1).enabled.read(),
            "the previous-day Command must disable at the range minimum");
    (*primary).value = DateTimeValue{2026, 8U, 12U};

    click(window, window.find("gallery.date.commands.item.3"));
    (*window.find("gallery.surface.dates-and-calendar")).visible = false;
    require(!(*primary).dropped_down.read() && window.focus_scope_depth() == 0U &&
                !window.find("gallery.date.primary.overlay.calendar"),
            "hiding a page must revoke its open calendar and contained focus");
    (*window.find("gallery.surface.dates-and-calendar")).visible = true;

    const std::shared_ptr<gui_forms::MonthCalendar> month = std::dynamic_pointer_cast<MonthCalendar>(
        window.find("gallery.date.month.calendar"));
    const std::shared_ptr<gui_forms::Label> month_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.date.month.status"));
    require(month && month_status && (*month).realized_days().size() == 31U + 30U &&
                (*month).calendar_dimensions.read() == CalendarDimensions{2U, 1U},
            "the month calendar specimen shows August and September");
    require(window.perform_semantic_action("gallery.date.month.calendar.day.2026-09-07", SemanticAction::select) &&
                (*month).selection_start() == DateTimeValue{2026, 9U, 7U} &&
                (*month_status).text.read() == "Selected 9/7",
            "selecting a day reports to the page");
}

} // namespace gallery_pages_tests
