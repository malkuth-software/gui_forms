// Form documents' C++ generator (stage FD, phase FD5). Gate: the generated
// header builds the tree the loader builds, with the same values; it
// connects the document's handlers to the owner's members, and a second
// initialization releases the first one's connections; the generator
// refuses what it cannot spell, located, leaving the caller's text as it
// was; and the golden header is what the generator writes today.

#include "../../src/core/form_language/form_generator/form_generator.hpp"
#include "../../src/core/form_language/form_loader/form_loader.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "../../tools/form_compiler/form_catalog.hpp"

#include "generated/sample_dialog_form.hpp"

#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef GUI_FORMS_FORM_GENERATED_DIR
#error "GUI_FORMS_FORM_GENERATED_DIR must name tests/form_language/generated"
#endif

namespace {

using gui_forms::Control;
using gui_forms::form_language::FormGenerateOutcome;
using gui_forms::form_language::FormGeneratorOptions;
using gui_forms::form_language::FormLoadOutcome;
using gui_forms::form_language::FormModel;
using gui_forms::form_language::FormModelOutcome;
using gui_forms::form_language::FormProperty;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::FormSchemaType;
using gui_forms::form_language::LoadedForm;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSheetForm;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents{};
    contents << stream.rdbuf();
    std::string text = contents.str();
    return text;
}

const std::string sample_document_path = std::string(GUI_FORMS_FORM_GENERATED_DIR) + "/sample_dialog.form";
const std::string sample_header_path = std::string(GUI_FORMS_FORM_GENERATED_DIR) + "/sample_dialog_form.hpp";

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

void print_diagnostics(const Workspace& workspace) {
    for (const ThemeDiagnostic& record : workspace.diagnostics.records()) {
        std::cerr << "  " << gui_forms::theme_language::theme_diagnostic_log_line(record) << '\n';
    }
}

// Parses `text` and builds its model; returns whether both succeeded.
bool read_document(Workspace& workspace, std::string_view file, std::string_view text, const FormSchema& schema) {
    workspace.text = std::string(text);
    workspace.diagnostics.clear();
    workspace.source.file = file;
    workspace.source.text = workspace.text;
    static_cast<void>(gui_forms::theme_language::tokenize_theme_sheet(workspace.source, workspace.tokens,
                                                                      workspace.lines, workspace.diagnostics));
    const ParseOutcome parsed =
        gui_forms::theme_language::parse_theme_sheet(workspace.source, workspace.tokens, workspace.lines,
                                                     workspace.tree, workspace.diagnostics,
                                                     ThemeSheetForm::form_document);
    if (parsed != ParseOutcome::tree_complete) return false;
    const FormModelOutcome built = gui_forms::form_language::build_form_model(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model,
        workspace.diagnostics);
    return built == FormModelOutcome::model_built;
}

FormGenerateOutcome generate(Workspace& workspace, const FormSchema& schema, const FormGeneratorOptions& options,
                             std::string& generated) {
    const FormGenerateOutcome outcome = gui_forms::form_language::generate_form_cpp(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, options,
        generated, workspace.diagnostics);
    return outcome;
}

FormGeneratorOptions sample_options() {
    FormGeneratorOptions options{};
    options.class_name = "SampleDialogForm";
    options.namespace_name = "form_generated";
    options.source_name = "sample_dialog.form";
    return options;
}

// The control with `stable_id` in the subtree of `control`, or null.
Control::Ptr find_control(const Control::Ptr& control, std::string_view stable_id) {
    if ((*control).stable_id().value() == stable_id) return control;
    for (const Control::Ptr& child : (*control).children()) {
        const Control::Ptr found = find_control(child, stable_id);
        if (found) return found;
    }
    return nullptr;
}

