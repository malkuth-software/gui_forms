#include "../src/core/theme_language/custom_properties/theme_custom_properties.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/media/theme_media_query.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "../src/core/theme_language/value/theme_value.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace theme_language = gui_forms::theme_language;

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("theme sheet fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

// Whether the tokens cover `size` bytes in order, each token non-empty, and
// each numeric token naming the next number in the buffer.
bool tokens_cover(const theme_language::ThemeTokenBuffer& buffer, std::size_t size) noexcept {
    std::size_t next = 0U;
    std::uint32_t next_number = 0U;
    for (const theme_language::Token& token : buffer.tokens) {
        if (token.offset.value != next || token.length.value == 0U) return false;
        next += token.length.value;
        const bool numeric = token.kind == theme_language::TokenKind::number ||
                             token.kind == theme_language::TokenKind::percentage ||
                             token.kind == theme_language::TokenKind::dimension;
        if (numeric) {
            if (token.number != next_number || next_number >= buffer.numbers.size()) return false;
            const theme_language::ThemeNumber& number = buffer.numbers[next_number];
            if (number.numeric_length.value == 0U || number.numeric_length.value > token.length.value) return false;
            ++next_number;
        }
    }
    return next == size && next_number == buffer.numbers.size();
}

// Whether the diagnostics lie within the sheet as at most `runs_admitted`
// runs, each in offset order.
bool diagnostics_in_order(std::span<const theme_language::ThemeDiagnostic> records, std::size_t size,
                          std::size_t runs_admitted) noexcept {
    std::uint32_t previous = 0U;
    std::size_t runs = records.empty() ? 0U : 1U;
    for (const theme_language::ThemeDiagnostic& record : records) {
        const std::uint32_t offset = record.position.offset.value;
        if (offset > size) return false;
        if (offset < previous) ++runs;
        previous = offset;
    }
    return runs <= runs_admitted;
}

// Whether each node's content lies within its own tokens.
bool contents_within_nodes(const theme_language::ThemeSyntaxTree& tree) noexcept {
    for (const theme_language::SyntaxNode& node : tree.nodes) {
        const bool within = node.content.first.value >= node.tokens.first.value &&
                            node.content.first.value <= node.content.end.value &&
                            node.content.end.value <= node.tokens.end.value;
        if (!within) return false;
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

// Whether each component's subtree lies within its parent's: a leaf's ends
// at itself, a function's after its children.
bool components_nest(const theme_language::ThemeValue& value) noexcept {
    std::array<std::uint32_t, theme_language::maximum_nesting_depth> open_ends{};
    std::size_t depth = 0U;
    const std::size_t count = value.components.size();
    for (std::size_t index = 0U; index < count; ++index) {
        const theme_language::ValueComponent& component = value.components[index];
        while (depth != 0U && open_ends[depth - 1U] <= index) --depth;
        const std::size_t end = component.subtree_end;
        if (end <= index || end > count) return false;
        if (depth != 0U && end > open_ends[depth - 1U]) return false;
        const bool function = component.kind == theme_language::ValueComponentKind::function;
        if (!function && end != index + 1U) return false;
        if (!function || end == index + 1U) continue;
        if (depth == open_ends.size()) return false;
        open_ends[depth] = static_cast<std::uint32_t>(end);
        ++depth;
    }
    return true;
}

// Whether every expression names a program, and every program lies in the
// value's instructions and compiles with the context's limits.
bool programs_compile(const theme_language::ThemeValue& value, const theme_language::ValueContext& context) noexcept {
    for (const theme_language::ValueComponent& component : value.components) {
        const bool expression = component.kind == theme_language::ValueComponentKind::expression;
        if (expression && component.program >= value.programs.size()) return false;
    }
    gui_forms::ExpressionLimits limits{};
    limits.inputs = gui_forms::engine_input_count + context.published_values;
    limits.parameters = context.parameters.size();
    gui_forms::EngineExpression expression{};
    for (const theme_language::ValueProgram program : value.programs) {
        const std::size_t end = static_cast<std::size_t>(program.first) + program.count;
        if (end > value.instructions.size()) return false;
        const gui_forms::ExpressionCompilation compilation =
            expression.compile(value.program_instructions(program), 0.0, limits);
        if (compilation.defect != gui_forms::ExpressionDefect::none) return false;
    }
    return true;
}

// Substitutes and reads every declaration's value but the custom
// properties'. A refusal adds exactly one error and leaves nothing; a read
// value adds none, holds at most maximum_substituted_tokens tokens, nests,
// compiles, and reads the same a second time.
void read_values(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& buffer,
                 const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree) {
    theme_language::ThemeDiagnosticList diagnostics{};
    theme_language::SourcePositionCursor cursor(lines, source.text);
    const std::array<std::string, 1> published{{"progress"}};
    theme_language::ReservedInputs reserved{};
    reserved.published_values = published;
    theme_language::CustomPropertyTable table{};
    if (!table.collect(source, buffer, tree, reserved, cursor, diagnostics)) {
        if (diagnostics.error_count() != 1U || table.name_count() != 0U) fail("a refused collection reports one error");
        return;
    }
    const std::array<std::string_view, 2> parameters{{"depth", "radius"}};
    const std::array<std::string_view, 1> resources{{"icon.check"}};
    theme_language::ValueContext context{};
    context.parameters = parameters;
    context.published_values = published.size();
    context.resources = resources;
    const theme_language::SubstitutionInput substitution_input{
        .source = source, .tokens = buffer, .table = table, .reserved = reserved, .cursor = cursor};
    theme_language::VariableSubstitution substitution{};
    theme_language::ValueReader reader{};
    std::vector<theme_language::ValueToken> substituted{};
    theme_language::ThemeValue value{};
    theme_language::ThemeValue again{};
    for (const theme_language::SyntaxNode& node : tree.nodes) {
        if (node.kind != theme_language::SyntaxNodeKind::declaration) continue;
        const theme_language::Token& property = buffer.tokens[node.tokens.first.value];
        const std::string_view name = source.text.substr(property.offset.value, property.length.value);
        if (name.substr(0U, 2U) == "--") continue;
        const std::size_t errors = diagnostics.error_count();
        const theme_language::SubstitutionOutcome outcome =
            substitution.substitute(substitution_input, node.content, substituted, diagnostics);
        if (outcome == theme_language::SubstitutionOutcome::refused) {
            if (!substituted.empty() || diagnostics.error_count() != errors + 1U) {
                fail("a refused substitution reports one error and keeps nothing");
            }
            continue;
        }
        if (substituted.size() > theme_language::maximum_substituted_tokens) fail("a value holds at most 4,096 tokens");
        const theme_language::ValueInput value_input{.source = source,
                                                     .tokens = buffer,
                                                     .written = node.content,
                                                     .substituted = substituted,
                                                     .context = context,
                                                     .cursor = cursor};
        const theme_language::ValueOutcome read = reader.read(value_input, value, diagnostics);
        if (read == theme_language::ValueOutcome::refused) {
            if (!value.components.empty() || !value.programs.empty() || diagnostics.error_count() != errors + 1U) {
                fail("a refused value reports one error and keeps nothing");
            }
            continue;
        }
        if (diagnostics.error_count() != errors) fail("a read value reports no error");
        if (!components_nest(value)) fail("a value's components nest");
        if (!programs_compile(value, context)) fail("a value's programs compile");
        const theme_language::ValueOutcome reread = reader.read(value_input, again, diagnostics);
        const bool same = reread == read && again.components == value.components &&
                          again.programs == value.programs && again.instructions == value.instructions;
        if (!same) fail("reading a value is repeatable");
    }
}

// Reads every @media prelude (P8). A refusal names a token of the prelude;
// a read list has at least one query, each unknown feature names a token
// of the prelude, every resolution bound is above zero, and a second read
// gives the same queries.
void read_media(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& buffer,
                const theme_language::ThemeSyntaxTree& tree) {
    std::vector<theme_language::MediaQuery> queries{};
    std::vector<theme_language::MediaQuery> again{};
    for (const theme_language::SyntaxNode& node : tree.nodes) {
        if (node.kind != theme_language::SyntaxNodeKind::media_rule) continue;
        theme_language::MediaQueryProblem problem{};
        const theme_language::MediaQueryOutcome read =
            theme_language::read_media_queries(source.text, buffer, node.content, queries, problem);
        if (read == theme_language::MediaQueryOutcome::refused) {
            if (problem.token < node.content.first.value || problem.token >= node.content.end.value) {
                fail("a media refusal names a token of the prelude");
            }
            continue;
        }
        if (queries.empty()) fail("a read media list has a query");
        for (const theme_language::MediaQuery& query : queries) {
            const bool inside =
                query.unknown_token >= node.content.first.value && query.unknown_token < node.content.end.value;
            if (query.unknown_feature && !inside) fail("an unknown feature names a token of the prelude");
            const gui_forms::MediaCondition& condition = query.condition;
            const bool low_valid = !condition.minimum_resolution || *condition.minimum_resolution > 0.0;
            const bool high_valid = !condition.maximum_resolution || *condition.maximum_resolution > 0.0;
            if (!low_valid || !high_valid) fail("a read resolution bound is above zero");
        }
        theme_language::MediaQueryProblem second{};
        static_cast<void>(theme_language::read_media_queries(source.text, buffer, node.content, again, second));
        bool same = again.size() == queries.size();
        for (std::size_t index = 0U; same && index < queries.size(); ++index) {
            same = again[index].condition == queries[index].condition &&
                   again[index].unknown_feature == queries[index].unknown_feature &&
                   again[index].never == queries[index].never;
        }
        if (!same) fail("reading a media list is repeatable");
    }
}

// Tokenizes and parses the input. The tokens must cover it exactly; the
// tree, complete or stopped, must nest and write back the input's bytes;
// the parse adds at most one error; and a second tokenization and parse
// into fresh storage must give the same tokens, numbers, nodes and
// diagnostics. A complete tree's values are then read (read_values).
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    theme_language::ThemeSource source{};
    source.file = "fuzz.theme.css";
    source.text = std::string_view(reinterpret_cast<const char*>(data), size);
    theme_language::ThemeTokenBuffer buffer{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::TokenizeOutcome outcome =
        theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics);
    if (outcome == theme_language::TokenizeOutcome::refused_too_large) {
        if (!buffer.tokens.empty() || diagnostics.error_count() != 1U) fail("a refused sheet keeps nothing");
        return 0;
    }
    if (!tokens_cover(buffer, size)) fail("the tokens cover the sheet");
    if (!diagnostics_in_order(diagnostics.records(), size, 2U)) {
        fail("token diagnostics lie in the sheet, in two ordered runs");
    }
    bool has_invalid = false;
    for (const theme_language::Token& token : buffer.tokens) {
        if (token.kind == theme_language::TokenKind::invalid) has_invalid = true;
    }
    if (has_invalid && diagnostics.error_count() == 0U) fail("an invalid token has an error");

    const std::size_t lexical_errors = diagnostics.error_count();
    const std::size_t lexical_records = diagnostics.records().size();
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed = theme_language::parse_theme_sheet(source, buffer, lines, tree, diagnostics);
    if (parsed == theme_language::ParseOutcome::tree_complete && has_invalid) fail("an invalid token stops the parse");
    if (parsed == theme_language::ParseOutcome::stopped_at_lexical_error && !has_invalid) {
        fail("a lexical stop needs an invalid token");
    }
    const std::size_t parse_errors = diagnostics.error_count() - lexical_errors;
    const std::size_t parse_errors_admitted = parsed == theme_language::ParseOutcome::stopped_at_error ? 1U : 0U;
    if (parse_errors != parse_errors_admitted) fail("a stopped parse reports one error, a complete one none");
    const std::span<const theme_language::ThemeDiagnostic> parse_records =
        diagnostics.records().subspan(lexical_records);
    if (!diagnostics_in_order(parse_records, size, 2U)) fail("parse diagnostics lie in the sheet");
    if (!contents_within_nodes(tree)) fail("contents lie within their nodes");
    std::string written{};
    const theme_language::WriteOutcome write = theme_language::write_theme_sheet(tree, buffer, source.text, written);
    if (write != theme_language::WriteOutcome::written) fail("the tree nests");
    if (written != source.text) fail("the tree writes back the sheet's bytes");

    theme_language::ThemeTokenBuffer again{};
    theme_language::ThemeDiagnosticList again_diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, again, lines, again_diagnostics));
    if (again.tokens != buffer.tokens || again.numbers != buffer.numbers) fail("tokenizing is repeatable");
    theme_language::ThemeSyntaxTree again_tree{};
    const theme_language::ParseOutcome reparsed =
        theme_language::parse_theme_sheet(source, again, lines, again_tree, again_diagnostics);
    if (reparsed != parsed || again_tree.nodes != tree.nodes) fail("parsing is repeatable");
    if (!same_records(diagnostics.records(), again_diagnostics.records())) fail("diagnostics are repeatable");

    // The same tokens as a form document (stage FD): the same structural
    // invariants.
    theme_language::ThemeDiagnosticList form_diagnostics{};
    theme_language::ThemeSyntaxTree form_tree{};
    const theme_language::ParseOutcome form_parsed = theme_language::parse_theme_sheet(
        source, buffer, lines, form_tree, form_diagnostics, theme_language::ThemeSheetForm::form_document);
    if (form_parsed == theme_language::ParseOutcome::tree_complete && has_invalid) {
        fail("an invalid token stops a form parse");
    }
    const std::size_t form_errors_admitted = form_parsed == theme_language::ParseOutcome::stopped_at_error ? 1U : 0U;
    if (form_diagnostics.error_count() != form_errors_admitted) fail("a stopped form parse reports one error");
    if (!contents_within_nodes(form_tree)) fail("form contents lie within their nodes");
    std::string form_written{};
    const theme_language::WriteOutcome form_write =
        theme_language::write_theme_sheet(form_tree, buffer, source.text, form_written);
    if (form_write != theme_language::WriteOutcome::written) fail("the form tree nests");
    if (form_written != source.text) fail("the form tree writes back the document's bytes");
    if (parsed == theme_language::ParseOutcome::tree_complete) {
        read_values(source, buffer, lines, tree);
        read_media(source, buffer, tree);
    }
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
namespace {

// One xorshift64 step: advances `state` and returns it.
std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

} // namespace

int main() {
    // Each seed parses completely; the mutations explore around them.
    const std::array<std::string_view, 6> seeds{
        "@import \"palette.theme.css\";\n"
        "@gf-theme \"aurora\" { version: 1; }\n"
        "button:hover > part(face), #player-play[checked] {\n"
        "  background: rgb(10 20 30 / 50%);\n"
        "  --glow: 1.5e1px;\r\n"
        "  icon: url(\"resource:icon.check\") !important;\n"
        "}\n",
        "/* comment */ @media (forced-colors: active) { * { color: CanvasText } }\r"
        "a{b:'c\\\nd';e:-.5em+3%}",
        "\xEF\xBB\xBF" "caf\xC3\xA9 { width: calc(100% - 12px); }",
        "@keyframes pulse { from { scale: 1 } 50% { scale: f(g([1])); } }\n"
        "@x y { z { w } ; }\n"
        "a { b: ; c: /* */ }",
        "a { --r: 4px; --c: var(--r, 1px); width: calc(var(--r) * 2 + var(--pointer-x) / 3);\n"
        "  color: color-mix(in srgb, red 30%, rgb(0 0 0 / 50%)); x: hsl(120 50% 50%) #abc;\n"
        "  y: url(\"resource:icon.check\") mix(1px, 2px, state(hot)) clamp(1, param(depth), 3);\n"
        "  z: f(var(--progress), choose(less(1s, var(--time-in-state)), 2deg, 0.5turn)), 'q' / 2; }",
        "@media screen and (prefers-color-scheme: dark), (min-resolution: 2x) and (max-resolution: 3dppx),\n"
        "  (hover: hover) and (prefers-reduced-motion: reduce) { a { b: c } }"};
    std::uint64_t state = 0xbb67ae8584caa73bULL;
    for (std::size_t run = 0; run < 100'000; ++run) {
        const std::uint64_t run_draw = next_random(state);
        std::string input(seeds[run % seeds.size()]);
        const std::size_t mutations = 1U + static_cast<std::size_t>(run_draw % 8U);
        // Each mutation flips one bit of one byte.
        for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
            const std::uint64_t draw = next_random(state);
            if (input.empty()) continue;
            const std::size_t offset = static_cast<std::size_t>(draw % input.size());
            const char flipped_bit = static_cast<char>(1U << (draw % 8U));
            input[offset] = static_cast<char>(input[offset] ^ flipped_bit);
        }
        if (run % 5U == 0 && !input.empty()) {
            input.resize(static_cast<std::size_t>(state % input.size()));
        } else if (run % 7U == 0) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), '9');
        } else if (run % 11U == 0) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), '"');
        }
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    }
    return 0;
}
#endif
