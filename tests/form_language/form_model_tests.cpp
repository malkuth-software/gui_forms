// Form documents' model (stage FD, phase FD2; XML since cp34): a read
// document checked against a schema the run-time registry describes. Gate: a valid document
// becomes objects, properties of their kinds and bindings (FD-6); every
// defect is reported at its place, all of them in one pass, and a refused
// document leaves the previous model as it was.

#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/form_language/form_xml/form_xml.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/form_binding/form_binding.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/binding/value/binding_value_kind/binding_value_kind.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/grid_panel.hpp"
#include "gui_forms/controls/group_box.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/layout/grid_span/grid_span.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

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
using gui_forms::form_language::FormSyntax;
using gui_forms::form_language::ObjectIndex;
using gui_forms::form_language::read_form_model;
using gui_forms::form_language::FormStringReading;
using gui_forms::form_language::describe_form_type;
using gui_forms::form_language::quote_form_string;
using gui_forms::form_language::read_form_string;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::diagnostic_code_name;

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
    // Described with its creator, so that it has its Command slot.
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::Button>("button"));
    schema.types.push_back(describe_form_type("command", *command));
    // A type with a property no document may set.
    FormSchemaType sealed = describe_form_type("sealed_box", *check_box);
    for (PropertyDescriptor& property : sealed.properties) {
        if (property.name == "text") property.writable = false;
    }
    schema.types.push_back(std::move(sealed));
    // A type that holds spans (study 10.9).
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::GridPanel>("grid_panel"));
    // A text a user edits: two way by its property's default.
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::TextBox>("text_box"));
    return schema;
}

// The caller-owned storage one document fills.
struct Workspace final {
    std::string text{};
    FormSyntax syntax{};
    ThemeDiagnosticList diagnostics{};
};

