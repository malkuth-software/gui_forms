// Form documents' C++ generator (stage FD, phase FD5). Gate: the generated
// header builds the tree the loader builds, with the same values; it
// binds the document's properties to the owner's sources at creation, and
// a released control ends its bindings; the generator
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
using gui_forms::form_language::FormSources;
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

// The owner class of sample_dialog.form: the sources its bindings name,
// and counts of its commands' invocations.
class SampleOwner final {
public:
    SampleOwner() {
        using Invoked = gui_forms::Delegate<const gui_forms::CommandInvocation&>;
        accepted_ = accept_.invoked().subscribe(Invoked::bind<SampleOwner, &SampleOwner::count_accept>(*this));
        dismissed_ = dismiss_.invoked().subscribe(Invoked::bind<SampleOwner, &SampleOwner::count_dismiss>(*this));
    }

    void count_accept(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++accept_calls;
    }
    void count_dismiss(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++dismiss_calls;
    }

    gui_forms::Value<bool> auto_save_{true};
    gui_forms::Value<double> level_{25.0};
    gui_forms::Command accept_{};
    gui_forms::Command dismiss_{};
    int accept_calls{};
    int dismiss_calls{};

private:
    gui_forms::SubscriptionToken accepted_{};
    gui_forms::SubscriptionToken dismissed_{};
};

// The sources a run-time load of sample_dialog.form binds: `owner`'s.
FormSources sample_sources(SampleOwner& owner) {
    FormSources sources{};
    sources.add("auto_save_", owner.auto_save_);
    sources.add("level_", owner.level_);
    sources.add("accept_", owner.accept_);
    sources.add("dismiss_", owner.dismiss_);
    return sources;
}

