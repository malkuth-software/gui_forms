// The theme language's hostile input suite (stage T3, phase P11; PLAN.md
// section 6, item 3): each owner limit (E2) and each derived limit (R2-14,
// the round 002 addendum, P12) at its value compiles, and one past it is
// refused with its code. The limits a package alone reaches (the import
// count, E506) and the lowered size (E513, theme_compiler_tests) are
// tested where their inputs are built.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../../src/core/theme_language/custom_properties/theme_custom_properties.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/motion/theme_motion_lowering.hpp"
#include "../../src/core/theme_language/selector/theme_selector.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;
namespace theme_language = gui_forms::theme_language;
using theme_language::DiagnosticCode;

int failures = 0;

// The work bound (PLAN section 6 item 3): every compilation here, at a
// limit or past it, does at most this many counted operations
// (ThemeCompileWork::operations) per input byte, plus a constant for the
// smallest inputs. A rule reading varying custom properties is lowered
// once per combination, at most maximum_variable_combinations times, so
// the bound is a few operations per byte for each copy; measured at
// 2026-11-01, the worst input (the combinations at their limit) does 276.
// The worst ratio seen is printed.
constexpr std::uint64_t maximum_operations_per_byte = 4U * theme_language::maximum_variable_combinations;
constexpr std::uint64_t operations_allowance = 4096U;
double worst_ratio = 0.0;
std::string worst_input{};

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::array<PartDeclaration, 1> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
constexpr std::array<AnatomyState, 2> button_states{{AnatomyState::hot, AnatomyState::pressed}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = button_parts, .states = button_states, .custom_states = {}, .item_systems = {}},
}};

struct Outcome final {
    bool compiled{};
    // The first error's code, if any.
    std::optional<DiagnosticCode> error{};
    std::size_t records{};
    std::size_t warnings{};
    std::size_t dropped{};
};

Outcome compile(std::string_view text) {
    const theme_language::ThemeSource source{.file = "hostile.theme.css", .text = text};
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = builtin_primitive_registry(),
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  .published_values = {},
                                                  .resources = {},
                                                  .images = {},
                                                  .requires_engine = current_engine_version};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    Outcome outcome{};
    outcome.compiled = compiler.compile(input, sheet, diagnostics) == theme_language::CompileOutcome::compiled;
    const std::uint64_t operations = compiler.work().operations();
    const std::uint64_t bytes = text.size();
    const double ratio = static_cast<double>(operations) / static_cast<double>(bytes == 0U ? 1U : bytes);
    if (ratio > worst_ratio) {
        worst_ratio = ratio;
        worst_input = std::string(text.substr(0U, 48U));
    }
    if (operations > maximum_operations_per_byte * bytes + operations_allowance) {
        std::cerr << "  " << operations << " operations for " << bytes << " bytes: " << text.substr(0U, 48U) << '\n';
        require(false, "the work is bounded per input byte");
    }
    for (const theme_language::ThemeDiagnostic& diagnostic : diagnostics.records()) {
        const bool error =
            theme_language::diagnostic_severity(diagnostic.code) == theme_language::DiagnosticSeverity::error;
        if (error && !outcome.error) outcome.error = diagnostic.code;
    }
    outcome.records = diagnostics.records().size();
    outcome.warnings = diagnostics.warning_count();
    outcome.dropped = diagnostics.dropped_count();
    return outcome;
}

// `within` compiles; `past` is refused with `code`.
void require_limit(std::string_view within, std::string_view past, DiagnosticCode code, std::string_view limit) {
    const Outcome accepted = compile(within);
    if (!accepted.compiled && accepted.error) {
        std::cerr << "  at the limit: " << theme_language::diagnostic_code_label(*accepted.error) << '\n';
    }
    require(accepted.compiled, std::string(limit) + ": at its value, compiled");
    const Outcome refused = compile(past);
    const bool named = refused.error && *refused.error == code;
    if (!named && refused.error) {
        std::cerr << "  past the limit: " << theme_language::diagnostic_code_label(*refused.error) << '\n';
    }
    require(!refused.compiled && named,
            std::string(limit) + ": one past it, refused with " + theme_language::diagnostic_code_label(code));
}

std::string repeated(std::string_view text, std::size_t count) {
    std::string result{};
    result.reserve(text.size() * count);
    for (std::size_t index = 0U; index < count; ++index) result += text;
    return result;
}

// -- owner limits (E2) -----------------------------------------------------------

// A sheet of `bytes` bytes: one rule, then a comment filling the rest.
std::string sheet_of_size(std::size_t bytes) {
    std::string sheet = "button { opacity: 1 } /*";
    const std::size_t closing = 2U;
    sheet.append(bytes - sheet.size() - closing, 'x');
    sheet += "*/";
    return sheet;
}

void test_sheet_size() {
    const std::size_t limit = theme_language::maximum_sheet_bytes;
    require_limit(sheet_of_size(limit), sheet_of_size(limit + 1U), DiagnosticCode::sheet_too_large, "1 MiB per sheet");
}