FormModelOutcome build(Workspace& workspace, std::string_view text, const FormSchema& schema, FormModel& model) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    ThemeSource source{};
    source.file = "test.form";
    source.text = workspace.text;
    const FormModelOutcome outcome = read_form_model(source, workspace.syntax, schema, model, workspace.diagnostics);
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
    const std::string_view document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- settings.form: a comment before the root -->\n"
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"settings.root\" back-color=\"#336699\"\n"
        "    minimum-size=\"480 320\">\n"
        "  <group-box id=\"settings.general\" margin=\"1 2 3 4\" tab-index=\"0\">\n"
        "    <check-box id=\"settings.autosave\"\n"
        "        text=\"Save automatically\"\n"
        "        checked=\"true\"\n"
        "        check-state=\"indeterminate\"\n"
        "        anchor=\"top left\"\n"
        "        tab-index=\"1\"/>\n"
        "  </group-box>\n"
        "  <!-- a comment between objects -->\n"
        "  <button id=\"settings.ok\" text=\"OK\" bounds=\"10 20 80 24\" command=\"bind(accept_)\"/>\n"
        "  <command id=\"settings.save\"/>\n"
        "</user-control>\n";
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
    require(model.objects[0].element == 0U && model.objects[2].element == 2U && model.objects[4].element == 4U,
            "valid: each object's element in document order");
    require(model.objects[0].position.line.value == 3U && model.objects[0].position.column.value == 1U &&
                model.objects[2].position.line.value == 6U && model.objects[2].position.column.value == 5U,
            "valid: each object located at its start tag");

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

    const FormProperty* command = find_property(model, schema, 3U, "command");
    require(command != nullptr && (*command).binding && (*(*command).binding).source == "accept_" &&
                (*(*command).binding).field.empty(),
            "valid: the button's Command slot, bound");
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
    require(build(workspace, "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"kept.root\"/>", schema, model) ==
                FormModelOutcome::model_built,
            "defects: the earlier document builds");
    const std::string_view document =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"      // 1
        "  <check-box id=\"app.z\" frobnicate=\"1\"/>\n"                      // 2 E804
        "  <slider id=\"app.speed\"/>\n"                                        // 3 E802
        "  <check-box id=\"app.a\" checked=\"maybe\"/>\n"                     // 4 E807
        "  <check-box id=\"app.a\" text=\"x\"/>\n"                            // 5 E803
        "  <check-box id=\"app.b\" on-explode=\"boom\"/>\n"                   // 6 E816
        "  <check-box id=\"app.c\" checked=\"bind(two words)\"/>\n"           // 7 E817
        "  <check-box id=\"app.d\" checked=\"bind(on, sometimes)\"/>\n"       // 8 E818
        "  <command id=\"app.e\"><button id=\"app.f\"/></command>\n"          // 9 E809
        "  <sealed-box id=\"app.g\" text=\"no\"/>\n"                          // 10 E805
        "  <check-box id=\"app.h\"><style/></check-box>\n"                     // 11 E810
        "  <check-box id=\"app.i\" image=\"x\"/>\n"                           // 12 E999
        "  <check-box id=\"app.j\" tab-index=\"-1\"/>\n"                      // 13 E807
        "  <check-box id=\"app.k\" tab_index=\"1\"/>\n"                       // 14 E804
        "  <check-box id=\"app.l\" fore-color=\"red\"/>\n"                    // 15 E813
        "  <check-box id=\"app.m\" text=\"x\">stray</check-box>\n"            // 16 E821
        "  <check-box text=\"no id\"/>\n"                                       // 17 E801
        "  <check-box id=\"app.n\" xmlns=\"urn:gui-forms:form:1\"/>\n"        // 18 E821
        "</user-control>\n";
    const std::array<ExpectedDiagnostic, 17> expected{{
        {DiagnosticCode::form_property_unknown, 2U, 25U},
        {DiagnosticCode::form_type_unknown, 3U, 3U},
        {DiagnosticCode::form_value_invalid, 4U, 34U},
        {DiagnosticCode::form_identity_repeated, 5U, 3U},
        {DiagnosticCode::form_handler_removed, 6U, 25U},
        {DiagnosticCode::form_binding_invalid, 7U, 43U},
        {DiagnosticCode::form_binding_option_invalid, 8U, 43U},
        {DiagnosticCode::form_children_not_admitted, 9U, 23U},
        {DiagnosticCode::form_property_read_only, 10U, 26U},
        {DiagnosticCode::form_style_block, 11U, 25U},
        {DiagnosticCode::not_yet_built, 12U, 32U},
        {DiagnosticCode::form_value_invalid, 13U, 36U},
        {DiagnosticCode::form_property_unknown, 14U, 25U},
        {DiagnosticCode::form_presentation_property, 15U, 25U},
        {DiagnosticCode::form_xml_invalid, 16U, 3U},
        {DiagnosticCode::form_root_invalid, 17U, 3U},
        {DiagnosticCode::form_xml_invalid, 18U, 25U},
    }};
    require(build(workspace, document, schema, model) == FormModelOutcome::refused, "defects: refused");
    check_diagnostics(workspace, expected, "defects: every defect at its place, in order");
    require(model.objects.size() == 1U && model.objects[0].stable_id == "kept.root",
            "defects: the previous model is kept");
}