// Whether two trees have the same stable IDs in the same child order.
bool same_shape(const Control::Ptr& left, const Control::Ptr& right) {
    if ((*left).stable_id().value() != (*right).stable_id().value()) return false;
    const std::span<const Control::Ptr> left_children = (*left).children();
    const std::span<const Control::Ptr> right_children = (*right).children();
    if (left_children.size() != right_children.size()) return false;
    for (std::size_t index = 0U; index < left_children.size(); ++index) {
        if (!same_shape(left_children[index], right_children[index])) return false;
    }
    return true;
}

// The owner class of sample_dialog.form: its handlers count their calls.
class SampleOwner final {
public:
    void auto_save_changed(bool checked) {
        ++auto_save_calls;
        last_checked = checked;
    }
    void accept(gui_forms::ButtonBase& button) {
        static_cast<void>(button);
        ++accept_calls;
    }
    void dismiss(gui_forms::ButtonBase& button) {
        static_cast<void>(button);
        ++dismiss_calls;
    }
    void save(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++save_calls;
    }

    int auto_save_calls{};
    bool last_checked{};
    int accept_calls{};
    int dismiss_calls{};
    int save_calls{};
};

// The generated header builds the loader's tree with the loader's values.
void test_generated_matches_loader() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    Workspace workspace{};
    const std::string text = read_file(sample_document_path);
    const bool read = read_document(workspace, "sample_dialog.form", text, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "loader: the sample document's model builds");
    if (!read) return;
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, loaded,
        workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded, "loader: the sample document loads");
    if (outcome != FormLoadOutcome::loaded) return;

    SampleOwner owner{};
    form_generated::SampleDialogForm<SampleOwner> form{};
    form.initialize_component(owner);
    const Control::Ptr root = form.root_control();
    require(root != nullptr && same_shape(root, loaded.root), "generated: the loader's tree, in child order");
    for (const FormProperty& property : workspace.model.properties) {
        if (property.object.value == 0U) continue;
        const std::string& stable_id = workspace.model.objects[property.object.value].stable_id;
        const std::string& name =
            gui_forms::form_language::form_property_descriptor(schema, workspace.model, property).name;
        const Control::Ptr generated = find_control(root, stable_id);
        const Control::Ptr from_loader = find_control(loaded.root, stable_id);
        const bool same = generated != nullptr && from_loader != nullptr &&
                          (*generated).property_value(name) == (*from_loader).property_value(name) &&
                          (*generated).property_value(name) == std::optional<gui_forms::BindingValue>(property.value);
        require(same, "generated: " + stable_id + " " + name + " as the loader sets it");
    }
    require(form.sample_save != nullptr, "generated: the command, held by the form");

    // The window's description.
    require(form_generated::SampleDialogForm<SampleOwner>::window_title == "R\303\251glages",
            "window: the title, in UTF-8");
    require(form_generated::SampleDialogForm<SampleOwner>::window_client_size == gui_forms::Size{480.0, 320.0},
            "window: the client size");
    gui_forms::Window window(root, gui_forms::Size{480.0, 320.0});
    form.attach_buttons(window);
    require(window.accept_button() == form.sample_ok && window.cancel_button() == form.sample_cancel,
            "window: the accept and cancel buttons");
}

// The document's handlers reach the owner's members; a second
// initialization releases the first one's connections.
void test_handlers() {
    SampleOwner owner{};
    form_generated::SampleDialogForm<SampleOwner> form{};
    form.initialize_component(owner);
    require(form.sample_general_auto_save != nullptr && (*form.sample_general_auto_save).checked(),
            "handlers: the check box starts checked");
    if (form.sample_general_auto_save == nullptr) return;
    (*form.sample_general_auto_save).set_checked(false);
    require(owner.auto_save_calls == 1 && !owner.last_checked, "handlers: checked_changed reaches the owner");
    gui_forms::Window window(form.root_control(), gui_forms::Size{480.0, 320.0});
    static_cast<void>((*form.sample_ok).perform_click());
    static_cast<void>((*form.sample_cancel).perform_click());
    require(owner.accept_calls == 1 && owner.dismiss_calls == 1, "handlers: clicked reaches accept and dismiss");
    static_cast<void>((*form.sample_save).execute());
    require(owner.save_calls == 1, "handlers: the command's invoked reaches save");

    const std::shared_ptr<gui_forms::CheckBox> first = form.sample_general_auto_save;
    form.initialize_component(owner);
    require(form.sample_general_auto_save != first, "handlers: a second initialization makes new objects");
    (*first).set_checked(true);
    require(owner.auto_save_calls == 1, "handlers: the first initialization's connection is released");
    (*form.sample_general_auto_save).set_checked(false);
    require(owner.auto_save_calls == 2, "handlers: the second initialization's connection reaches the owner");
}

