// A form document from arbitrary bytes (audit N115; XML since cp34): read as
// XML, its model built against a sample schema, and loaded into controls
// when the model builds. Invariants: a refused read reports exactly one
// error; a model that is not built, or a load that is refused, reports at
// least one error; every diagnostic lies within the document; a load leaves
// a root control and an object slot for every model object; and a second
// model build of the same syntax gives the same outcome and diagnostics.

#include "../src/core/form_language/form_loader/form_loader.hpp"
#include "../src/core/form_language/form_model/form_model.hpp"
#include "../src/core/form_language/form_xml/form_xml.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/grid_panel.hpp"
#include "gui_forms/controls/group_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/table_layout_panel_compat.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/controls/value_bar.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace {

namespace form_language = gui_forms::form_language;
namespace theme_language = gui_forms::theme_language;

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("form document fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

// form_loader_tests' sample schema: a user control, a group, a check box, a
// button, a numeric up-down, a text box, a value bar, a command, a panel the
// loader cannot make, the form root, a grid and a table.
form_language::FormSchema sample_schema() {
    form_language::FormSchema schema{};
    schema.types.push_back(form_language::form_control_type<gui_forms::UserControl>("user_control"));
    schema.types.push_back(form_language::form_control_type<gui_forms::GroupBox>("group_box"));
    schema.types.push_back(form_language::form_control_type<gui_forms::CheckBox>("check_box"));
    schema.types.push_back(form_language::form_control_type<gui_forms::Button>("button"));
    schema.types.push_back(form_language::form_control_type<gui_forms::NumericUpDown>("numeric_up_down"));
    schema.types.push_back(form_language::form_control_type<gui_forms::TextBox>("text_box"));
    schema.types.push_back(form_language::form_control_type<gui_forms::ValueBar>("value_bar"));
    schema.types.push_back(form_language::form_component_type<gui_forms::Command>("command"));
    const std::shared_ptr<gui_forms::Panel> panel =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("prototype"));
    schema.types.push_back(form_language::describe_form_type("panel", *panel));
    schema.types.push_back(form_language::form_root_type());
    schema.types.push_back(form_language::form_control_type<gui_forms::GridPanel>("grid_panel"));
    schema.types.push_back(
        form_language::form_control_type<gui_forms::TableLayoutPanelCompat>("table_layout_panel_compat"));
    return schema;
}

// Whether every record's offset lies within the document's `size` bytes.
bool records_within(std::span<const theme_language::ThemeDiagnostic> records, std::size_t size) noexcept {
    for (const theme_language::ThemeDiagnostic& record : records) {
        if (record.position.offset.value > size) return false;
    }
    return true;
}

bool same_records(std::span<const theme_language::ThemeDiagnostic> first,
                  std::span<const theme_language::ThemeDiagnostic> second) noexcept {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        if (!(first[index] == second[index])) return false;
    }
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const form_language::FormSchema schema = sample_schema();
    theme_language::ThemeSource source{};
    source.file = "fuzz.form";
    source.text = std::string_view(reinterpret_cast<const char*>(data), size);
    form_language::FormSyntax syntax{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormSyntaxOutcome read = form_language::read_form_syntax(source, syntax, diagnostics);
    if (read != form_language::FormSyntaxOutcome::read) {
        if (diagnostics.error_count() != 1U) fail("a refused read reports one error");
        if (!records_within(diagnostics.records(), size)) fail("a read's diagnostic lies in the document");
        return 0;
    }
    if (syntax.locations.elements.empty()) fail("a read document has its root's location");

    const std::size_t parse_errors = diagnostics.error_count();
    const std::size_t parse_records = diagnostics.records().size();
    form_language::FormModel model{};
    const form_language::FormModelOutcome built =
        form_language::build_form_model(source, syntax, schema, model, diagnostics);
    if (!records_within(diagnostics.records(), size)) fail("model diagnostics lie in the document");
    theme_language::ThemeDiagnosticList again_diagnostics{};
    form_language::FormModel again{};
    const form_language::FormModelOutcome rebuilt =
        form_language::build_form_model(source, syntax, schema, again, again_diagnostics);
    if (rebuilt != built) fail("building a model is repeatable");
    const std::span<const theme_language::ThemeDiagnostic> model_records =
        diagnostics.records().subspan(parse_records);
    if (!same_records(model_records, again_diagnostics.records())) fail("model diagnostics are repeatable");
    if (built != form_language::FormModelOutcome::model_built) {
        if (diagnostics.error_count() == parse_errors) fail("a refused model reports an error");
        return 0;
    }

    const std::size_t model_errors = diagnostics.error_count();
    const form_language::FormSources no_sources{};
    form_language::LoadedForm loaded{};
    const form_language::FormLoadOutcome outcome =
        form_language::load_form(source, schema, model, no_sources, loaded, diagnostics);
    if (outcome == form_language::FormLoadOutcome::refused) {
        if (diagnostics.error_count() == model_errors) fail("a refused load reports an error");
        return 0;
    }
    if (loaded.root == nullptr && !loaded.window) fail("a load makes a root");
    if (loaded.objects.size() != model.objects.size()) fail("a load has a slot for every model object");
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The seeds are documents of the sample schema's types, with properties,
// a binding the load refuses (no sources) and a form root; the mutations
// explore around them.
int main(int argument_count, char** argument_values) {
    const std::array<std::string_view, 3> seeds{
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"settings.root\" back-color=\"#336699\">\n"
        "  <group-box id=\"settings.general\">\n"
        "    <check-box id=\"settings.autosave\" text=\"Save automatically\" checked=\"true\" tab-index=\"2\"/>\n"
        "    <button id=\"settings.apply\" text=\"Apply\"/>\n"
        "  </group-box>\n"
        "  <!-- a comment -->\n"
        "  <numeric-up-down id=\"settings.count\"/>\n"
        "  <value-bar id=\"settings.level\"/>\n"
        "  <command id=\"settings.save\"/>\n"
        "  <button id=\"settings.ok\" text=\"OK &amp; done&#10;\"/>\n"
        "</user-control>\n",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<form xmlns=\"urn:gui-forms:form:1\" id=\"settings.dialog\" text=\"Settings\" size=\"480 320\"\n"
        "    accept-button=\"settings.ok\">\n"
        "  <text-box id=\"settings.name\" text=\"bind(settings_, 'name', update on-change)\"/>\n"
        "  <button id=\"settings.ok\" text=\"OK\" command=\"bind(accept_)\"/>\n"
        "</form>\n",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<user-control xmlns=\"urn:gui-forms:form:1\" id=\"grid.root\">\n"
        "  <grid-panel id=\"grid.cells\"><span name=\"head\" anchor=\"0 0\" columns=\"2\"/>"
        "<button id=\"grid.a\" area=\"head\" text=\"A\"/></grid-panel>\n"
        "  <table-layout-panel-compat id=\"grid.table\">"
        "<button id=\"grid.b\" text=\"B\"/></table-layout-panel-compat>\n"
        "  <panel id=\"grid.unmade\"/>\n"
        "</user-control>\n"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "form document";
    run.seeds = seeds;
    run.mutated_runs = 20'000U;
    run.random_state = 0x428a2f98d728ae22ULL;
    run.first_fill = '<';
    run.second_fill = '"';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
