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
//   content takes none); a hundred instances compile nothing more; the look
//   layers over the window's theme: its rules stand over the theme's where
//   they say something, a theme switch reaches the composite's part and
//   child elsewhere, forced high contrast draws both, and the look reaches
//   no control outside; a document without a look has none.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "../../src/core/control_language/composite_definition/composite_definition.hpp"
#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/text_box.hpp"
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

// The look of an example: compiled once with the definition, shared by the
// instance and the children its definition made, not by slot content.
void look_shared(const std::string_view example, const std::string_view child, const Size size) {
    const CompositeLoadResult loaded = load_example(example);
    require(static_cast<bool>(loaded), "the example loads for its look");
    if (!loaded) return;
    const CompositeDefinition& definition = *loaded.definition;
    require(!definition.look.empty() && definition.look_rules != nullptr, "the example's look compiles");
    if (definition.look_rules == nullptr) return;
    require((*definition.look_rules).look() && !(*definition.look_rules).empty(), "the look is a look's rule set");
    require(definition.look_compiles == 1U && definition.look_rule_diagnostics == 0U,
            "the look compiles once, every rule kept");
    const std::shared_ptr<CompositeControl> composite =
        make_control<CompositeControl>(StableId(std::string("looked")), loaded.definition);
    (*composite).set_requested_bounds({0.0, 0.0, size.width, size.height});
    Window window(composite, size);
    window.perform_layout();
    const CompiledThemeRules* const look = definition.look_rules.get();
    require((*composite).look_rules() == look, "the instance takes the definition's look");
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
    require((*loaded.definition).look_rules == nullptr && (*loaded.definition).look_compiles == 0U,
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
    button { color: #405060; }
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
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
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
        const bool takes = (*instance).look_rules() == definition.look_rules.get() && action != nullptr &&
                           (*action).look_rules() == definition.look_rules.get();
        if (takes && (*instance).statistics().look_compiles == 1U) shared = shared + 1U;
    }
    Window window(host, {400.0, 400.0});
    window.perform_layout();
    require(shared == 100U, "every instance and its child share the definition's look");
    require(definition.look_compiles == 1U, "a hundred instances cause one compile");
}

} // namespace

int main() {
    toggle_pill();
    card_with_action();
    stepper_field_refused();
    live_drag();
    look_shared("toggle-pill.control", {}, Size{160.0, 30.0});
    look_shared("card-with-action.control", "title", Size{240.0, 160.0});
    no_look();
    look_layers_over_theme_switch();
    look_under_forced_high_contrast();
    hundred_instances_one_compile();
    if (failures != 0) {
        std::cerr << failures << " composite control check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_control_tests: all checks passed\n";
    return 0;
}
