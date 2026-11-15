// Composite controls in form documents (cp34; composite_forms.hpp). Gate:
// - a form loaded at run time names a composite by its document's name,
//   sets its registered properties, and its children go into the slot in
//   document order (card-with-action);
// - a composite without a slot refuses a form's children (E809,
//   toggle-pill);
// - the generated code writes a slotted parent's children through slot();
// - form_control_type describes a composite class (as generated) with its
//   slot placement, and a slotless one as refusing children.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "../../src/controls/composite_control/composite_forms.hpp"
#include "../../src/core/form_language/form_generator/form_generator.hpp"
#include "../../src/core/form_language/form_loader/form_loader.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/user_control.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
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
using form_language::FormChildPlacement;
using form_language::FormGenerateOutcome;
using form_language::FormGeneratorOptions;
using form_language::FormLoadOutcome;
using form_language::FormModel;
using form_language::FormModelOutcome;
using form_language::FormSchema;
using form_language::FormSchemaType;
using form_language::FormSources;
using form_language::FormSyntax;
using form_language::LoadedForm;
using theme_language::DiagnosticCode;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;

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

constexpr std::array<CompositeChildType, 4> example_child_types{
    composite_child_type<FlexPanel>("flex_panel"),
    composite_child_type<Label>("label"),
    composite_child_type<Button>("button"),
    composite_child_type<TextBox>("text_box"),
};

std::shared_ptr<const CompositeDefinition> load_example(const std::string_view name) {
    const std::string text = read_example(name);
    const CompositeLoadResult loaded = load_composite_definition(name, text, example_child_types);
    for (const std::string& line : loaded.diagnostics) std::cerr << "  " << line << '\n';
    if (!loaded) throw std::runtime_error("an example was refused");
    return loaded.definition;
}

// The classes gui_forms_add_control would generate, over the examples: one
// definition per class, loaded on first use.
class TestCard final : public CompositeControl {
public:
    explicit TestCard(StableId stable_id) : CompositeControl(std::move(stable_id), shared_definition()) {}
    [[nodiscard]] static std::shared_ptr<const CompositeDefinition> shared_definition() {
        static const std::shared_ptr<const CompositeDefinition> made = load_example("card-with-action.control");
        return made;
    }
};

class TestPill final : public CompositeControl {
public:
    explicit TestPill(StableId stable_id) : CompositeControl(std::move(stable_id), shared_definition()) {}
    [[nodiscard]] static std::shared_ptr<const CompositeDefinition> shared_definition() {
        static const std::shared_ptr<const CompositeDefinition> made = load_example("toggle-pill.control");
        return made;
    }
};

// The schema a run-time load uses: the stock types, then the composites
// from their definitions.
FormSchema run_time_schema() {
    FormSchema schema{};
    schema.types.push_back(form_language::form_control_type<UserControl>("user_control"));
    schema.types.push_back(form_language::form_control_type<Label>("label"));
    schema.types.push_back(form_language::form_control_type<Button>("button"));
    schema.types.push_back(composite_form_type(TestCard::shared_definition()));
    schema.types.push_back(composite_form_type(TestPill::shared_definition()));
    return schema;
}

constexpr std::string_view slotted_form = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<user-control xmlns="urn:gui-forms:form:1" id="app.root">
  <card-with-action id="app.card" title="Storage" action-text="Manage">
    <label id="app.first" text="First"/>
    <button id="app.second" text="Second"/>
  </card-with-action>
  <toggle-pill id="app.pill" checked="true"/>
</user-control>
)xml";

constexpr std::string_view refused_form = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<user-control xmlns="urn:gui-forms:form:1" id="app.root">
  <toggle-pill id="app.pill">
    <label id="app.inside" text="Inside"/>
  </toggle-pill>
</user-control>
)xml";

struct Workspace final {
    ThemeSource source{};
    FormSyntax syntax{};
    FormModel model{};
    ThemeDiagnosticList diagnostics{};
};

FormModelOutcome build(Workspace& workspace, const std::string_view text, const FormSchema& schema) {
    workspace.source.file = "composites.form";
    workspace.source.text = text;
    const FormModelOutcome built = form_language::read_form_model(workspace.source, workspace.syntax, schema,
                                                                  workspace.model, workspace.diagnostics);
    return built;
}

bool reported(const ThemeDiagnosticList& diagnostics, const DiagnosticCode code) {
    for (const ThemeDiagnostic& record : diagnostics.records()) {
        if (record.code == code) return true;
    }
    return false;
}

std::optional<std::string> text_of(const Control& control, const std::string_view property) {
    const std::optional<BindingValue> value = control.property_value(property);
    const std::string* held = value ? std::get_if<std::string>(&*value) : nullptr;
    if (held == nullptr) return std::nullopt;
    return *held;
}

