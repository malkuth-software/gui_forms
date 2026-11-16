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
// - the look (phase 3): the examples' <look> compiles once with the
//   definition and every instance and its made children share it (slot
//   content takes none); a hundred instances compile nothing more; a look
//   reading a theme's custom property compiles once per theme on first
//   use, follows a theme switch, reuses a kept theme's compilation on
//   switching back, and a hundred instances under one theme compile it
//   once; the look
//   layers over the window's theme: its rules stand over the theme's where
//   they say something, a theme switch reaches the composite's part and
//   child elsewhere, forced high contrast draws both, and the look reaches
//   no control outside; a document without a look has none;
// - a query field (SwiftEdit's QueryField as a composite): its semantics
//   come from its focus target, so the composite publishes no node and the
//   editor's node, named by the document, stands for it; the document sets
//   the content editor's multiline and draws-selection; its `paint="hook"`
//   part is drawn by a typed class, with the part's bounds and text, and
//   draws nothing without one; a composite with its own semantics publishes
//   its own node;
// - real pointer input on a capsule (PlaySuite's command capsule, reduced):
//   a pill part covering the client area, a document button and one the
//   class adds, both placed in arrange and painted in the overlay plane,
//   and a hit test of the pill alone: a press and release on either button
//   clicks it, also with a layout pass between them, a press on the bare
//   pill lands on the composite and one beside it on nothing, as the press
//   trace (SurfaceRoot::last_press_trace) records.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "../../src/core/control_language/composite_definition/composite_definition.hpp"
#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "../support/clip_tracking_painter.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/content_text_editor.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

// A key action and a child binding that name a property of the base type
// (A-gf-core-03-F1): the checker admits them, but the composite reads and
// writes only its own properties by index, so the compiler refuses them
// (E999) rather than reading property 0, which this document has none of.
constexpr std::string_view base_toggle = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="base-toggle" version="1.0" base="user-control" family="choice">
  <part name="face" role="face" box="inset(region, 0)"/>
  <focus target="self" tab-stop="yes" visible-cue="part(focus)"/>
  <key gesture="space"><toggle property="enabled"/></key>
  <semantics role="button" name="property(accessible-name)"/>
</control>
)xml";

constexpr std::string_view base_binding = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="base-binding" version="1.0" base="user-control" family="card">
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <label id="@caption" dock="fill" text="bind(self.accessible-name, one_way)"/>
  </children>
  <focus target="none"/>
  <semantics role="group" name="property(accessible-name)" children="exposed"/>
</control>
)xml";

bool refused_as_not_built(const std::string_view name, const std::string_view text) {
    const CompositeLoadResult loaded = load_composite_definition(name, text, example_child_types);
    bool deferred = false;
    for (const std::string& line : loaded.diagnostics) {
        if (line.find("E999") != std::string::npos) deferred = true;
    }
    const bool refused = !loaded && deferred;
    if (!refused) {
        for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    }
    return refused;
}

void base_properties_refused() {
    require(refused_as_not_built("base-toggle.control", base_toggle),
            "a key action on a base property is refused (E999), not compiled to property 0");
    require(refused_as_not_built("base-binding.control", base_binding),
            "a child binding to a base property is refused (E999), not compiled to property 0");
}

// A two-way child binding and a second child bound to the same property
// (A-gf-controls-02-F3): typing in the field must still update the echo; only
// the write back to the field itself is skipped.
constexpr std::string_view echo_field = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="echo-field" version="1.0" base="user-control" family="card">
  <property name="query" kind="text" default="" bindable="two-way"/>
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <text-box id="@entry" dock="top" text="bind(self.query)"/>
    <label id="@echo" dock="top" text="bind(self.query, one_way)"/>
  </children>
  <focus target="child(@entry)"/>
  <semantics role="group" name="property(accessible-name)" children="exposed"/>
</control>
)xml";

std::string text_property(const Control& control, const std::string_view name) {
    const std::optional<BindingValue> value = control.property_value(name);
    const std::string* const text = value ? std::get_if<std::string>(&*value) : nullptr;
    std::string result = text != nullptr ? *text : std::string{};
    return result;
}

void two_way_child_updates_the_others() {
    const CompositeLoadResult loaded = load_composite_definition("echo-field.control", echo_field, example_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(loaded), "the echo field loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> field =
        make_control<CompositeControl>(StableId(std::string("echo")), loaded.definition);
    const std::span<const Control::Ptr> children = (*field).children();
    std::shared_ptr<TextBox> entry{};
    std::shared_ptr<Label> echo{};
    for (const Control::Ptr& child : children) {
        if (!entry) entry = std::dynamic_pointer_cast<TextBox>(child);
        if (!echo) echo = std::dynamic_pointer_cast<Label>(child);
    }
    require(entry && echo, "the field has its text box and label");
    if (!entry || !echo) return;
    (*entry).text = "typed";
    require(text_property(*field, "query") == "typed", "the text box writes the composite's property");
    require(text_property(*echo, "text") == "typed", "the label bound to the same property follows");
    (*field).set_property_value("query", BindingValue{std::string("set")});
    require(text_property(*entry, "text") == "set" && text_property(*echo, "text") == "set",
            "a write from the composite still reaches both children");
}

// Conditions that read themselves (A-gf-controls-02-F1) and a command that
// runs itself (A-gf-controls-02-F2): each ends instead of recursing without
// bound. `lit` reads the composite's own hot state, so its evaluation reads
// anatomy_states again; `armed` and `go` read each other; `again` runs
// itself.
constexpr std::string_view self_reading = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="self-reading" version="1.0" base="user-control" family="card">
  <property name="level" kind="number" default="0"/>
  <state name="lit" when="state(hot) or pressed()"/>
  <state name="armed" when="command-enabled(go)"/>
  <command name="go" enabled="state(armed)" run="step(level, 1)"/>
  <command name="again" enabled="enabled(self)" run="run(again)"/>
  <part name="face" role="face" box="inset(region, 0)"/>
  <focus target="none"/>
  <semantics role="group" name="property(accessible-name)"/>
</control>
)xml";

