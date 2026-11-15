// The composite-control language's model and checker (cp34, phase 1;
// control_model.hpp). Gate: the design note's three worked examples are
// read as documents (demo/composites); every check of section 5.3 refuses
// its defect with its code (E900 to E925) at its element, all defects in
// one pass, and a refused document leaves the model as it was; a custom
// state without a look rule is a warning (W950) that does not refuse.

#include "../../src/core/control_language/control_model/control_model.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/content_text_editor.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef GUI_FORMS_COMPOSITES_DIR
#error "GUI_FORMS_COMPOSITES_DIR names demo/composites"
#endif

namespace {

using gui_forms::AnatomyState;
using gui_forms::Dirty;
using gui_forms::PartRole;
using gui_forms::SemanticRole;
using gui_forms::control_language::ControlDragKind;
using gui_forms::control_language::ControlDragMode;
using gui_forms::control_language::ControlFocusTarget;
using gui_forms::control_language::ControlKeyPhase;
using gui_forms::control_language::ControlModel;
using gui_forms::control_language::ControlModelOutcome;
using gui_forms::control_language::ControlPartHit;
using gui_forms::control_language::ControlPress;
using gui_forms::control_language::ControlPropertyKind;
using gui_forms::control_language::ControlSyntax;
using gui_forms::control_language::read_control_model;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::form_control_type;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::diagnostic_code_label;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The types the examples' bases and children name, described as a loader
// describes the registry's.
FormSchema composite_schema() {
    FormSchema schema{};
    schema.types.push_back(form_control_type<gui_forms::UserControl>("user_control"));
    schema.types.push_back(form_control_type<gui_forms::TextBox>("text_box"));
    schema.types.push_back(form_control_type<gui_forms::FlexPanel>("flex_panel"));
    schema.types.push_back(form_control_type<gui_forms::Label>("label"));
    schema.types.push_back(form_control_type<gui_forms::Button>("button"));
    schema.types.push_back(form_control_type<gui_forms::CheckBox>("check_box"));
    schema.types.push_back(form_control_type<gui_forms::ContentTextEditor>("content_text_editor"));
    return schema;
}

// The caller-owned storage one document fills.
struct Workspace final {
    std::string text{};
    ControlSyntax syntax{};
    ThemeDiagnosticList diagnostics{};
};

ControlModelOutcome build(Workspace& workspace, const std::string_view text, const FormSchema& schema,
                          ControlModel& model) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    ThemeSource source{};
    source.file = "test.control";
    source.text = workspace.text;
    const ControlModelOutcome outcome =
        read_control_model(source, workspace.syntax, schema, model, workspace.diagnostics);
    return outcome;
}

std::string read_file(const std::string_view name) {
    const std::string path = std::string(GUI_FORMS_COMPOSITES_DIR) + "/" + std::string(name);
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents{};
    contents << stream.rdbuf();
    return contents.str();
}

std::size_t count_code(const ThemeDiagnosticList& diagnostics, const DiagnosticCode code) {
    std::size_t count = 0U;
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) count += diagnostic.code == code ? 1U : 0U;
    return count;
}

void print_diagnostics(const ThemeDiagnosticList& diagnostics) {
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
        std::cerr << "  " << diagnostic_code_label(diagnostic.code) << " line " << diagnostic.position.line.value
                  << " rule " << diagnostic.rule << " property " << diagnostic.property << ": " << diagnostic.detail
                  << '\n';
    }
}

// A document around `body`: a composite with nothing of its own.
std::string document(const std::string_view body) {
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<control xmlns=\"urn:gui-forms:control:1\" name=\"sample-box\" version=\"1.0\">\n";
    text += body;
    text += "\n</control>\n";
    return text;
}

// The part, focus and semantics an active composite needs, so that a case
// about something else reports only its own code.
constexpr std::string_view accessible =
    "<semantics role=\"button\" name=\"property(accessible-name)\"/>"
    "<focus target=\"self\" visible-cue=\"part(focus)\"/>";