void test_rule_count() {
    const std::size_t limit = theme_language::maximum_rules;
    require_limit(repeated("button { opacity: 1 }\n", limit), repeated("button { opacity: 1 }\n", limit + 1U),
                  DiagnosticCode::too_many_rules, "10,000 rules");
    // Rules made by expanding selector lists count against the limit.
    const std::string list = "button, button { opacity: 1 }\n";
    require_limit(repeated(list, limit / 2U), repeated(list, limit / 2U) + "button { opacity: 1 }\n",
                  DiagnosticCode::too_many_rules, "10,000 rules, counting expansions");
}

void test_declaration_count() {
    const std::size_t limit = theme_language::maximum_declarations_per_rule;
    const std::string within = "button { " + repeated("opacity: 1; ", limit) + "}";
    const std::string past = "button { " + repeated("opacity: 1; ", limit + 1U) + "}";
    require_limit(within, past, DiagnosticCode::too_many_declarations, "64 declarations per rule");
}

// A rule whose value nests `depth` groups: the block, `calc(` and
// `depth - 2` parentheses inside it (bare parentheses are no value form).
std::string nested_value(std::size_t depth) {
    const std::size_t parentheses = depth - 2U;
    return "button { primitive: bulge; amplitude: calc(" + repeated("(", parentheses) + "0.5" +
           repeated(")", parentheses) + ") }";
}

void test_nesting_depth() {
    const std::size_t limit = theme_language::maximum_nesting_depth;
    require_limit(nested_value(limit), nested_value(limit + 1U), DiagnosticCode::nesting_too_deep, "nesting 32 deep");
}

std::string string_of(std::size_t bytes) {
    return "@import \"" + std::string(bytes, 'a') + "\";";
}

void test_string_length() {
    // Strings appear in `@import` and `url()`; the tokenizer measures
    // each before anything reads it, so the import's name is never looked
    // up past the limit. At the limit, the string is read and the import,
    // not installed, is the refusal.
    const std::size_t limit = theme_language::maximum_string_bytes;
    const Outcome within = compile(string_of(limit));
    require(within.error && *within.error == DiagnosticCode::import_not_installed,
            "4 KiB strings: at its value, read (an import not installed)");
    const Outcome past = compile(string_of(limit + 1U));
    require(past.error && *past.error == DiagnosticCode::string_too_long, "4 KiB strings: one past, E505");
}

// A sum of `inputs` reads of the pointer: 2 * inputs - 1 instructions.
std::string expression_of(std::size_t inputs) {
    std::string sum = "var(--pointer-x)";
    for (std::size_t index = 1U; index < inputs; ++index) sum += " + var(--pointer-x)";
    return "button { primitive: bulge; amplitude: calc(" + sum + ") }";
}

// The deepest stack: `a - (b - (c - ...))` keeps every left operand.
std::string stack_of(std::size_t depth) {
    std::string value = "var(--pointer-x)";
    for (std::size_t index = 1U; index < depth; ++index) value = "var(--pointer-x) - (" + value + ")";
    return "button { primitive: bulge; amplitude: calc(" + value + ") }";
}

void test_expression_budget() {
    const std::size_t operations = maximum_expression_operations;
    require_limit(expression_of((operations + 1U) / 2U), expression_of((operations + 1U) / 2U + 1U),
                  DiagnosticCode::expression_over_budget, "64 instructions per expression");
    require_limit(stack_of(maximum_expression_stack), stack_of(maximum_expression_stack + 1U),
                  DiagnosticCode::expression_over_budget, "a stack of 16");
}

// -- derived limits ------------------------------------------------------------------

std::string is_of(std::size_t arguments) {
    std::string list = ":is(";
    for (std::size_t index = 0U; index < arguments; ++index) {
        if (index != 0U) list += ", ";
        list += ".c" + std::to_string(index);
    }
    return "button" + list + ") { opacity: 1 }";
}

std::string compound_of(std::size_t simples) {
    // The control type and `simples - 1` classes.
    std::string selector = "button";
    for (std::size_t index = 1U; index < simples; ++index) selector += ".c" + std::to_string(index);
    return selector + " { opacity: 1 }";
}

// `count` custom properties, 64 to a rule, and one rule reading the last.
std::string custom_properties_of(std::size_t count) {
    std::string sheet{};
    for (std::size_t first = 0U; first < count; first += 64U) {
        sheet += ".d" + std::to_string(first) + " {";
        for (std::size_t index = first; index < count && index < first + 64U; ++index) {
            sheet += " --p" + std::to_string(index) + ": 0.5;";
        }
        sheet += " }\n";
    }
    return sheet + "button { primitive: bulge; amplitude: var(--p0, 0.5) }";
}

