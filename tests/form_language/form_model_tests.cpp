// Form documents' model (stage FD, phase FD2): a parsed document checked
// against a schema the run-time registry describes. Gate: a valid document
// becomes objects, properties of their kinds and handler references; every
// defect is reported at its place, all of them in one pass, and a refused
// document leaves the previous model as it was.

#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using gui_forms::BindingValue;
using gui_forms::BindingValueKind;
using gui_forms::Dirty;
using gui_forms::PropertyDescriptor;
using gui_forms::PropertyEnumChoice;
using gui_forms::PropertyEnumValue;
using gui_forms::StableId;
using gui_forms::form_language::FormModel;
using gui_forms::form_language::FormModelOutcome;
using gui_forms::form_language::FormObject;
using gui_forms::form_language::FormProperty;
using gui_forms::form_language::FormPropertyProvider;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::FormSchemaType;
using gui_forms::form_language::ObjectIndex;
using gui_forms::form_language::build_form_model;
using gui_forms::form_language::describe_form_type;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSheetForm;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;
using gui_forms::theme_language::diagnostic_code_name;
using gui_forms::theme_language::parse_theme_sheet;
using gui_forms::theme_language::tokenize_theme_sheet;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A schema from fresh instances of a few registry types, as a loader
// builds it from the whole registry.
FormSchema sample_schema() {
    FormSchema schema{};
    const std::shared_ptr<gui_forms::UserControl> user_control =
        gui_forms::make_control<gui_forms::UserControl>(StableId("prototype"));
    const std::shared_ptr<gui_forms::GroupBox> group_box =
        gui_forms::make_control<gui_forms::GroupBox>(StableId("prototype"));
    const std::shared_ptr<gui_forms::CheckBox> check_box =
        gui_forms::make_control<gui_forms::CheckBox>(StableId("prototype"));
    const std::shared_ptr<gui_forms::Button> button = gui_forms::make_control<gui_forms::Button>(StableId("prototype"));
    const std::shared_ptr<gui_forms::Command> command = std::make_shared<gui_forms::Command>();
    // A document refuses a presentation colour (E813); the user control's
    // back colour is marked as content here, so that the colour's value
    // forms stay covered.
    FormSchemaType user_control_type = describe_form_type("user_control", *user_control);
    for (PropertyDescriptor& property : user_control_type.properties) {
        if (property.name == "back_color") property.content_value = true;
    }
    schema.types.push_back(std::move(user_control_type));
    // The group box provides a column span to its children (attached
    // properties, study section 10).
    FormSchemaType group_box_type = describe_form_type("group_box", *group_box);
    group_box_type.child_properties.push_back(PropertyDescriptor{
        "column_span", BindingValueKind::unsigned_integer, "Layout", "The columns the child spans.",
        BindingValue{std::uint64_t{1U}}, Dirty::measure});
    schema.types.push_back(std::move(group_box_type));
    schema.types.push_back(describe_form_type("check_box", *check_box));
    schema.types.push_back(describe_form_type("button", *button));
    schema.types.push_back(describe_form_type("command", *command));
    // A type with a property no document may set.
    FormSchemaType sealed = describe_form_type("sealed_box", *check_box);
    for (PropertyDescriptor& property : sealed.properties) {
        if (property.name == "text") property.writable = false;
    }
    schema.types.push_back(std::move(sealed));
    // A type that holds spans (study 10.9).
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::GridPanel>("grid_panel"));
    return schema;
}

// The caller-owned storage one document fills.
struct Workspace final {
    std::string text{};
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    ThemeDiagnosticList diagnostics{};
};

