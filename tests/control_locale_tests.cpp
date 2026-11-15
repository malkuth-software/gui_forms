// The application locale (exhibit gap E11): ApplicationResources::locale,
// its seeding from a host that reports HostCapability::locale through
// HostSession, the program's precedence, and the controls that format with
// it: DateTimePicker, MonthCalendar's first day and names, and
// NumericUpDown's separators. The locales are test fixtures
// (support/locale_fixtures.hpp), as a host would report them.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/calendar_values.hpp"
#include "gui_forms/controls/date_time_format_provider.hpp"
#include "gui_forms/controls/date_time_picker.hpp"
#include "gui_forms/controls/date_time_picker_values.hpp"
#include "gui_forms/controls/month_calendar.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/host/session/host_session.hpp"
#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"
#include "gui_forms/host/types/host_locale/host_locale.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/window/window.hpp"
#include "support/locale_fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    Fixture() : window(root, {520.0, 420.0}) {
        (*picker).set_requested_bounds({16.0, 16.0, 240.0, 29.0});
        (*picker).value = {2026, 8U, 5U, 14U, 7U, 9U};
        (*picker).format = DateTimePickerFormat::short_date;
        (*calendar).set_requested_bounds({16.0, 60.0, 240.0, 200.0});
        (*numeric).set_requested_bounds({280.0, 16.0, 160.0, 29.0});
        (*numeric).set_range(0.0, 100000.0);
        (*numeric).decimal_places = static_cast<std::uint8_t>(2U);
        (*numeric).thousands_separator = true;
        (*numeric).value = 1234.5;
        (*root).add_child(picker);
        (*root).add_child(calendar);
        (*root).add_child(numeric);
        window.perform_layout();
    }

    ApplicationResources& resources() { return *window.resources(); }

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("locale.root"));
    std::shared_ptr<DateTimePicker> picker = make_control<DateTimePicker>(StableId("locale.picker"));
    std::shared_ptr<MonthCalendar> calendar = make_control<MonthCalendar>(StableId("locale.calendar"));
    std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("locale.numeric"));
    Window window;
};

std::string editor_text(const NumericUpDown& numeric) {
    const std::shared_ptr<TextBox> editor = numeric.editor();
    require(editor != nullptr, "the numeric field has an editor");
    const std::string text{(*editor).text.read()};
    return text;
}

void portable_default_until_told() {
    Fixture fixture;
    require(fixture.resources().locale() == portable_host_locale() && fixture.resources().locale_generation() == 0U,
            "a window's resources start with the portable locale");
    require((*fixture.picker).follows_host_locale() && (*fixture.picker).formatted_value() == "8/5/2026",
            "the picker follows the portable locale by default");
    require((*fixture.calendar).effective_first_day_of_week() == CalendarWeekday::sunday,
            "the calendar's week starts on the portable default's Sunday");
    require(editor_text(*fixture.numeric) == "1,234.50", "the numeric field groups with the portable separators");
}

void program_locale_reaches_controls() {
    Fixture fixture;
    fixture.resources().set_locale(test_support::english_united_kingdom_locale());
    fixture.window.perform_layout();
    require(fixture.resources().locale_generation() != 0U, "setting a locale changes the generation");
    require((*fixture.picker).effective_locale().name == "en-GB", "an attached control sees its window's locale");
    require((*fixture.picker).formatted_value() == "05/08/2026", "the picker shows the en-GB short date");
    (*fixture.picker).format = DateTimePickerFormat::time;
    require((*fixture.picker).formatted_value() == "14:07", "the picker shows the en-GB time");
    require((*fixture.calendar).effective_first_day_of_week() == CalendarWeekday::monday,
            "the calendar's week follows the locale to Monday");

    fixture.resources().set_locale(test_support::german_germany_locale());
    fixture.window.perform_layout();
    require(editor_text(*fixture.numeric) == "1.234,50", "the numeric field shows de-DE separators after a change");
    (*fixture.picker).format = DateTimePickerFormat::long_date;
    require((*fixture.picker).formatted_value() == "Mittwoch, 5. August 2026", "the picker shows German names");
    const std::shared_ptr<TextBox> editor = (*fixture.numeric).editor();
    (*editor).text = std::string{"2,5"};
    require((*fixture.numeric).value.read() == 2.5, "typed text is read with the locale's decimal separator");
    (*editor).text = std::string{"1.000,25"};
    require((*fixture.numeric).value.read() == 1000.25, "the locale's group separator is accepted when typed");
}