// Builds `text` and requires a refusal reporting `code`.
void expect_refused(const FormSchema& schema, const std::string_view text, const DiagnosticCode code,
                    const std::string_view message) {
    Workspace workspace{};
    ControlModel model{};
    model.name = "kept";
    const ControlModelOutcome outcome = build(workspace, text, schema, model);
    const bool reported = count_code(workspace.diagnostics, code) != 0U;
    require(outcome == ControlModelOutcome::refused, std::string(message) + ": refused");
    require(reported, std::string(message) + ": reports " + std::string(diagnostic_code_label(code)));
    require(model.name == "kept", std::string(message) + ": a refused document leaves the model as it was");
    if (!reported) print_diagnostics(workspace.diagnostics);
}

void test_toggle_pill() {
    const FormSchema schema = composite_schema();
    const std::string text = read_file("toggle-pill.control");
    require(!text.empty(), "toggle-pill.control is read");
    Workspace workspace{};
    ControlModel model{};
    const ControlModelOutcome outcome = build(workspace, text, schema, model);
    require(outcome == ControlModelOutcome::model_built, "the toggle switch builds");
    require(workspace.diagnostics.records().empty(), "the toggle switch has no diagnostics");
    print_diagnostics(workspace.diagnostics);
    if (outcome != ControlModelOutcome::model_built) return;
    require(model.name == "toggle-pill" && model.version_major == 1U && model.version_minor == 0U,
            "its name and version");
    require(model.properties.size() == 3U && model.properties[0].kind == ControlPropertyKind::boolean &&
                model.properties[0].default_value == "false" && model.properties[0].changed == "checked-changed",
            "checked is a boolean raising checked-changed");
    require((model.properties[1].affects & Dirty::layout) == Dirty::layout && model.properties[1].localizable,
            "text affects layout and is localizable");
    require(model.properties[2].presentational, "drag-position is presentational");
    require(model.commands.size() == 1U && model.commands[0].semantic_action == gui_forms::SemanticAction::press,
            "toggle maps to press");
    require(model.states.size() == 1U && !model.states[0].custom && model.states[0].standard == AnatomyState::checked,
            "checked is published");
    require(model.parts.size() == 4U && model.parts[1].role == PartRole::thumb, "four parts, the thumb second");
    const std::uint8_t click_drag =
        static_cast<std::uint8_t>(ControlPress::click) | static_cast<std::uint8_t>(ControlPress::drag);
    require(model.parts[1].press == click_drag && model.parts[1].hit == ControlPartHit::shape &&
                model.parts[1].drag == std::size_t{0U},
            "the thumb presses click drag on its shape and holds the drag");
    require(model.drags.size() == 1U && model.drags[0].kind == ControlDragKind::value &&
                model.drags[0].mode == ControlDragMode::commit && model.drags[0].settles &&
                model.drags[0].keyboard.empty() && model.drags[0].maps_to == "checked",
            "a commit-mode value drag onto checked, with no keyboard gesture of its own");
    require(model.parts[3].hit == ControlPartHit::box, "the hit area hits its box");
    require(model.focus && (*model.focus).target == ControlFocusTarget::self && (*model.focus).tab_stop &&
                (*model.focus).visible_cue == "focus" && (*model.focus).mnemonic == "text",
            "focus is the composite's own, cued by the focus part");
    require(model.rules.size() == 1U && model.rules[0].key && model.rules[0].phase == ControlKeyPhase::key_up &&
                model.rules[0].action_count == 1U,
            "space toggles after KeyUp");
    require(model.semantics && (*model.semantics).role == SemanticRole::toggle_switch,
            "the semantic role is toggle_switch");
    require(model.look.find("toggle-pill::part(thumb)") != std::string::npos, "the look is kept as written");
}