FormModelOutcome build(Workspace& workspace, std::string_view text, const FormSchema& schema, FormModel& model) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    ThemeSource source{};
    source.file = "test.form";
    source.text = workspace.text;
    static_cast<void>(tokenize_theme_sheet(source, workspace.tokens, workspace.lines, workspace.diagnostics));
    const ParseOutcome parsed = parse_theme_sheet(source, workspace.tokens, workspace.lines, workspace.tree,
                                                  workspace.diagnostics, ThemeSheetForm::form_document);
    require(parsed == ParseOutcome::tree_complete, "the document parses");
    const FormModelOutcome outcome =
        build_form_model(source, workspace.tokens, workspace.lines, workspace.tree, schema, model, workspace.diagnostics);
    return outcome;
}

const FormProperty* find_property(const FormModel& model, const FormSchema& schema, std::uint32_t object,
                                  std::string_view name) {
    for (const FormProperty& property : model.properties) {
        if (property.object.value != object) continue;
        const FormObject& owner = model.objects[object];
        if (schema.types[owner.schema_type].properties[property.schema_property].name == name) return &property;
    }
    return nullptr;
}

std::int64_t choice_value(const FormSchema& schema, std::size_t type, std::string_view property,
                          std::string_view choice) {
    for (const PropertyDescriptor& descriptor : schema.types[type].properties) {
        if (descriptor.name != property || !descriptor.enumeration) continue;
        for (const PropertyEnumChoice& entry : (*descriptor.enumeration).choices) {
            if (entry.name == choice) return entry.value;
        }
    }
    return -1;
}

void test_valid_document() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "user-control settings.root {\n"
                                      "  back-color: #336699;\n"
                                      "  minimum-size: 480 320;\n"
                                      "  group-box settings.general {\n"
                                      "    margin: 1 2 3 4;\n"
                                      "    tab-index: 0;\n"
                                      "    check-box settings.autosave {\n"
                                      "      text: \"Save automatically\";\n"
                                      "      checked: true;\n"
                                      "      check-state: indeterminate;\n"
                                      "      anchor: top left;\n"
                                      "      tab-index: 1;\n"
                                      "      on-checked-changed: autosave_changed;\n"
                                      "    }\n"
                                      "  }\n"
                                      "  button settings.ok { text: \"OK\"; bounds: 10 20 80 24; on-clicked: accept; }\n"
                                      "  command settings.save { on-invoked: save; }\n"
                                      "}\n";
    Workspace workspace{};
    FormModel model{};
    const FormModelOutcome outcome = build(workspace, document, schema, model);
    for (const ThemeDiagnostic& record : workspace.diagnostics.records()) {
        std::cerr << "  " << diagnostic_code_name(record.code) << ' ' << record.position.line.value << ':'
                  << record.position.column.value << ' ' << record.rule << ' ' << record.property << ' '
                  << record.detail << '\n';
    }
    require(outcome == FormModelOutcome::model_built, "valid: built");
    require(model.objects.size() == 5U, "valid: five objects");
    if (model.objects.size() != 5U) return;
    require(model.objects[0].stable_id == "settings.root" && !model.objects[0].parent, "valid: the root");
    const std::vector<ObjectIndex> expected_children{ObjectIndex{1U}, ObjectIndex{3U}, ObjectIndex{4U}};
    require(model.objects[0].children == expected_children, "valid: the root's children in document order");
    require(model.objects[2].parent && (*model.objects[2].parent).value == 1U, "valid: the check box's parent");

    const FormProperty* color = find_property(model, schema, 0U, "back_color");
    require(color != nullptr && std::get<gui_forms::Color>((*color).value) == gui_forms::Color{0x33, 0x66, 0x99, 255},
            "valid: a hash colour");
    const FormProperty* size = find_property(model, schema, 0U, "minimum_size");
    require(size != nullptr && std::get<gui_forms::Size>((*size).value) == gui_forms::Size{480.0, 320.0},
            "valid: a size");
    const FormProperty* margin = find_property(model, schema, 1U, "margin");
    require(margin != nullptr &&
                std::get<gui_forms::Insets>((*margin).value) == gui_forms::Insets{4.0, 1.0, 2.0, 3.0},
            "valid: insets in CSS order");
    const FormProperty* text = find_property(model, schema, 2U, "text");
    require(text != nullptr && std::get<std::string>((*text).value) == "Save automatically", "valid: a text");
    const FormProperty* checked = find_property(model, schema, 2U, "checked");
    require(checked != nullptr && std::get<bool>((*checked).value), "valid: a boolean");
    const FormProperty* tab = find_property(model, schema, 2U, "tab_index");
    require(tab != nullptr && std::get<std::uint64_t>((*tab).value) == 1U, "valid: an unsigned integer");
    const FormProperty* state = find_property(model, schema, 2U, "check_state");
    require(state != nullptr && std::get<PropertyEnumValue>((*state).value).name == "indeterminate",
            "valid: an enumeration");
    const FormProperty* anchor = find_property(model, schema, 2U, "anchor");
    const std::int64_t top_left = choice_value(schema, 2U, "anchor", "top") | choice_value(schema, 2U, "anchor", "left");
    require(anchor != nullptr && std::get<PropertyEnumValue>((*anchor).value).value == top_left,
            "valid: a flags enumeration");
    const FormProperty* bounds = find_property(model, schema, 3U, "bounds");
    require(bounds != nullptr &&
                std::get<gui_forms::Rect>((*bounds).value) == gui_forms::Rect{10.0, 20.0, 80.0, 24.0},
            "valid: a rectangle");

    require(model.handlers.size() == 3U, "valid: three handlers");
    if (model.handlers.size() == 3U) {
        require(model.handlers[0].object.value == 2U && model.handlers[0].interaction == "checked_changed" &&
                    model.handlers[0].handler == "autosave_changed",
                "valid: a handler reference");
        require(model.handlers[1].interaction == "clicked" && model.handlers[1].handler == "accept",
                "valid: the button's handler");
        require(model.handlers[2].object.value == 4U && model.handlers[2].handler == "save",
                "valid: a component's handler");
    }
}

