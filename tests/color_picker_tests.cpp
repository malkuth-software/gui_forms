// The ImGui-class demonstrator's I2: ColorPicker. HSV and RGB conversion
// (a grey keeps its hue, black its saturation), set_hsv and its refusals,
// a program's write against the picker's own, pointer drags on the field,
// hue and alpha tracks through the window, the keyboard, alpha_enabled,
// the published parts, and ColorValueEditor's drop-down picker.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/color_picker.hpp"
#include "gui_forms/controls/color_value_editor.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace gui_forms;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "color picker test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(double left, double right, double tolerance) {
    const bool result = std::fabs(left - right) <= tolerance;
    return result;
}

struct Recorder final {
    int changes{};
    int commits{};
    Color last_change{};
    Color last_commit{};

    void on_change(Color colour) {
        ++changes;
        last_change = colour;
    }
    void on_commit(Color colour) {
        ++commits;
        last_commit = colour;
    }
};

void subscribe(ColorPicker& picker, Recorder& recorder, std::vector<SubscriptionToken>& tokens) {
    tokens.push_back(picker.value_changed().subscribe(Delegate<Color>::bind<Recorder, &Recorder::on_change>(recorder)));
    tokens.push_back(
        picker.value_committed().subscribe(Delegate<Color>::bind<Recorder, &Recorder::on_commit>(recorder)));
}

void press(Window& window, PointerAction action, PointerButton button, Point position) {
    PointerEvent event{};
    event.action = action;
    event.button = button;
    event.position = position;
    static_cast<void>(window.dispatch_pointer(event));
}

void test_conversions() {
    const Color red = ColorPicker::color_of(ColorPickerHsv{0.0, 1.0, 1.0}, 255U);
    require(red == Color::rgba(255, 0, 0), "hue 0 is red");
    const Color green = ColorPicker::color_of(ColorPickerHsv{120.0, 1.0, 1.0}, 128U);
    require(green == Color::rgba(0, 255, 0, 128), "hue 120 is green, alpha kept");
    const Color blue = ColorPicker::color_of(ColorPickerHsv{240.0, 1.0, 0.5}, 255U);
    require(blue == Color::rgba(0, 0, 128), "half-bright blue");
    const ColorPickerHsv back = ColorPicker::hsv_of(Color::rgba(255, 128, 0), ColorPickerHsv{});
    require(near(back.hue, 30.1, 0.2) && near(back.saturation, 1.0, 1e-9) && near(back.brightness, 1.0, 1e-9),
            "orange's HSV");
    const ColorPickerHsv grey = ColorPicker::hsv_of(Color::rgba(128, 128, 128), ColorPickerHsv{200.0, 0.7, 0.2});
    require(near(grey.hue, 200.0, 1e-9) && near(grey.saturation, 0.0, 1e-9), "a grey keeps the hue");
    const ColorPickerHsv black = ColorPicker::hsv_of(Color::rgba(0, 0, 0), ColorPickerHsv{200.0, 0.7, 0.2});
    require(near(black.hue, 200.0, 1e-9) && near(black.saturation, 0.7, 1e-9) && near(black.brightness, 0.0, 1e-9),
            "black keeps the hue and the saturation");
    for (int hue = 0; hue < 360; hue += 15) {
        const Color made = ColorPicker::color_of(ColorPickerHsv{static_cast<double>(hue), 1.0, 1.0}, 255U);
        const ColorPickerHsv read = ColorPicker::hsv_of(made, ColorPickerHsv{});
        require(near(read.hue, static_cast<double>(hue), 1.0), "a pure hue survives the round trip");
    }
}

void test_value_and_hsv() {
    const std::shared_ptr<ColorPicker> picker = make_control<ColorPicker>(StableId("picker"), Color::rgba(255, 0, 0));
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*picker, recorder, tokens);
    require(near((*picker).hsv().saturation, 1.0, 1e-9), "the initial value places the thumbs");
    (*picker).set_hsv(ColorPickerHsv{120.0, 1.0, 1.0});
    require((*picker).value.read() == Color::rgba(0, 255, 0), "set_hsv writes the value");
    require(recorder.changes == 1 && recorder.commits == 0, "set_hsv raises value_changed only");
    (*picker).set_hsv(ColorPickerHsv{120.0, 0.0, 0.5});
    (*picker).set_hsv(ColorPickerHsv{300.0, 0.0, 0.5});
    require(near((*picker).hsv().hue, 300.0, 1e-9), "a grey's hue moves without changing the colour");
    require(recorder.changes == 2, "an unchanged colour raises nothing");
    (*picker).value = Color::rgba(0, 0, 255, 10);
    require(near((*picker).hsv().hue, 240.0, 0.5), "a program's write moves the thumbs");
    require(recorder.changes == 3 && recorder.commits == 0, "a program's write is not a commit");
    bool refused = false;
    try {
        (*picker).set_hsv(ColorPickerHsv{10.0, 1.5, 1.0});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picker).value.read() == Color::rgba(0, 0, 255, 10), "an out-of-range HSV changes nothing");
    refused = false;
    try {
        (*picker).set_hsv(ColorPickerHsv{std::nan(""), 0.5, 0.5});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a NaN component is refused");
}

