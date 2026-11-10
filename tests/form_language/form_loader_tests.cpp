// Form documents' run-time loader (stage FD, phase FD3): a checked model
// made into controls and components. Gate: the tree, child order and
// property values are the document's; bound properties are bound at
// creation to the caller's sources (FD-6); a refusal by a run-time type or
// of a source is located and leaves the previous load as it was; a form
// loads as a window whose controls are found by stable ID (stage B).

#include "../../src/core/form_language/form_loader/form_loader.hpp"
#include "../../src/core/form_language/form_loader/form_window_loader.hpp"
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
using gui_forms::form_language::FormSources;
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
    schema.types.push_back(form_control_type<gui_forms::NumericUpDown>("numeric_up_down"));
    schema.types.push_back(form_control_type<gui_forms::TextBox>("text_box"));
    schema.types.push_back(form_component_type<gui_forms::Command>("command"));
    // A type a document may describe but the loader cannot make.
    const std::shared_ptr<gui_forms::Panel> panel = gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("prototype"));
    schema.types.push_back(describe_form_type("panel", *panel));
    schema.types.push_back(gui_forms::form_language::form_root_type());
    // A type that holds spans (study 10.9).
    schema.types.push_back(form_control_type<gui_forms::GridPanel>("grid_panel"));
    // A table: its children's spans need their cells (study 10.9 section 5).
    schema.types.push_back(form_control_type<gui_forms::TableLayoutPanelCompat>("table_layout_panel_compat"));
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

FormLoadOutcome load(Workspace& workspace, std::string_view text, const FormSchema& schema,
                     const FormSources& sources, LoadedForm& loaded) {
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
                                              schema, workspace.model, sources, loaded, workspace.diagnostics);
    return outcome;
}

// A load of a document that binds nothing.
FormLoadOutcome load(Workspace& workspace, std::string_view text, const FormSchema& schema, LoadedForm& loaded) {
    const FormSources no_sources{};
    const FormLoadOutcome outcome = load(workspace, text, schema, no_sources, loaded);
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
                                      "    }\n"
                                      "    button settings.apply { text: \"Apply\"; }\n"
                                      "  }\n"
                                      "  command settings.save { }\n"
                                      "  button settings.ok { text: \"OK\"; }\n"
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
    require(check != nullptr && (*check).text.read() == "Save automatically" && (*check).checked.read() &&
                (*check).tab_index.read() == 2U,
            "load: the check box's properties");
    require(std::dynamic_pointer_cast<gui_forms::Command>(loaded.objects[4]) != nullptr, "load: the command");
}

// Counts a command's invocations.
struct InvocationCount final {
    void count(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++calls;
    }

    int calls{};
};