struct ExpectedDiagnostic final {
    DiagnosticCode code{};
    std::uint32_t line{};
    std::uint32_t column{};
};

void check_diagnostics(const Workspace& workspace, std::span<const ExpectedDiagnostic> expected,
                       std::string_view label) {
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    bool same = records.size() == expected.size();
    for (std::size_t index = 0U; same && index < records.size(); ++index) {
        same = records[index].code == expected[index].code && records[index].position.line.value == expected[index].line &&
               records[index].position.column.value == expected[index].column;
    }
    if (!same) {
        std::cerr << "  " << label << ':';
        for (const ThemeDiagnostic& record : records) {
            std::cerr << ' ' << diagnostic_code_name(record.code) << ' ' << record.position.line.value << ':'
                      << record.position.column.value;
        }
        std::cerr << '\n';
    }
    require(same, label);
}

// Every defect of a document is reported in one pass, in document order,
// and the refused document leaves the previous model as it was.
void test_defects_reported_together() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    require(build(workspace, "user-control kept.root { }", schema, model) == FormModelOutcome::model_built,
            "defects: the earlier document builds");
    const std::string_view document = "user-control app.root {\n"                       // 1
                                      "  frobnicate: 1;\n"                              // 2 E804
                                      "  slider app.speed { }\n"                        // 3 E802
                                      "  check-box app.a { checked: maybe; }\n"         // 4 E807
                                      "  check-box app.a { text: \"x\"; text: \"y\"; }\n" // 5 E803 E806
                                      "  check-box app.b { on-explode: boom; }\n"       // 6 E804
                                      "  check-box app.c { on-clicked: two words; }\n"  // 7 E808
                                      "  check-box app.d { on-clicked: has-dash; }\n"   // 8 E808
                                      "  command app.e { button app.f { } }\n"          // 9 E809
                                      "  sealed-box app.g { text: \"no\"; }\n"          // 10 E805
                                      "  check-box app.h { style { } }\n"               // 11 E810
                                      "  check-box app.i { image: x; }\n"               // 12 E999
                                      "  check-box app.j { tab-index: -1; }\n"          // 13 E807
                                      "  check-box app.k { text: \"x\" !important; }\n" // 14 E807
                                      "  check-box app.l { tab_index: 1; }\n"           // 15 E804
                                      "  check-box app.m { fore-color: red; }\n"        // 16 E813
                                      "}\n";
    const std::array<ExpectedDiagnostic, 16> expected{{
        {DiagnosticCode::form_property_unknown, 2U, 3U},
        {DiagnosticCode::form_type_unknown, 3U, 3U},
        {DiagnosticCode::form_value_invalid, 4U, 30U},
        {DiagnosticCode::form_identity_repeated, 5U, 13U},
        {DiagnosticCode::form_member_repeated, 5U, 32U},
        {DiagnosticCode::form_property_unknown, 6U, 21U},
        {DiagnosticCode::form_handler_invalid, 7U, 33U},
        {DiagnosticCode::form_handler_invalid, 8U, 33U},
        {DiagnosticCode::form_children_not_admitted, 9U, 19U},
        {DiagnosticCode::form_property_read_only, 10U, 22U},
        {DiagnosticCode::form_style_block, 11U, 21U},
        {DiagnosticCode::not_yet_built, 12U, 28U},
        {DiagnosticCode::form_value_invalid, 13U, 32U},
        {DiagnosticCode::form_value_invalid, 14U, 21U},
        {DiagnosticCode::form_property_unknown, 15U, 21U},
        {DiagnosticCode::form_presentation_property, 16U, 21U},
    }};
    require(build(workspace, document, schema, model) == FormModelOutcome::refused, "defects: refused");
    check_diagnostics(workspace, expected, "defects: every defect at its place, in order");
    require(model.objects.size() == 1U && model.objects[0].stable_id == "kept.root",
            "defects: the previous model is kept");
}