// The golden header is what the generator writes today, and the generator
// writes the same bytes each time.
void test_golden() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    Workspace workspace{};
    const std::string text = read_file(sample_document_path);
    require(read_document(workspace, "sample_dialog.form", text, schema), "golden: the model builds");
    std::string first{};
    std::string second{};
    require(generate(workspace, schema, sample_options(), first) == FormGenerateOutcome::generated &&
                generate(workspace, schema, sample_options(), second) == FormGenerateOutcome::generated,
            "golden: generated");
    require(first == second, "golden: the same bytes each time");
    const std::string golden = read_file(sample_header_path);
    require(first == golden, "golden: tests/form_language/generated/sample_dialog_form.hpp is current "
                             "(regenerate it with gui_forms_form_compiler)");
}

FormSchema sample_schema(bool spelled) {
    FormSchema schema{};
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::UserControl>("user_control"));
    schema.types.push_back(gui_forms::form_language::form_control_type<gui_forms::Button>("button"));
    if (spelled) {
        schema.types[0].cpp_type = "gui_forms::UserControl";
        schema.types[0].header = "gui_forms/controls/panel/user_control/user_control.hpp";
        schema.types[1].cpp_type = "gui_forms::Button";
        schema.types[1].header = "gui_forms/controls/button_base/button/button.hpp";
    }
    return schema;
}

// What the generator cannot spell is refused, located, and every defect
// reported; the caller's text is unchanged.
void test_refusals() {
    const FormSchema schema = sample_schema(true);
    const std::string_view document = "user-control app.root {\n"
                                      "  button app.ok { }\n"
                                      "  button app-ok { }\n"
                                      "  button 1st.button { }\n"
                                      "  button delete { on-clicked: class; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "test.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "refusals: the model builds");
    std::string generated = "kept";
    const FormGenerateOutcome outcome = generate(workspace, schema, sample_options(), generated);
    require(outcome == FormGenerateOutcome::refused && generated == "kept", "refusals: refused, text kept");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool located =
        records.size() == 4U && records[0].code == DiagnosticCode::form_generation_refused &&
        records[0].rule == "app-ok" && records[0].position.line.value == 3U &&
        records[0].position.column.value == 3U && records[1].rule == "1st.button" &&
        records[1].position.line.value == 4U && records[2].rule == "delete" && records[2].position.line.value == 5U &&
        records[3].rule == "delete" && records[3].property == "clicked";
    if (!located) print_diagnostics(workspace);
    require(located, "refusals: the shared member, the digit, the keyword and the handler, located");

    // A type without a C++ spelling.
    const FormSchema unspelled = sample_schema(false);
    Workspace plain{};
    require(read_document(plain, "test.form", "user-control app.root { }\n", unspelled), "refusals: plain model");
    std::string plain_text{};
    require(generate(plain, unspelled, sample_options(), plain_text) == FormGenerateOutcome::refused &&
                plain.diagnostics.records().size() == 1U &&
                plain.diagnostics.records()[0].code == DiagnosticCode::form_generation_refused,
            "refusals: a type without a C++ spelling");

    // Options the generator cannot use are refused before anything else.
    FormGeneratorOptions options = sample_options();
    options.class_name = "1Form";
    bool thrown = false;
    try {
        static_cast<void>(generate(plain, schema, options, plain_text));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown, "refusals: a class name that is no identifier throws");
    options = sample_options();
    options.namespace_name = "app::";
    thrown = false;
    try {
        static_cast<void>(generate(plain, schema, options, plain_text));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown, "refusals: a malformed namespace throws");
}