void run_time_slot() {
    const FormSchema schema = run_time_schema();
    Workspace workspace{};
    require(build(workspace, slotted_form, schema) == FormModelOutcome::model_built, "the slotted form builds");
    const FormSources sources{};
    LoadedForm loaded{};
    const FormLoadOutcome outcome =
        form_language::load_form(workspace.source, schema, workspace.model, sources, loaded, workspace.diagnostics);
    require(outcome == FormLoadOutcome::loaded && loaded.root != nullptr, "the slotted form loads");
    if (outcome != FormLoadOutcome::loaded || loaded.root == nullptr) return;
    require((*loaded.root).child_count() == 2U, "the root holds the card and the pill");
    // Objects in document order: root, card, first, second, pill.
    const std::shared_ptr<CompositeControl> card = std::dynamic_pointer_cast<CompositeControl>(loaded.objects[1]);
    const std::shared_ptr<CompositeControl> pill = std::dynamic_pointer_cast<CompositeControl>(loaded.objects[4]);
    require(card != nullptr && pill != nullptr, "the composites are made from their definitions");
    if (card == nullptr || pill == nullptr) return;
    require((*card).stable_id().value() == "app.card", "the card has the document's stable ID");
    const std::optional<std::string> title = text_of(*card, "title");
    require(title && *title == "Storage", "the form sets the card's title");
    const std::optional<BindingValue> checked = (*pill).property_value("checked");
    const bool* on = checked ? std::get_if<bool>(&*checked) : nullptr;
    require(on != nullptr && *on, "the form sets the pill's checked");
    const Control::Ptr content = (*card).slot();
    require(content != nullptr, "the card has its slot");
    if (content == nullptr) return;
    const Control::ChildView slotted = (*content).children();
    const Control::Ptr first = std::dynamic_pointer_cast<Control>(loaded.objects[2]);
    const Control::Ptr second = std::dynamic_pointer_cast<Control>(loaded.objects[3]);
    require(slotted.size() == 2U && slotted[0] == first && slotted[1] == second,
            "the form's children go into the slot in document order");
    require((*card).child("title") != nullptr && (*(*card).child("title")).parent() != content,
            "the composite's own children stay outside the slot");
}

void slotless_refused() {
    const FormSchema schema = run_time_schema();
    Workspace workspace{};
    require(build(workspace, refused_form, schema) == FormModelOutcome::refused, "a slotless composite's children");
    require(reported(workspace.diagnostics, DiagnosticCode::form_children_not_admitted), "are refused with E809");
}

void generated_slot() {
    FormSchema schema = run_time_schema();
    schema.types[0].cpp_type = "gui_forms::UserControl";
    schema.types[0].header = "gui_forms/controls/user_control.hpp";
    schema.types[1].cpp_type = "gui_forms::Label";
    schema.types[1].header = "gui_forms/controls/label.hpp";
    schema.types[2].cpp_type = "gui_forms::Button";
    schema.types[2].header = "gui_forms/controls/button.hpp";
    schema.types[3].cpp_type = "app::CardWithAction";
    schema.types[3].header = "card_with_action_control.hpp";
    schema.types[4].cpp_type = "app::TogglePill";
    schema.types[4].header = "toggle_pill_control.hpp";
    Workspace workspace{};
    require(build(workspace, slotted_form, schema) == FormModelOutcome::model_built, "the generator's form builds");
    FormGeneratorOptions options{};
    options.class_name = "CompositeForm";
    options.source_name = "composites.form";
    std::string generated{};
    const FormGenerateOutcome outcome = form_language::generate_form_cpp(workspace.source, schema, workspace.model,
                                                                         options, generated, workspace.diagnostics);
    require(outcome == FormGenerateOutcome::generated, "the form generates");
    require(generated.find(".slot()).add_child(") != std::string::npos, "the card's children go through slot()");
    require(generated.find("#include \"card_with_action_control.hpp\"") != std::string::npos,
            "the composite's generated header is included");
}

void described_classes() {
    const FormSchemaType card = form_language::form_control_type<TestCard>("card_with_action");
    require(card.child_placement == FormChildPlacement::slot && card.find_slot != nullptr,
            "a slotted composite class takes children into its slot");
    const FormSchemaType pill = form_language::form_control_type<TestPill>("toggle_pill");
    require(pill.child_placement == FormChildPlacement::refused, "a slotless composite class refuses children");
    const FormSchemaType label = form_language::form_control_type<Label>("label");
    require(label.child_placement == FormChildPlacement::self, "a stock control holds its own children");
    const FormSchemaType run_time = composite_form_type(TestCard::shared_definition());
    require(run_time.name == "card_with_action" && run_time.create_composite != nullptr &&
                run_time.composite == TestCard::shared_definition(),
            "a run-time composite type is made from its definition");
}

} // namespace

int main() {
    try {
        run_time_slot();
        slotless_refused();
        generated_slot();
        described_classes();
    } catch (const std::exception& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " composite form check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_form_tests: all checks passed\n";
    return 0;
}