void written_properties_stop_following() {
    Fixture fixture;
    fixture.resources().set_locale(test_support::german_germany_locale());
    fixture.window.perform_layout();
    DateTimeFormatProvider own = DateTimeFormatProvider::english_united_states();
    own.short_date_pattern = "yyyy-MM-dd";
    (*fixture.picker).format_provider = own;
    require(!(*fixture.picker).follows_host_locale() && (*fixture.picker).formatted_value() == "2026-08-05",
            "writing format_provider stops following the locale");
    fixture.resources().set_locale(test_support::english_united_kingdom_locale());
    require((*fixture.picker).formatted_value() == "2026-08-05", "a picker not following ignores locale changes");
    (*fixture.picker).follow_host_locale();
    require((*fixture.picker).follows_host_locale() && (*fixture.picker).formatted_value() == "05/08/2026",
            "follow_host_locale returns to the locale's provider");

    (*fixture.calendar).first_day_of_week = CalendarWeekday::wednesday;
    require(!(*fixture.calendar).follows_host_locale() &&
                (*fixture.calendar).effective_first_day_of_week() == CalendarWeekday::wednesday,
            "writing first_day_of_week stops following the locale");
    (*fixture.calendar).follow_host_locale();
    require((*fixture.calendar).follows_host_locale() &&
                (*fixture.calendar).effective_first_day_of_week() == CalendarWeekday::monday,
            "follow_host_locale returns the calendar to the locale's first day");
}

void invalid_locale_is_refused() {
    Fixture fixture;
    HostLocale broken = test_support::german_germany_locale();
    broken.group_separator = broken.decimal_separator;
    bool refused = false;
    try {
        fixture.resources().set_locale(broken);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && fixture.resources().locale() == portable_host_locale() &&
                fixture.resources().locale_generation() == 0U,
            "an invalid locale is refused and changes nothing");
}

HostCapabilities reporting(bool with_locale) {
    HostCapabilities capabilities{HostCapabilities::current_protocol_version, "test", HostCapability::lifecycle};
    capabilities.locale = test_support::german_germany_locale();
    if (with_locale) capabilities.available = capabilities.available | HostCapability::locale;
    return capabilities;
}

void host_session_seeds_the_locale() {
    {
        Fixture fixture;
        HostSession session(fixture.window, reporting(false));
        require(fixture.resources().locale() == portable_host_locale(),
                "a host that does not report HostCapability::locale leaves the portable default");
    }
    {
        Fixture fixture;
        HostSession session(fixture.window, reporting(true));
        fixture.window.perform_layout();
        require(fixture.resources().locale().name == "de-DE" && editor_text(*fixture.numeric) == "1.234,50",
                "a host reporting its locale seeds the window's resources");
    }
    {
        Fixture fixture;
        fixture.resources().set_locale(test_support::english_united_kingdom_locale());
        HostSession session(fixture.window, reporting(true));
        require(fixture.resources().locale().name == "en-GB", "a program's locale wins over the host's");
    }
    {
        Fixture fixture;
        HostCapabilities broken = reporting(true);
        broken.locale.month_names[0].clear();
        HostSession session(fixture.window, broken);
        require(fixture.resources().locale() == portable_host_locale(), "an invalid host report is ignored");
    }
}

void capabilities_json_names_the_locale() {
    const HostCapabilities capabilities = reporting(true);
    const std::string json = capabilities.to_json();
    require(json.find("\"locale\":{\"name\":\"de-DE\"") != std::string::npos,
            "the capabilities JSON carries the locale record");
    require(host_protocol_revisions[6].version == 14U, "the locale is protocol revision 14");
    const std::size_t listed = json.find("\"locale\"");
    require(listed != std::string::npos && json.find("\"locale\"", listed + 1U) != std::string::npos,
            "the capability is listed by name as well as carrying the record");
}

} // namespace

int main() {
    try {
        portable_default_until_told();
        program_locale_reaches_controls();
        written_properties_stop_following();
        invalid_locale_is_refused();
        host_session_seeds_the_locale();
        capabilities_json_names_the_locale();
    } catch (const std::exception& error) {
        std::cerr << "control locale tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "control locale tests passed\n";
    return EXIT_SUCCESS;
}