// The generated header builds the loader's tree with the loader's values.
void test_generated_matches_loader() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    Workspace workspace{};
    const std::string text = read_file(sample_document_path);
    const bool read = read_document(workspace, "sample_dialog.form", text, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "loader: the sample document's model builds");
    if (!read) return;
    // Declared before both forms: their controls hold bindings to it.
    SampleOwner owner{};
    const FormSources sources = sample_sources(owner);
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, sources,
        loaded, workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded, "loader: the sample document loads");
    if (outcome != FormLoadOutcome::loaded) return;

    form_generated::SampleDialogForm<SampleOwner> form{};
    form.initialize_component(owner);
    const Control::Ptr root = form.root_control();
    require(root != nullptr && same_shape(root, loaded.root), "generated: the loader's tree, in child order");
    for (const FormProperty& property : workspace.model.properties) {
        // A bound property's value is its source's, compared below.
        if (property.object.value == 0U || property.binding) continue;
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
    const Control::Ptr loaded_check = find_control(loaded.root, "sample.general.auto-save");
    const Control::Ptr loaded_level = find_control(loaded.root, "sample.level");
    const bool bound_alike =
        loaded_check != nullptr && loaded_level != nullptr && form.sample_general_auto_save != nullptr &&
        form.sample_level != nullptr &&
        (*loaded_check).property_value("checked") == (*form.sample_general_auto_save).property_value("checked") &&
        (*loaded_level).property_value("value") == (*form.sample_level).property_value("value") &&
        (*form.sample_level).value.read() == 25.0;
    require(bound_alike, "generated: the bound properties take their sources' values, as the loader's do");

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

// The document's bindings, made at creation: the check box takes its
// Value's state and writes the user's change back (two way, its
// property's default); the numeric value follows its Value one way (the
// document narrows it); the buttons run the owner's commands. A released
// control ends its bindings, so its source changes on alone.
void test_bindings() {
    SampleOwner owner{};
    std::weak_ptr<gui_forms::CheckBox> released{};
    {
        form_generated::SampleDialogForm<SampleOwner> form{};
        form.initialize_component(owner);
        require(form.sample_general_auto_save != nullptr && form.sample_level != nullptr &&
                    form.sample_ok != nullptr && form.sample_cancel != nullptr,
                "bindings: the bound controls");
        if (form.sample_general_auto_save == nullptr || form.sample_level == nullptr || form.sample_ok == nullptr ||
            form.sample_cancel == nullptr) {
            return;
        }
        gui_forms::CheckBox& check = *form.sample_general_auto_save;
        require(check.checked.read(), "bindings: the check box takes its Value's state");
        check.checked = false;
        require(!owner.auto_save_.get(), "bindings: two way, the check box's change reaches its Value");
        owner.auto_save_.set(true);
        require(check.checked.read(), "bindings: the Value's change reaches the check box");

        gui_forms::NumericUpDown& level = *form.sample_level;
        owner.level_.set(40.0);
        require(level.value.read() == 40.0, "bindings: one way, the Value's change reaches the control");
        level.value = 10.0;
        require(owner.level_.get() == 40.0, "bindings: one way, the control's change stays in the control");

        gui_forms::Window window(form.root_control(), gui_forms::Size{480.0, 320.0});
        static_cast<void>((*form.sample_ok).perform_click());
        static_cast<void>((*form.sample_cancel).perform_click());
        require(owner.accept_calls == 1 && owner.dismiss_calls == 1, "bindings: the buttons run their commands");
        released = form.sample_general_auto_save;
    }
    require(released.expired(), "bindings: the form's controls are released with it");
    owner.auto_save_.set(false);
    owner.level_.set(1.0);
    require(!owner.auto_save_.get() && owner.level_.get() == 1.0,
            "bindings: the sources change on alone once their controls are gone");
}

// The library calls under the generated code: what they refuse leaves the
// control as it was, and a disposed Value is no longer written.
void test_value_bindings() {
    const std::shared_ptr<gui_forms::CheckBox> check =
        gui_forms::make_control<gui_forms::CheckBox>(gui_forms::StableId("check"));
    gui_forms::Value<int> number{3};
    gui_forms::Value<bool> flag{true};
    int refusals = 0;
    try {
        gui_forms::bind_form_value(*check, "checked", number, gui_forms::PropertyBindingDirection::one_way);
    } catch (const std::invalid_argument&) {
        ++refusals;
    }
    try {
        gui_forms::bind_form_value(*check, "no_such_property", flag, gui_forms::PropertyBindingDirection::one_way);
    } catch (const std::invalid_argument&) {
        ++refusals;
    }
    require(refusals == 2 && !(*check).checked.read(), "values: another kind and an unknown property, refused");

    gui_forms::bind_form_value(*check, "checked", flag, gui_forms::PropertyBindingDirection::two_way);
    require((*check).checked.read(), "values: bound, the property takes the Value's state");
    flag.dispose();
    (*check).checked = false;
    require(!(*check).checked.read() && flag.get(), "values: a disposed Value is no longer written");
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
                                      "  button delete { command: bind(class); }\n"
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
        records[3].rule == "delete" && records[3].property == "command";
    if (!located) print_diagnostics(workspace);
    require(located, "refusals: the shared member, the digit, the keyword and the binding's source, located");

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
    require(form.sample_modes != nullptr && (*form.sample_modes).items.read().size() == 3U &&
                (*form.sample_modes).items[2] == "Wide FM",
            "items: the list box's items");
    require(form.sample_band != nullptr && (*form.sample_band).items.read().size() == 3U &&
                (*form.sample_band).items[0] == "HF",
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
    const FormSources no_sources{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, no_sources,
        loaded, workspace.diagnostics);
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
    require((*root).design_size.read() == gui_forms::Size{200.0, 100.0}, "page descriptors: the panel's design size");
    require((*group).design_size.read() == gui_forms::Size{50.0, 50.0}, "page descriptors: the group's design size");
    const gui_forms::FontSpec body{gui_forms::FontRole::content, 13.0, 400, true, 0.5};
    require((*italic).font.read() == body, "page descriptors: italic, then spacing");
    require((*check).indicator_style.read() == gui_forms::ChoiceIndicatorStyle::toggle,
            "page descriptors: the check box's indicator style");
    require((*radio).indicator_style.read() == gui_forms::ChoiceIndicatorStyle::modern,
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

// The refusal is staged complete (FD-5b): every writable colour or font
// property of every registry type is refused with E813 at its name unless
// its descriptor marks it as content, so a property a control adds later
// is covered without a test of its own. Each property is written in a
// document of its own, whose root is the type; the refusal comes before
// the value is read, so one value per kind serves every property.
void test_presentation_refused_registry_wide() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    std::size_t colours_refused = 0U;
    std::size_t fonts_refused = 0U;
    Workspace workspace{};
    for (const FormSchemaType& type : schema.types) {
        for (const gui_forms::PropertyDescriptor& property : type.properties) {
            const bool colour = property.kind == gui_forms::BindingValueKind::color;
            const bool font = property.kind == gui_forms::BindingValueKind::font;
            if (!(colour || font) || property.content_value || !property.writable) continue;
            std::string type_name = type.name;
            std::replace(type_name.begin(), type_name.end(), '_', '-');
            std::string property_name = property.name;
            std::replace(property_name.begin(), property_name.end(), '_', '-');
            const std::string_view value = colour ? "red" : "control 12";
            std::string document = type_name;
            document += " app.x { ";
            document += property_name;
            document += ": ";
            document += value;
            document += "; }\n";
            const bool built = read_document(workspace, "presentation.form", document, schema);
            bool refused = false;
            for (const ThemeDiagnostic& record : workspace.diagnostics.records()) {
                if (record.code == DiagnosticCode::form_presentation_property &&
                    record.detail.find("project theme") != std::string::npos) {
                    refused = true;
                }
            }
            if (!refused) {
                std::cerr << "  not refused: " << type.name << '.' << property.name << '\n';
                print_diagnostics(workspace);
            }
            require(!built && refused, "presentation: every colour and font property refused, naming the theme");
            if (colour && refused) ++colours_refused;
            if (font && refused) ++fonts_refused;
        }
    }
    require(colours_refused > 0U && fonts_refused > 0U,
            "presentation: the registry has colour and font properties, and each was refused");
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
    const FormSources no_sources{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, no_sources,
        loaded, workspace.diagnostics);
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
    require(generated.find("(*made.app_root).flow_break[*made.app_a] = gui_forms::assigned_property_value<"
                           "decltype((*made.app_root).flow_break)>(gui_forms::BindingValue{true});") !=
                std::string::npos,
            "attached: assigned through the parent's child view, after the child is added");

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

// A string's escapes reach both paths as the characters they stand for: the
// loader sets the decoded text, and the generator spells it as a C++
// literal (a line feed as an octal escape, a quote escaped).
void test_string_escapes() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "panel app.root {\n"
                                      "  label app.note { text: \"Two\\nlines \\\"quoted\\\" \\u{3A9}\"; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "escapes.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "escapes: the model builds");
    if (!read) return;
    const FormSources no_sources{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome = gui_forms::form_language::load_form(
        workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, no_sources,
        loaded, workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded, "escapes: the document loads");
    if (outcome != FormLoadOutcome::loaded) return;
    const std::shared_ptr<gui_forms::Label> note =
        std::dynamic_pointer_cast<gui_forms::Label>(find_control(loaded.root, "app.note"));
    require(note != nullptr && (*note).text.read() == "Two\nlines \"quoted\" \xCE\xA9",
            "escapes: the loader sets the decoded text");

    FormGeneratorOptions options = sample_options();
    options.class_name = "EscapesForm";
    options.source_name = "escapes.form";
    std::string generated{};
    require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
            "escapes: the generator writes the document");
    require(generated.find("\"Two\\012lines \\\"quoted\\\" \\316\\251\"") != std::string::npos,
            "escapes: the generated literal spells the decoded text");
}

// A field binding and a narrowed Value binding, spelled as the library
// calls the generated code makes.
void test_binding_spelling() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "user-control app.root {\n"
                                      "  text-box app.name { text: bind(records_, \"name\", update on-change); }\n"
                                      "  check-box app.flag { checked: bind(flag_, one_way); }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "bound.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "binding spelling: the model builds");
    if (!read) return;
    FormGeneratorOptions options = sample_options();
    options.class_name = "BoundForm";
    options.source_name = "bound.form";
    std::string generated{};
    require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
            "binding spelling: the generator writes the document");
    require(generated.find("gui_forms::bind_form_field(*made.app_name, \"text\", owner.records_, "
                           "std::string(\"name\"), gui_forms::PropertyBindingDirection::two_way, "
                           "gui_forms::FormUpdateTrigger::on_change);") != std::string::npos,
            "binding spelling: the field, two way by the text's default, on every change");
    require(generated.find("gui_forms::bind_form_value<gui_forms::BindingValueKind::boolean>(*made.app_flag, "
                           "\"checked\", owner.flag_, gui_forms::PropertyBindingDirection::one_way);") !=
                std::string::npos,
            "binding spelling: the Value, narrowed to one way, with the property's kind");
    require(generated.find("#include \"gui_forms/binding/form_binding/form_binding.hpp\"") != std::string::npos,
            "binding spelling: the generated header includes the binding calls");
}