void test_roots() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    require(build(workspace, "/* nothing */\n", schema, model) == FormModelOutcome::refused, "roots: none refused");
    const std::array<ExpectedDiagnostic, 1> none{{{DiagnosticCode::form_root_invalid, 2U, 1U}}};
    check_diagnostics(workspace, none, "roots: no root reported at the end");
    require(build(workspace, "button a.b { }\nbutton a.c { }\n", schema, model) == FormModelOutcome::refused,
            "roots: two refused");
    const std::array<ExpectedDiagnostic, 1> two{{{DiagnosticCode::form_root_invalid, 2U, 1U}}};
    check_diagnostics(workspace, two, "roots: the second root reported");
}

// A child writes the properties its parent's type provides in its own
// block; anywhere else they are unknown, and they repeat and refuse values
// as the object's own do.
void test_attached_properties() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view document = "user-control app.root {\n"
                                      "  group-box app.group {\n"
                                      "    check-box app.a { column-span: 2; text: \"A\"; }\n"
                                      "  }\n"
                                      "}\n";
    require(build(workspace, document, schema, model) == FormModelOutcome::model_built, "attached: built");
    const bool recorded = model.properties.size() == 2U &&
                          model.properties[0].provider == FormPropertyProvider::parent_type &&
                          model.properties[0].schema_property == 0U && model.properties[0].object.value == 2U &&
                          std::get<std::uint64_t>(model.properties[0].value) == 2U &&
                          model.properties[1].provider == FormPropertyProvider::object_type;
    require(recorded, "attached: recorded as the parent type's, then the object's own");

    const std::string_view defects = "group-box app.root {\n"                                     // 1
                                     "  column-span: 2;\n"                                         // 2 E804
                                     "  check-box app.a { column-span: 2; column-span: 3; }\n"    // 3 E806
                                     "  check-box app.b { column-span: -1; }\n"                   // 4 E807
                                     "  group-box app.c { check-box app.d { column-span: 1; } }\n" // 5
                                     "}\n";
    const std::array<ExpectedDiagnostic, 3> expected{{
        {DiagnosticCode::form_property_unknown, 2U, 3U},
        {DiagnosticCode::form_member_repeated, 3U, 37U},
        {DiagnosticCode::form_value_invalid, 4U, 34U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "attached: defects refused");
    check_diagnostics(workspace, expected, "attached: a root's, a repeated and a bad value, each located");

    const std::string_view stranger = "user-control app.root {\n"
                                      "  check-box app.a { column-span: 2; }\n"
                                      "}\n";
    require(build(workspace, stranger, schema, model) == FormModelOutcome::refused, "attached: wrong parent refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool named = !records.empty() && records.back().code == DiagnosticCode::form_property_unknown &&
                       records.back().detail.find("nor its parent's type user_control") != std::string::npos;
    require(named, "attached: a parent that does not provide it is named");
}

// Spans (study 10.9): `span` blocks inside a type that holds them, the
// placements checked against them, every conflict located.
void test_spans() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view document = "grid-panel app.root {\n"
                                      "  span footer { anchor: 2 0; columns: 3; }\n"
                                      "  span { anchor: 0 2; rows: 2; }\n"
                                      "  check-box app.title { cell: 0 0; text: \"T\"; }\n"
                                      "  check-box app.ok { area: \"footer\"; }\n"
                                      "}\n";
    require(build(workspace, document, schema, model) == FormModelOutcome::model_built, "spans: built");
    const gui_forms::GridSpan footer{"footer", gui_forms::GridCell{2U, 0U}, 1U, 3U};
    const gui_forms::GridSpan region{"", gui_forms::GridCell{0U, 2U}, 2U, 1U};
    const bool recorded = model.spans.size() == 2U && model.spans[0].span == footer &&
                          model.spans[1].span == region && model.spans[0].owner.value == 0U &&
                          model.objects.size() == 3U && model.properties.size() == 3U;
    require(recorded, "spans: recorded on their container, not as objects");

    const std::string_view defects = "grid-panel app.root {\n"                        // 1
                                     "  span footer { anchor: 2 0; columns: 3; }\n"    // 2
                                     "  span footer { anchor: 0 0; }\n"                // 3 E806
                                     "  span { anchor: 2 1; }\n"                       // 4 E814
                                     "  span { rows: 2; }\n"                           // 5 E807
                                     "  span { anchor: 0 2; sides: 1; }\n"             // 6 E804
                                     "  check-box app.a { area: \"nowhere\"; }\n"     // 7 E814
                                     "  check-box app.b { cell: 2 1; }\n"              // 8 E814
                                     "  group-box app.c { span { anchor: 0 0; } }\n"   // 9 E809
                                     "}\n";
    const std::array<ExpectedDiagnostic, 7> expected{{
        {DiagnosticCode::form_value_invalid, 5U, 3U},
        {DiagnosticCode::form_property_unknown, 6U, 23U},
        {DiagnosticCode::form_children_not_admitted, 9U, 21U},
        {DiagnosticCode::form_member_repeated, 3U, 3U},
        {DiagnosticCode::form_span_conflict, 4U, 3U},
        {DiagnosticCode::form_span_conflict, 7U, 27U},
        {DiagnosticCode::form_span_conflict, 8U, 27U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "spans: defects refused");
    check_diagnostics(workspace, expected, "spans: every conflict located");
}

} // namespace

int main() {
    test_valid_document();
    test_defects_reported_together();
    test_roots();
    test_attached_properties();
    test_spans();
    if (failures != 0) {
        std::cerr << failures << " form model checks failed\n";
        return 1;
    }
    std::cout << "form model: objects, values of every expressible kind, handlers, located defects, refusal and "
                 "roots passed\n";
    return 0;
}
