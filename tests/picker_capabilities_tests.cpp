// Stage B8 property capabilities: the pickers (DateTimePicker,
// ColorValueEditor, FlagsValueEditor, FontDialog; MonthCalendar has its own
// tests). Each test writes the properties through their cells or views,
// checks that a refusal leaves the value unchanged, and checks the order of
// coupled writes.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/color_value_editor.hpp"
#include "gui_forms/controls/date_time_picker.hpp"
#include "gui_forms/controls/date_time_picker_values.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/flags_value_editor.hpp"
#include "gui_forms/controls/font_dialog.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_picker_capabilities_tests: " << message << '\n';
    std::exit(1);
}

struct CountDateChanges final {
    int* count{};
    void operator()(DateTimeValue) const { ++*count; }
};

struct CountChecked final {
    int* count{};
    void operator()(bool) const { ++*count; }
};

void test_date_time_picker() {
    std::shared_ptr<DateTimePicker> picker = make_control<DateTimePicker>(StableId("caps.date"));
    int changes = 0;
    const SubscriptionToken token = (*picker).value_changed().subscribe(CountDateChanges{&changes});
    (*picker).value = DateTimeValue{2005, 6U, 7U};
    (*picker).value = DateTimeValue{2005, 6U, 7U};
    require(changes == 1, "a value write raises value_changed once per change");
    bool refused = false;
    try {
        (*picker).value = DateTimeValue{2005, 2U, 30U};
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*picker).value.read() == DateTimeValue{2005, 6U, 7U}, "a date that does not exist is refused");

    (*picker).minimum = DateTimeValue{9999, 1U, 1U};
    require((*picker).maximum.read() == DateTimeValue{9999, 1U, 1U} &&
                (*picker).value.read() == DateTimeValue{9999, 1U, 1U} && changes == 2,
            "a minimum after maximum moves maximum to it and clamps value");
    (*picker).set_range(DateTimeValue{2000, 1U, 1U}, DateTimeValue{2010, 12U, 31U});
    require((*picker).minimum.read() == DateTimeValue{2000, 1U, 1U} &&
                (*picker).maximum.read() == DateTimeValue{2010, 12U, 31U} &&
                (*picker).value.read() == DateTimeValue{2010, 12U, 31U} && changes == 3,
            "set_range sets both bounds and clamps value once");
    (*picker).maximum = DateTimeValue{1999, 1U, 1U};
    require((*picker).minimum.read() == DateTimeValue{1999, 1U, 1U} && changes == 4,
            "a maximum before minimum moves minimum to it");
    refused = false;
    try {
        (*picker).set_range(DateTimeValue{2010, 1U, 1U}, DateTimeValue{2000, 1U, 1U});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picker).maximum.read() == DateTimeValue{1999, 1U, 1U}, "a reversed range is refused");
    static_cast<void>(token);

    int checks = 0;
    const SubscriptionToken checked_token = (*picker).checked_changed().subscribe(CountChecked{&checks});
    (*picker).checked = false;
    require(!(*picker).checked.read() && checks == 1, "checked raises checked_changed");
    static_cast<void>(checked_token);

    refused = false;
    try {
        (*picker).format = static_cast<DateTimePickerFormat>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picker).format.read() == DateTimePickerFormat::long_date, "an undeclared format is refused");
    refused = false;
    try {
        (*picker).custom_format = std::string("\xff");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picker).custom_format.read() == "MMMM d, yyyy", "an invalid UTF-8 pattern is refused");
    refused = false;
    try {
        (*picker).drop_down_alignment = static_cast<DateTimeDropDownAlignment>(5);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picker).drop_down_alignment.read() == DateTimeDropDownAlignment::left,
            "an undeclared alignment is refused");
    (*picker).show_up_down = true;
    require((*picker).show_up_down.read() && !(*picker).dropped_down.read(), "the arrows replace the calendar");
    BasicControlStyle authored = (*picker).style.read();
    authored.text = Color::rgba(10, 20, 30);
    (*picker).style = authored;
    require((*picker).style.read() == authored, "a style write is read back");
}

void test_color_value_editor() {
    std::shared_ptr<ColorValueEditor> editor =
        make_control<ColorValueEditor>(StableId("caps.colour"), Color::rgba(1, 2, 3));
    require((*editor).value.read() == Color::rgba(1, 2, 3), "the constructor's colour is the value");
    (*(*editor).editor()).text = std::string("not a colour");
    (*editor).value = Color::rgba(1, 2, 3);
    require((*(*editor).editor()).text.read() == ColorValueEditor::format_value(Color::rgba(1, 2, 3)),
            "an equal write still restores the text");
    bool refused = false;
    try {
        (*editor).swatch_width = 97.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*editor).swatch_width.read() == 30.0, "a swatch wider than 96 is refused");
}

PropertyEnumDescriptor edges_descriptor() {
    PropertyEnumDescriptor descriptor;
    descriptor.type_name = "Edges";
    descriptor.flags = true;
    descriptor.choices.push_back(PropertyEnumChoice{"None", 0});
    descriptor.choices.push_back(PropertyEnumChoice{"Top", 1});
    descriptor.choices.push_back(PropertyEnumChoice{"Left", 2});
    return descriptor;
}

void test_flags_value_editor() {
    std::shared_ptr<FlagsValueEditor> editor = make_control<FlagsValueEditor>(
        StableId("caps.flags"), edges_descriptor(), PropertyEnumValue{"Edges", "", 3});
    require((*editor).value.read().name == "Top, Left" && (*editor).descriptor.read() == edges_descriptor(),
            "the constructor normalizes the value against the descriptor");
    (*editor).value = PropertyEnumValue{"Edges", "", 1};
    require((*editor).value.read().name == "Top", "a value write is normalized");
    bool refused = false;
    try {
        (*editor).value = PropertyEnumValue{"Edges", "", 8};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*editor).value.read().value == 1, "a bit no choice declares is refused");
    PropertyEnumDescriptor not_flags = edges_descriptor();
    not_flags.flags = false;
    refused = false;
    try {
        (*editor).descriptor = not_flags;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*editor).descriptor.read().flags, "a descriptor without flags is refused");
    refused = false;
    try {
        (*editor).overlay_width = 79.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*editor).overlay_width.read() == 240.0, "a list narrower than 80 is refused");
    (*editor).dropped_down = true;
    require(!(*editor).dropped_down.read(), "a detached editor keeps its list closed");
}

void test_font_dialog() {
    std::shared_ptr<FontDialog> dialog = make_control<FontDialog>(StableId("caps.font.dialog"));
    const FontSpec chosen{FontRole::monospace, 14.0, 400, false};
    (*dialog).font = chosen;
    require((*dialog).font.read() == chosen && (*dialog).selection().size == 14.0,
            "a font write resets the selection to it");
    bool refused = false;
    try {
        (*dialog).font = FontSpec{FontRole::content, -1.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*dialog).font.read() == chosen, "an invalid font is refused");
    (*dialog).show_apply = false;
    require(!(*dialog).show_apply.read() && !(*(*dialog).apply_button()).visible.read(),
            "hiding Apply hides its button");
    refused = false;
    try {
        (*dialog).preview_text = std::string("\xff");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*dialog).preview_text.read() == "AaBbYyZz 0123", "invalid UTF-8 preview text is refused");
}

} // namespace

int main() {
    test_date_time_picker();
    test_color_value_editor();
    test_flags_value_editor();
    test_font_dialog();
    std::cout << "gui_forms_picker_capabilities_tests: all tests passed\n";
    return 0;
}