// CLOSURE P-6: a property on a capability is assigned through its cell
// (a child property through the parent's child view); one not yet migrated
// is still written by name. The descriptors say which.
void test_capability_assignment() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view document = "grid-panel app.root {\n"
                                      "  check-box app.check { text: \"Wrap\"; tab-index: 2; area: \"side\"; }\n"
                                      "  label app.note { text: \"Note\"; cell: 1 0; }\n"
                                      "  span side { anchor: 0 0; }\n"
                                      "}\n";
    Workspace workspace{};
    const bool read = read_document(workspace, "cells.form", document, schema);
    if (!read) print_diagnostics(workspace);
    require(read, "capabilities: the model builds");
    if (!read) return;
    FormGeneratorOptions options = sample_options();
    options.class_name = "CellsForm";
    options.source_name = "cells.form";
    std::string generated{};
    require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
            "capabilities: the generator writes the document");
    require(generated.find("(*made.app_check).text = gui_forms::assigned_property_value<"
                           "decltype((*made.app_check).text)>(gui_forms::BindingValue{std::string(\"Wrap\")});") !=
                std::string::npos,
            "capabilities: a button's text is assigned through its cell");
    require(generated.find("(*made.app_check).tab_index = gui_forms::assigned_property_value<"
                           "decltype((*made.app_check).tab_index)>(gui_forms::BindingValue{std::uint64_t{2U}});") !=
                std::string::npos,
            "capabilities: Control's tab index is assigned through its cell");
    require(generated.find("(*made.app_root).area[*made.app_check] = gui_forms::assigned_property_value<"
                           "decltype((*made.app_root).area)>(gui_forms::BindingValue{std::string(\"side\")});") !=
                std::string::npos,
            "capabilities: a grid area is assigned through the parent's child view");
    require(generated.find("(*made.app_note).text = gui_forms::assigned_property_value<"
                           "decltype((*made.app_note).text)>(gui_forms::BindingValue{std::string(\"Note\")});") !=
                std::string::npos,
            "capabilities: a label's text is assigned through its cell");
    require(generated.find("(*made.app_root).set_child_property_value(*made.app_note, \"cell\"") !=
                std::string::npos,
            "capabilities: a grid cell, which converts through a named write, is written by name");
    require(generated.find("#include \"gui_forms/binding/property_cell_entry/property_cell_entry.hpp\"") !=
                std::string::npos,
            "capabilities: the generated header includes assigned_property_value");

    // The conversion the generated assignments use.
    const std::shared_ptr<gui_forms::CheckBox> check = gui_forms::make_control<gui_forms::CheckBox>(
        gui_forms::StableId("capabilities.check"));
    const std::uint32_t index = gui_forms::assigned_property_value<decltype((*check).tab_index)>(
        gui_forms::BindingValue{std::uint64_t{7U}});
    require(index == 7U, "assigned value: an unsigned literal narrowed to the cell's uint32");
    const gui_forms::AnchorStyles anchor = gui_forms::assigned_property_value<decltype((*check).anchor)>(
        gui_forms::BindingValue{gui_forms::PropertyEnumValue{.type_name = std::string("anchor_styles"),
                                                             .name = std::string("top, right"),
                                                             .value = std::int64_t{9}}});
    require(anchor == (gui_forms::AnchorStyles::top | gui_forms::AnchorStyles::right),
            "assigned value: an enumeration literal as the cell's enum");
    bool refused = false;
    try {
        const bool flag = gui_forms::assigned_property_value<decltype((*check).visible)>(
            gui_forms::BindingValue{gui_forms::Point{.x = 1.0, .y = 2.0}});
        static_cast<void>(flag);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "assigned value: a value with no form of the cell's type is refused");
    bool too_wide = false;
    try {
        const std::uint32_t wide = gui_forms::assigned_property_value<decltype((*check).tab_index)>(
            gui_forms::BindingValue{std::uint64_t{1U} << 40U});
        static_cast<void>(wide);
    } catch (const std::invalid_argument&) {
        too_wide = true;
    }
    require(too_wide, "assigned value: a value outside the cell's narrower type is refused");
}