// A value substituting `--t`, a definition of `tokens` tokens (at least
// 3): `calc(1)` with comments inside it, each a token the substitution
// passes over and the value reader skips. A declaration's value leaves out
// its leading and trailing trivia, so the comments go inside the calc().
std::string substitution_of(std::size_t tokens) {
    std::string list = "calc(";
    for (std::size_t index = 3U; index < tokens; ++index) list += "/**/";
    list += "1)";
    return ":root { --t: " + list + " } button { primitive: bulge; amplitude: var(--t) }";
}

// `names` names, each defined at the root and again under its own class,
// read together by one rule: 2^names combinations.
std::string combinations_of(std::size_t names) {
    std::string sheet = ":root {";
    std::string reads = "button { primitive: bulge; amplitude: calc(0";
    for (std::size_t index = 0U; index < names; ++index) {
        const std::string name = "--n" + std::to_string(index);
        sheet += " " + name + ": 0.01;";
        reads += " + var(" + name + ")";
    }
    sheet += " }";
    for (std::size_t index = 0U; index < names; ++index) {
        sheet += " .c" + std::to_string(index) + " { --n" + std::to_string(index) + ": 0.02 }";
    }
    return sheet + " " + reads + ") }";
}

// `names` scoped names, each read by its own rule.
std::string scoped_of(std::size_t names) {
    std::string sheet{};
    for (std::size_t index = 0U; index < names; ++index) {
        const std::string name = "--s" + std::to_string(index);
        sheet += ":root { " + name + ": 0.1 } .loud { " + name + ": 0.2 } #a" + std::to_string(index) +
                 " { primitive: bulge; amplitude: var(" + name + ") }\n";
    }
    return sheet;
}

std::string keyframes_of(std::size_t count) {
    std::string sheet{};
    for (std::size_t index = 0U; index < count; ++index) {
        sheet += "@keyframes k" + std::to_string(index) + " { from { opacity: 0 } to { opacity: 1 } }\n";
    }
    return sheet + "button:hover { animation: k0 100ms }";
}

void test_derived_limits() {
    require_limit(is_of(theme_language::maximum_is_arguments), is_of(theme_language::maximum_is_arguments + 1U),
                  DiagnosticCode::too_many_is_arguments, "32 arguments in one :is()");
    require_limit(compound_of(theme_language::maximum_simple_selectors),
                  compound_of(theme_language::maximum_simple_selectors + 1U), DiagnosticCode::selector_too_long,
                  "32 simple selectors per expanded selector");
    require_limit(custom_properties_of(theme_language::maximum_custom_properties),
                  custom_properties_of(theme_language::maximum_custom_properties + 1U),
                  DiagnosticCode::too_many_custom_properties, "1,024 custom properties per sheet");
    // The substitution counts every token it passes over. The longest
    // list that substitutes, found by stepping down from the bound, is
    // within the var()'s own few tokens of it, and one more is refused.
    const std::size_t tokens = theme_language::maximum_substituted_tokens;
    std::size_t numbers = tokens;
    while (numbers > 3U && !compile(substitution_of(numbers)).compiled) numbers = numbers - 1U;
    std::cout << "substitution: " << numbers << " tokens substitute\n";
    require(numbers + 8U >= tokens, "4,096 substituted tokens: the longest list is at the bound");
    require_limit(substitution_of(numbers), substitution_of(numbers + 1U), DiagnosticCode::substitution_too_long,
                  "4,096 substituted tokens");
    // 2^7 = 128 copies of the reading rule; 2^8 is past the limit.
    require_limit(combinations_of(7U), combinations_of(8U), DiagnosticCode::too_many_variable_combinations,
                  "128 combinations per rule reading var()");
    require_limit(scoped_of(maximum_scoped_custom_properties), scoped_of(maximum_scoped_custom_properties + 1U),
                  DiagnosticCode::too_many_scoped_variables, "64 scoped custom properties");
    require_limit(keyframes_of(theme_language::maximum_keyframes_rules),
                  keyframes_of(theme_language::maximum_keyframes_rules + 1U), DiagnosticCode::too_many_keyframes,
                  "64 @keyframes per sheet");
}

// At most 1,000 diagnostic records are kept; the rest are counted.
void test_diagnostic_records() {
    const std::size_t limit = theme_language::maximum_diagnostic_records;
    const Outcome within = compile(repeated("button { frobnicate: 1 }\n", limit));
    require(within.compiled && within.records == limit && within.dropped == 0U, "1,000 records: all kept");
    const Outcome past = compile(repeated("button { frobnicate: 1 }\n", limit + 1U));
    require(past.compiled && past.records == limit && past.dropped == 1U && past.warnings == limit + 1U,
            "1,001 records: 1,000 kept, one counted");
}

} // namespace

int main() {
    test_sheet_size();
    test_rule_count();
    test_declaration_count();
    test_nesting_depth();
    test_string_length();
    test_expression_budget();
    test_derived_limits();
    test_diagnostic_records();
    std::cout << "work: at most " << worst_ratio << " operations per byte, for " << worst_input << '\n';
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme hostile input tests passed\n";
    return 0;
}