// Bindings (owner rulings FD-6 R1 and R6): the direction is the property's
// default unless the document narrows it to one way; a two-way text bound
// to a field takes an update trigger; a Command slot takes its source alone.
void test_bindings() {
    const FormSchema schema = sample_schema();
    const std::string_view document =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
        "  <check-box id=\"app.a\" checked=\"bind(flag_)\"/>\n"
        "  <check-box id=\"app.b\" checked=\"bind(flag_, one_way)\"/>\n"
        "  <button id=\"app.c\" command=\"bind(save_)\"/>\n"
        "  <text-box id=\"app.d\" text=\"bind(names_, 'name', update on-change)\"/>\n"
        "  <check-box id=\"app.e\" text='bind(names_, \"title\")'/>\n"
        "</user-control>\n";
    Workspace workspace{};
    FormModel model{};
    const FormModelOutcome outcome = build(workspace, document, schema, model);
    const std::array<ExpectedDiagnostic, 0> none{};
    check_diagnostics(workspace, none, "bindings: no diagnostics");
    require(outcome == FormModelOutcome::model_built && model.objects.size() == 6U, "bindings: built");
    if (outcome != FormModelOutcome::model_built || model.objects.size() != 6U) return;
    const FormProperty* two_way = find_property(model, schema, 1U, "checked");
    const FormProperty* narrowed = find_property(model, schema, 2U, "checked");
    const FormProperty* command = find_property(model, schema, 3U, "command");
    const FormProperty* edited = find_property(model, schema, 4U, "text");
    const FormProperty* shown = find_property(model, schema, 5U, "text");
    require(two_way != nullptr && narrowed != nullptr && command != nullptr && edited != nullptr && shown != nullptr &&
                (*two_way).binding && (*narrowed).binding && (*command).binding && (*edited).binding &&
                (*shown).binding,
            "bindings: each bound property recorded with its binding");
    if (two_way == nullptr || narrowed == nullptr || command == nullptr || edited == nullptr || shown == nullptr ||
        !(*two_way).binding || !(*narrowed).binding || !(*command).binding || !(*edited).binding ||
        !(*shown).binding) {
        return;
    }
    using gui_forms::FormUpdateTrigger;
    using gui_forms::PropertyBindingDirection;
    require((*(*two_way).binding).source == "flag_" && (*(*two_way).binding).field.empty() &&
                (*(*two_way).binding).direction == PropertyBindingDirection::two_way,
            "bindings: a Value, two way by the check box's default");
    require((*(*narrowed).binding).direction == PropertyBindingDirection::one_way, "bindings: narrowed to one way");
    require((*(*command).binding).source == "save_", "bindings: the Command slot");
    require((*(*edited).binding).field == "name" &&
                (*(*edited).binding).direction == PropertyBindingDirection::two_way &&
                (*(*edited).binding).trigger == FormUpdateTrigger::on_change,
            "bindings: a field, two way by the text box's default, written back on every change");
    require((*(*shown).binding).field == "title" &&
                (*(*shown).binding).direction == PropertyBindingDirection::one_way &&
                (*(*shown).binding).trigger == FormUpdateTrigger::on_validate,
            "bindings: a field, one way by the check box text's default");

    const std::string_view defects =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"                  // 1
        "  <check-box id=\"app.a\" on-checked-changed=\"changed\"/>\n"                     // 2 E816
        "  <check-box id=\"app.b\" text=\"bind(names_)\"/>\n"                              // 3 E817
        "  <check-box id=\"app.c\" checked=\"bind(flag_, one_way, one_way)\"/>\n"          // 4 E818
        "  <check-box id=\"app.d\" text=\"bind(names_, 'n', update on-change)\"/>\n"       // 5 E818
        "  <button id=\"app.e\" command=\"bind(save_, 'field')\"/>\n"                      // 6 E817
        "  <check-box id=\"app.f\" checked=\"bind(flag_, convert x)\"/>\n"                 // 7 E999
        "  <check-box id=\"app.g\" checked=\"bind(flag_, update sometimes)\"/>\n"          // 8 E818
        "  <check-box id=\"app.h\" tab-index=\"bind(n_)\"/>\n"                             // 9 E817, not bindable
        "  <check-box id=\"app.i\" checked=\"bind(flag_, 'bad \\q')\"/>\n"               // 10 E815
        "</user-control>\n";
    const std::array<ExpectedDiagnostic, 9> expected{{
        {DiagnosticCode::form_handler_removed, 2U, 25U},
        {DiagnosticCode::form_binding_invalid, 3U, 36U},
        {DiagnosticCode::form_binding_option_invalid, 4U, 55U},
        {DiagnosticCode::form_binding_option_invalid, 5U, 49U},
        {DiagnosticCode::form_binding_invalid, 6U, 36U},
        {DiagnosticCode::not_yet_built, 7U, 46U},
        {DiagnosticCode::form_binding_option_invalid, 8U, 53U},
        {DiagnosticCode::form_binding_invalid, 9U, 36U},
        {DiagnosticCode::form_escape_invalid, 10U, 46U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "bindings: defects refused");
    check_diagnostics(workspace, expected, "bindings: every defect at its token, in order");
}

// Whether the diagnostics are one E821 (form_xml_invalid), wherever the
// reader found the defect.
bool one_xml_refusal(const Workspace& workspace) {
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool one = records.size() == 1U && records[0].code == DiagnosticCode::form_xml_invalid;
    return one;
}

// A document is one root element in the bounded XML subset, declaring the
// form namespace; anything else is E821 and builds nothing.
void test_roots() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    require(build(workspace, "<!-- nothing -->\n", schema, model) == FormModelOutcome::refused &&
                one_xml_refusal(workspace),
            "roots: none refused by the reader");
    require(build(workspace, "<button id=\"a.b\"/>\n<button id=\"a.c\"/>\n", schema, model) ==
                    FormModelOutcome::refused &&
                one_xml_refusal(workspace),
            "roots: two refused by the reader");
    require(build(workspace, "<button xmlns=\"urn:gui-forms:form:1\" id=\"a.b\">\n", schema, model) ==
                    FormModelOutcome::refused &&
                one_xml_refusal(workspace),
            "roots: an unclosed element refused by the reader");
    require(build(workspace, "<panel xmlns=\"urn:gui-forms:form:1\" id=\"a.b\"><![CDATA[x]]></panel>", schema,
                  model) == FormModelOutcome::refused &&
                one_xml_refusal(workspace),
            "roots: CDATA, outside the subset, refused by the reader");
    require(build(workspace, "<button id=\"a.b\"/>", schema, model) == FormModelOutcome::refused,
            "roots: no namespace");
    const std::array<ExpectedDiagnostic, 1> missing{{{DiagnosticCode::form_xml_invalid, 1U, 1U}}};
    check_diagnostics(workspace, missing, "roots: a missing namespace reported at the root");
    require(build(workspace, "<button xmlns=\"urn:gui-forms:form:2\" id=\"a.b\"/>", schema, model) ==
                FormModelOutcome::refused,
            "roots: another namespace");
    check_diagnostics(workspace, missing, "roots: another namespace reported at the root");
    require(build(workspace, "<button xmlns=\"urn:gui-forms:form:1\" xmlns:x=\"urn:x\" id=\"a.b\"/>", schema,
                  model) == FormModelOutcome::refused,
            "roots: a prefix declared");
    const std::array<ExpectedDiagnostic, 1> prefixed{{{DiagnosticCode::form_xml_invalid, 1U, 38U}}};
    check_diagnostics(workspace, prefixed, "roots: a prefix declaration reported at it");
}

// A child writes the properties its parent's type provides in its own
// block; anywhere else they are unknown, and they repeat and refuse values
// as the object's own do.
void test_attached_properties() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view document = "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
                                      "  <group-box id=\"app.group\">\n"
                                      "    <check-box id=\"app.a\" column-span=\"2\" text=\"A\"/>\n"
                                      "  </group-box>\n"
                                      "</user-control>\n";
    require(build(workspace, document, schema, model) == FormModelOutcome::model_built, "attached: built");
    // The object's own properties come first, then those its parent
    // provides, in the parent's table order (ProvidedPropertyOrder).
    const bool recorded = model.properties.size() == 2U &&
                          model.properties[0].provider == FormPropertyProvider::object_type &&
                          model.properties[1].provider == FormPropertyProvider::parent_type &&
                          model.properties[1].schema_property == 0U && model.properties[1].object.value == 2U &&
                          std::get<std::uint64_t>(model.properties[1].value) == 2U;
    require(recorded, "attached: recorded as the object's own, then the parent type's");

    // A child writing a provided property twice is an XML defect (one
    // attribute per name), so E806 cannot arise here.
    const std::string_view defects =
        "<group-box xmlns=\"urn:gui-forms:form:1\" id=\"app.root\" column-span=\"2\">\n"          // 1 E804
        "  <check-box id=\"app.b\" column-span=\"-1\"/>\n"                                       // 2 E807
        "  <group-box id=\"app.c\"><check-box id=\"app.d\" column-span=\"1\"/></group-box>\n"  // 3
        "</group-box>\n";
    const std::array<ExpectedDiagnostic, 2> expected{{
        {DiagnosticCode::form_property_unknown, 1U, 55U},
        {DiagnosticCode::form_value_invalid, 2U, 38U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "attached: defects refused");
    check_diagnostics(workspace, expected, "attached: a root's and a bad value, each located");

    const std::string_view stranger = "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
                                      "  <check-box id=\"app.a\" column-span=\"2\"/>\n"
                                      "</user-control>\n";
    require(build(workspace, stranger, schema, model) == FormModelOutcome::refused, "attached: wrong parent refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool named = !records.empty() && records.back().code == DiagnosticCode::form_property_unknown &&
                       records.back().detail.find("nor its parent's type user_control") != std::string::npos;
    require(named, "attached: a parent that does not provide it is named");
}

// Spans (study 10.9): `span` elements inside a type that holds them, the
// placements checked against them, every conflict located.
void test_spans() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view document = "<grid-panel xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
                                      "  <span name=\"footer\" anchor=\"2 0\" columns=\"3\"/>\n"
                                      "  <span anchor=\"0 2\" rows=\"2\"/>\n"
                                      "  <check-box id=\"app.title\" cell=\"0 0\" text=\"T\"/>\n"
                                      "  <check-box id=\"app.ok\" area=\"footer\"/>\n"
                                      "</grid-panel>\n";
    require(build(workspace, document, schema, model) == FormModelOutcome::model_built, "spans: built");
    const gui_forms::GridSpan footer{"footer", gui_forms::GridCell{2U, 0U}, 1U, 3U};
    const gui_forms::GridSpan region{"", gui_forms::GridCell{0U, 2U}, 2U, 1U};
    const bool recorded = model.spans.size() == 2U && model.spans[0].span == footer &&
                          model.spans[1].span == region && model.spans[0].owner.value == 0U &&
                          model.objects.size() == 3U && model.properties.size() == 3U &&
                          model.spans[0].element == 1U && model.spans[1].position.line.value == 3U;
    require(recorded, "spans: recorded on their container, not as objects");

    const std::string_view defects =
        "<grid-panel xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"   // 1
        "  <span name=\"footer\" anchor=\"2 0\" columns=\"3\"/>\n"      // 2
        "  <span name=\"footer\" anchor=\"0 0\"/>\n"                      // 3 E806
        "  <span anchor=\"2 1\"/>\n"                                       // 4 E814
        "  <span rows=\"2\"/>\n"                                           // 5 E807
        "  <span anchor=\"0 2\" sides=\"1\"/>\n"                          // 6 E804
        "  <check-box id=\"app.a\" area=\"nowhere\"/>\n"                  // 7 E814
        "  <check-box id=\"app.b\" cell=\"2 1\"/>\n"                      // 8 E814
        "  <group-box id=\"app.c\"><span anchor=\"0 0\"/></group-box>\n"  // 9 E809
        "  <span anchor=\"9 9\" name=\"Bad\"/>\n"                         // 10 E807
        "</grid-panel>\n";
    const std::array<ExpectedDiagnostic, 8> expected{{
        {DiagnosticCode::form_value_invalid, 5U, 3U},
        {DiagnosticCode::form_property_unknown, 6U, 22U},
        {DiagnosticCode::form_children_not_admitted, 9U, 25U},
        {DiagnosticCode::form_value_invalid, 10U, 28U},
        {DiagnosticCode::form_member_repeated, 3U, 3U},
        {DiagnosticCode::form_span_conflict, 4U, 3U},
        {DiagnosticCode::form_span_conflict, 7U, 25U},
        {DiagnosticCode::form_span_conflict, 8U, 25U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "spans: defects refused");
    check_diagnostics(workspace, expected, "spans: every conflict located");
}

// Text (form_model.hpp): an attribute value as XML decodes it, entities
// and character references included, less one backslash before `bind(`.
// String escapes (form_model.hpp, Strings) apply to a collection's quoted
// items and a binding's field: \n, \r, \t, the quotes, the backslash and
// \u{H} read as their characters; any other escape refuses the document
// with E815 at the string, its detail the escape as written;
// quote_form_string writes what read_form_string reads back.
void test_string_escapes() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view document =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
        "  <button id=\"app.ok\" text=\"One&#10;two&#9;&quot;q&quot; \\ &#x3A9;&#x1F680;&amp;&lt;\"/>\n"
        "  <button id=\"app.single\" text='it&apos;s \"fine\"'/>\n"
        "  <button id=\"app.call\" text=\"\\bind(x) is a call\"/>\n"
        "  <button id=\"app.calls\" text=\"\\\\bind(y)\"/>\n"
        "  <button id=\"app.items\" classes=\"plain 'two words' 'it\\'s' '\\u{3A9}' &quot;q&quot;\"/>\n"
        "</user-control>\n";
    require(build(workspace, document, schema, model) == FormModelOutcome::model_built, "escapes: built");
    const FormProperty* text = find_property(model, schema, 1U, "text");
    require(text != nullptr &&
                std::get<std::string>((*text).value) == "One\ntwo\t\"q\" \\ \xCE\xA9\xF0\x9F\x9A\x80&<",
            "escapes: XML's entities and character references read, a backslash kept");
    const FormProperty* single = find_property(model, schema, 2U, "text");
    require(single != nullptr && std::get<std::string>((*single).value) == "it's \"fine\"",
            "escapes: a single-quoted attribute");
    const FormProperty* call = find_property(model, schema, 3U, "text");
    require(call != nullptr && std::get<std::string>((*call).value) == "bind(x) is a call",
            "escapes: one backslash before bind( removed");
    const FormProperty* calls = find_property(model, schema, 4U, "text");
    require(calls != nullptr && std::get<std::string>((*calls).value) == "\\bind(y)",
            "escapes: one of two backslashes removed");
    const FormProperty* items = find_property(model, schema, 5U, "classes");
    bool listed = items != nullptr;
    if (listed) {
        const std::span<const BindingValue> read =
            gui_forms::property_collection_items(std::get<gui_forms::PropertyCollectionValue>((*items).value));
        const std::array<std::string_view, 5> expected_items{"plain", "two words", "it's", "\xCE\xA9", "q"};
        listed = read.size() == expected_items.size();
        for (std::size_t index = 0U; listed && index < read.size(); ++index) {
            listed = std::get<std::string>(read[index]) == expected_items[index];
        }
    }
    require(listed, "escapes: a collection's items, quoted where needed");

    const std::string_view defects =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"   // 1
        "  <button id=\"app.a\" classes=\"ok 'bad \\q'\"/>\n"            // 2 E815
        "  <button id=\"app.b\" classes=\"'\\u{D800}'\"/>\n"             // 3 E815
        "  <button id=\"app.c\" classes=\"'\\u{110000}'\"/>\n"           // 4 E815
        "  <button id=\"app.d\" classes=\"'\\u{}'\"/>\n"                 // 5 E815
        "  <button id=\"app.e\" classes=\"'\\x41'\"/>\n"                 // 6 E815
        "  <button id=\"app.f\" classes=\"'open\"/>\n"                     // 7 E807, not closed
        "  <button id=\"app.g\" classes=\"'a'b\"/>\n"                      // 8 E807, not separated
        "</user-control>\n";
    const std::array<ExpectedDiagnostic, 7> expected{{
        {DiagnosticCode::form_escape_invalid, 2U, 34U},
        {DiagnosticCode::form_escape_invalid, 3U, 31U},
        {DiagnosticCode::form_escape_invalid, 4U, 31U},
        {DiagnosticCode::form_escape_invalid, 5U, 31U},
        {DiagnosticCode::form_escape_invalid, 6U, 31U},
        {DiagnosticCode::form_value_invalid, 7U, 31U},
        {DiagnosticCode::form_value_invalid, 8U, 31U},
    }};
    require(build(workspace, defects, schema, model) == FormModelOutcome::refused, "escapes: defects refused");
    check_diagnostics(workspace, expected, "escapes: every refused escape located");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    require(records.size() == 7U && records[0].detail == "\\q" && records[1].detail == "\\u{D800}" &&
                records[3].detail == "\\u{}" && records[4].detail == "\\x",
            "escapes: the detail is the escape as written");

    const std::array<std::string_view, 6> texts{
        "plain", "", "a \"quote\" and a \\ backslash", "two\nlines\r\n\ttabbed",
        "\x01\x1F\x7F controls", "Graphemes: a\xCC\x81 \xCE\xA9 \xF0\x9F\x9A\x80"};
    for (const std::string_view original : texts) {
        const std::string quoted = quote_form_string(original);
        const FormStringReading reading = read_form_string(quoted);
        require(reading.valid && reading.text == original, "escapes: quote_form_string round-trips");
    }
    require(quote_form_string("a\nb") == "\"a\\nb\"", "escapes: a line feed is written as \\n");
    require(quote_form_string("\x01") == "\"\\u{1}\"", "escapes: a control byte is written as \\u{H}");
    require(!read_form_string("no quotes").valid, "escapes: a token without quotes is refused");
}

} // namespace

// Owner ruling of 2026-11-11: a form writes Windows Forms' RightToLeft
// as `right-to-left` (no, yes, inherit); `layout-direction`, its storage,
// is refused (E820) with the spelling to write and the value mapping.
void test_right_to_left_spelling() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    FormModel model{};
    const std::string_view accepted = "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"
                                      "  <check-box id=\"app.a\" right-to-left=\"yes\"/>\n"
                                      "</user-control>\n";
    require(build(workspace, accepted, schema, model) == FormModelOutcome::model_built,
            "right-to-left: the RightToLeft spelling builds");
    const FormProperty* spelled = find_property(model, schema, 1U, "right_to_left");
    require(spelled != nullptr && std::get<PropertyEnumValue>((*spelled).value).name == "yes" &&
                std::get<PropertyEnumValue>((*spelled).value).value == 1,
            "right-to-left: `yes` is RightToLeft.Yes");
    const std::string_view refused =
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"app.root\">\n"         // 1
        "  <check-box id=\"app.b\" layout-direction=\"right-to-left\"/>\n"        // 2 E820
        "</user-control>\n";
    const std::array<ExpectedDiagnostic, 1> expected{{
        {DiagnosticCode::form_property_renamed, 2U, 25U},
    }};
    require(build(workspace, refused, schema, model) == FormModelOutcome::refused,
            "right-to-left: layout-direction is refused");
    check_diagnostics(workspace, expected, "right-to-left: one E820 at the property");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    require(records.size() == 1U && records[0].detail.find("right-to-left") != std::string::npos &&
                records[0].detail.find("left-to-right is no") != std::string::npos &&
                records[0].detail.find("right-to-left is yes") != std::string::npos,
            "right-to-left: the refusal names the spelling and the value mapping");
}

int main() {
    test_valid_document();
    test_right_to_left_spelling();
    test_defects_reported_together();
    test_bindings();
    test_roots();
    test_attached_properties();
    test_spans();
    test_string_escapes();
    if (failures != 0) {
        std::cerr << failures << " form model checks failed\n";
        return 1;
    }
    std::cout << "form model: XML objects, values of every expressible kind, bindings, located defects, refusal, "
                 "roots and namespaces, text and string escapes and the RightToLeft spelling passed\n";
    return 0;
}