// Fonts and text items (owner decisions FD6-3 and FD6-4): written, loaded
// and generated alike; malformed values refused, located. A font in a form
// is content only (a TypographySpecimen's); every other font belongs to
// the project theme (E813).
void test_fonts_and_items() {
    SampleOwner owner{};
    form_generated::SampleDialogForm<SampleOwner> form{};
    form.initialize_component(owner);
    require(form.sample_modes != nullptr && (*form.sample_modes).items().size() == 3U &&
                (*form.sample_modes).items()[2] == "Wide FM",
            "items: the list box's items");
    require(form.sample_band != nullptr && (*form.sample_band).items().size() == 3U &&
                (*form.sample_band).items()[0] == "HF",
            "items: the domain's items");
    require(form.sample_heading != nullptr, "fonts: the heading, its font left to the theme");

    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "user-control app.root {\n"
                                      "  typography-specimen app.a { font: control 12; }\n"
                                      "  typography-specimen app.b { font: serif 12 400; }\n"
                                      "  typography-specimen app.c { font: control 12 1001; }\n"
                                      "  typography-specimen app.d { font: control 12 700 bold; }\n"
                                      "  list-box app.e { items: \"one\" 2; }\n"
                                      "}\n";
    Workspace workspace{};
    require(!read_document(workspace, "test.form", document, schema), "values: refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    // Each refusal at the token it stopped at.
    const bool located = records.size() == 5U && records[0].code == DiagnosticCode::form_value_invalid &&
                         records[0].position.line.value == 2U && records[0].position.column.value == 45U &&
                         records[1].position.line.value == 3U && records[1].position.column.value == 37U &&
                         records[2].position.line.value == 4U && records[2].position.column.value == 48U &&
                         records[3].position.line.value == 5U && records[3].position.column.value == 52U &&
                         records[4].position.line.value == 6U && records[4].position.column.value == 33U;
    if (!located) print_diagnostics(workspace);
    require(located, "values: each malformed font and item, located");
}

// FD6.3's descriptors: letter spacing in a content font, the scaled
// containers' design size (their children placed by their
// own bounds, read in design coordinates), and the choice controls'
// indicator style.
void test_page_descriptors() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "scaled-panel app.root {\n"
                                      "  design-size: 200 100;\n"
                                      "  scaled-group-box app.group {\n"
                                      "    text: \"Group\";\n"
                                      "    design-size: 50 50;\n"
                                      "    bounds: 100 0 100 100;\n"
                                      "    check-box app.check { indicator-style: toggle; bounds: 0 0 25 10; }\n"
                                      "  }\n"
                                      "  radio-button app.radio { indicator-style: modern; bounds: 0 50 100 50; }\n"
                                      "  typography-specimen app.italic { font: content 13 400 italic spacing 0.5; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "page.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "page descriptors: the model builds");
    if (!read) return;
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, loaded,
        workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded, "page descriptors: the document loads");
    if (outcome != FormLoadOutcome::loaded) return;

    const std::shared_ptr<gui_forms::ScaledPanel> root =
        std::dynamic_pointer_cast<gui_forms::ScaledPanel>(loaded.root);
    const std::shared_ptr<gui_forms::ScaledGroupBox> group =
        std::dynamic_pointer_cast<gui_forms::ScaledGroupBox>(find_control(loaded.root, "app.group"));
    const std::shared_ptr<gui_forms::CheckBox> check =
        std::dynamic_pointer_cast<gui_forms::CheckBox>(find_control(loaded.root, "app.check"));
    const std::shared_ptr<gui_forms::RadioButton> radio =
        std::dynamic_pointer_cast<gui_forms::RadioButton>(find_control(loaded.root, "app.radio"));
    const std::shared_ptr<gui_forms::TypographySpecimen> italic =
        std::dynamic_pointer_cast<gui_forms::TypographySpecimen>(find_control(loaded.root, "app.italic"));
    require(root != nullptr && group != nullptr && check != nullptr && radio != nullptr && italic != nullptr,
            "page descriptors: every object made");
    if (root == nullptr || group == nullptr || check == nullptr || radio == nullptr || italic == nullptr) return;
    require((*root).design_size() == gui_forms::Size{200.0, 100.0}, "page descriptors: the panel's design size");
    require((*group).design_size() == gui_forms::Size{50.0, 50.0}, "page descriptors: the group's design size");
    const gui_forms::FontSpec body{gui_forms::FontRole::content, 13.0, 400, true, 0.5};
    require((*italic).font() == body, "page descriptors: italic, then spacing");
    require((*check).indicator_style() == gui_forms::ChoiceIndicatorStyle::toggle,
            "page descriptors: the check box's indicator style");
    require((*radio).indicator_style() == gui_forms::ChoiceIndicatorStyle::modern,
            "page descriptors: the radio button's indicator style");

    // Arranged at twice the design size: each child's own bounds, scaled.
    (*root).arrange({0.0, 0.0, 400.0, 200.0});
    require((*group).pending_layout_bounds() == gui_forms::Rect{200.0, 0.0, 200.0, 200.0},
            "page descriptors: a scaled panel places a child by its own bounds");
    require((*radio).pending_layout_bounds() == gui_forms::Rect{0.0, 100.0, 200.0, 100.0},
            "page descriptors: a second child, scaled");

    const std::string_view refused = "user-control app.root {\n"
                                     "  typography-specimen app.a { font: control 12 700 spacing; }\n"
                                     "  typography-specimen app.b { font: control 12 700 spacing 13; }\n"
                                     "  typography-specimen app.c { font: control 12 700 spacing 1 italic; }\n"
                                     "  check-box app.d { indicator-style: round; }\n"
                                     "}\n";
    Workspace refusing{};
    require(!read_document(refusing, "refused.form", refused, schema), "page descriptors: malformed values refused");
    const std::span<const ThemeDiagnostic> records = refusing.diagnostics.records();
    const bool counted = records.size() == 4U && records[0].code == DiagnosticCode::form_value_invalid;
    if (!counted) print_diagnostics(refusing);
    require(counted, "page descriptors: one refusal each");
}