// The document's bindings, made at creation from the caller's sources
// (owner rulings FD-6 R1 and R2): a Value two way by its property's default
// and one way where the document narrows it, a field of a BindingSource's
// current record written back on every change, and a Command. The controls
// keep their bindings and end them when they go.
void test_bindings() {
    const FormSchema schema = sample_schema();
    const std::shared_ptr<gui_forms::Panel> host =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("host"));
    gui_forms::Window window(host, gui_forms::Size{320.0, 200.0});
    gui_forms::BindingRecord::FieldMap fields{};
    fields["name"] = std::string("Ada");
    const std::shared_ptr<gui_forms::BindingSource> people = std::make_shared<gui_forms::BindingSource>(window);
    (*people).set_records({gui_forms::BindingRecord("person.1", fields)});
    gui_forms::Value<bool> autosave{true};
    gui_forms::Value<double> level{25.0};
    gui_forms::Command save{};
    InvocationCount saved{};
    using Invoked = gui_forms::Delegate<const gui_forms::CommandInvocation&>;
    const gui_forms::SubscriptionToken counting =
        save.invoked().subscribe(Invoked::bind<InvocationCount, &InvocationCount::count>(saved));
    gui_forms::Value<bool> disposed{true};
    disposed.dispose();
    FormSources sources{};
    sources.add("autosave_", autosave);
    sources.add("level_", level);
    sources.add("save_", save);
    sources.add("people_", people);
    sources.add("disposed_", disposed);

    int refusals = 0;
    try {
        sources.add("level_", autosave);
    } catch (const std::invalid_argument&) {
        ++refusals;
    }
    try {
        sources.add("two words", autosave);
    } catch (const std::invalid_argument&) {
        ++refusals;
    }
    require(refusals == 2 && sources.find("two words") == nullptr && sources.find("level_") != nullptr,
            "bindings: a repeated name and a name that is no identifier, refused");

    const std::string_view document = "user-control app.root {\n"
                                      "  check-box app.autosave { checked: bind(autosave_); }\n"
                                      "  check-box app.mirror { checked: bind(autosave_, one_way); }\n"
                                      "  numeric-up-down app.level { value: bind(level_); }\n"
                                      "  text-box app.name { text: bind(people_, \"name\", update on-change); }\n"
                                      "  button app.save { text: \"Save\"; command: bind(save_); }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, sources, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded && loaded.objects.size() == 6U, "bindings: loaded");
    if (outcome != FormLoadOutcome::loaded || loaded.objects.size() != 6U) return;
    (*host).add_child(loaded.root);
    // Held here only for this block: released below, the controls go.
    std::weak_ptr<gui_forms::CheckBox> released{};
    {
        const std::shared_ptr<gui_forms::CheckBox> check =
            std::dynamic_pointer_cast<gui_forms::CheckBox>(loaded.objects[1]);
        const std::shared_ptr<gui_forms::CheckBox> mirror =
            std::dynamic_pointer_cast<gui_forms::CheckBox>(loaded.objects[2]);
        const std::shared_ptr<gui_forms::NumericUpDown> number =
            std::dynamic_pointer_cast<gui_forms::NumericUpDown>(loaded.objects[3]);
        const std::shared_ptr<gui_forms::TextBox> name =
            std::dynamic_pointer_cast<gui_forms::TextBox>(loaded.objects[4]);
        const std::shared_ptr<gui_forms::Button> button =
            std::dynamic_pointer_cast<gui_forms::Button>(loaded.objects[5]);
        require(check != nullptr && mirror != nullptr && number != nullptr && name != nullptr && button != nullptr,
                "bindings: every bound control");
        if (check == nullptr || mirror == nullptr || number == nullptr || name == nullptr || button == nullptr) return;

        require((*check).checked.read() && (*mirror).checked.read() && (*number).value.read() == 25.0 && (*name).text.read() == "Ada",
                "bindings: each property takes its source's value at creation");
        (*check).checked = false;
        require(!autosave.get() && !(*mirror).checked.read(), "bindings: two way, the change reaches the Value and on");
        (*mirror).checked = true;
        require(!autosave.get(), "bindings: one way, the control's change stays in the control");
        (*number).value = 30.0;
        require(level.get() == 30.0, "bindings: the numeric value, two way by its property's default");
        (*name).text = std::string("Ada Lovelace");
        const gui_forms::BindingRecord* record = (*people).current();
        const gui_forms::BindingValue written{std::string("Ada Lovelace")};
        require(record != nullptr && (*record).fields.at("name") == written,
                "bindings: the field, written back on every change");
        static_cast<void>((*button).perform_click());
        require(saved.calls == 1, "bindings: the button runs its command");
        released = check;
    }

    // A missing source, a source of another kind and a disposed one: every
    // refusal located, the earlier load kept.
    const std::string_view refused = "user-control bad.root {\n"
                                     "  check-box bad.a { checked: bind(missing_); }\n"
                                     "  check-box bad.b { checked: bind(level_); }\n"
                                     "  button bad.c { command: bind(autosave_); }\n"
                                     "  check-box bad.d { checked: bind(disposed_); }\n"
                                     "}\n";
    require(load(workspace, refused, schema, sources, loaded) == FormLoadOutcome::refused, "bindings: refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool located = records.size() == 4U && records[0].code == DiagnosticCode::form_binding_source_refused &&
                         records[0].position.line.value == 2U && records[0].position.column.value == 21U &&
                         records[1].code == DiagnosticCode::form_binding_source_refused &&
                         records[1].position.line.value == 3U && records[1].property == "checked" &&
                         records[2].code == DiagnosticCode::form_binding_source_refused &&
                         records[2].position.line.value == 4U && records[2].position.column.value == 18U &&
                         records[3].code == DiagnosticCode::form_binding_source_refused &&
                         records[3].position.line.value == 5U;
    if (!located) print_diagnostics(workspace);
    require(located, "bindings: each refused source located");
    require(loaded.root != nullptr && (*loaded.root).stable_id().value() == "app.root",
            "bindings: the earlier load is kept");

    // Released, the controls end their bindings: the sources change alone.
    static_cast<void>((*host).remove_child((*loaded.root).runtime_id()));
    loaded = LoadedForm{};
    require(released.expired(), "bindings: the released form's controls are gone");
    autosave.set(true);
    level.set(5.0);
    require(autosave.get() && level.get() == 5.0, "bindings: the sources change alone once their controls are gone");
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

// The button properties written bare, as the object writes its stable ID
// and as the guides write them; a bare name naming no object is still
// refused, and spaced names are not one identity.
void test_form_root_bare_buttons() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "form settings.dialog {\n"
                                      "  accept-button: settings.ok;\n"
                                      "  cancel-button: settings.cancel-2;\n"
                                      "  user-control settings.body {\n"
                                      "    button settings.ok { text: \"OK\"; }\n"
                                      "    button settings.cancel-2 { text: \"Cancel\"; }\n"
                                      "  }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded, "bare buttons: loaded");
    require(loaded.window.has_value() && (*loaded.window).accept_button == "settings.ok" &&
                (*loaded.window).cancel_button == "settings.cancel-2",
            "bare buttons: the window's buttons");

    const std::string_view refused_document = "form app.window {\n"
                                              "  accept-button: app.missing;\n"
                                              "  cancel-button: app . b;\n"
                                              "  user-control app.b { }\n"
                                              "}\n";
    Workspace refused{};
    refused.text = std::string(refused_document);
    refused.source.file = "test.form";
    refused.source.text = refused.text;
    static_cast<void>(tokenize_theme_sheet(refused.source, refused.tokens, refused.lines, refused.diagnostics));
    static_cast<void>(parse_theme_sheet(refused.source, refused.tokens, refused.lines, refused.tree,
                                        refused.diagnostics, ThemeSheetForm::form_document));
    const FormModelOutcome built = build_form_model(refused.source, refused.tokens, refused.lines, refused.tree,
                                                    schema, refused.model, refused.diagnostics);
    std::size_t invalid = 0U;
    for (const ThemeDiagnostic& record : refused.diagnostics.records()) {
        if (record.code == DiagnosticCode::form_value_invalid) ++invalid;
    }
    if (invalid != 2U) print_diagnostics(refused);
    require(built == FormModelOutcome::refused && invalid == 2U,
            "bare buttons: a missing and a spaced name refused");
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

// A form loaded as a window (stage B, B.3): the tree and bindings as
// load_form makes them, the window over the form's control with its client
// size and buttons, its controls found by stable ID; a refused source, a
// control root and a parse failure make nothing.
void test_load_window() {
    using gui_forms::ControlLookup;
    using gui_forms::ControlLookupError;
    using gui_forms::form_language::LoadedWindowResult;
    using gui_forms::form_language::WindowLoadError;
    using gui_forms::form_language::load_window;
    const FormSchema schema = sample_schema();
    const std::string_view document = "form settings.dialog {\n"
                                      "  text: \"Settings\";\n"
                                      "  client-size: 480 320;\n"
                                      "  accept-button: \"settings.ok\";\n"
                                      "  cancel-button: \"settings.cancel\";\n"
                                      "  command settings.save { }\n"
                                      "  user-control settings.body {\n"
                                      "    check-box settings.auto-save { checked: bind(auto_save_); }\n"
                                      "    button settings.ok { text: \"OK\"; }\n"
                                      "    button settings.cancel { text: \"Cancel\"; }\n"
                                      "  }\n"
                                      "}\n";
    // Declared before the window: its check box keeps a binding to it.
    gui_forms::Value<bool> auto_save{true};
    FormSources sources{};
    sources.add("auto_save_", auto_save);
    ThemeDiagnosticList diagnostics{};
    LoadedWindowResult loaded = load_window("settings.form", document, schema, sources, diagnostics);
    require(static_cast<bool>(loaded) && loaded.window != nullptr && diagnostics.records().empty(),
            "load window: loaded");
    if (!loaded) return;
    gui_forms::Window& window = *loaded.window;
    const Control::Ptr root = window.root();
    require(root != nullptr && (*root).stable_id().value() == "settings.body", "load window: the form's control is the root");
    require(window.client_size() == gui_forms::Size{480.0, 320.0} && loaded.title == "Settings",
            "load window: the client size and the title");
    require(loaded.components.size() == 1U && loaded.components[0] != nullptr, "load window: the command, kept");
    const ControlLookup ok = window.control("settings.ok");
    const ControlLookup cancel = window.control("settings.cancel");
    require(static_cast<bool>(ok) && window.accept_button() == ok.control && static_cast<bool>(cancel) &&
                window.cancel_button() == cancel.control,
            "load window: the accept and cancel buttons, found by ID");
    const ControlLookup check = window.control("settings.auto-save");
    const std::shared_ptr<gui_forms::CheckBox> box = std::dynamic_pointer_cast<gui_forms::CheckBox>(check.control);
    require(box != nullptr && (*box).checked.read(), "load window: bound at creation");
    if (box != nullptr) {
        (*box).checked = false;
        require(!auto_save.get(), "load window: two way, the control's change reaches the source");
    }
    require(window.control("").error == ControlLookupError::empty_id &&
                window.control("settings.none").error == ControlLookupError::not_found &&
                window.control("settings.none").control == nullptr,
            "lookup: an empty ID and an absent one, refused");
    // Stable IDs are unique in a window: a detached control is no longer found.
    static_cast<void>((*root).remove_child((*ok.control).runtime_id()));
    require(window.control("settings.ok").error == ControlLookupError::not_found,
            "lookup: a detached control is not found");

    FormSources missing{};
    ThemeDiagnosticList refusals{};
    const LoadedWindowResult refused = load_window("settings.form", document, schema, missing, refusals);
    const bool located = !refusals.records().empty() &&
                         refusals.records()[0].code == DiagnosticCode::form_binding_source_refused;
    require(refused.error == WindowLoadError::refused && refused.window == nullptr && refused.title.empty() &&
                refused.components.empty() && located,
            "load window: a missing source refuses the whole load, located");

    ThemeDiagnosticList control_root{};
    const LoadedWindowResult panel =
        load_window("panel.form", "user-control app.root { }\n", schema, sources, control_root);
    require(panel.error == WindowLoadError::not_a_window && panel.window == nullptr,
            "load window: a control root describes no window");
    ThemeDiagnosticList broken{};
    const LoadedWindowResult unparsed = load_window("broken.form", "form app.window {\n", schema, sources, broken);
    require(unparsed.error == WindowLoadError::refused && unparsed.window == nullptr && !broken.records().empty(),
            "load window: a document that does not parse");
}

// The application overload joins the application it names.
void test_load_window_application() {
    gui_forms::ApplicationCreateResult created = gui_forms::Application::create(gui_forms::ApplicationConfig{});
    require(static_cast<bool>(created), "load window application: made");
    if (!created) return;
    gui_forms::Application& application = *created.application;
    {
        const FormSchema schema = sample_schema();
        const FormSources sources{};
        ThemeDiagnosticList diagnostics{};
        const gui_forms::form_language::LoadedWindowResult loaded = gui_forms::form_language::load_window(
            application, "app.form", "form app.window { user-control app.body { } }\n", schema, sources,
            diagnostics);
        require(loaded.window != nullptr && (*loaded.window).application() == &application,
                "load window application: the window joins it");
    }
    const gui_forms::ApplicationResult closed = application.close();
    require(closed.accepted(), "load window application: closed once the window is gone");
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

// A table child's cell is applied before its spans whatever their order in
// the block (the table lists the cell first); a span without a cell is
// refused at load.
void test_table_placement() {
    const FormSchema schema = sample_schema();
    const std::string_view document = "table-layout-panel-compat app.root {\n"
                                      "  check-box app.header { column-span: 2; cell: 0 0; text: \"Header\"; }\n"
                                      "  check-box app.other { }\n"
                                      "}\n";
    Workspace workspace{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = load(workspace, document, schema, loaded);
    print_diagnostics(workspace);
    require(outcome == FormLoadOutcome::loaded, "table placement: loaded");
    const std::shared_ptr<gui_forms::TableLayoutPanelCompat> table =
        std::dynamic_pointer_cast<gui_forms::TableLayoutPanelCompat>(loaded.root);
    require(table != nullptr && loaded.objects.size() == 3U, "table placement: the table and its two children");
    if (table == nullptr || loaded.objects.size() != 3U) return;
    const Control::Ptr header = std::dynamic_pointer_cast<Control>(loaded.objects[1]);
    require(header != nullptr && (*table).has_cell(*header) && (*table).column_span[*header].read() == 2U,
            "table placement: the cell, then the span");

    const std::string_view unplaced = "table-layout-panel-compat app.root {\n"
                                      "  check-box app.header { column-span: 2; }\n"
                                      "}\n";
    require(load(workspace, unplaced, schema, loaded) == FormLoadOutcome::refused,
            "table placement: a span without a cell is refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool located = records.size() == 1U && records[0].code == DiagnosticCode::form_load_refused &&
                         records[0].position.line.value == 2U && records[0].rule == "app.header";
    if (!located) print_diagnostics(workspace);
    require(located, "table placement: the refusal located at the span");
}

} // namespace

int main() {
    test_load();
    test_bindings();
    test_refusals();
    test_form_root();
    test_form_root_defects();
    test_form_root_bare_buttons();
    test_spans();
    test_table_placement();
    test_load_window();
    test_load_window_application();
    if (failures != 0) {
        std::cerr << failures << " form loader checks failed\n";
        return 1;
    }
    std::cout << "form loader: tree, child order, values, components, bindings and refusals passed\n";
    return 0;
}