void self_reading_conditions_end() {
    const std::span<const CompositeChildType> none{};
    const CompositeLoadResult loaded = load_composite_definition("self-reading.control", self_reading, none);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    // The checker admits these today (it finds cycles between custom states
    // only); if it learns to refuse them, this check moves to the checker.
    require(static_cast<bool>(loaded), "the self-reading definition loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> control =
        make_control<CompositeControl>(StableId(std::string("self.reading")), loaded.definition);
    const AnatomyStateSet states = (*control).anatomy_states();
    require(states.contains(AnatomyState::normal), "a state reading the composite's own states is decided");
    static_cast<void>((*control).command_enabled("go"));
    const std::uint64_t refused_before = (*control).statistics().refused_firings;
    static_cast<void>((*control).perform("again"));
    require((*control).statistics().refused_firings > refused_before,
            "a command that runs itself stops at the rule depth");
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
  <focus target="self" tab-stop="yes" visible-cue="part(focus)"/>
  <!-- The keyboard reaches level (E913), as the drag does not. -->
  <key gesture="arrow-right"><step property="level" by="1"/></key>
  <key gesture="arrow-left"><step property="level" by="calc(0 - 1)"/></key>
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

// The look of an example: compiled with the definition, shared by the
// instance and the children its definition made, not by slot content.
void look_shared(const std::string_view example, const std::string_view child, const Size size) {
    const CompositeLoadResult loaded = load_example(example);
    require(static_cast<bool>(loaded), "the example loads for its look");
    if (!loaded) return;
    const CompositeDefinition& definition = *loaded.definition;
    require(!definition.look.empty() && definition.compiled_look != nullptr, "the example's look compiles");
    if (definition.compiled_look == nullptr) return;
    require(definition.look_compiles == 1U, "the look compiles alone once");
    const std::shared_ptr<CompositeControl> composite =
        make_control<CompositeControl>(StableId(std::string("looked")), loaded.definition);
    (*composite).set_requested_bounds({0.0, 0.0, size.width, size.height});
    Window window(composite, size);
    window.perform_layout();
    const CompiledThemeRules* const look = (*composite).look_rules();
    require(look != nullptr && (*look).look() && !(*look).empty(), "the instance takes the definition's look");
    require((*composite).statistics().look_compiles == 1U, "the instance reports the definition's one compile");
    require((*composite).theme_override() == nullptr && (*composite).theme_sheet().empty(),
            "the look is no theme sheet and no override");
    if (!child.empty()) {
        const Control::Ptr made = (*composite).child(child);
        require(made != nullptr && (*made).look_rules() == look, "a made child takes the look");
    }
    const Control::Ptr slot = (*composite).slot();
    if (slot == nullptr) return;
    const std::shared_ptr<Label> content = make_control<Label>(StableId(std::string("looked.filled")));
    (*slot).add_child(content);
    require((*content).look_rules() == nullptr, "slot content takes no look");
}

void no_look() {
    const std::span<const CompositeChildType> none{};
    const CompositeLoadResult loaded = load_composite_definition("live-slider.control", live_slider, none);
    require(static_cast<bool>(loaded), "the live slider loads");
    if (!loaded) return;
    require((*loaded.definition).compiled_look == nullptr && (*loaded.definition).look_compiles == 0U,
            "nothing compiles without a look");
    const std::shared_ptr<CompositeControl> slider =
        make_control<CompositeControl>(StableId(std::string("plain")), loaded.definition);
    Window window(slider, {120.0, 20.0});
    window.perform_layout();
    require((*slider).look_rules() == nullptr && (*slider).statistics().look_compiles == 0U,
            "a document without a look has none");
}

// A composite whose look sets the text colour of its face and of its
// button child, and nothing else.
constexpr std::string_view looked_pair = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="looked-pair" version="1.0" base="user-control" family="panel">
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <button id="@action" text="Go"/>
  </children>
  <focus target="none"/>
  <look>
    looked-pair::part(face) { color: #102030; }
    looked-pair > button { color: #405060; }
  </look>
</control>
)xml";

constexpr Color look_face_text = Color::rgba(0x10, 0x20, 0x30);
constexpr Color look_button_text = Color::rgba(0x40, 0x50, 0x60);
constexpr Color first_muted = Color::rgba(0xaa, 0x00, 0x00);
constexpr Color second_muted = Color::rgba(0x00, 0xaa, 0x00);

ThemePackage sheet_package(const std::string_view sheet) {
    const std::string hash = theme_language::package_sha256_hex(theme_language::package_sha256(sheet));
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"app\", \"version\": \"1.0.0\", "
                    "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"app.theme.css\", \"sha256\": \"" +
                    hash + "\"}}";
    made.files.push_back(ThemePackageFile{"app.theme.css", std::string(sheet)});
    return made;
}

// An application theme from `sheet`; a literal colour's warning (W201) is
// the only finding admitted.
std::shared_ptr<const Theme> app_theme(const std::string_view sheet) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemeCompileResult result = compile_theme_package(sheet_package(sheet), library, images, options);
    bool clean = result.succeeded();
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == "W201") continue;
        clean = false;
        std::cerr << "  " << diagnostic.code << ' ' << diagnostic.name << '\n';
    }
    require(clean, "the application theme compiles");
    std::shared_ptr<const Theme> theme = result.theme();
    return theme;
}

