// The composite control's C++ generator (cp34, phase 2;
// control_generator.hpp): the header for a compiled definition embeds the
// document, lists the child types it names with their C++ classes and
// headers, constructs from a stable ID and has typed read_ and write_
// functions for the boolean, number and text properties; a class name that
// is no identifier, a child type without a header and a document holding
// the raw literal's delimiter are refused with E812.

#include "../../src/core/control_language/composite_definition/composite_definition.hpp"
#include "../../src/core/control_language/control_generator/control_generator.hpp"
#include "../../src/core/control_language/control_model/control_model.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/user_control.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#ifndef GUI_FORMS_COMPOSITES_DIR
#error "GUI_FORMS_COMPOSITES_DIR names demo/composites"
#endif

namespace {

using namespace gui_forms;
using control_language::ControlGenerateOutcome;
using control_language::ControlGeneratorOptions;
using form_language::FormSchema;
using form_language::FormSchemaType;
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

FormSchemaType described(FormSchemaType type, const std::string_view cpp_type, const std::string_view header) {
    type.cpp_type = std::string(cpp_type);
    type.header = std::string(header);
    return type;
}

// The examples' types with their C++ classes and headers, as the registry
// schema describes them; `label_header` empty leaves the label without one.
FormSchema generator_schema(const std::string_view label_header) {
    FormSchema schema{};
    schema.types.push_back(described(form_language::form_control_type<UserControl>("user_control"),
                                     "gui_forms::UserControl", "gui_forms/controls/user_control.hpp"));
    schema.types.push_back(described(form_language::form_control_type<FlexPanel>("flex_panel"),
                                     "gui_forms::FlexPanel", "gui_forms/controls/flex_panel.hpp"));
    schema.types.push_back(
        described(form_language::form_control_type<Label>("label"), "gui_forms::Label", label_header));
    schema.types.push_back(described(form_language::form_control_type<Button>("button"), "gui_forms::Button",
                                     "gui_forms/controls/button.hpp"));
    return schema;
}

struct Generated final {
    ControlGenerateOutcome outcome{ControlGenerateOutcome::refused};
    std::string header{};
    ThemeDiagnosticList diagnostics{};
};

Generated generate(const std::string_view file, const std::string& text, const std::string_view class_name,
                   const std::string_view label_header) {
    Generated result{};
    ThemeSource source{};
    source.file = file;
    source.text = text;
    FormSchema schema = generator_schema(label_header);
    control_language::ControlSyntax syntax{};
    control_language::ControlModel model{};
    const control_language::ControlModelOutcome read =
        control_language::read_control_model(source, syntax, schema, model, result.diagnostics);
    require(read == control_language::ControlModelOutcome::model_built, "the document reads");
    if (read != control_language::ControlModelOutcome::model_built) return result;
    const std::shared_ptr<const CompositeDefinition> definition =
        control_language::compile_composite_definition(source, syntax, model, std::move(schema), result.diagnostics);
    require(definition != nullptr, "the document compiles");
    if (definition == nullptr) return result;
    ControlGeneratorOptions options{};
    options.class_name = std::string(class_name);
    options.namespace_name = "composite_generated";
    options.source_name = std::string(file);
    result.outcome =
        control_language::generate_control_cpp(source, *definition, options, result.header, result.diagnostics);
    return result;
}

bool holds(const std::string& text, const std::string_view part) {
    const bool found = text.find(part) != std::string::npos;
    return found;
}

bool refused_with_e812(const Generated& generated) {
    if (generated.outcome != ControlGenerateOutcome::refused || !generated.header.empty()) return false;
    for (const ThemeDiagnostic& record : generated.diagnostics.records()) {
        if (record.code == DiagnosticCode::form_generation_refused) return true;
    }
    return false;
}

void toggle_pill_header() {
    const std::string text = read_example("toggle-pill.control");
    const Generated generated = generate("toggle-pill.control", text, "TogglePill", "gui_forms/controls/label.hpp");
    require(generated.outcome == ControlGenerateOutcome::generated, "the pill's header is generated");
    const std::string& header = generated.header;
    require(holds(header, "namespace composite_generated {"), "the header opens its namespace");
    require(holds(header, "class TogglePill : public gui_forms::CompositeControl {"), "the class derives");
    require(holds(header, "document_name = \"toggle-pill.control\""), "the document is named");
    require(holds(header, "R\"gfcontrol(") && holds(header, "name=\"toggle-pill\""), "the document is embedded");
    require(holds(header, "std::array<gui_forms::CompositeChildType, 0> child_types{};"),
            "the pill names no child types");
    require(holds(header, "explicit TogglePill(gui_forms::StableId stable_id)"), "made from a stable ID");
    require(holds(header, "bool read_checked() const") && holds(header, "void write_checked(bool value)"),
            "checked has typed functions");
    require(holds(header, "std::string read_text() const"), "text reads as std::string");
    require(!holds(header, "drag_position"), "a presentational property has none");
}

void card_header() {
    const std::string text = read_example("card-with-action.control");
    const Generated generated =
        generate("card-with-action.control", text, "CardWithAction", "gui_forms/controls/label.hpp");
    require(generated.outcome == ControlGenerateOutcome::generated, "the card's header is generated");
    const std::string& header = generated.header;
    require(holds(header, "std::array<gui_forms::CompositeChildType, 3> child_types{"),
            "the card names three child types, the base aside");
    require(holds(header, "gui_forms::composite_child_type<gui_forms::Label>(\"label\")"), "the label is listed");
    require(holds(header, "#include \"gui_forms/controls/button.hpp\""), "the button's header is included");
    require(!holds(header, "read_action_command"), "a command property has no typed functions");
}

void refusals() {
    const std::string pill = read_example("toggle-pill.control");
    require(refused_with_e812(generate("toggle-pill.control", pill, "Not A Name", "gui_forms/controls/label.hpp")),
            "a class name that is no identifier is refused");
    const std::string card = read_example("card-with-action.control");
    require(refused_with_e812(generate("card-with-action.control", card, "CardWithAction", "")),
            "a child type without a header is refused");
    std::string closed = pill;
    const std::size_t comment = closed.find("-->");
    closed.insert(comment, " )gfcontrol\" ");
    require(refused_with_e812(generate("toggle-pill.control", closed, "TogglePill", "gui_forms/controls/label.hpp")),
            "a document holding the delimiter is refused");
}

} // namespace

int main() {
    toggle_pill_header();
    card_header();
    refusals();
    if (failures != 0) {
        std::cerr << failures << " control generator check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_control_generator_tests: all checks passed\n";
    return 0;
}
