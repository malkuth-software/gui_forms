// Form documents' run-time loader (stage FD, phase FD3): a checked model
// made into controls and components. Gate: the tree, child order and
// property values are the document's; a refusal by a run-time type is
// located and leaves the previous load as it was.

#include "../../src/core/form_language/form_loader/form_loader.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace {

using gui_forms::Control;
using gui_forms::form_language::FormLoadOutcome;
using gui_forms::form_language::FormModel;
using gui_forms::form_language::FormModelOutcome;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::FormSchemaType;
using gui_forms::form_language::LoadedForm;
using gui_forms::form_language::build_form_model;
using gui_forms::form_language::describe_form_type;
using gui_forms::form_language::form_component_type;
using gui_forms::form_language::form_control_type;
using gui_forms::form_language::load_form;
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

FormSchema sample_schema() {
    FormSchema schema{};
    // A document refuses a presentation colour (E813); the user control's
    // back colour is marked as content here, so that loading a colour stays
    // covered.
    FormSchemaType user_control_type = form_control_type<gui_forms::UserControl>("user_control");
    for (gui_forms::PropertyDescriptor& property : user_control_type.properties) {
        if (property.name == "back_color") property.content_value = true;
    }
    schema.types.push_back(std::move(user_control_type));
    schema.types.push_back(form_control_type<gui_forms::GroupBox>("group_box"));
    schema.types.push_back(form_control_type<gui_forms::CheckBox>("check_box"));
    schema.types.push_back(form_control_type<gui_forms::Button>("button"));
    schema.types.push_back(form_component_type<gui_forms::Command>("command"));
    // A type a document may describe but the loader cannot make.
    const std::shared_ptr<gui_forms::Panel> panel = gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("prototype"));
    schema.types.push_back(describe_form_type("panel", *panel));
    schema.types.push_back(gui_forms::form_language::form_root_type());
    // A type that holds spans (study 10.9).
    schema.types.push_back(form_control_type<gui_forms::GridPanel>("grid_panel"));
    return schema;
}

// The caller-owned storage one document fills.
struct Workspace final {
    std::string text{};
    ThemeSource source{};
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeSyntaxTree tree{};
    FormModel model{};
    ThemeDiagnosticList diagnostics{};
};

FormLoadOutcome load(Workspace& workspace, std::string_view text, const FormSchema& schema, LoadedForm& loaded) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    workspace.source.file = "test.form";
    workspace.source.text = workspace.text;
    static_cast<void>(
        tokenize_theme_sheet(workspace.source, workspace.tokens, workspace.lines, workspace.diagnostics));
    const ParseOutcome parsed = parse_theme_sheet(workspace.source, workspace.tokens, workspace.lines,
                                                  workspace.tree, workspace.diagnostics,
                                                  ThemeSheetForm::form_document);
    require(parsed == ParseOutcome::tree_complete, "the document parses");
    const FormModelOutcome built = build_form_model(workspace.source, workspace.tokens, workspace.lines,
                                                    workspace.tree, schema, workspace.model, workspace.diagnostics);
    require(built == FormModelOutcome::model_built, "the document's model builds");
    const FormLoadOutcome outcome = load_form(workspace.source, workspace.tokens, workspace.lines, workspace.tree,
                                              schema, workspace.model, loaded, workspace.diagnostics);
    return outcome;
}

void print_diagnostics(const Workspace& workspace) {
    for (const ThemeDiagnostic& record : workspace.diagnostics.records()) {
        std::cerr << "  " << diagnostic_code_name(record.code) << ' ' << record.position.line.value << ':'
                  << record.position.column.value << ' ' << record.rule << ' ' << record.property << ' '
                  << record.detail << '\n';
    }
}