// A font is presentation (owner, 2026-11-04): on a label or a group box it
// is refused with E813, which names the project theme; a typography
// specimen's font is content and stays.
void test_presentation_fonts() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "user-control app.root {\n"
                                      "  label app.a { font: control 12; }\n"
                                      "  group-box app.b { font: control 12 700; }\n"
                                      "  typography-specimen app.c { font: content 20 400 italic; }\n"
                                      "}\n";
    Workspace workspace{};
    require(!read_document(workspace, "fonts.form", document, schema), "presentation fonts: refused");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool refused = records.size() == 2U &&
                         records[0].code == DiagnosticCode::form_presentation_property &&
                         records[0].position.line.value == 2U &&
                         records[1].code == DiagnosticCode::form_presentation_property &&
                         records[1].position.line.value == 3U &&
                         records[0].detail.find("project theme") != std::string::npos;
    if (!refused) print_diagnostics(workspace);
    require(refused, "presentation fonts: each refused at its name, pointing at the theme; content stays");
}

// A child writes what its parent provides (study section 10): the loader
// stores it in the parent, the generator writes it through the parent, and
// under a parent that does not provide it the model refuses it.
void test_attached_properties() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "flow-layout-panel-compat app.root {\n"
                                      "  button app.a { text: \"A\"; flow-break: true; }\n"
                                      "  button app.b { text: \"B\"; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "flow.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "attached: the model builds");
    if (!read) return;
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, loaded,
        workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded, "attached: the document loads");
    if (outcome != FormLoadOutcome::loaded) return;
    const std::shared_ptr<gui_forms::FlowLayoutPanelCompat> flow =
        std::dynamic_pointer_cast<gui_forms::FlowLayoutPanelCompat>(loaded.root);
    const Control::Ptr first = find_control(loaded.root, "app.a");
    const Control::Ptr second = find_control(loaded.root, "app.b");
    require(flow != nullptr && first != nullptr && second != nullptr, "attached: every object made");
    if (flow == nullptr || first == nullptr || second == nullptr) return;
    require((*flow).flow_break[*first].read() && !(*flow).flow_break[*second].read(),
            "attached: the parent stores the child's flow break");

    FormGeneratorOptions options = sample_options();
    options.class_name = "FlowForm";
    options.source_name = "flow.form";
    std::string generated{};
    require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
            "attached: the generator writes the document");
    require(generated.find("(*made.app_root).set_child_property_value(*made.app_a, \"flow_break\", "
                           "gui_forms::BindingValue{true});") != std::string::npos,
            "attached: generated through the parent, after the child is added");

    const std::string_view stranger = "panel app.root {\n"
                                      "  button app.a { flow-break: true; }\n"
                                      "}\n";
    require(!read_document(workspace, "stranger.form", stranger, schema), "attached: a panel provides none");
    const std::span<const ThemeDiagnostic> records = workspace.diagnostics.records();
    const bool refused = records.size() == 1U && records[0].code == DiagnosticCode::form_property_unknown &&
                         records[0].detail.find("nor its parent's type panel") != std::string::npos;
    if (!refused) print_diagnostics(workspace);
    require(refused, "attached: unknown under a parent that does not provide it, naming the parent");
}