void test_card_with_action() {
    const FormSchema schema = composite_schema();
    const std::string text = read_file("card-with-action.control");
    Workspace workspace{};
    ControlModel model{};
    const ControlModelOutcome outcome = build(workspace, text, schema, model);
    require(outcome == ControlModelOutcome::model_built, "the card builds");
    require(workspace.diagnostics.records().empty(), "the card has no diagnostics");
    print_diagnostics(workspace.diagnostics);
    if (outcome != ControlModelOutcome::model_built) return;
    require(model.properties.size() == 3U && model.properties[2].kind == ControlPropertyKind::command,
            "action-command is a command property");
    require(model.events.size() == 1U && !model.events[0].payload, "action-invoked has no payload");
    require(model.children.size() == 4U, "the header, the title, the action and the slot");
    require(model.children[0].local_id == "header" && !model.children[0].parent, "the header is at the top");
    require(model.children[1].parent == std::size_t{0U} && model.children[2].parent == std::size_t{0U},
            "the title and the action are the header's");
    require(model.children[3].slot && model.children[3].local_id == "content", "the slot");
    require(model.children[2].attributes.size() == 3U && model.children[2].attributes[1].name == "command" &&
                model.children[2].attributes[1].bound,
            "the action's command is bound to the composite");
    require(model.focus && (*model.focus).target == ControlFocusTarget::none, "the card is no Tab stop");
    require(model.semantics && (*model.semantics).children_exposed, "its children are exposed");
    require(model.look.find("card-with-action > flex-panel") != std::string::npos, "the look's &gt; is decoded");
}

void test_stepper_field() {
    // Its field binds through a converter, reserved until converters exist
    // (DEPENDS): exactly that one refusal.
    const FormSchema schema = composite_schema();
    const std::string text = read_file("stepper-field.control");
    Workspace workspace{};
    ControlModel model{};
    const ControlModelOutcome outcome = build(workspace, text, schema, model);
    require(outcome == ControlModelOutcome::refused, "the stepper waits on converters");
    require(workspace.diagnostics.error_count() == 1U &&
                count_code(workspace.diagnostics, DiagnosticCode::not_yet_built) == 1U,
            "its one refusal is the reserved converter (E999)");
    require(workspace.diagnostics.warning_count() == 0U, "the stepper has no warnings");
    if (workspace.diagnostics.error_count() != 1U) print_diagnostics(workspace.diagnostics);
    // Without the converter it builds.
    std::string bound = text;
    const std::size_t converter = bound.find(", convert number");
    require(converter != std::string::npos, "the converter is written");
    if (converter == std::string::npos) return;
    bound.erase(converter, std::string_view(", convert number").size());
    const ControlModelOutcome without = build(workspace, bound, schema, model);
    require(without == ControlModelOutcome::model_built, "the stepper builds without its converter");
    print_diagnostics(workspace.diagnostics);
    if (without != ControlModelOutcome::model_built) return;
    require(model.parts.size() == 7U && model.parts[3].activates == "step-up" &&
                model.parts[3].press == static_cast<std::uint8_t>(ControlPress::repeat),
            "the increment arrow repeats step-up");
    require(model.states.size() == 2U && model.states[1].standard == AnatomyState::invalid,
            "read-only and invalid are published");
    require(model.focus && (*model.focus).target == ControlFocusTarget::child && (*model.focus).child == "field",
            "focus goes to the field");
    require(model.rules.size() == 5U && !model.rules[4].key, "four keys and the wheel rule");
}

void test_minimal() {
    const FormSchema schema = composite_schema();
    Workspace workspace{};
    ControlModel model{};
    const ControlModelOutcome outcome = build(workspace, document(""), schema, model);
    require(outcome == ControlModelOutcome::model_built && workspace.diagnostics.records().empty(),
            "an empty composite builds");
}