// The theme in effect for `control`, resolved for its part `part` (empty:
// the control itself) in `context`.
ControlVisualRecipe recipe_of(const Control& control, const std::string_view part, const ControlVisualContext context) {
    const Theme& theme = control.effective_theme();
    const AnatomyStateSet states{};
    const ResolvedControlRecipe resolved = theme.resolve(control, ControlVisualRole::panel, context, states, part);
    ControlVisualRecipe recipe = resolved.recipe();
    return recipe;
}

// The look layers over the window's theme: a theme switch reaches the
// composite's face and its button child wherever the look says nothing,
// the look's text colours stand over the theme's, and a button outside
// the composite sees the theme alone.
void look_layers_over_theme_switch() {
    const CompositeLoadResult loaded = load_composite_definition("looked-pair.control", looked_pair, example_child_types);
    // A look is an application sheet, not the colour authority: its literal
    // text colours cannot follow forced high contrast's remapping, and each
    // warns W201 literal_colour_unremappable, as the rule says. Expected
    // here; anything else is printed.
    std::size_t literal_warnings = 0U;
    for (const std::string& line : loaded.diagnostics) {
        if (line.find("W201") != std::string::npos) {
            literal_warnings = literal_warnings + 1U;
            continue;
        }
        std::cerr << "  " << line << '\n';
    }
    require(literal_warnings > 0U, "the look's literal text colours warn W201");
    require(static_cast<bool>(loaded), "the looked pair loads");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> pair =
        make_control<CompositeControl>(StableId(std::string("pair")), loaded.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    Window window(pair, {200.0, 80.0});
    const std::shared_ptr<Button> outside = make_control<Button>(StableId(std::string("outside")));
    (*outside).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
    Window other(outside, {80.0, 24.0});
    const Control::Ptr action = (*pair).child("action");
    require(action != nullptr, "the pair makes its button");
    if (action == nullptr) return;

    const std::string_view first_sheet = "#pair { color: #aa0000; muted-color: #aa0000; }\n"
                                         "button { color: #aa0000; muted-color: #aa0000; }\n";
    const std::string_view second_sheet = "#pair { color: #00aa00; muted-color: #00aa00; }\n"
                                          "button { color: #00aa00; muted-color: #00aa00; }\n";
    const std::shared_ptr<const Theme> first = app_theme(first_sheet);
    const std::shared_ptr<const Theme> second = app_theme(second_sheet);
    if (first == nullptr || second == nullptr) return;
    const ControlVisualContext rest{};

    window.set_theme(first);
    other.set_theme(first);
    window.perform_layout();
    const ControlVisualRecipe face = recipe_of(*pair, "face", rest);
    const ControlVisualRecipe child = recipe_of(*action, {}, rest);
    require(face.text == look_face_text, "the look's face colour stands over the theme's");
    require(face.muted_text == first_muted, "the theme reaches the face where the look says nothing");
    require(child.text == look_button_text, "the look's button colour stands over the theme's");
    require(child.muted_text == first_muted, "the theme reaches the child where the look says nothing");
    const ControlVisualRecipe plain = recipe_of(*outside, {}, rest);
    require(plain.text == first_muted && (*outside).look_rules() == nullptr, "the look reaches no other button");

    window.set_theme(second);
    window.perform_layout();
    const ControlVisualRecipe switched_face = recipe_of(*pair, "face", rest);
    const ControlVisualRecipe switched_child = recipe_of(*action, {}, rest);
    require(switched_face.muted_text == second_muted, "a theme switch reaches the composite's face");
    require(switched_child.muted_text == second_muted, "a theme switch reaches the composite's child");
    require(switched_face.text == look_face_text && switched_child.text == look_button_text,
            "the look holds over the new theme");
    require((*pair).statistics().look_compiles == 1U, "a theme switch compiles no look");
}

// Forced high contrast draws the composite's face and child from the
// engine's pass, the look lying under it as every sheet does.
void look_under_forced_high_contrast() {
    const CompositeLoadResult loaded = load_composite_definition("looked-pair.control", looked_pair, example_child_types);
    require(static_cast<bool>(loaded), "the looked pair loads for high contrast");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> pair =
        make_control<CompositeControl>(StableId(std::string("pair")), loaded.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    Window window(pair, {200.0, 80.0});
    window.perform_layout();
    const Control::Ptr action = (*pair).child("action");
    require(action != nullptr, "the pair makes its button for high contrast");
    if (action == nullptr) return;
    ControlVisualContext forced{};
    forced.high_contrast = true;
    const Theme& theme = (*pair).effective_theme();
    const ControlVisualRecipe& floor = theme.resolve(ControlVisualRole::panel, forced);
    const ControlVisualRecipe face = recipe_of(*pair, "face", forced);
    const ControlVisualRecipe child = recipe_of(*action, {}, forced);
    require(face.text == floor.text && face.text != look_face_text, "forced high contrast draws the face");
    require(child.text == floor.text && child.text != look_button_text, "forced high contrast draws the child");
    // The cascade's own account: the text is the pass's, not the look's.
    const std::size_t text = static_cast<std::size_t>(RecipeField::text);
    CompiledThemeRules::PartProvenance looked{};
    theme.provenance(*action, ControlVisualRole::panel, ControlVisualContext{}, looked);
    require(looked.recipe_fields[text].authored && looked.recipe_fields[text].level == CascadeLevel::theme_sheet,
            "at rest the child's text is the look's");
    CompiledThemeRules::PartProvenance passed{};
    theme.provenance(*action, ControlVisualRole::panel, forced, passed);
    require(passed.recipe_fields[text].level == CascadeLevel::forced_high_contrast,
            "under forced high contrast the child's text is the pass's");
}

// A hundred instances of one definition share its one compiled look.
void hundred_instances_one_compile() {
    const CompositeLoadResult loaded = load_composite_definition("looked-pair.control", looked_pair, example_child_types);
    require(static_cast<bool>(loaded), "the looked pair loads for a hundred instances");
    if (!loaded) return;
    const CompositeDefinition& definition = *loaded.definition;
    const std::shared_ptr<FlexPanel> host = make_control<FlexPanel>(StableId(std::string("host")));
    std::size_t shared = 0U;
    for (std::size_t index = 0U; index < 100U; ++index) {
        const std::shared_ptr<CompositeControl> instance =
            make_control<CompositeControl>(StableId("pair" + std::to_string(index)), loaded.definition);
        (*host).add_child(instance);
        const Control::Ptr action = (*instance).child("action");
        const bool takes = (*instance).look_rules() != nullptr && action != nullptr &&
                           (*action).look_rules() == (*instance).look_rules();
        if (takes && (*instance).statistics().look_compiles == 1U) shared = shared + 1U;
    }
    Window window(host, {400.0, 400.0});
    window.perform_layout();
    require(definition.compiled_look != nullptr && !(*definition.compiled_look).theme_bound(),
            "a look of literal colours is theme-independent");
    require(shared == 100U, "every instance and its child share the definition's look");
    require(definition.look_compiles == 1U && (*definition.compiled_look).theme_compiles() == 0U,
            "a hundred instances cause one compile");
}

// A composite whose look reads the theme's custom property --brand.
constexpr std::string_view branded_pair = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="branded-pair" version="1.0" base="user-control" family="panel">
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <button id="@action" text="Go"/>
  </children>
  <focus target="none"/>
  <look>
    branded-pair::part(face) { color: var(--brand, #000000); }
    branded-pair > button { color: var(--brand, #000000); }
  </look>
</control>
)xml";

constexpr Color first_brand = Color::rgba(0xaa, 0x00, 0x00);
constexpr Color second_brand = Color::rgba(0x00, 0xaa, 0x00);

// A look reading a theme's custom property follows a theme switch: it is
// compiled once per theme when the theme is applied, shared by the
// instance and its child, and switching back to the first theme reuses
// that theme's kept compilation.
void theme_bound_look_follows_switch() {
    const CompositeLoadResult loaded =
        load_composite_definition("branded-pair.control", branded_pair, example_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(loaded), "the branded pair loads");
    if (!loaded) return;
    const CompositeDefinition& definition = *loaded.definition;
    require(definition.compiled_look != nullptr && (*definition.compiled_look).theme_bound(),
            "a look reading a theme's custom property is theme-bound");
    if (definition.compiled_look == nullptr) return;
    const detail::CompositeLook& look = *definition.compiled_look;
    const std::shared_ptr<const Theme> first = app_theme(":root { --brand: #aa0000; }\n");
    const std::shared_ptr<const Theme> second = app_theme(":root { --brand: #00aa00; }\n");
    if (first == nullptr || second == nullptr) return;
    const std::shared_ptr<CompositeControl> pair =
        make_control<CompositeControl>(StableId(std::string("branded")), loaded.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    Window window(pair, {200.0, 80.0});
    const Control::Ptr action = (*pair).child("action");
    require(action != nullptr, "the branded pair makes its button");
    if (action == nullptr) return;
    const ControlVisualContext rest{};

    window.set_theme(first);
    window.perform_layout();
    require(recipe_of(*pair, "face", rest).text == first_brand, "the face follows the first theme's --brand");
    require(recipe_of(*action, {}, rest).text == first_brand, "the child follows the first theme's --brand");
    require((*action).look_rules() == (*pair).look_rules(), "the child shares the instance's compilation");
    const std::uint64_t first_done = look.theme_compiles();

    window.set_theme(second);
    window.perform_layout();
    require(recipe_of(*pair, "face", rest).text == second_brand, "a theme switch changes the face's --brand");
    require(recipe_of(*action, {}, rest).text == second_brand, "a theme switch changes the child's --brand");
    const std::uint64_t after_second = look.theme_compiles();
    require(after_second == first_done + 1U, "the second theme compiles the look once");

    // Back to the first theme: its compilation is still kept (within
    // maximum_composite_look_entries), so it is reused, not compiled again.
    const std::uint64_t reuses = look.theme_reuses();
    window.set_theme(first);
    window.perform_layout();
    require(recipe_of(*pair, "face", rest).text == first_brand, "switching back restores the first --brand");
    require(look.theme_compiles() == after_second, "switching back reuses the first theme's compilation");
    require(look.theme_reuses() > reuses && (*pair).statistics().look_theme_reuses == look.theme_reuses(),
            "the reuse is counted");
    require(look.theme_refusals() == 0U && (*pair).statistics().look_theme_refusals == 0U, "no theme refuses it");
    require(look.missing() == 0U, "every resolution found a prepared compilation");
}

// Resolution never compiles a look: applying a theme compiles it, and the
// style pass that follows (layout, paint, explicit resolutions) only reads
// it; a theme never prepared is a counted defect that compiles nothing.
void resolution_never_compiles() {
    const CompositeLoadResult loaded =
        load_composite_definition("branded-pair.control", branded_pair, example_child_types);
    require(static_cast<bool>(loaded), "the branded pair loads for the style pass");
    if (!loaded || (*loaded.definition).compiled_look == nullptr) return;
    const detail::CompositeLook& look = *(*loaded.definition).compiled_look;
    const std::shared_ptr<const Theme> theme = app_theme(":root { --brand: #aa0000; }\n");
    const std::shared_ptr<const Theme> unprepared = app_theme(":root { --brand: #00aa00; }\n");
    if (theme == nullptr || unprepared == nullptr) return;
    const std::shared_ptr<CompositeControl> pair =
        make_control<CompositeControl>(StableId(std::string("styled")), loaded.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    Window window(pair, {200.0, 80.0});
    window.set_theme(theme);
    const std::uint64_t prepared = look.theme_compiles();
    require(prepared >= 1U, "applying the theme compiled the look before the style pass");
    window.perform_layout();
    test_support::ClipTrackingPainter painter{};
    window.paint(painter);
    for (std::size_t round = 0U; round < 10U; ++round) {
        static_cast<void>(recipe_of(*pair, "face", ControlVisualContext{}));
    }
    require(look.theme_compiles() == prepared, "the style pass compiles no look");
    require(look.missing() == 0U, "the style pass finds the prepared compilation");
    // A theme no fixed point prepared (resolved directly, not applied):
    // counted as missing, nothing drawn from the look, nothing compiled.
    detail::CompositeLook& shared = *(*loaded.definition).compiled_look;
    const CompiledThemeRules* const found = shared.rules_for(*unprepared);
    require(found == nullptr && look.missing() == 1U && look.theme_compiles() == prepared,
            "a missing compilation is a counted defect, not a compile");
    require((*pair).statistics().look_missing == 1U, "the statistics report the defect");
}

// A hundred instances of a theme-bound look under one theme cause one
// compilation for that theme, made when the theme is applied.
void theme_bound_hundred_one_compile() {
    const CompositeLoadResult loaded =
        load_composite_definition("branded-pair.control", branded_pair, example_child_types);
    require(static_cast<bool>(loaded), "the branded pair loads for a hundred instances");
    if (!loaded || (*loaded.definition).compiled_look == nullptr) return;
    const detail::CompositeLook& look = *(*loaded.definition).compiled_look;
    const std::shared_ptr<const Theme> theme = app_theme(":root { --brand: #aa0000; }\n");
    if (theme == nullptr) return;
    const std::shared_ptr<FlexPanel> host = make_control<FlexPanel>(StableId(std::string("host")));
    std::vector<std::shared_ptr<CompositeControl>> instances{};
    instances.reserve(100U);
    for (std::size_t index = 0U; index < 100U; ++index) {
        std::shared_ptr<CompositeControl> instance =
            make_control<CompositeControl>(StableId("branded" + std::to_string(index)), loaded.definition);
        (*host).add_child(instance);
        instances.push_back(std::move(instance));
    }
    Window window(host, {400.0, 400.0});
    const std::uint64_t before = look.theme_compiles();
    window.set_theme(theme);
    require(look.theme_compiles() == before + 1U, "applying the theme compiles the look once, before any resolution");
    window.perform_layout();
    std::size_t following = 0U;
    for (const std::shared_ptr<CompositeControl>& instance : instances) {
        if (recipe_of(*instance, "face", ControlVisualContext{}).text == first_brand) following = following + 1U;
    }
    require(following == 100U, "every instance follows the theme");
    require(look.theme_compiles() == before + 1U, "a hundred instances under one theme cause one compile");
    require(instances[0] != nullptr && (*instances[0]).statistics().look_theme_compiles == look.theme_compiles() &&
                (*instances[0]).statistics().look_compiles == 1U,
            "the statistics report both counts");
}

// Ten windows showing ten theme-bound themes at once each hold their
// compilation: none is evicted (more than maximum_composite_look_entries
// are kept while held), every window keeps drawing its look and nothing is
// missing. When the windows go, the compilations become evictable, not
// freed: the most recently prepared maximum_composite_look_entries stay.
void ten_themes_held() {
    const CompositeLoadResult loaded =
        load_composite_definition("branded-pair.control", branded_pair, example_child_types);
    require(static_cast<bool>(loaded), "the branded pair loads for ten windows");
    if (!loaded || (*loaded.definition).compiled_look == nullptr) return;
    const detail::CompositeLook& look = *(*loaded.definition).compiled_look;
    constexpr std::size_t count = 10U;
    std::vector<std::shared_ptr<const Theme>> themes{};
    std::vector<Color> brands{};
    themes.reserve(count);
    brands.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint8_t red = static_cast<std::uint8_t>(0x10U + index * 0x10U);
        const std::string hex = "0123456789abcdef";
        std::string sheet = ":root { --brand: #";
        sheet += hex[red / 16U];
        sheet += hex[red % 16U];
        sheet += "2030; }\n";
        std::shared_ptr<const Theme> theme = app_theme(sheet);
        if (theme == nullptr) return;
        themes.push_back(std::move(theme));
        brands.push_back(Color::rgba(red, 0x20, 0x30));
    }
    std::vector<std::shared_ptr<CompositeControl>> pairs{};
    std::vector<std::unique_ptr<Window>> windows{};
    pairs.reserve(count);
    windows.reserve(count);
    const std::uint64_t before = look.theme_compiles();
    for (std::size_t index = 0U; index < count; ++index) {
        std::shared_ptr<CompositeControl> pair =
            make_control<CompositeControl>(StableId("held" + std::to_string(index)), loaded.definition);
        (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
        std::unique_ptr<Window> window = std::make_unique<Window>(pair, Size{200.0, 80.0});
        (*window).set_theme_override(themes[index]);
        (*window).perform_layout();
        pairs.push_back(std::move(pair));
        windows.push_back(std::move(window));
    }
    require(look.theme_compiles() >= before + count, "each of the ten themes compiles the look");
    require(look.held_count() == count, "the ten shown themes each hold their compilation");
    // Every window draws again, in turn, after all ten were shown.
    std::size_t drawing = 0U;
    for (std::size_t round = 0U; round < 2U; ++round) {
        for (std::size_t index = 0U; index < count; ++index) {
            test_support::ClipTrackingPainter painter{};
            (*windows[index]).paint(painter);
            const Color text = recipe_of(*pairs[index], "face", ControlVisualContext{}).text;
            if (text == brands[index]) drawing = drawing + 1U;
        }
    }
    require(drawing == 2U * count, "all ten windows keep drawing their looks");
    require(look.missing() == 0U && (*pairs[0]).statistics().look_missing == 0U, "no compilation is missing");
    const std::uint64_t settled = look.theme_compiles();
    windows.clear();
    pairs.clear();
    require(look.held_count() == 0U, "with the windows gone no theme holds a compilation");
    require(look.entry_count() == maximum_composite_look_entries,
            "the unheld compilations stay kept, evictable, up to the bound");
    require(look.theme_compiles() == settled, "releasing compiles nothing");
}

} // namespace

// A query field: the editor is the focus target and its node is the
// field's; the field draws its query itself, over the editor's box, through
// a paint hook.
constexpr std::string_view query_field = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="query-field" version="1.0" base="user-control" family="card">
  <property name="query" kind="text" default="find me" category="Appearance"/>
  <part name="query-text" role="label" paint="hook" box="inset(region, 2) text-extent(query) align-block(center)"/>
  <children>
    <content-text-editor id="@editing" dock="fill" multiline="false" draws-selection="false"
                         accessible-name="Query"/>
  </children>
  <focus target="child(@editing)"/>
  <semantics from="child(@editing)"/>
</control>
)xml";

constexpr std::array<CompositeChildType, 1> query_child_types{
    composite_child_type<ContentTextEditor>("content_text_editor"),
};

// The class gui_forms_add_control would generate for the query field, with
// the consumer's paint hook: records each hooked part and draws its text.
class HookedQueryField final : public CompositeControl {
public:
    HookedQueryField(StableId stable_id, std::shared_ptr<const CompositeDefinition> definition)
        : CompositeControl(std::move(stable_id), std::move(definition)) {}

    std::vector<std::string> hooked_parts{};
    std::vector<std::string> hooked_texts{};
    std::vector<Rect> hooked_bounds{};

protected:
    void paint_hook(Painter& painter, const CompositePaintHook& hook) override {
        hooked_parts.push_back(std::string(hook.part));
        hooked_texts.push_back(std::string(hook.text));
        hooked_bounds.push_back(hook.bounds);
        const Point origin{hook.bounds.x, hook.bounds.y};
        painter.draw_text_utf8(origin, hook.text, FontSpec{}, Color{0, 0, 0, 255});
    }
};

// The nodes in `nodes` and below with `runtime_id`.
std::size_t nodes_with_id(const std::vector<SemanticNode>& nodes, const std::uint64_t runtime_id) {
    std::size_t count = 0U;
    for (const SemanticNode& node : nodes) {
        if (node.runtime_id == runtime_id) count = count + 1U;
        count = count + nodes_with_id(node.children, runtime_id);
    }
    return count;
}

// The first node in `nodes` and below with `runtime_id`; null for none.
const SemanticNode* node_with_id(const std::vector<SemanticNode>& nodes, const std::uint64_t runtime_id) {
    for (const SemanticNode& node : nodes) {
        if (node.runtime_id == runtime_id) return &node;
        const SemanticNode* const below = node_with_id(node.children, runtime_id);
        if (below != nullptr) return below;
    }
    return nullptr;
}
const SemanticNode* node_with_id(std::vector<SemanticNode>&& nodes, std::uint64_t runtime_id) = delete;

std::shared_ptr<const CompositeDefinition> load_query_field() {
    const CompositeLoadResult loaded = load_composite_definition("query-field.control", query_field, query_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(loaded), "the query field loads");
    return loaded.definition;
}

void query_field_takes_editor_node() {
    const std::shared_ptr<const CompositeDefinition> definition = load_query_field();
    if (definition == nullptr) return;
    require(!(*definition).parts.empty() && (*definition).parts.back().paint_hook, "the query part is a paint hook");
    const std::shared_ptr<HookedQueryField> field =
        make_control<HookedQueryField>(StableId(std::string("query")), definition);
    (*field).set_requested_bounds({0.0, 0.0, 200.0, 24.0});
    Window window(field, {200.0, 24.0});
    window.perform_layout();
    const Control::Ptr editing = (*field).child("editing");
    const ContentTextEditor* const editor = dynamic_cast<const ContentTextEditor*>(editing.get());
    require(editor != nullptr, "the edit part is a content text editor");
    if (editor == nullptr) return;
    require(!(*editor).multiline(), "the document makes the editor single-line");
    require(!(*editor).draws_selection(), "the document leaves the selection to the field");
    require(!(*field).semantic_descriptor().exposed, "the field publishes no node of its own");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(nodes_with_id(snapshot.roots, (*field).runtime_id().value) == 0U, "no node is the field's own");
    require(nodes_with_id(snapshot.roots, (*editing).runtime_id().value) == 1U, "the editor's node is published once");
    const SemanticNode* const node = node_with_id(snapshot.roots, (*editing).runtime_id().value);
    require(node != nullptr && (*node).role == SemanticRole::text_box && (*node).name == "Query",
            "the editor's node is a text box named by the document");
    require((*field).focus_target() == editing, "the editor is the focus target");
    static_cast<void>(window.request_focus(field));
    require(window.focused_control() == editing, "focusing the field focuses the editor, whose node is the field's");
    test_support::ClipTrackingPainter painter{};
    window.paint(painter);
    require((*field).hooked_parts.size() == 1U && (*field).hooked_parts[0] == "query-text",
            "the typed class draws the hooked part");
    require(!(*field).hooked_texts.empty() && (*field).hooked_texts[0] == "find me", "the hook receives the part's text");
    require(!(*field).hooked_bounds.empty() && (*field).hooked_bounds[0] == (*field).part_bounds("query-text"),
            "the hook receives the part's bounds");
    require(painter.count("find me") == 1U, "the hook's drawing is painted");
}

void hook_without_class_draws_nothing() {
    const std::shared_ptr<const CompositeDefinition> definition = load_query_field();
    if (definition == nullptr) return;
    const std::shared_ptr<CompositeControl> field =
        make_control<CompositeControl>(StableId(std::string("plain-query")), definition);
    (*field).set_requested_bounds({0.0, 0.0, 200.0, 24.0});
    Window window(field, {200.0, 24.0});
    window.perform_layout();
    test_support::ClipTrackingPainter painter{};
    window.paint(painter);
    require(painter.count("find me") == 0U, "a hook part draws nothing without a typed class");
}

void own_semantics_exposed() {
    const CompositeLoadResult loaded = load_example("toggle-pill.control");
    if (!loaded) return;
    const std::shared_ptr<CompositeControl> pill =
        make_control<CompositeControl>(StableId(std::string("exposed-pill")), loaded.definition);
    (*pill).set_requested_bounds({0.0, 0.0, 160.0, 24.0});
    Window window(pill, {160.0, 24.0});
    window.perform_layout();
    require((*pill).semantic_descriptor().exposed, "a composite with its own semantics publishes its node");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(nodes_with_id(snapshot.roots, (*pill).runtime_id().value) == 1U, "the composite's node is in the snapshot");
}

// A capsule like PlaySuite's CommandCapsule, reduced: the document makes
// the pill part (covering the client area less a 14 margin) and one button;
// the class adds a second, places both with set_child_layout in arrange,
// paints its children in the overlay plane, and hit-tests its pill alone.
constexpr std::string_view capsule_probe = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="capsule-probe" version="1.0" base="user-control" family="panel">
  <part name="pill" role="face" shape="pill" box="inset(region, 14)"/>
  <children>
    <button id="@go" text="Go"/>
  </children>
  <focus target="none"/>
  <semantics role="toolbar" name="property(accessible-name)" children="exposed"/>
</control>
)xml";

constexpr double capsule_margin = 14.0;
constexpr Rect capsule_go_slot{20.0, 20.0, 40.0, 24.0};
constexpr Rect capsule_added_slot{70.0, 20.0, 40.0, 24.0};

// Counts a button's clicked().
class ClickCounter final : public Component {
public:
    void on_clicked() { count = count + 1U; }
    std::size_t count{};
};

class CapsuleProbe final : public CompositeControl {
public:
    CapsuleProbe(StableId stable_id, std::shared_ptr<const CompositeDefinition> definition)
        : CompositeControl(std::move(stable_id), std::move(definition)) {}

    // The document's children first, then the class's own button.
    void initialize_control_tree() {
        CompositeControl::initialize_control_tree();
        added = make_control<Button>(StableId(std::string("capsule.added")));
        add_child(added);
    }

    void arrange(const Rect final_bounds) override {
        for (const Control::Ptr& held : children()) (*held).set_paint_plane(PaintPlane::overlay);
        CompositeControl::arrange(final_bounds);
        const Control::Ptr go = child("go");
        if (go != nullptr) set_child_layout(go, capsule_go_slot);
        if (added != nullptr) set_child_layout(added, capsule_added_slot);
        arranged = arranged + 1U;
    }

    // The pill alone: the margin is the shadow's.
    [[nodiscard]] bool hit_test_local(const Point local) const override {
        const Rect own = client_rectangle();
        const bool inside = local.x >= capsule_margin && local.y >= capsule_margin &&
                            local.x < own.width - capsule_margin && local.y < own.height - capsule_margin;
        return inside;
    }

    std::shared_ptr<Button> added{};
    std::size_t arranged{};
};

void press_and_release(Window& window, const Point position) {
    static_cast<void>(dispatch_press(window, position, PointerAction::down));
    static_cast<void>(dispatch_press(window, position, PointerAction::up));
}

void capsule_pointer_reaches_buttons() {
    const CompositeLoadResult loaded =
        load_composite_definition("capsule-probe.control", capsule_probe, example_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(loaded), "the capsule probe loads");
    if (!loaded) return;
    const std::shared_ptr<CapsuleProbe> capsule =
        make_control<CapsuleProbe>(StableId(std::string("capsule")), loaded.definition);
    (*capsule).set_requested_bounds({0.0, 0.0, 200.0, 64.0});
    Window window(capsule, {200.0, 64.0});
    window.perform_layout();
    const Control::Ptr go = (*capsule).child("go");
    require(go != nullptr && (*capsule).added != nullptr, "the capsule has both buttons");
    if (go == nullptr || (*capsule).added == nullptr) return;
    ClickCounter go_clicks{};
    ClickCounter added_clicks{};
    on(static_cast<Button&>(*go).clicked(), go_clicks, &ClickCounter::on_clicked);
    on((*(*capsule).added).clicked(), added_clicks, &ClickCounter::on_clicked);

    // The document's button: the press lands on it and the release clicks.
    press_and_release(window, centre_of(capsule_go_slot));
    const PressTrace& trace = window.last_press_trace();
    require(trace.outcome == PressOutcome::delivered && trace.target_stable_id == "capsule.go",
            "a press on the document's button lands on it (trace: " + trace.target_stable_id + ")");
    require(go_clicks.count == 1U, "a real press and release click the document's button");

    // The class's button, with a layout pass between press and release (the
    // capsule re-lays out on every frame of its opening).
    const std::size_t arranged_before = (*capsule).arranged;
    static_cast<void>(dispatch_press(window, centre_of(capsule_added_slot), PointerAction::down));
    require(window.last_press_trace().target_stable_id == "capsule.added", "a press lands on the class's button");
    (*capsule).invalidate(Dirty::layout);
    window.perform_layout();
    require((*capsule).arranged > arranged_before, "the capsule was arranged during the press");
    static_cast<void>(dispatch_press(window, centre_of(capsule_added_slot), PointerAction::up));
    require(added_clicks.count == 1U, "a layout pass between press and release keeps the click");

    // The bare pill is the composite's; the shadow margin is nothing's.
    press_and_release(window, Point{150.0, 32.0});
    require(window.last_press_trace().outcome == PressOutcome::delivered &&
                window.last_press_trace().target_stable_id == "capsule",
            "a press on the bare pill lands on the capsule");
    press_and_release(window, Point{4.0, 4.0});
    require(window.last_press_trace().outcome == PressOutcome::no_target, "a press beside the pill lands on nothing");
    require(go_clicks.count == 1U && added_clicks.count == 1U, "neither button clicked again");
    require(window.last_press_trace().presses == 4U, "every primary press is traced (four: go, added, pill, margin)");
}

int main() {
    toggle_pill();
    card_with_action();
    stepper_field_refused();
    base_properties_refused();
    two_way_child_updates_the_others();
    self_reading_conditions_end();
    live_drag();
    look_shared("toggle-pill.control", {}, Size{160.0, 30.0});
    look_shared("card-with-action.control", "title", Size{240.0, 160.0});
    no_look();
    look_layers_over_theme_switch();
    look_under_forced_high_contrast();
    hundred_instances_one_compile();
    theme_bound_look_follows_switch();
    theme_bound_hundred_one_compile();
    resolution_never_compiles();
    ten_themes_held();
    query_field_takes_editor_node();
    hook_without_class_draws_nothing();
    own_semantics_exposed();
    capsule_pointer_reaches_buttons();
    if (failures != 0) {
        std::cerr << failures << " composite control check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_control_tests: all checks passed\n";
    return 0;
}