// The scroll properties on every layout panel that scrolls its content
// (flex, grid, block and flow): a document sets them, the loader makes the
// panel with them, and the generator writes them.
void test_panel_scroll_properties() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::string_view panel_types[] = {"flex-panel", "grid-panel", "block-panel", "flow-layout-panel-compat"};
    for (const std::string_view panel_type : panel_types) {
        const std::string what = "scroll properties on " + std::string(panel_type) + ": ";
        const std::string document = std::string(panel_type) +
                                     " app.root {\n"
                                     "  auto-scroll: true;\n"
                                     "  auto-scroll-margin: 4 8;\n"
                                     "  auto-scroll-min-size: 200 300;\n"
                                     "  button app.a { text: \"A\"; }\n"
                                     "}\n";
        Workspace workspace{};
        const bool read = read_document(workspace, "scroll.form", document, schema);
        if (!read) print_diagnostics(workspace);
        require(read, what + "the model builds");
        if (!read) continue;
        const FormSources no_sources{};
        LoadedForm loaded{};
        const FormLoadOutcome outcome = gui_forms::form_language::load_form(
            workspace.source, workspace.tokens, workspace.lines, workspace.tree, schema, workspace.model, no_sources,
            loaded, workspace.diagnostics);
        require(outcome == FormLoadOutcome::loaded, what + "the document loads");
        const std::shared_ptr<gui_forms::ScrollableControl> panel =
            std::dynamic_pointer_cast<gui_forms::ScrollableControl>(loaded.root);
        require(panel != nullptr, what + "the root is a scrollable control");
        if (panel == nullptr) continue;
        require((*panel).auto_scroll.read(), what + "auto-scroll set");
        require((*panel).auto_scroll_margin.read() == gui_forms::Size{4.0, 8.0}, what + "auto-scroll-margin set");
        require((*panel).auto_scroll_min_size.read() == gui_forms::Size{200.0, 300.0},
                what + "auto-scroll-min-size set");
        FormGeneratorOptions options = sample_options();
        options.class_name = "ScrollForm";
        options.source_name = "scroll.form";
        std::string generated{};
        require(generate(workspace, schema, options, generated) == FormGenerateOutcome::generated,
                what + "the generator writes the document");
        require(generated.find("(*made.app_root).auto_scroll = ") != std::string::npos,
                what + "the generator assigns auto_scroll");
    }
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
    test_bindings();
    test_value_bindings();
    test_golden();
    test_fonts_and_items();
    test_presentation_fonts();
    test_presentation_refused_registry_wide();
    test_attached_properties();
    test_spans();
    test_page_descriptors();
    test_refusals();
    test_member_names();
    test_binding_spelling();
    test_string_escapes();
    test_capability_assignment();
    test_panel_scroll_properties();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "form generator tests passed\n";
    return 0;
}