void test_load() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "user-control settings.root {\n"
                                      "  back-color: #336699;\n"
                                      "  group-box settings.general {\n"
                                      "    check-box settings.autosave {\n"
                                      "      text: \"Save automatically\";\n"
                                      "      checked: true;\n"
                                      "      tab-index: 2;\n"
                                      "      on-checked-changed: autosave_changed;\n"
                                      "    }\n"
                                      "    button settings.apply { text: \"Apply\"; }\n"
                                      "  }\n"
                                      "  command settings.save { on-invoked: save; }\n"
                                      "  button settings.ok { text: \"OK\"; on-clicked: accept; }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded, "load: loaded");
    require(loaded.root != nullptr && (*loaded.root).stable_id().value() == "settings.root", "load: the root");
    require(loaded.objects.size() == 6U, "load: every object");
    if (loaded.root == nullptr || loaded.objects.size() != 6U) return;
    require((*loaded.root).back_color.read() == gui_forms::Color{0x33, 0x66, 0x99, 255}, "load: the root's colour");

    // Child order is document order; the command is the form's, not a child.
    const std::span<const Control::Ptr> root_children = (*loaded.root).children();
    require(root_children.size() == 2U && root_children[0] == loaded.objects[1] && root_children[1] == loaded.objects[5],
            "load: the root's children in document order");
    const std::shared_ptr<gui_forms::GroupBox> group =
        std::dynamic_pointer_cast<gui_forms::GroupBox>(loaded.objects[1]);
    require(group != nullptr && (*group).children().size() == 2U && (*group).children()[0] == loaded.objects[2] &&
                (*group).children()[1] == loaded.objects[3],
            "load: the group's children in document order");

    const std::shared_ptr<gui_forms::CheckBox> check = std::dynamic_pointer_cast<gui_forms::CheckBox>(loaded.objects[2]);
    require(check != nullptr && (*check).text.read() == "Save automatically" && (*check).checked() &&
                (*check).tab_index.read() == 2U,
            "load: the check box's properties");
    require(std::dynamic_pointer_cast<gui_forms::Command>(loaded.objects[4]) != nullptr, "load: the command");
    require(loaded.handlers.size() == 3U && loaded.handlers[0].handler == "autosave_changed" &&
                loaded.handlers[1].handler == "save" && loaded.handlers[2].handler == "accept",
            "load: the handler references, for the caller");
}

void test_refusals() {
    const FormSchema schema = sample_schema();
    Workspace workspace{};
    LoadedForm loaded{};
    require(load(workspace, "user-control kept.root { }", schema, loaded) == FormLoadOutcome::loaded,
            "refusals: the earlier load");

    // A value the setter refuses and a type the loader cannot make: nothing
    // is kept.
    const std::string_view refused = "user-control app.root {\n"
                                     "  maximum-size: 10 10;\n"
                                     "  minimum-size: 20 20;\n"
                                     "  panel app.side { }\n"
                                     "}\n";
    require(load(workspace, refused, schema, loaded) == FormLoadOutcome::refused, "refusals: refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    // The missing creator is found while objects are made, before any
    // property is set, so it alone is reported.
    const bool located = records.size() == 1U && records[0].code == DiagnosticCode::form_load_refused &&
                         records[0].position.line.value == 4U && records[0].position.column.value == 3U &&
                         records[0].rule == "app.side";
    if (!located) print_diagnostics(workspace);
    require(located, "refusals: the missing creator located");
    require(loaded.root != nullptr && (*loaded.root).stable_id().value() == "kept.root",
            "refusals: the earlier load is kept");

    // A minimum size over the maximum: each value is of its kind, and the
    // setter refuses the pair.
    const std::string_view value = "user-control app.root {\n  maximum-size: 10 10;\n  minimum-size: 20 20;\n}\n";
    require(load(workspace, value, schema, loaded) == FormLoadOutcome::refused, "refusals: a value refused");
    const std::span<const ThemeDiagnostic> value_records = workspace.diagnostics.records();
    const bool value_located = value_records.size() == 1U &&
                               value_records[0].code == DiagnosticCode::form_load_refused &&
                               value_records[0].position.line.value == 3U &&
                               value_records[0].position.column.value == 3U &&
                               value_records[0].property == "minimum_size";
    if (!value_located) print_diagnostics(workspace);
    require(value_located, "refusals: the refused value located");
    require(loaded.root != nullptr && (*loaded.root).stable_id().value() == "kept.root",
            "refusals: still the earlier load");
}

// The reserved form root: the window's properties for the caller, its one
// control as the root, its components kept.
void test_form_root() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "form settings.dialog {\n"
                                      "  text: \"Settings\";\n"
                                      "  client-size: 480 320;\n"
                                      "  accept-button: \"settings.ok\";\n"
                                      "  cancel-button: \"settings.cancel\";\n"
                                      "  command settings.save { }\n"
                                      "  user-control settings.body {\n"
                                      "    button settings.ok { text: \"OK\"; }\n"
                                      "    button settings.cancel { text: \"Cancel\"; }\n"
                                      "  }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded, "form root: loaded");
    require(loaded.window.has_value(), "form root: the window");
    if (loaded.window) {
        require((*loaded.window).title == "Settings" && (*loaded.window).client_size == gui_forms::Size{480.0, 320.0} &&
                    (*loaded.window).accept_button == "settings.ok" &&
                    (*loaded.window).cancel_button == "settings.cancel",
                "form root: the window's properties");
    }
    require(loaded.root != nullptr && (*loaded.root).stable_id().value() == "settings.body",
            "form root: its control is the root");
    require(loaded.objects.size() == 5U && loaded.objects[0] == nullptr && loaded.objects[1] != nullptr,
            "form root: no object for the form, the command kept");
}