// Spans (study 10.9): generated after the objects and before any
// placement.
void test_spans() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "grid-panel app.root {\n"
                                      "  button app.a { text: \"A\"; cell: 0 2; }\n"
                                      "  span side { anchor: 0 2; rows: 2; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "grid.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "spans: the model builds");
    if (!read) return;
    FormGeneratorOptions options = sample_options();
    options.class_name = "GridForm";
    options.source_name = "grid.form";
    std::string generated{};
    require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
            "spans: the generator writes the document");
    const std::size_t span = generated.find("if ((*made.app_root).define_span(gui_forms::GridSpan{\"side\", "
                                            "gui_forms::GridCell{0U, 2U}, 2U, 1U}) != "
                                            "gui_forms::GridSpanOutcome::applied) {");
    const std::size_t placement = generated.find("(*made.app_root).set_child_property_value(*made.app_a, \"cell\"");
    require(span != std::string::npos && placement != std::string::npos && span < placement,
            "spans: the span is defined before the placement");
    require(generated.find("#include \"gui_forms/layout/grid_span/grid_span.hpp\"") != std::string::npos,
            "spans: the generated header includes the span vocabulary");
}

void test_member_names() {
    require(gui_forms::form_language::form_member_name("settings.auto-save") == "settings_auto_save",
            "member names: '.' and '-' spelled '_'");
    require(gui_forms::form_language::form_member_name("2nd").empty(), "member names: a leading digit");
    require(gui_forms::form_language::form_member_name("class").empty(), "member names: a keyword");
    require(!gui_forms::form_language::form_identifier("_reserved"), "identifiers: a leading underscore");
    require(gui_forms::form_language::form_identifier("on_save2"), "identifiers: letters, digits, '_'");
}

} // namespace

int main() {
    test_generated_matches_loader();
    test_handlers();
    test_golden();
    test_fonts_and_items();
    test_presentation_fonts();
    test_attached_properties();
    test_spans();
    test_page_descriptors();
    test_refusals();
    test_member_names();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "form generator tests passed\n";
    return 0;
}
