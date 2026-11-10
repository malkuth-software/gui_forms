// The theme compiler over arbitrary sheets (audit N115): the stage after
// theme_sheet_fuzz's tokenizer and parser, through selectors, values,
// custom-property and palette enumeration and lowering. Invariants: a
// compiled sheet has no error and a refused one at least one; the counted
// work stays within the hostile suite's bound per input byte
// (theme_hostile_tests); and a second compilation into fresh storage gives
// the same outcome and diagnostics.
//
// The standalone program (GUI_FORMS_FUZZ_STANDALONE) runs the theme sheet
// corpus, whose palette-enumeration-bound.css reads fourteen roles with a
// light and a dark variant each (2^14 palette combinations): it must reach
// the palette bound (N113, E508), or the program fails.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../src/core/theme_language/custom_properties/theme_custom_properties.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/selector/theme_selector.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "gui_forms/gui_forms.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;
namespace theme_language = gui_forms::theme_language;
using theme_language::DiagnosticCode;

// theme_hostile_tests' bound: operations per input byte, plus a constant
// for the smallest inputs.
constexpr std::uint64_t maximum_operations_per_byte = 4U * theme_language::maximum_variable_combinations;
constexpr std::uint64_t operations_allowance = 4096U;

// Inputs whose compilation stopped at the palette bound, counted for the
// standalone program's check: E508 naming maximum_variable_steps (the
// custom-property bound reports E508 naming maximum_variable_combinations).
std::size_t palette_bound_reached = 0U;

constexpr std::array<PartDeclaration, 1> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
constexpr std::array<AnatomyState, 2> button_states{{AnatomyState::hot, AnatomyState::pressed}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = button_parts, .states = button_states, .custom_states = {}, .item_systems = {}},
}};

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("theme compile fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

struct Compilation final {
    theme_language::CompileOutcome outcome{theme_language::CompileOutcome::refused};
    theme_language::ThemeDiagnosticList diagnostics{};
    std::uint64_t operations{};
};

// Compiles `text` into fresh storage.
void compile(std::string_view text, Compilation& compilation) {
    const theme_language::ThemeSource source{.file = "fuzz.theme.css", .text = text};
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = builtin_primitive_registry(),
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  .published_values = {},
                                                  .resources = {},
                                                  .images = {},
                                                  .requires_engine = current_engine_version};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    compilation.outcome = compiler.compile(input, sheet, compilation.diagnostics);
    compilation.operations = compiler.work().operations();
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
    static const std::string palette_bound_detail = std::to_string(theme_language::maximum_variable_steps);
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    Compilation first{};
    compile(text, first);
    const std::size_t errors = first.diagnostics.error_count();
    const bool compiled = first.outcome == theme_language::CompileOutcome::compiled;
    if (compiled && errors != 0U) fail("a compiled sheet has no error");
    if (!compiled && errors == 0U) fail("a refused sheet has an error");
    if (first.operations > maximum_operations_per_byte * size + operations_allowance) {
        fail("the work is bounded per input byte");
    }
    for (const theme_language::ThemeDiagnostic& record : first.diagnostics.records()) {
        const bool palette_bound = record.code == DiagnosticCode::too_many_variable_combinations &&
                                   record.detail == palette_bound_detail;
        if (palette_bound) ++palette_bound_reached;
    }
    Compilation second{};
    compile(text, second);
    if (second.outcome != first.outcome) fail("compiling is repeatable");
    if (!same_records(first.diagnostics.records(), second.diagnostics.records())) fail("diagnostics are repeatable");
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The corpus directories on the command line (fuzz/corpus/theme_sheet in the
// suite), then the seeds below, then mutations of them all. With a corpus,
// the palette bound must have been reached.
int main(int argument_count, char** argument_values) {
    const std::array<std::string_view, 3> seeds{
        "button:hover > part(face) { background: rgb(10 20 30 / 50%); opacity: 0.5 }\n"
        "button { --r: 4px; corner-radius: calc(var(--r) * 2) }\n",
        ":root { ink-on-paper: #102030 on #f0f0e0 }\n"
        "@media (prefers-color-scheme: dark) { :root { ink-on-paper: #f0f0e0 on #102030 } }\n"
        "#a { color: color-mix(in srgb, role(ink-on-paper), red 30%) }\n",
        "@keyframes pulse { from { opacity: 1 } to { opacity: 0.5 } }\n"
        "button:pressed { animation: pulse 120ms; }\n"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "theme compile";
    run.seeds = seeds;
    run.mutated_runs = 4'000U;
    run.random_state = 0x3c6ef372fe94f82bULL;
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    if (status != 0) return status;
    if (counts.corpus_files != 0U && palette_bound_reached == 0U) {
        std::fputs("theme compile fuzz: no corpus input reached the palette bound (N113)\n", stderr);
        return 1;
    }
    std::printf("theme compile fuzz: %zu inputs reached the palette bound\n", palette_bound_reached);
    return 0;
}
#endif
