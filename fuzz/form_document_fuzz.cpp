// A form document from arbitrary bytes (audit N115): tokenized and parsed
// as a form document, its model built against a sample schema, and loaded
// into controls when the model builds. Invariants: a model that is not
// built, or a load that is refused, reports at least one error; a load
// leaves a root control and an object slot for every model object; and a
// second model build of the same tree gives the same outcome and
// diagnostics.

#include "../src/core/form_language/form_loader/form_loader.hpp"
#include "../src/core/form_language/form_model/form_model.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/gui_forms.hpp"
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
    theme_language::ThemeTokenBuffer tokens{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, tokens, lines, diagnostics));
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed = theme_language::parse_theme_sheet(
        source, tokens, lines, tree, diagnostics, theme_language::ThemeSheetForm::form_document);
    if (parsed != theme_language::ParseOutcome::tree_complete) return 0;

    const std::size_t parse_errors = diagnostics.error_count();
    const std::size_t parse_records = diagnostics.records().size();
    form_language::FormModel model{};
    const form_language::FormModelOutcome built =
        form_language::build_form_model(source, tokens, lines, tree, schema, model, diagnostics);
    theme_language::ThemeDiagnosticList again_diagnostics{};
    form_language::FormModel again{};
    const form_language::FormModelOutcome rebuilt =
        form_language::build_form_model(source, tokens, lines, tree, schema, again, again_diagnostics);
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
        form_language::load_form(source, tokens, lines, tree, schema, model, no_sources, loaded, diagnostics);
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
        "user-control settings.root {\n"
        "  back-color: #336699;\n"
        "  group-box settings.general {\n"
        "    check-box settings.autosave { text: \"Save automatically\"; checked: true; tab-index: 2; }\n"
        "    button settings.apply { text: \"Apply\"; }\n"
        "  }\n"
        "  numeric-up-down settings.count { }\n"
        "  value-bar settings.level { }\n"
        "  command settings.save { }\n"
        "  button settings.ok { text: \"OK\"; }\n"
        "}\n",
        "form settings.dialog {\n"
        "  text: \"Settings\";\n"
        "  size: 480 320;\n"
        "  accept-button: settings.ok;\n"
        "  text-box settings.name { text: bind(settings_, \"name\", update on-change); }\n"
        "  button settings.ok { text: \"OK\"; command: bind(accept_); }\n"
        "}\n",
        "user-control grid.root {\n"
        "  grid-panel grid.cells { button grid.a { text: \"A\"; } }\n"
        "  table-layout-panel-compat grid.table { button grid.b { text: \"B\"; } }\n"
        "  panel grid.unmade { }\n"
        "}\n"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "form document";
    run.seeds = seeds;
    run.mutated_runs = 20'000U;
    run.random_state = 0x428a2f98d728ae22ULL;
    run.first_fill = '{';
    run.second_fill = '"';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