void test_document_codes() {
    const FormSchema schema = composite_schema();
    // E900: outside the subset, the namespace, text, a namespace below the root.
    expect_refused(schema, "<control name=\"a\"", DiagnosticCode::control_xml_invalid, "not XML");
    expect_refused(schema, "<control name=\"sample-box\" version=\"1.0\"/>", DiagnosticCode::control_xml_invalid,
                   "no namespace");
    expect_refused(schema, document("<event name=\"e\">text</event>"), DiagnosticCode::control_xml_invalid,
                   "text in a declaration");
    expect_refused(schema, document("<event xmlns=\"urn:x\" name=\"e\"/>"), DiagnosticCode::control_xml_invalid,
                   "a namespace below the root");
    expect_refused(schema, document("<look>sample-box { }<b/></look>"), DiagnosticCode::control_xml_invalid,
                   "an element in the look");
    // E901: the root.
    expect_refused(schema, "<form xmlns=\"urn:gui-forms:control:1\"/>", DiagnosticCode::control_root_invalid,
                   "another root");
    expect_refused(schema, "<control xmlns=\"urn:gui-forms:control:1\" name=\"sample-box\" version=\"1\"/>",
                   DiagnosticCode::control_root_invalid, "a version without its minor");
    expect_refused(schema, "<control xmlns=\"urn:gui-forms:control:1\" name=\"Sample\" version=\"1.0\"/>",
                   DiagnosticCode::control_root_invalid, "a name not in kebab case");
    expect_refused(schema, "<control xmlns=\"urn:gui-forms:control:1\" name=\"check-box\" version=\"1.0\"/>",
                   DiagnosticCode::control_root_invalid, "the name of a registered type");
    // E902, E903, E904.
    expect_refused(schema, document("<widget/>"), DiagnosticCode::control_element_unknown, "an unknown element");
    expect_refused(schema, document("<focus target=\"none\"/><focus target=\"none\"/>"),
                   DiagnosticCode::control_element_unknown, "a second focus");
    expect_refused(schema, document("<event name=\"e\" colour=\"red\"/>"), DiagnosticCode::control_attribute_unknown,
                   "an unknown attribute");
    expect_refused(schema, document("<event/>"), DiagnosticCode::control_attribute_missing, "an event's name");
    expect_refused(schema, document("<property name=\"p\"/>"), DiagnosticCode::control_attribute_missing,
                   "a property's kind");
    // E905: the closed vocabularies.
    expect_refused(schema, document("<property name=\"p\" kind=\"real\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown kind");
    expect_refused(schema, document("<part name=\"p\" role=\"knob\"/>"), DiagnosticCode::control_vocabulary_unknown,
                   "an unknown part role");
    expect_refused(schema,
                   "<control xmlns=\"urn:gui-forms:control:1\" name=\"sample-box\" version=\"1.0\" family=\"spinner\"/>",
                   DiagnosticCode::control_vocabulary_unknown, "an unknown family");
    expect_refused(schema, document("<publish state=\"glowing\" when=\"enabled(self)\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown standard state");
    expect_refused(schema, document("<state name=\"lit\" when=\"frobnicate(self)\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown function");
    expect_refused(schema, document("<semantics role=\"spinner\" name=\"property(accessible-name)\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown semantic role");
    expect_refused(schema, document("<command name=\"c\" semantic-action=\"wiggle\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown semantic action");
    // E906: names.
    expect_refused(schema,
                   document("<property name=\"p\" kind=\"number\"/><property name=\"p\" kind=\"number\"/>"),
                   DiagnosticCode::control_name_repeated, "a property declared twice");
    expect_refused(schema, document("<property name=\"enabled\" kind=\"boolean\"/>"),
                   DiagnosticCode::control_name_repeated, "a property of the base");
    expect_refused(schema, document("<part name=\"focus\" role=\"focus\"/>"), DiagnosticCode::control_name_repeated,
                   "a part every control has");
}

void test_declaration_codes() {
    const FormSchema schema = composite_schema();
    // E907: the bounds.
    std::string parts{};
    for (std::size_t index = 0U; index < 31U; ++index) {
        parts += "<part name=\"p" + std::to_string(index) + "\" role=\"face\"/>";
    }
    // 30 declared parts with region and focus fill the anatomy's 32.
    expect_refused(schema, document(parts), DiagnosticCode::control_limit_exceeded, "31 parts");
    std::string states{"<property name=\"b\" kind=\"boolean\"/><look>"};
    for (std::size_t index = 0U; index < 17U; ++index) states += ":state(s" + std::to_string(index) + ") { }";
    states += "</look>";
    for (std::size_t index = 0U; index < 17U; ++index) {
        states += "<state name=\"s" + std::to_string(index) + "\" when=\"property(b)\"/>";
    }
    expect_refused(schema, document(states), DiagnosticCode::control_limit_exceeded, "17 custom states");
    // E908.
    expect_refused(schema, document("<state name=\"hot\" when=\"enabled(self)\"/>"),
                   DiagnosticCode::control_state_shadows_standard, "a custom state named hot");
    // E909, E910.
    expect_refused(schema, document("<part name=\"p\" role=\"face\" box=\"width(pointer-x)\"/>"),
                   DiagnosticCode::control_geometry_engine_input, "geometry reading the pointer");
    expect_refused(schema, document("<state name=\"s\" when=\"less(time-in-state, 3)\"/><look>:state(s) { }</look>"),
                   DiagnosticCode::control_geometry_engine_input, "a state reading time");
    expect_refused(schema, document("<part name=\"p\" role=\"face\" box=\"align-inline(left)\"/>"),
                   DiagnosticCode::control_physical_direction, "a physical position");
    // E911, E912.
    expect_refused(schema, document(std::string("<part name=\"p\" role=\"actuator\" press=\"click\"/>") +
                                    std::string(accessible)),
                   DiagnosticCode::control_part_inactive, "an active part activating nothing");
    expect_refused(schema, document("<property name=\"n\" kind=\"number\"/><command name=\"c\" run=\"step(n, 1)\"/>"),
                   DiagnosticCode::control_command_unreachable, "a command nothing runs");
    // E913: drags.
    expect_refused(schema, document(std::string("<part name=\"p\" role=\"thumb\" press=\"drag\"/>") +
                                    std::string(accessible)),
                   DiagnosticCode::control_drag_invalid, "press drag without a drag");
    expect_refused(schema,
                   document(std::string("<property name=\"n\" kind=\"number\"/><command name=\"c\" "
                                        "run=\"step(n, 1)\"/><part name=\"p\" role=\"thumb\" press=\"click\" "
                                        "activates=\"c\"><drag kind=\"value\" mode=\"live\" maps-to=\"n\" "
                                        "keyboard=\"arrow-up\"/></part>") +
                            std::string(accessible)),
                   DiagnosticCode::control_drag_invalid, "a drag on a part not pressing drag");
    expect_refused(schema,
                   document(std::string("<property name=\"t\" kind=\"text\"/><part name=\"p\" role=\"thumb\" "
                                        "press=\"drag\"><drag kind=\"value\" mode=\"live\" maps-to=\"t\" "
                                        "keyboard=\"arrow-up\"/></part>") +
                            std::string(accessible)),
                   DiagnosticCode::control_drag_invalid, "a value drag onto text");
    expect_refused(schema,
                   document(std::string("<property name=\"n\" kind=\"number\"/><property name=\"v\" "
                                        "kind=\"number\"/><part name=\"p\" role=\"thumb\" press=\"drag\"><drag "
                                        "kind=\"value\" mode=\"commit\" maps-to=\"n\" keyboard=\"arrow-up\" "
                                        "preview=\"set(v, frame-value)\"/></part>") +
                            std::string(accessible)),
                   DiagnosticCode::control_drag_invalid, "a preview writing a property that is not presentational");
    expect_refused(schema,
                   document(std::string("<property name=\"n\" kind=\"number\"/><part name=\"p\" role=\"thumb\" "
                                        "press=\"drag\"><drag kind=\"value\" mode=\"live\" maps-to=\"n\"/></part>") +
                            std::string(accessible)),
                   DiagnosticCode::control_drag_invalid, "a drag the keyboard cannot reach");
    expect_refused(schema,
                   document(std::string("<property name=\"n\" kind=\"number\"/><part name=\"p\" role=\"thumb\" "
                                        "press=\"drag\"><drag kind=\"value\" maps-to=\"n\" "
                                        "keyboard=\"arrow-up\"/></part>") +
                            std::string(accessible)),
                   DiagnosticCode::control_attribute_missing, "a drag without its mode");
    // E914: semantics and the focus cue.
    expect_refused(schema,
                   document("<command name=\"c\" semantic-action=\"press\"/><part name=\"p\" role=\"actuator\" "
                            "press=\"click\" activates=\"c\"/>"),
                   DiagnosticCode::control_semantics_missing, "an active composite without semantics");
    expect_refused(schema,
                   document("<focus target=\"self\"/><semantics role=\"button\" "
                            "name=\"property(accessible-name)\"/>"),
                   DiagnosticCode::control_semantics_missing, "a focusable composite without a cue");
    // E915.
    expect_refused(schema, document("<key gesture=\"tab\"><focus target=\"self\"/></key>"),
                   DiagnosticCode::control_key_not_admitted, "a key on Tab");
    expect_refused(schema, document("<key gesture=\"enter\"><focus target=\"self\"/></key>"),
                   DiagnosticCode::control_key_not_admitted, "Enter without claiming the dialog key");
}

void test_reference_codes() {
    const FormSchema schema = composite_schema();
    // E916: presentation properties.
    expect_refused(schema, document("<property name=\"tint\" kind=\"color\"/>"),
                   DiagnosticCode::control_presentation_property, "a colour property");
    expect_refused(schema, document("<property name=\"face-font\" kind=\"font\"/>"),
                   DiagnosticCode::control_presentation_property, "a font property");
    expect_refused(schema, document("<children><text-box id=\"@field\" back-color=\"#000000\"/></children>"),
                   DiagnosticCode::control_presentation_property, "a child's presentation colour");
    // E917.
    expect_refused(schema, document("<style/>"), DiagnosticCode::control_style_outside_look, "style outside look");
    // E918.
    expect_refused(schema, document("<state name=\"s\" when=\"property(missing)\"/><look>:state(s) { }</look>"),
                   DiagnosticCode::control_reference_unknown, "an unknown word");
    expect_refused(schema, document("<property name=\"n\" kind=\"number\" changed=\"n-changed\"/>"),
                   DiagnosticCode::control_reference_unknown, "an undeclared change event");
    expect_refused(schema, document("<part name=\"p\" role=\"face\" box=\"below(@missing, 0)\"/>"),
                   DiagnosticCode::control_reference_unknown, "an unknown child");
    expect_refused(schema, document("<children><label id=\"@title\" text=\"bind(self.missing)\"/></children>"),
                   DiagnosticCode::control_reference_unknown, "a binding to no property");
    // E919.
    expect_refused(schema, document("<property name=\"a\" alias=\"b\"/><property name=\"b\" alias=\"a\"/>"),
                   DiagnosticCode::control_cycle, "an alias cycle");
    expect_refused(schema,
                   document("<state name=\"x\" when=\"state(y)\"/><state name=\"y\" when=\"state(x)\"/>"
                            "<look>:state(x) { } :state(y) { }</look>"),
                   DiagnosticCode::control_cycle, "a derived-state cycle");
    expect_refused(schema, document("<property name=\"n\" kind=\"number\" range=\"n 10\"/>"),
                   DiagnosticCode::control_cycle, "a range bounded by itself");
    // E920.
    expect_refused(schema, document("<look>button { }</look>"), DiagnosticCode::control_look_foreign_type,
                   "a look rule styling another type");
    // E921.
    expect_refused(schema, document("<children><label text=\"x\"/></children>"), DiagnosticCode::control_child_invalid,
                   "a child without a local ID");
    expect_refused(schema, document("<children><spinner id=\"@s\"/></children>"),
                   DiagnosticCode::control_child_invalid, "a child of no registered type");
    expect_refused(schema, document("<children><slot id=\"@a\"/><slot id=\"@b\"/></children>"),
                   DiagnosticCode::control_child_invalid, "two slots");
    expect_refused(schema, document("<children><label id=\"@a\" colour=\"x\"/></children>"),
                   DiagnosticCode::control_child_invalid, "a child property its type lacks");
    // E922.
    expect_refused(schema, document("<property name=\"t\" kind=\"text\" localizable=\"maybe\"/>"),
                   DiagnosticCode::control_value_invalid, "a flag neither yes nor no");
    expect_refused(schema, document("<property name=\"b\" kind=\"boolean\" default=\"no\"/>"),
                   DiagnosticCode::control_value_invalid, "a boolean default is true or false, as forms write it");
    expect_refused(schema, document("<property name=\"e\" kind=\"enumeration\"/>"),
                   DiagnosticCode::control_value_invalid, "an enumeration without choices");
    // E924, E925.
    expect_refused(schema, document("<state name=\"s\" when=\"less(a,\"/>"),
                   DiagnosticCode::control_expression_invalid, "a malformed expression");
    std::string long_condition{};
    for (std::size_t index = 0U; index < 33U; ++index) {
        if (index != 0U) long_condition += " or ";
        long_condition += "enabled";
    }
    expect_refused(schema, document("<state name=\"s\" when=\"" + long_condition + "\"/><look>:state(s) { }</look>"),
                   DiagnosticCode::control_expression_over_budget, "65 instructions");
    std::string deep{};
    for (std::size_t level = 0U; level < 17U; ++level) deep += "not(";
    deep += "enabled";
    for (std::size_t level = 0U; level < 17U; ++level) deep += ")";
    expect_refused(schema, document("<state name=\"s\" when=\"" + deep + "\"/><look>:state(s) { }</look>"),
                   DiagnosticCode::control_expression_over_budget, "seventeen levels");
    // E999 in a child's binding.
    expect_refused(schema,
                   document("<property name=\"n\" kind=\"number\"/><children><text-box id=\"@f\" "
                            "text=\"bind(self.n, convert number)\"/></children>"),
                   DiagnosticCode::not_yet_built, "a converter is reserved");
}

void test_warning_and_all_at_once() {
    const FormSchema schema = composite_schema();
    // W950 warns and does not refuse.
    Workspace workspace{};
    ControlModel model{};
    ControlModelOutcome outcome =
        build(workspace, document("<property name=\"b\" kind=\"boolean\"/><state name=\"lit\" when=\"property(b)\"/>"),
              schema, model);
    require(outcome == ControlModelOutcome::model_built, "a state without a cue still builds");
    require(workspace.diagnostics.warning_count() == 1U &&
                count_code(workspace.diagnostics, DiagnosticCode::control_state_without_cue) == 1U,
            "it warns W950");
    outcome = build(workspace,
                    document("<property name=\"b\" kind=\"boolean\"/><state name=\"lit\" when=\"property(b)\"/>"
                             "<look>sample-box:state(lit) { }</look>"),
                    schema, model);
    require(outcome == ControlModelOutcome::model_built && workspace.diagnostics.records().empty(),
            "a styled custom state has no warning");
    // Every defect in one pass, each at its line.
    outcome = build(workspace, document("<widget/>\n<event name=\"e\" colour=\"red\"/>\n<style/>"), schema, model);
    require(outcome == ControlModelOutcome::refused && workspace.diagnostics.error_count() == 3U,
            "three defects, three diagnostics");
    if (workspace.diagnostics.records().size() == 3U) {
        require(workspace.diagnostics.records()[0].position.line.value == 3U &&
                    workspace.diagnostics.records()[1].position.line.value == 4U &&
                    workspace.diagnostics.records()[2].position.line.value == 5U,
                "each at its own line");
        require(workspace.diagnostics.records()[1].property == "colour", "the attribute is the property");
    }
}

} // namespace

// SwiftEdit's QueryField gaps: semantics taken from the focus target
// (`from`), the content editor's multiline and draws-selection as document
// properties, and a part the typed class paints (`paint="hook"`).
constexpr std::string_view query_children =
    "<children><content-text-editor id=\"@editing\" multiline=\"false\" draws-selection=\"false\"/></children>";

void test_query_field_gaps() {
    const FormSchema schema = composite_schema();
    {
        Workspace workspace{};
        ControlModel model{};
        const std::string text = document(std::string("<part name=\"query-text\" role=\"label\" paint=\"hook\" "
                                                      "box=\"inset(region, 2)\"/>") +
                                          std::string(query_children) +
                                          "<focus target=\"child(@editing)\"/><semantics from=\"child(@editing)\"/>");
        const ControlModelOutcome outcome = build(workspace, text, schema, model);
        print_diagnostics(workspace.diagnostics);
        require(outcome == ControlModelOutcome::model_built && workspace.diagnostics.records().empty(),
                "a query field builds: semantics from its focus target, a hook part, the editor's properties");
        require(model.semantics && (*model.semantics).from_child == "editing", "the semantics come from @editing");
        require(!model.parts.empty() && model.parts[0].paint_hook, "the query part is a paint hook");
    }
    expect_refused(schema,
                   document(std::string(query_children) +
                            "<focus target=\"child(@editing)\"/><semantics from=\"child(@editing)\" role=\"group\"/>"),
                   DiagnosticCode::control_value_invalid, "a node taken from a child with a role of its own");
    expect_refused(schema,
                   document(std::string(query_children) +
                            "<focus target=\"self\" visible-cue=\"part(focus)\"/>"
                            "<semantics from=\"child(@editing)\"/>"),
                   DiagnosticCode::control_value_invalid, "semantics from a child that is not the focus target");
    expect_refused(schema,
                   document(std::string(query_children) +
                            "<focus target=\"child(@editing)\"/><semantics from=\"child(@missing)\"/>"),
                   DiagnosticCode::control_reference_unknown, "semantics from no child");
    expect_refused(schema,
                   document(std::string(query_children) +
                            "<focus target=\"child(@editing)\"/><semantics from=\"parent\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "a semantics source that is no child");
    expect_refused(schema, document("<semantics name=\"property(accessible-name)\"/>"),
                   DiagnosticCode::control_attribute_missing, "semantics with neither role nor from");
    expect_refused(schema, document("<part name=\"p\" role=\"face\" paint=\"sprite\" box=\"inset(region, 0)\"/>"),
                   DiagnosticCode::control_vocabulary_unknown, "an unknown paint source");
    expect_refused(schema, document("<part name=\"p\" role=\"region\" paint=\"hook\" box=\"inset(region, 0)\"/>"),
                   DiagnosticCode::control_value_invalid, "a region painted by a hook");
    expect_refused(schema, document("<children><content-text-editor id=\"@editing\" wraps=\"yes\"/></children>"),
                   DiagnosticCode::control_child_invalid, "an editor attribute that is no property");
}

int main() {
    test_toggle_pill();
    test_card_with_action();
    test_stepper_field();
    test_minimal();
    test_document_codes();
    test_declaration_codes();
    test_reference_codes();
    test_warning_and_all_at_once();
    test_query_field_gaps();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "control model tests passed\n";
    return 0;
}