// What the model refuses of a form root: a second control, a form below
// the root, and a button property naming no object.
void test_form_root_defects() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "form app.window {\n"
                                      "  accept-button: \"app.missing\";\n"
                                      "  user-control app.a { form app.inner { } }\n"
                                      "  user-control app.b { }\n"
                                      "}\n";
    Workspace workspace{};
    workspace.text = std::string(document);
    workspace.source.file = "test.form";
    workspace.source.text = workspace.text;
    static_cast<void>(
        tokenize_theme_sheet(workspace.source, workspace.tokens, workspace.lines, workspace.diagnostics));
    static_cast<void>(parse_theme_sheet(workspace.source, workspace.tokens, workspace.lines, workspace.tree,
                                        workspace.diagnostics, ThemeSheetForm::form_document));
    const FormModelOutcome built = build_form_model(workspace.source, workspace.tokens, workspace.lines,
                                                    workspace.tree, schema, workspace.model, workspace.diagnostics);
    require(built == FormModelOutcome::refused, "form root defects: refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    // The nested form in document order, then the root's own checks.
    const bool reported = records.size() == 3U && records[0].code == DiagnosticCode::form_root_invalid &&
                          records[0].position.line.value == 3U && records[0].position.column.value == 24U &&
                          records[1].code == DiagnosticCode::form_root_invalid &&
                          records[1].position.line.value == 1U && records[1].position.column.value == 1U &&
                          records[2].code == DiagnosticCode::form_value_invalid &&
                          records[2].position.line.value == 2U && records[2].position.column.value == 18U;
    if (!reported) print_diagnostics(workspace);
    require(reported, "form root defects: each located");
}

// Spans are defined before any placement, so the children's `area` and
// `cell` find them (study 10.9).
void test_spans() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "grid-panel app.root {\n"
                                      "  check-box app.ok { area: \"footer\"; }\n"
                                      "  check-box app.side { cell: 0 2; }\n"
                                      "  span footer { anchor: 2 0; columns: 3; }\n"
                                      "  span { anchor: 0 2; rows: 2; }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded, "spans: loaded");
    const std::shared_ptr<gui_forms::GridPanel> grid = std::dynamic_pointer_cast<gui_forms::GridPanel>(loaded.root);
    require(grid != nullptr && loaded.objects.size() == 3U, "spans: the grid and its two children");
    if (grid == nullptr || loaded.objects.size() != 3U) return;
    const std::span<const gui_forms::GridSpan> spans = (*grid).spans();
    const bool defined = spans.size() == 2U &&
                         spans[0] == gui_forms::GridSpan{"footer", gui_forms::GridCell{2U, 0U}, 1U, 3U} &&
                         spans[1] == gui_forms::GridSpan{"", gui_forms::GridCell{0U, 2U}, 2U, 1U};
    require(defined, "spans: defined in document order");
    const Control::Ptr ok = std::dynamic_pointer_cast<Control>(loaded.objects[1]);
    const Control::Ptr side = std::dynamic_pointer_cast<Control>(loaded.objects[2]);
    require(ok != nullptr && (*grid).area[*ok].read() == "footer", "spans: placed by area");
    require(side != nullptr && (*grid).cell[*side].read() == gui_forms::GridCell{0U, 2U}, "spans: placed by cell");
}

} // namespace

int main() {
    test_load();
    test_refusals();
    test_form_root();
    test_form_root_defects();
    test_spans();
    if (failures != 0) {
        std::cerr << failures << " form loader checks failed\n";
        return 1;
    }
    std::cout << "form loader: tree, child order, values, components, handler references and refusals passed\n";
    return 0;
}