void test_pointer_and_keys() {
    const std::shared_ptr<ColorPicker> picker =
        make_control<ColorPicker>(StableId("picker.input"), Color::rgba(255, 0, 0));
    Window window(picker, {220.0, 180.0});
    window.perform_layout();
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*picker, recorder, tokens);
    const ColorPicker::Parts parts = (*picker).parts();
    const Rect field = parts.field.bounds;
    const Rect hue = parts.hue_track.bounds;
    const Rect alpha = parts.alpha_track.bounds;
    require(field.width > 0.0 && hue.width > 0.0 && alpha.width > 0.0, "the parts are laid out");

    // A drag on the field: the bottom-left corner is black.
    press(window, PointerAction::down, PointerButton::primary, Point{field.x + field.width * 0.5, field.y + 1.0});
    require((*picker).dragged_part() == ColorPickerPart::field, "a press on the field drags it");
    press(window, PointerAction::move, PointerButton::none, Point{field.x - 40.0, field.y + field.height + 40.0});
    require(near((*picker).hsv().saturation, 0.0, 1e-9) && near((*picker).hsv().brightness, 0.0, 1e-9),
            "the drag is clamped into the field");
    require(recorder.commits == 0, "moves do not commit");
    press(window, PointerAction::up, PointerButton::primary, Point{field.x + field.width, field.y});
    require((*picker).dragged_part() == ColorPickerPart::none, "the release ends the drag");
    require(recorder.commits == 1 && recorder.last_commit == Color::rgba(255, 0, 0), "the release commits");

    // The hue track: the middle is cyan.
    press(window, PointerAction::down, PointerButton::primary, Point{hue.x + 2.0, hue.y + hue.height * 0.5});
    press(window, PointerAction::up, PointerButton::primary, Point{hue.x + 2.0, hue.y + hue.height * 0.5});
    require(near((*picker).hsv().hue, 180.0, 2.0), "the hue track's middle is 180 degrees");

    // The alpha track: the bottom is transparent.
    press(window, PointerAction::down, PointerButton::primary, Point{alpha.x + 2.0, alpha.y + alpha.height - 1.0});
    press(window, PointerAction::up, PointerButton::primary, Point{alpha.x + 2.0, alpha.y + alpha.height + 20.0});
    require((*picker).value.read().alpha == 0U, "the alpha track's bottom is transparent");

    // Keys.
    require(window.request_focus(picker), "the picker takes focus");
    const int commits = recorder.commits;
    KeyEvent end{};
    end.physical_key = PhysicalKey::end;
    end.modifiers = Modifier::shift;
    require(window.dispatch_key(end), "Shift+End is handled");
    require((*picker).value.read().alpha == 255U, "Shift+End makes it opaque");
    KeyEvent left{};
    left.physical_key = PhysicalKey::left;
    const double saturation = (*picker).hsv().saturation;
    static_cast<void>(window.dispatch_key(left));
    require(near((*picker).hsv().saturation, std::max(0.0, saturation - 0.01), 1e-9), "Left lowers the saturation");
    KeyEvent page{};
    page.physical_key = PhysicalKey::page_up;
    const double before = (*picker).hsv().hue;
    static_cast<void>(window.dispatch_key(page));
    require(near((*picker).hsv().hue, std::fmod(before + 5.0, 360.0), 1e-9), "Page Up turns the hue 5 degrees");
    require(recorder.commits == commits + 3, "each key edit commits");

    // Without the alpha track, Home and End are not the picker's.
    (*picker).alpha_enabled = false;
    window.perform_layout();
    require((*picker).parts().alpha_track.bounds.width == 0.0, "the alpha track is hidden");
    KeyEvent home{};
    home.physical_key = PhysicalKey::home;
    const Color kept = (*picker).value.read();
    static_cast<void>(window.dispatch_key(home));
    require((*picker).value.read() == kept, "Home changes nothing without the alpha track");
}

void test_value_editor_drop_down() {
    const std::shared_ptr<ColorValueEditor> editor =
        make_control<ColorValueEditor>(StableId("editor"), Color::rgba(10, 20, 30));
    Window window(editor, {300.0, 300.0});
    window.perform_layout();
    require(!(*editor).picker_open(), "the picker starts closed");
    require((*editor).open_picker(), "the drop-down opens");
    const std::shared_ptr<ColorPicker> picker = (*editor).picker();
    require(picker != nullptr && (*picker).value.read() == Color::rgba(10, 20, 30), "the picker shows the value");
    Recorder recorder{};
    const SubscriptionToken token =
        (*editor).value_changed().subscribe(Delegate<Color>::bind<Recorder, &Recorder::on_change>(recorder));
    (*picker).value = Color::rgba(200, 100, 50);
    require(recorder.changes == 1 && (*editor).value.read() == Color::rgba(200, 100, 50), "a pick writes the editor's value");
    require((*(*editor).editor()).text.read() == "#C86432FF", "the text follows");
    (*editor).close_picker();
    require(!(*editor).picker_open(), "the drop-down closes");
    (*picker).value = Color::rgba(1, 2, 3);
    require((*editor).value.read() == Color::rgba(200, 100, 50), "a closed picker no longer writes the value");
    require((*editor).open_picker() && (*editor).picker_open(), "it opens again");
    (*editor).close_picker();
}

} // namespace

int main() {
    test_conversions();
    test_value_and_hsv();
    test_pointer_and_keys();
    test_value_editor_drop_down();
    std::cout << "color picker tests passed\n";
    return EXIT_SUCCESS;
}
