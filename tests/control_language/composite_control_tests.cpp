// The composite control (cp34, phase 2; composite_control.hpp). Gate: the
// design note's worked examples run as controls loaded from their
// documents (demo/composites):
// - toggle-pill: perform, a click and Space's key up toggle `checked`, which
//   raises checked-changed with its payload; a commit-mode drag moves only
//   the presentational drag-position and Escape puts everything back; the
//   semantics are a toggle switch, checked when checked;
// - card-with-action: the children are made under the instance's stable ID,
//   the title and the action's text and visibility follow the composite's
//   properties, the action's activation raises action-invoked, and the slot
//   is the content child;
// - stepper-field: refused until converters are built (E999);
// - an inline document: a live drag writes its property on every frame and
//   Escape writes the pressed value back;
// - the look (phase 3): the examples' <look> is the instance's theme sheet,
//   pending until the instance is attached, then compiled once against the
//   composite's own anatomy and applied as the subtree's theme override; a
//   document without a look sets no sheet.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#ifndef GUI_FORMS_COMPOSITES_DIR
#error "GUI_FORMS_COMPOSITES_DIR names demo/composites"
#endif

namespace {

using namespace gui_forms;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read_example(const std::string_view name) {
    std::string path = GUI_FORMS_COMPOSITES_DIR;
    path += '/';
    path += name;
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    std::string read = text.str();
    return read;
}

// The child types the examples name.
constexpr std::array<CompositeChildType, 4> example_child_types{
    composite_child_type<FlexPanel>("flex_panel"),
    composite_child_type<Label>("label"),
    composite_child_type<Button>("button"),
    composite_child_type<TextBox>("text_box"),
};

CompositeLoadResult load_example(const std::string_view name) {
    const std::string text = read_example(name);
    CompositeLoadResult loaded = load_composite_definition(name, text, example_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    return loaded;
}

// Records the events a composite raises.
class RaisedRecorder final : public Component {
public:
    void on_raised(const CompositeEvent& event) {
        names.push_back(std::string(event.name));
        payloads.push_back(event.payload);
    }
    std::vector<std::string> names{};
    std::vector<BindingValue> payloads{};
};

bool boolean_property(const Control& control, const std::string_view name, const bool expected) {
    const std::optional<BindingValue> value = control.property_value(name);
    if (!value) return false;
    const bool* truth = std::get_if<bool>(&*value);
    const bool matches = truth != nullptr && *truth == expected;
    return matches;
}

bool dispatch_press(Window& window, const Point position, const PointerAction action) {
    PointerEvent event{};
    event.action = action;
    event.button = PointerButton::primary;
    event.position = position;
    const bool taken = window.dispatch_pointer(event);
    return taken;
}

void move_pointer(Window& window, const Point position) {
    PointerEvent event{};
    event.action = PointerAction::move;
    event.position = position;
    static_cast<void>(window.dispatch_pointer(event));
}

void press_key(Window& window, const std::uint32_t key, const KeyAction action) {
    KeyEvent event{action, key};
    static_cast<void>(window.dispatch_key(event));
}

Point centre_of(const Rect box) {
    const Point centre{box.x + box.width * 0.5, box.y + box.height * 0.5};
    return centre;
}

void toggle_pill() {
    const CompositeLoadResult loaded = load_example("toggle-pill.control");
    require(static_cast<bool>(loaded), "toggle-pill loads");
    if (!loaded) return;
    require(composite_type_name(*loaded.definition) == "toggle_pill", "toggle-pill registers as toggle_pill");
    const std::shared_ptr<CompositeControl> pill =
        make_control<CompositeControl>(StableId(std::string("pill")), loaded.definition);
    (*pill).set_requested_bounds({0.0, 0.0, 160.0, 30.0});
    Window window(pill, {160.0, 30.0});
    window.perform_layout();
    RaisedRecorder recorder{};
    on((*pill).raised(), recorder, &RaisedRecorder::on_raised);

    require(boolean_property(*pill, "checked", false), "checked starts false");
    require((*pill).perform("toggle"), "the toggle command runs");
    require(boolean_property(*pill, "checked", true), "toggle sets checked");
    require(recorder.names.size() == 1U && recorder.names[0] == "checked-changed",
            "a change raises checked-changed");
    const bool* payload = recorder.payloads.empty() ? nullptr : std::get_if<bool>(&recorder.payloads[0]);
    require(payload != nullptr && *payload, "checked-changed carries the new value");
    (*pill).set_property_value("checked", BindingValue{false});
    require(boolean_property(*pill, "checked", false), "a property write reaches the composite");

    // A click on the track toggles (the hit area covers track and label).
    const Rect track = (*pill).part_bounds("track");
    require(!track.empty(), "the track is laid out");
    require(dispatch_press(window, centre_of(track), PointerAction::down), "the press reaches the composite");
    static_cast<void>(dispatch_press(window, centre_of(track), PointerAction::up));
    require(boolean_property(*pill, "checked", true), "a click toggles");

    // Space toggles on its key up.
    require(window.request_focus(pill), "the pill takes focus");
    press_key(window, PhysicalKey::space, KeyAction::down);
    require(boolean_property(*pill, "checked", true), "Space's key down does not toggle");
    press_key(window, PhysicalKey::space, KeyAction::up);
    require(boolean_property(*pill, "checked", false), "Space's key up toggles");

    const SemanticDescriptor unchecked = (*pill).semantic_descriptor();
    require(unchecked.role == SemanticRole::toggle_switch, "the semantics are a toggle switch");
    require(!has_semantic_state(unchecked.states, SemanticState::checked), "unchecked is not checked");
    require((*pill).perform("toggle"), "toggled again");
    require(has_semantic_state((*pill).semantic_descriptor().states, SemanticState::checked),
            "checked is published");

    // A commit-mode drag: the thumb moves by drag-position only; Escape
    // restores.
    const Rect thumb = (*pill).part_bounds("thumb");
    const Point start = centre_of(thumb);
    static_cast<void>(dispatch_press(window, start, PointerAction::down));
    move_pointer(window, Point{start.x - 6.0, start.y});
    move_pointer(window, Point{start.x - 12.0, start.y});
    const BindingValue dragging = (*pill).presentational_value("drag-position");
    require(std::holds_alternative<double>(dragging), "the drag moves the presentational drag-position");
    require(boolean_property(*pill, "checked", true), "a commit drag does not write checked while it runs");
    press_key(window, PhysicalKey::escape, KeyAction::down);
    require(boolean_property(*pill, "checked", true), "Escape keeps checked as it was");
    require(std::holds_alternative<std::monostate>((*pill).presentational_value("drag-position")),
            "Escape clears the drag-position");
    require((*pill).statistics().drags_restored == 1U, "the restore is counted");
    static_cast<void>(dispatch_press(window, Point{start.x - 12.0, start.y}, PointerAction::up));
    require(boolean_property(*pill, "checked", true), "the release after Escape is no click");
}

void card_with_action() {
    const CompositeLoadResult loaded = load_example("card-with-action.control");
    require(static_cast<bool>(loaded), "card-with-action loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> card =
        make_control<CompositeControl>(StableId(std::string("card")), loaded.definition);
    (*card).set_requested_bounds({0.0, 0.0, 240.0, 160.0});
    Window window(card, {240.0, 160.0});
    window.perform_layout();
    RaisedRecorder recorder{};
    on((*card).raised(), recorder, &RaisedRecorder::on_raised);

    const Control::Ptr title = (*card).child("title");
    const Control::Ptr action = (*card).child("action");
    const Control::Ptr content = (*card).slot();
    require(title != nullptr && action != nullptr && content != nullptr, "the children are made");
    if (title == nullptr || action == nullptr || content == nullptr) return;
    require((*title).stable_id().value() == "card.title", "a child's stable ID is under the instance's");
    require((*content).stable_id().value() == "card.content", "the slot is the content child");

    (*card).set_property_value("title", BindingValue{std::string("Storage")});
    const std::optional<BindingValue> shown = (*title).property_value("text");
    const std::string* shown_text = shown ? std::get_if<std::string>(&*shown) : nullptr;
    require(shown_text != nullptr && *shown_text == "Storage", "the title follows the composite's title");

    require(boolean_property(*action, "visible", false), "the action is hidden without text");
    (*card).set_property_value("action_text", BindingValue{std::string("Manage")});
    require(boolean_property(*action, "visible", true), "the action shows with text");

    Button& button = static_cast<Button&>(*action);
    require(button.perform_click(), "the action is clicked");
    require(!recorder.names.empty() && recorder.names.back() == "action-invoked",
            "the action's activation raises action-invoked");
}

void stepper_field_refused() {
    const std::string text = read_example("stepper-field.control");
    const CompositeLoadResult loaded = load_composite_definition("stepper-field.control", text, example_child_types);
    require(!loaded, "stepper-field is refused until converters are built");
    bool deferred = false;
    for (const std::string& line : loaded.diagnostics) {
        if (line.find("E999") != std::string::npos) deferred = true;
    }
    require(deferred, "the refusal is E999");
}

// A live drag: `level` follows every frame; Escape writes back the value
// the press found.
constexpr std::string_view live_slider = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="live-slider" version="1.0" base="user-control" family="choice">
  <property name="level" kind="number" default="0" range="0 100" bindable="two-way" affects="paint semantics"/>
  <part name="rail" role="track" box="inset(region, 0) height(10) align-block(center)"/>
  <part name="knob" role="thumb" shape="circle" states="hot pressed captured"
        box="inset(rail, 0) width(10) align-inline(at(calc(start-of(rail) + level)))"
        hit="box" press="drag">
    <drag kind="value" mode="live" axis="inline" range="start-of(rail) end-of(rail)" maps-to="level"
          keyboard="none"/>
  </part>
  <focus target="self" tab-stop="yes"/>
  <semantics role="slider" name="property(accessible-name)" value="level" range="0 100"/>
</control>
)xml";

void live_drag() {
    const std::span<const CompositeChildType> none{};
    const CompositeLoadResult loaded = load_composite_definition("live-slider.control", live_slider, none);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(loaded), "the live slider loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> slider =
        make_control<CompositeControl>(StableId(std::string("slider")), loaded.definition);
    (*slider).set_requested_bounds({0.0, 0.0, 120.0, 20.0});
    Window window(slider, {120.0, 20.0});
    window.perform_layout();
    const Rect knob = (*slider).part_bounds("knob");
    require(!knob.empty(), "the knob is laid out");
    const Point start = centre_of(knob);
    static_cast<void>(dispatch_press(window, start, PointerAction::down));
    move_pointer(window, Point{start.x + 20.0, start.y});
    move_pointer(window, Point{start.x + 40.0, start.y});
    const std::optional<BindingValue> moved = (*slider).property_value("level");
    const double* level = moved ? std::get_if<double>(&*moved) : nullptr;
    require(level != nullptr && *level > 0.0, "a live drag writes level as it runs");
    press_key(window, PhysicalKey::escape, KeyAction::down);
    const std::optional<BindingValue> restored = (*slider).property_value("level");
    const double* back = restored ? std::get_if<double>(&*restored) : nullptr;
    require(back != nullptr && *back == 0.0, "Escape writes back the pressed value");
    static_cast<void>(dispatch_press(window, Point{start.x + 40.0, start.y}, PointerAction::up));
}

// Prints a refused look's diagnostics, so a failure says why.
class LookReporter final : public Component {
public:
    void on_compiled(const ThemeCompileResult& result) {
        if (result.succeeded()) return;
        for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
            std::cerr << "  look: " << diagnostic.code << ' ' << diagnostic.name << ' ' << diagnostic.file << ':'
                      << diagnostic.line << ':' << diagnostic.column << ' ' << diagnostic.rule << ' '
                      << diagnostic.detail << '\n';
        }
    }
};

void look_applies(const std::string_view example, const Size size) {
    const CompositeLoadResult loaded = load_example(example);
    require(static_cast<bool>(loaded), "the example loads for its look");
    if (!loaded) return;
    require(!(*loaded.definition).look.empty(), "the example declares a look");
    const std::shared_ptr<CompositeControl> composite =
        make_control<CompositeControl>(StableId(std::string("looked")), loaded.definition);
    LookReporter reporter{};
    on((*composite).theme_sheet_compiled(), reporter, &LookReporter::on_compiled);
    require((*composite).theme_sheet() == (*loaded.definition).look, "the look is the instance's theme sheet");
    require((*composite).theme_sheet_pending() && (*composite).statistics().look_compiles == 0U,
            "the look waits for an attachment");
    (*composite).set_requested_bounds({0.0, 0.0, size.width, size.height});
    Window window(composite, size);
    window.perform_layout();
    require(!(*composite).theme_sheet_pending(), "the look compiles on attachment");
    require((*composite).statistics().look_compiles == 1U, "the look compiles once");
    require((*composite).statistics().look_refusals == 0U, "the look compiles against the composite's anatomy");
    require((*composite).theme_override() != nullptr, "the look is the subtree's theme override");
}

void no_look() {
    const std::span<const CompositeChildType> none{};
    const CompositeLoadResult loaded = load_composite_definition("live-slider.control", live_slider, none);
    require(static_cast<bool>(loaded), "the live slider loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> slider =
        make_control<CompositeControl>(StableId(std::string("plain")), loaded.definition);
    Window window(slider, {120.0, 20.0});
    window.perform_layout();
    require((*slider).theme_sheet().empty() && (*slider).theme_override() == nullptr,
            "a document without a look sets no sheet");
    require((*slider).statistics().look_compiles == 0U, "nothing compiles without a look");
}

} // namespace

int main() {
    toggle_pill();
    card_with_action();
    stepper_field_refused();
    live_drag();
    look_applies("toggle-pill.control", Size{160.0, 30.0});
    look_applies("card-with-action.control", Size{240.0, 160.0});
    no_look();
    if (failures != 0) {
        std::cerr << failures << " composite control check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_control_tests: all checks passed\n";
    return 0;
}
