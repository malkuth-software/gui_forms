// The theme language's values (stage T3, phase P4). Gate: a fixture for
// every value form of PLAN.md section 1.6 and every refusal; custom
// properties and var() with their limits; budget overruns are errors; and
// folded and unfolded programs evaluate to the same bits.

#include "../../src/core/theme_language/custom_properties/theme_custom_properties.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "../../src/core/theme_language/value/theme_value.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::AnatomyState;
using gui_forms::AnatomyStateSet;
using gui_forms::Color;
using gui_forms::EngineExpression;
using gui_forms::EngineInput;
using gui_forms::ExpressionCompilation;
using gui_forms::ExpressionDefect;
using gui_forms::ExpressionEnvironment;
using gui_forms::ExpressionInstruction;
using gui_forms::ExpressionLimits;
using gui_forms::ExpressionOperation;
using gui_forms::ExpressionParameterIndex;
using gui_forms::ExpressionResult;
using gui_forms::engine_input_count;
using gui_forms::engine_input_position;
using gui_forms::published_value_position;
using gui_forms::theme_language::CustomPropertyTable;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::DiagnosticSeverity;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::ReservedInputs;
using gui_forms::theme_language::SourcePositionCursor;
using gui_forms::theme_language::SubstitutionInput;
using gui_forms::theme_language::SubstitutionOutcome;
using gui_forms::theme_language::SyntaxNode;
using gui_forms::theme_language::SyntaxNodeKind;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;
using gui_forms::theme_language::ThemeValue;
using gui_forms::theme_language::TokenRange;
using gui_forms::theme_language::TokenizeOutcome;
using gui_forms::theme_language::ValueComponent;
using gui_forms::theme_language::ValueComponentKind;
using gui_forms::theme_language::ValueContext;
using gui_forms::theme_language::ValueInput;
using gui_forms::theme_language::ValueOutcome;
using gui_forms::theme_language::ValueProgram;
using gui_forms::theme_language::ValueReader;
using gui_forms::theme_language::ValueToken;
using gui_forms::theme_language::ValueUnit;
using gui_forms::theme_language::VariableSubstitution;
using gui_forms::theme_language::diagnostic_code_name;
using gui_forms::theme_language::diagnostic_code_label;
using gui_forms::theme_language::diagnostic_severity;
using gui_forms::theme_language::maximum_custom_properties;
using gui_forms::theme_language::named_colour;
using gui_forms::theme_language::parse_theme_sheet;
using gui_forms::theme_language::tokenize_theme_sheet;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// -- one declaration's value, read -----------------------------------------------

// What a sheet's value may read. The views are borrowed for the read.
struct Options final {
    std::span<const std::string> published{};
    std::span<const std::string_view> parameters{};
    std::span<const std::string_view> resources{};
    bool fold{true};
};

// How far a read got: the sheet parsed, its custom properties were
// collected, the value was substituted, and the value was read.
enum class Stage : std::uint8_t { parse_failed, collect_refused, substitution_refused, read_refused, read };

// `value` borrows the sheet's text: every fixture is a literal or outlives
// the reading.
struct Reading final {
    Stage stage{Stage::parse_failed};
    ThemeValue value{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

// Reads the value of the first declaration of `value` in `sheet`.
Reading read(std::string_view sheet, const Options& options) {
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = sheet;
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeDiagnosticList diagnostics{};
    ThemeSyntaxTree tree{};
    Reading reading{};
    const TokenizeOutcome tokenized = tokenize_theme_sheet(source, tokens, lines, diagnostics);
    const ParseOutcome parsed = parse_theme_sheet(source, tokens, lines, tree, diagnostics);
    if (tokenized != TokenizeOutcome::tokens_cover_sheet || parsed != ParseOutcome::tree_complete) {
        for (const ThemeDiagnostic& diagnostic : diagnostics.records()) reading.diagnostics.push_back(diagnostic);
        return reading;
    }
    SourcePositionCursor cursor(lines, source.text);
    ReservedInputs reserved{};
    reserved.published_values = options.published;
    CustomPropertyTable table{};
    const bool collected = table.collect(source, tokens, tree, reserved, cursor, diagnostics);
    std::size_t declaration = tree.nodes.size();
    for (std::size_t index = 0U; index < tree.nodes.size() && declaration == tree.nodes.size(); ++index) {
        const SyntaxNode& node = tree.nodes[index];
        if (node.kind != SyntaxNodeKind::declaration) continue;
        const gui_forms::theme_language::Token& property = tokens.tokens[node.tokens.first.value];
        if (sheet.substr(property.offset.value, property.length.value) == "value") declaration = index;
    }
    reading.stage = Stage::collect_refused;
    if (collected && declaration != tree.nodes.size()) {
        const TokenRange written = tree.nodes[declaration].content;
        const SubstitutionInput substitution_input{
            .source = source, .tokens = tokens, .table = table, .reserved = reserved, .cursor = cursor};
        VariableSubstitution substitution{};
        std::vector<ValueToken> substituted{};
        const SubstitutionOutcome substituted_outcome =
            substitution.substitute(substitution_input, written, substituted, diagnostics);
        reading.stage = Stage::substitution_refused;
        if (substituted_outcome == SubstitutionOutcome::substituted) {
            ValueContext context{};
            context.parameters = options.parameters;
            context.published_values = options.published.size();
            context.resources = options.resources;
            context.fold_constants = options.fold;
            const ValueInput input{.source = source,
                                   .tokens = tokens,
                                   .written = written,
                                   .substituted = substituted,
                                   .context = context,
                                   .cursor = cursor};
            ValueReader reader{};
            const ValueOutcome outcome = reader.read(input, reading.value, diagnostics);
            reading.stage = outcome == ValueOutcome::read ? Stage::read : Stage::read_refused;
        }
    }
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) reading.diagnostics.push_back(diagnostic);
    return reading;
}

// Reads `value: <text>;` alone.
Reading read_value(std::string_view text, const Options& options, std::string& sheet) {
    sheet = "a { value: ";
    sheet += text;
    sheet += "; }";
    return read(sheet, options);
}

// Reads `text` as a value of exactly one component and returns it.
ValueComponent one_component(std::string_view text, const Options& options = Options{}) {
    std::string sheet{};
    const Reading reading = read_value(text, options, sheet);
    const bool one = reading.stage == Stage::read && reading.value.components.size() == 1U &&
                     reading.diagnostics.empty();
    require(one, std::string("one component: ") + std::string(text));
    if (!one) {
        for (const ThemeDiagnostic& diagnostic : reading.diagnostics) {
            std::cerr << "  got " << diagnostic_code_name(diagnostic.code) << " (" << diagnostic.detail << ")\n";
        }
        return ValueComponent{};
    }
    ValueComponent component = reading.value.components[0];
    // The text borrows the sheet, which ends here.
    component.text = {};
    return component;
}

bool is_number(const ValueComponent& component, double number, ValueUnit unit) {
    return component.kind == ValueComponentKind::number && component.unit == unit && component.number == number;
}

bool is_colour(const ValueComponent& component, Color colour) {
    return component.kind == ValueComponentKind::colour && component.colour == colour;
}

// Requires that `sheet` is refused with `code`, first and alone, at the
// column where `marker` starts (the sheets are one line of ASCII).
void require_refused(std::string_view sheet, DiagnosticCode code, std::string_view marker,
                     const Options& options = Options{}) {
    const Reading reading = read(sheet, options);
    const std::string what = std::string(diagnostic_code_name(code)) + ": " + std::string(sheet);
    const std::size_t found = sheet.find(marker);
    require(found != std::string_view::npos, what + " (the marker is in the sheet)");
    const std::uint32_t column = static_cast<std::uint32_t>(found + 1U);
    const bool refused = reading.stage != Stage::read && reading.diagnostics.size() == 1U &&
                         reading.diagnostics[0].code == code && reading.diagnostics[0].position.column.value == column;
    require(refused, what);
    if (!refused) {
        for (const ThemeDiagnostic& diagnostic : reading.diagnostics) {
            std::cerr << "  got " << diagnostic_code_name(diagnostic.code) << " at column "
                      << diagnostic.position.column.value << " (" << diagnostic.detail << ")\n";
        }
    }
}

// The same, for `value: <text>;`.
void require_value_refused(std::string_view text, DiagnosticCode code, std::string_view marker,
                           const Options& options = Options{}) {
    std::string sheet = "a { value: ";
    sheet += text;
    sheet += "; }";
    require_refused(sheet, code, marker, options);
}

// Evaluates a program of `value`, with the pointer at (`x`, `y`) and the
// other inputs at fixed values.
ExpressionResult evaluate(const ThemeValue& value, ValueProgram program, double x, double y,
                          std::size_t published = 0U) {
    std::vector<double> inputs(engine_input_count + published, 0.0);
    inputs[static_cast<std::size_t>(EngineInput::pointer_x)] = x;
    inputs[static_cast<std::size_t>(EngineInput::pointer_y)] = y;
    inputs[static_cast<std::size_t>(EngineInput::time_in_state)] = 0.375;
    inputs[static_cast<std::size_t>(EngineInput::display_scale)] = 2.0;
    inputs[static_cast<std::size_t>(EngineInput::text_scale)] = 1.25;
    for (std::size_t index = 0U; index < published; ++index) inputs[engine_input_count + index] = 0.5;
    const std::array<double, 2> parameters{3.0, 7.0};
    ExpressionLimits limits{};
    limits.inputs = inputs.size();
    limits.parameters = parameters.size();
    EngineExpression expression{};
    const ExpressionCompilation compilation =
        expression.compile(value.program_instructions(program), -1.0, limits);
    require(compilation.defect == ExpressionDefect::none, "a read program compiles");
    AnatomyStateSet states{};
    states.insert(AnatomyState::hot);
    ExpressionEnvironment environment{};
    environment.inputs = inputs;
    environment.states = states;
    environment.parameters = parameters;
    const ExpressionResult result = expression.evaluate(environment);
    return result;
}

// -- numbers and units ------------------------------------------------------------

void test_numbers_and_units() {
    require(is_number(one_component("1"), 1.0, ValueUnit::none), "a plain number");
    require(is_number(one_component("-1.5px"), -1.5, ValueUnit::px), "px");
    require(is_number(one_component("50%"), 50.0, ValueUnit::percent), "a percentage keeps its number");
    require(is_number(one_component("2em"), 2.0, ValueUnit::em), "em is parsed");
    require(is_number(one_component("2s"), 2.0, ValueUnit::seconds), "seconds");
    require(is_number(one_component("250ms"), 0.25, ValueUnit::seconds), "milliseconds read as seconds");
    require(is_number(one_component("90deg"), 90.0, ValueUnit::degrees), "degrees");
    require(is_number(one_component("0.5turn"), 180.0, ValueUnit::degrees), "turns read as degrees");
    const double radian = 180.0 / 3.14159265358979323846;
    require(is_number(one_component("1rad"), radian, ValueUnit::degrees), "radians read as degrees");
    require(is_number(one_component("2dppx"), 2.0, ValueUnit::dppx), "dppx");
    require(is_number(one_component("2x"), 2.0, ValueUnit::dppx), "x reads as dppx");
    require(is_number(one_component("10PX"), 10.0, ValueUnit::px), "units match in any ASCII case");

    const std::array<std::string_view, 8> refused_units{{"1rem", "1vw", "1vh", "1vmin", "1vmax", "1ch", "1ex",
                                                         "1furlong"}};
    for (const std::string_view unit : refused_units) {
        require_value_refused(unit, DiagnosticCode::unit_not_admitted, unit);
    }
}

// -- keywords, strings, separators and generic functions ---------------------------------

void test_components() {
    std::string sheet{};
    const Reading listed = read_value("solid 'a b', \"c\" / 2", Options{}, sheet);
    require(listed.stage == Stage::read && listed.value.components.size() == 6U, "six components");
    if (listed.value.components.size() == 6U) {
        const std::vector<ValueComponent>& parts = listed.value.components;
        require(parts[0].kind == ValueComponentKind::keyword && parts[0].text == "solid", "a keyword");
        require(parts[1].kind == ValueComponentKind::string && parts[1].text == "a b", "a string, without quotes");
        require(parts[2].kind == ValueComponentKind::comma, "a comma");
        require(parts[3].kind == ValueComponentKind::string && parts[3].text == "c", "a double-quoted string");
        require(parts[4].kind == ValueComponentKind::slash, "a slash");
        require(is_number(parts[5], 2.0, ValueUnit::none), "a number after the slash");
        for (std::size_t index = 0U; index < parts.size(); ++index) {
            require(parts[index].subtree_end == index + 1U, "a leaf's subtree is itself");
        }
    }

    const Reading gradient = read_value("linear-gradient(90deg, red, outer(inner(1), 2) 50%)", Options{}, sheet);
    require(gradient.stage == Stage::read && gradient.value.components.size() == 11U, "a generic function's tree");
    if (gradient.value.components.size() == 11U) {
        const std::vector<ValueComponent>& parts = gradient.value.components;
        require(parts[0].kind == ValueComponentKind::function && parts[0].text == "linear-gradient" &&
                    parts[0].subtree_end == 11U,
                "a function holds its arguments");
        require(is_number(parts[1], 90.0, ValueUnit::degrees) && parts[3].kind == ValueComponentKind::keyword,
                "its arguments in order");
        require(parts[5].kind == ValueComponentKind::function && parts[5].text == "outer" && parts[5].subtree_end == 10U,
                "a nested function");
        require(parts[6].kind == ValueComponentKind::function && parts[6].subtree_end == 8U &&
                    is_number(parts[7], 1.0, ValueUnit::none),
                "a function nested twice");
        require(is_number(parts[9], 2.0, ValueUnit::none) && is_number(parts[10], 50.0, ValueUnit::percent),
                "after the nested functions");
    }

    require_value_refused("a(b) + c", DiagnosticCode::value_syntax, "+");
    require_value_refused("(1px)", DiagnosticCode::value_syntax, "(1px)");
    require_value_refused("[a]", DiagnosticCode::value_syntax, "[a]");
    // An empty value points where it would start.
    require_refused("a { value: ; }", DiagnosticCode::value_syntax, " ; }");
}

// -- colours ----------------------------------------------------------------------------

void test_colours() {
    require(is_colour(one_component("#abc"), Color::rgba(0xAAU, 0xBBU, 0xCCU)), "#rgb");
    require(is_colour(one_component("#abcd"), Color::rgba(0xAAU, 0xBBU, 0xCCU, 0xDDU)), "#rgba");
    require(is_colour(one_component("#A1b2C3"), Color::rgba(0xA1U, 0xB2U, 0xC3U)), "#rrggbb in any case");
    require(is_colour(one_component("#a1b2c3d4"), Color::rgba(0xA1U, 0xB2U, 0xC3U, 0xD4U)), "#rrggbbaa");

    require(is_colour(one_component("rgb(255, 0, 0)"), Color::rgba(255U, 0U, 0U)), "legacy rgb()");
    require(is_colour(one_component("rgb(100%, 50%, 0%)"), Color::rgba(255U, 128U, 0U)),
            "percentages, rounded half up");
    require(is_colour(one_component("rgba(0, 0, 255, 0.5)"), Color::rgba(0U, 0U, 255U, 128U)), "legacy rgba()");
    require(is_colour(one_component("RGB(0 128 255 / 25%)"), Color::rgba(0U, 128U, 255U, 64U)),
            "modern rgb() with a percentage alpha, in any case");
    require(is_colour(one_component("rgb(10% 20 30)"), Color::rgba(26U, 20U, 30U)),
            "the modern form mixes numbers and percentages");
    require(is_colour(one_component("rgba(300 -5 0 / 2)"), Color::rgba(255U, 0U, 0U)), "channels clamp");

    require(is_colour(one_component("hsl(120, 100%, 50%)"), Color::rgba(0U, 255U, 0U)), "legacy hsl()");
    require(is_colour(one_component("hsl(0.5turn 100% 50%)"), Color::rgba(0U, 255U, 255U)), "a hue in turns");
    require(is_colour(one_component("hsl(-120 100 50)"), Color::rgba(0U, 0U, 255U)),
            "a negative hue wraps; modern numbers read as percentages");
    require(is_colour(one_component("hsl(240deg 100% 25% / 0.5)"), Color::rgba(0U, 0U, 128U, 128U)),
            "hsl() with an alpha");
    require(is_colour(one_component("hsla(60, 100%, 50%, 1)"), Color::rgba(255U, 255U, 0U)), "hsla()");

    require(is_colour(one_component("color-mix(in srgb, red, blue)"), Color::rgba(128U, 0U, 128U)),
            "color-mix() halves by default");
    require(is_colour(one_component("color-mix(in srgb, red 25%, blue)"), Color::rgba(64U, 0U, 191U)),
            "one percentage, the other the rest");
    require(is_colour(one_component("color-mix(in srgb, 25% red, blue 75%)"), Color::rgba(64U, 0U, 191U)),
            "a percentage on either side of its colour");
    require(is_colour(one_component("color-mix(in srgb, red 30%, blue 30%)"), Color::rgba(128U, 0U, 128U, 153U)),
            "percentages summing below 100% scale the alpha");
    require(is_colour(one_component("color-mix(in srgb, transparent, red)"), Color::rgba(255U, 0U, 0U, 128U)),
            "premultiplied: a transparent side adds no colour");
    require(is_colour(one_component("color-mix(in srgb, rgb(0 0 0), color-mix(in srgb, white, black))"),
                      Color::rgba(64U, 64U, 64U)),
            "colour functions nest");

    const std::optional<Color> purple = named_colour("RebeccaPurple");
    require(purple && *purple == Color::rgba(0x66U, 0x33U, 0x99U), "a named colour in any case");
    const std::optional<Color> first = named_colour("aliceblue");
    const std::optional<Color> last = named_colour("yellowgreen");
    require(first && *first == Color::rgba(0xF0U, 0xF8U, 0xFFU) && last && *last == Color::rgba(0x9AU, 0xCDU, 0x32U),
            "the table's first and last names");
    const std::optional<Color> clear = named_colour("transparent");
    require(clear && *clear == Color::rgba(0U, 0U, 0U, 0U), "transparent");
    require(!named_colour("notacolour") && !named_colour("lightgoldenrodyellowish") && !named_colour(""),
            "other words name no colour");
    const ValueComponent named = one_component("Red");
    require(named.kind == ValueComponentKind::keyword, "a named colour stays a keyword");

    require_value_refused("#abcde", DiagnosticCode::value_syntax, "#abcde");
    require_value_refused("#ggg", DiagnosticCode::value_syntax, "#ggg");
    require_value_refused("rgb(1, 2)", DiagnosticCode::value_syntax, "rgb(");
    require_value_refused("rgb(1 2 3 4)", DiagnosticCode::value_syntax, "rgb(");
    require_value_refused("rgb(1, 2 3)", DiagnosticCode::value_syntax, "rgb(");
    require_value_refused("rgb(10%, 20, 30)", DiagnosticCode::value_syntax, "rgb(");
    require_value_refused("rgb(red 0 0)", DiagnosticCode::value_wrong_kind, "red");
    require_value_refused("rgb(1px 0 0)", DiagnosticCode::value_wrong_kind, "1px");
    require_value_refused("rgb(0 0 0 / red)", DiagnosticCode::value_wrong_kind, "red");
    require_value_refused("rgb(var(--pointer-x) 0 0)", DiagnosticCode::value_wrong_kind, "var(");
    require_value_refused("rgb(from red r g b)", DiagnosticCode::value_form_not_admitted, "from");
    require_value_refused("hsl(10px 50% 50%)", DiagnosticCode::value_wrong_kind, "10px");
    require_value_refused("hsl(10, 50, 50)", DiagnosticCode::value_wrong_kind, "50,");
    require_value_refused("hwb(0 0% 0%)", DiagnosticCode::value_form_not_admitted, "hwb(");
    require_value_refused("oklch(0.5 0.1 120)", DiagnosticCode::value_form_not_admitted, "oklch(");
    require_value_refused("color-mix(in oklab, red, blue)", DiagnosticCode::value_form_not_admitted, "oklab");
    require_value_refused("color-mix(srgb, red, blue)", DiagnosticCode::value_syntax, "color-mix(");
    require_value_refused("color-mix(in srgb, red)", DiagnosticCode::value_syntax, "color-mix(");
    require_value_refused("color-mix(in srgb, red 120%, blue)", DiagnosticCode::value_out_of_range, "120%");
    require_value_refused("color-mix(in srgb, red 0%, blue 0%)", DiagnosticCode::value_out_of_range, "color-mix(");
    require_value_refused("color-mix(in srgb, red blue, white)", DiagnosticCode::value_wrong_kind, "blue");
    require_value_refused("color-mix(in srgb, 50%, white)", DiagnosticCode::value_syntax, "color-mix(");
}

// -- resources ----------------------------------------------------------------------------

void test_resources() {
    const std::array<std::string_view, 2> resources{{"grain", "noise"}};
    Options options{};
    options.resources = resources;
    std::string sheet{};
    const Reading reading = read_value("url(\"resource:noise\")", options, sheet);
    require(reading.stage == Stage::read && reading.value.components.size() == 1U &&
                reading.value.components[0].kind == ValueComponentKind::resource &&
                reading.value.components[0].text == "noise",
            "a listed resource");
    require_value_refused("url(\"resource:paper\")", DiagnosticCode::resource_unknown, "\"resource:paper", options);
    require_value_refused("url(\"resource:\")", DiagnosticCode::resource_unknown, "\"resource:", options);
    require_value_refused("url(\"https://example.com/a.png\")", DiagnosticCode::value_form_not_admitted, "\"https",
                          options);
    require_value_refused("url(\"resource:grain\" 1)", DiagnosticCode::value_syntax, "url(", options);
}

// -- custom properties --------------------------------------------------------------------

void test_custom_properties() {
    const Reading simple = read("a { --radius: 4px; value: var(--radius); }", Options{});
    require(simple.stage == Stage::read && simple.value.components.size() == 1U &&
                is_number(simple.value.components[0], 4.0, ValueUnit::px),
            "a custom property is substituted");

    const Reading across = read(":root { --ink: #102030; } a { value: 1px solid var(--ink); }", Options{});
    require(across.stage == Stage::read && across.value.components.size() == 3U &&
                is_colour(across.value.components[2], Color::rgba(0x10U, 0x20U, 0x30U)),
            "from another rule, among other components");

    const Reading chained = read("a { --a: calc(var(--b) * 2); --b: 3px; value: var(--a); }", Options{});
    require(chained.stage == Stage::read && chained.value.components.size() == 1U &&
                is_number(chained.value.components[0], 6.0, ValueUnit::px),
            "a definition that reads another, folded");

    const Reading fallback = read("a { value: var(--missing, 3px 4px); }", Options{});
    require(fallback.stage == Stage::read && fallback.value.components.size() == 2U &&
                is_number(fallback.value.components[1], 4.0, ValueUnit::px),
            "the fallback of an undefined name");

    const Reading nested_fallback = read("a { --b: 2px; value: var(--missing, var(--b)); }", Options{});
    require(nested_fallback.stage == Stage::read && nested_fallback.value.components.size() == 1U &&
                is_number(nested_fallback.value.components[0], 2.0, ValueUnit::px),
            "a fallback that reads another name");

    const Reading media = read("@media (prefers-contrast: more) { a { --r: 1px; } } a { value: var(--r); }",
                               Options{});
    require(media.stage == Stage::read, "a definition inside @media");

    require_refused("a { value: var(--missing); }", DiagnosticCode::variable_undefined, "--missing");
    require_refused("a { --a: var(--b); --b: var(--a); value: var(--a); }", DiagnosticCode::variable_cycle,
                    "--a); value");
    require_refused("a { --a: var(--a); value: var(--a); }", DiagnosticCode::variable_cycle, "--a); value");
    // Read without a choice of its definitions (as in a keyframe), a name
    // defined twice cannot vary (P12).
    require_refused("a { --r: 1px; } b { --r: 2px; } c { value: var(--r); }", DiagnosticCode::variable_cannot_vary,
                    "--r); }");
    require_refused("a { value: var(radius); }", DiagnosticCode::value_syntax, "var(");
    require_refused("a { value: var(--a 1px); }", DiagnosticCode::value_syntax, "var(");
    require_refused("a { --pointer-x: 1px; }", DiagnosticCode::reserved_input_set, "--pointer-x");
    require_refused("a { --text-scale: 1; }", DiagnosticCode::reserved_input_set, "--text-scale");

    const std::array<std::string, 2> published{{"scroll-progress", "battery"}};
    Options with_published{};
    with_published.published = published;
    require_refused("a { --battery: 1; }", DiagnosticCode::reserved_input_set, "--battery", with_published);
    const Reading reads_published = read("a { value: var(--battery); }", with_published);
    require(reads_published.stage == Stage::read && reads_published.value.programs.size() == 1U,
            "a published value reads as an input");
    if (reads_published.value.programs.size() == 1U) {
        const std::span<const ExpressionInstruction> program =
            reads_published.value.program_instructions(reads_published.value.programs[0]);
        require(program.size() == 1U && program[0] == ExpressionInstruction::input(published_value_position(1U)),
                "at its published position");
    }
    require(read("a { value: var(--Battery); }", with_published).stage == Stage::substitution_refused,
            "reserved names are case-sensitive, as custom properties are");

    // 1,024 distinct names pass; one more is refused (E511).
    std::string at_limit{};
    for (std::size_t name = 0U; name < maximum_custom_properties; ++name) {
        at_limit += "a{--p" + std::to_string(name) + ":1}";
    }
    std::string past_limit = at_limit + "a{--extra:1}";
    at_limit += "a{value:var(--p7)}";
    past_limit += "a{value:var(--p7)}";
    require(read(at_limit, Options{}).stage == Stage::read, "1,024 custom properties");
    const Reading too_many = read(past_limit, Options{});
    require(too_many.stage == Stage::collect_refused && too_many.diagnostics.size() == 1U &&
                too_many.diagnostics[0].code == DiagnosticCode::too_many_custom_properties,
            "1,025 custom properties are refused");
    std::string repeated{};
    for (std::size_t name = 0U; name < maximum_custom_properties + 10U; ++name) repeated += "a{--same:1}";
    require(read(repeated + "a{value:1}", Options{}).stage == Stage::read,
            "the limit counts names, not definitions");
}

// -- the substitution bound (E512) --------------------------------------------------------

void test_substitution_bound() {
    // `x, x, ..., x` is 3n - 2 tokens: 1,366 items are 4,096 tokens.
    std::string at_limit = "a { value: x";
    for (std::size_t item = 1U; item < 1366U; ++item) at_limit += ", x";
    std::string past_limit = at_limit + ", x; }";
    at_limit += "; }";
    const Reading accepted = read(at_limit, Options{});
    require(accepted.stage == Stage::read && accepted.value.components.size() == 2731U,
            "a value of 4,096 tokens");
    const Reading refused = read(past_limit, Options{});
    require(refused.stage == Stage::substitution_refused && refused.diagnostics.size() == 1U &&
                refused.diagnostics[0].code == DiagnosticCode::substitution_too_long,
            "a value of 4,099 tokens is refused");

    // A fallback that is skipped still counts: reading past it is work.
    std::string skipped = "a { --d: 1; value: var(--d, x";
    for (std::size_t item = 1U; item < 1400U; ++item) skipped += ", x";
    skipped += "); }";
    const Reading skipped_reading = read(skipped, Options{});
    require(skipped_reading.stage == Stage::substitution_refused && skipped_reading.diagnostics.size() == 1U &&
                skipped_reading.diagnostics[0].code == DiagnosticCode::substitution_too_long,
            "a skipped fallback of 4,198 tokens counts toward the bound");
    require(read("a { --d: 1; value: var(--d, x, x) var(--missing, 2px); }", Options{}).stage == Stage::read,
            "skipped and read fallbacks within the bound");

    // Each name reads the one before it twice: 2^20 tokens if expanded.
    std::string doubling = "a { --v0: 1 1;";
    for (std::size_t level = 1U; level <= 20U; ++level) {
        doubling += " --v" + std::to_string(level) + ": var(--v" + std::to_string(level - 1U) + ") var(--v" +
                    std::to_string(level - 1U) + ");";
    }
    doubling += " value: var(--v20); }";
    const Reading exploded = read(doubling, Options{});
    require(exploded.stage == Stage::substitution_refused && exploded.diagnostics.size() == 1U &&
                exploded.diagnostics[0].code == DiagnosticCode::substitution_too_long,
            "an expansion explosion stops at the bound");
}

// -- expressions ----------------------------------------------------------------------------

void test_constant_expressions() {
    require(is_number(one_component("calc(1px + 2px)"), 3.0, ValueUnit::px), "calc() adds");
    require(is_number(one_component("calc(2 * 3px)"), 6.0, ValueUnit::px), "a number times a length");
    require(is_number(one_component("calc(10px / 4)"), 2.5, ValueUnit::px), "a length divided by a number");
    require(is_number(one_component("calc(4px / 2px)"), 2.0, ValueUnit::none), "a length divided by a length");
    require(is_number(one_component("calc(1px + 2 * 3px)"), 7.0, ValueUnit::px), "products bind tighter");
    require(is_number(one_component("calc(10px - 2px - 3px)"), 5.0, ValueUnit::px), "left to right");
    require(is_number(one_component("calc((1px + 2px) * 3)"), 9.0, ValueUnit::px), "parentheses");
    require(is_number(one_component("CALC(calc(1px) + min(1px, 2px))"), 2.0, ValueUnit::px), "nested functions");
    require(is_number(one_component("min(3px, 1px, 2px)"), 1.0, ValueUnit::px), "min() of three");
    require(is_number(one_component("max(3px, 1px, 2px)"), 3.0, ValueUnit::px), "max() of three");
    require(is_number(one_component("max(4s)"), 4.0, ValueUnit::seconds), "max() of one");
    require(is_number(one_component("clamp(1px, 5px, 3px)"), 3.0, ValueUnit::px), "clamp() to its maximum");
    require(is_number(one_component("clamp(5px, 1px, 3px)"), 5.0, ValueUnit::px),
            "clamp() with a minimum above its maximum gives the minimum, as in CSS");
    require(is_number(one_component("calc(250ms + 0.5s)"), 0.75, ValueUnit::seconds), "durations of both units");
    require(is_number(one_component("calc(50% * 2)"), 100.0, ValueUnit::percent), "percentages");
    require(is_number(one_component("mix(0px, 10px, 0.25)"), 2.5, ValueUnit::px), "mix()");
    require(is_number(one_component("smoothstep(0px, 1px, 0.5px)"), 0.5, ValueUnit::none), "smoothstep()");
    require(is_number(one_component("choose(1, 2px, 3px)"), 2.0, ValueUnit::px), "choose()");
    require(is_number(one_component("less(1px, 2px)"), 1.0, ValueUnit::none), "less()");
    require(is_number(one_component("greater-equal(1, 2)"), 0.0, ValueUnit::none), "greater-equal()");
    require(is_number(one_component("not-equal(1, 2)"), 1.0, ValueUnit::none), "not-equal()");
    require(is_number(one_component("calc(1px + -2px)"), -1.0, ValueUnit::px), "a signed operand");
}

void test_input_expressions() {
    std::string sheet{};
    const Reading pointer = read_value("calc(var(--pointer-x) / 100px)", Options{}, sheet);
    require(pointer.stage == Stage::read && pointer.value.programs.size() == 1U &&
                pointer.value.components.size() == 1U &&
                pointer.value.components[0].kind == ValueComponentKind::expression &&
                pointer.value.components[0].unit == ValueUnit::none,
            "an expression of the pointer");
    if (pointer.value.programs.size() == 1U) {
        const std::vector<ExpressionInstruction> expected{
            ExpressionInstruction::input(engine_input_position(EngineInput::pointer_x)),
            ExpressionInstruction::number(100.0), ExpressionInstruction::apply(ExpressionOperation::divide)};
        const std::span<const ExpressionInstruction> program =
            pointer.value.program_instructions(pointer.value.programs[0]);
        require(std::vector<ExpressionInstruction>(program.begin(), program.end()) == expected,
                "its postfix program");
        require(evaluate(pointer.value, pointer.value.programs[0], 37.0, 0.0).value == 0.37, "which evaluates");
    }

    const Reading folded = read_value("calc(var(--pointer-x) * (2 / 4))", Options{}, sheet);
    Options unfolded_options{};
    unfolded_options.fold = false;
    std::string unfolded_sheet{};
    const Reading unfolded = read_value("calc(var(--pointer-x) * (2 / 4))", unfolded_options, unfolded_sheet);
    require(folded.value.instructions.size() == 3U && unfolded.value.instructions.size() == 5U,
            "a constant subexpression folds to one instruction");
    if (folded.value.instructions.size() == 3U) {
        require(folded.value.instructions[1] == ExpressionInstruction::number(0.5), "to its value");
    }

    const Reading time = read_value("var(--time-in-state)", Options{}, sheet);
    require(time.stage == Stage::read && time.value.components.size() == 1U &&
                time.value.components[0].kind == ValueComponentKind::expression &&
                time.value.components[0].unit == ValueUnit::seconds,
            "a reserved input alone is an expression in its unit");

    const Reading pressed = read_value("translate(choose(state(pressed), 1px, 0px), 0px)", Options{}, sheet);
    require(pressed.stage == Stage::read && pressed.value.components.size() == 4U &&
                pressed.value.components[1].kind == ValueComponentKind::expression &&
                pressed.value.components[1].unit == ValueUnit::px,
            "a state-reading expression inside a generic function");
    if (pressed.value.programs.size() == 1U) {
        const std::span<const ExpressionInstruction> program =
            pressed.value.program_instructions(pressed.value.programs[0]);
        require(program.size() == 4U && program[0] == ExpressionInstruction::state(AnatomyState::pressed),
                "state() reads a standard state");
    }

    const std::array<std::string_view, 2> parameters{{"corner_radius", "depth"}};
    Options with_parameters{};
    with_parameters.parameters = parameters;
    const Reading parameter = read_value("calc(param(depth) * 2)", with_parameters, sheet);
    require(parameter.stage == Stage::read && parameter.value.programs.size() == 1U, "param()");
    if (parameter.value.programs.size() == 1U) {
        const std::span<const ExpressionInstruction> program =
            parameter.value.program_instructions(parameter.value.programs[0]);
        require(program.size() == 3U && program[0] == ExpressionInstruction::parameter(ExpressionParameterIndex{1U}),
                "reads the parameter by position");
        require(evaluate(parameter.value, parameter.value.programs[0], 0.0, 0.0).value == 14.0,
                "and evaluates with it");
    }

    const Reading two = read_value("var(--pointer-x) var(--pointer-y)", Options{}, sheet);
    require(two.stage == Stage::read && two.value.programs.size() == 2U && two.value.programs[1].first == 1U,
            "two programs in one value, stored one after the other");
}

// Folded and unfolded programs give the same bits (the gate).
void test_folding_bits() {
    const std::array<std::string_view, 6> expressions{{
        "calc(var(--pointer-x) * 0.1 + 3px * 7 / 9)",
        "mix(1px, calc(10px / 3), smoothstep(0px, 100px, var(--pointer-y)))",
        "clamp(calc(1px / 3), var(--pointer-x), calc(2px * 7 - 0.3px))",
        "choose(less(var(--pointer-x), calc(50px / 7)), calc(1 / 3), calc(var(--pointer-y) / 11px))",
        "calc(var(--pointer-x) + 1px / 0)",
        "calc(var(--time-in-state) * (0.1 + 0.2) + var(--display-scale) * 1s / 3)",
    }};
    const std::array<double, 4> pointer_values{{0.0, 3.3, -17.25, 1e10}};
    Options unfolded_options{};
    unfolded_options.fold = false;
    for (const std::string_view text : expressions) {
        std::string folded_sheet{};
        std::string unfolded_sheet{};
        const Reading folded = read_value(text, Options{}, folded_sheet);
        const Reading unfolded = read_value(text, unfolded_options, unfolded_sheet);
        const bool both = folded.stage == Stage::read && unfolded.stage == Stage::read &&
                          folded.value.programs.size() == 1U && unfolded.value.programs.size() == 1U;
        require(both, std::string("folded and unfolded both read: ") + std::string(text));
        if (!both) continue;
        require(folded.value.instructions.size() <= unfolded.value.instructions.size(),
                std::string("folding never lengthens: ") + std::string(text));
        for (const double x : pointer_values) {
            for (const double y : pointer_values) {
                const ExpressionResult left = evaluate(folded.value, folded.value.programs[0], x, y);
                const ExpressionResult right = evaluate(unfolded.value, unfolded.value.programs[0], x, y);
                const bool same = std::bit_cast<std::uint64_t>(left.value) == std::bit_cast<std::uint64_t>(right.value) &&
                                  left.fallback == right.fallback;
                require(same, std::string("the same bits: ") + std::string(text));
            }
        }
    }
}

void test_expression_refusals() {
    require_value_refused("calc(1px + 1s)", DiagnosticCode::value_wrong_kind, "+");
    require_value_refused("calc(1px * 2px)", DiagnosticCode::value_wrong_kind, "*");
    require_value_refused("calc(1px / 1s)", DiagnosticCode::value_wrong_kind, "/");
    require_value_refused("calc(1px - 0)", DiagnosticCode::value_wrong_kind, "-");
    require_value_refused("calc(var(--pointer-x) + 1em)", DiagnosticCode::value_wrong_kind, "+");
    require_value_refused("calc(1px +2px)", DiagnosticCode::value_syntax, "+2px");
    require_value_refused("calc(1px+ 2px)", DiagnosticCode::value_syntax, "+ 2px");
    require_value_refused("calc(1px % 2px)", DiagnosticCode::value_syntax, "% 2px");
    require_value_refused("calc()", DiagnosticCode::value_syntax, ")");
    require_value_refused("calc(1px, 2px)", DiagnosticCode::value_syntax, "calc(");
    require_value_refused("min(1px,)", DiagnosticCode::value_syntax, ")");
    require_value_refused("calc((1px, 2px))", DiagnosticCode::value_syntax, ", 2px");
    require_value_refused("mix(1px, 2px)", DiagnosticCode::value_syntax, "mix(");
    require_value_refused("less(1, 2, 3)", DiagnosticCode::value_syntax, "less(");
    require_value_refused("mix(1px, 2px, 3px)", DiagnosticCode::value_wrong_kind, "mix(");
    require_value_refused("mix(1px, 2s, 0.5)", DiagnosticCode::value_wrong_kind, "mix(");
    require_value_refused("choose(1px, 1, 2)", DiagnosticCode::value_wrong_kind, "choose(");
    require_value_refused("clamp(1px, 2s, 3px)", DiagnosticCode::value_wrong_kind, "clamp(");
    require_value_refused("calc(sin(1))", DiagnosticCode::value_form_not_admitted, "sin(");
    require_value_refused("calc(rgb(1 2 3))", DiagnosticCode::value_wrong_kind, "rgb(");
    require_value_refused("calc(red)", DiagnosticCode::value_syntax, "red");
    require_value_refused("calc(1px 2px)", DiagnosticCode::value_syntax, "2px");
    require_value_refused("state(nope)", DiagnosticCode::value_syntax, "nope");
    require_value_refused("state(focused_visible)", DiagnosticCode::value_syntax, "focused_visible");
    require_value_refused("state(1)", DiagnosticCode::value_syntax, "state(");
    require_value_refused("param(depth)", DiagnosticCode::value_syntax, "depth");
    require_value_refused("calc(1px / 0)", DiagnosticCode::value_out_of_range, "calc(");
    require_value_refused("sqrt(4)", DiagnosticCode::value_form_not_admitted, "sqrt(");
    require_value_refused("attr(title)", DiagnosticCode::value_form_not_admitted, "attr(");
}

// The engine's budget (E507) and the nesting limit (E504).
void test_expression_budgets() {
    // 32 inputs summed are 63 instructions; 33 are 65.
    std::string within = "calc(var(--pointer-x)";
    for (std::size_t input = 1U; input < 32U; ++input) within += " + var(--pointer-x)";
    std::string over = within + " + var(--pointer-x))";
    within += ")";
    std::string sheet{};
    require(read_value(within, Options{}, sheet).stage == Stage::read, "a 63-instruction program");
    require_value_refused(over, DiagnosticCode::expression_over_budget, "calc(");

    // Sixteen inputs nested to the right keep sixteen values on the stack.
    std::string shallow = "calc(var(--pointer-x)";
    std::string deep = "calc(var(--pointer-x)";
    for (std::size_t level = 1U; level < 16U; ++level) shallow += " + (var(--pointer-x)";
    for (std::size_t level = 1U; level < 17U; ++level) deep += " + (var(--pointer-x)";
    shallow += std::string(15U, ')') + ")";
    deep += std::string(16U, ')') + ")";
    require(read_value(shallow, Options{}, sheet).stage == Stage::read, "a stack of 16");
    require_value_refused(deep, DiagnosticCode::expression_over_budget, "calc(");

    // Folded constants cost nothing; unfolded they overrun.
    std::string constants = "calc(var(--text-scale) + (1";
    for (std::size_t term = 1U; term < 100U; ++term) constants += " + 1";
    constants += "))";
    const Reading folded = read_value(constants, Options{}, sheet);
    require(folded.stage == Stage::read && folded.value.instructions.size() == 3U,
            "a hundred folded constants are one instruction");
    Options unfolded_options{};
    unfolded_options.fold = false;
    require_value_refused(constants, DiagnosticCode::expression_over_budget, "calc(", unfolded_options);

    // A sheet nests at most 32 groups, its rule's block included; a
    // substituted definition can carry an expression past that, and the
    // expression reader bounds its own groups at 32.
    std::string definition = "1";
    for (std::size_t level = 0U; level < 30U; ++level) definition = "calc(" + definition + ")";
    const std::string at_limit = "a { --deep: " + definition + "; value: calc(calc(var(--deep))); }";
    const std::string past_limit = "a { --deep: " + definition + "; value: calc(calc(calc(var(--deep)))); }";
    require(read(at_limit, Options{}).stage == Stage::read, "32 nested expression groups");
    const Reading too_deep = read(past_limit, Options{});
    require(too_deep.stage == Stage::read_refused && too_deep.diagnostics.size() == 1U &&
                too_deep.diagnostics[0].code == DiagnosticCode::nesting_too_deep,
            "33 nested expression groups are refused");
}

// The codes' numbers, names and severities that other stages raise (the
// code list has one definition, in the diagnostics source): the value
// errors, and the property warnings the lowering raises.
struct ValueCodeCase final {
    DiagnosticCode code{};
    std::string_view label{};
    std::string_view name{};
    DiagnosticSeverity severity{};
};

void test_value_codes() {
    const std::array<ValueCodeCase, 13> cases{{
        {DiagnosticCode::value_syntax, "E401", "value_syntax", DiagnosticSeverity::error},
        {DiagnosticCode::value_wrong_kind, "E402", "value_wrong_kind", DiagnosticSeverity::error},
        {DiagnosticCode::value_out_of_range, "E403", "value_out_of_range", DiagnosticSeverity::error},
        {DiagnosticCode::unit_not_admitted, "E404", "unit_not_admitted", DiagnosticSeverity::error},
        {DiagnosticCode::value_form_not_admitted, "E405", "value_form_not_admitted", DiagnosticSeverity::error},
        {DiagnosticCode::keyword_unknown, "E406", "keyword_unknown", DiagnosticSeverity::error},
        {DiagnosticCode::expression_invalid, "E408", "expression_invalid", DiagnosticSeverity::error},
        {DiagnosticCode::variable_undefined, "E409", "variable_undefined", DiagnosticSeverity::error},
        {DiagnosticCode::variable_cycle, "E410", "variable_cycle", DiagnosticSeverity::error},
        {DiagnosticCode::keyframes_undefined, "E411", "keyframes_undefined", DiagnosticSeverity::error},
        {DiagnosticCode::resource_unknown, "E413", "resource_unknown", DiagnosticSeverity::error},
        {DiagnosticCode::unknown_property, "W101", "unknown_property", DiagnosticSeverity::warning},
        {DiagnosticCode::property_not_admitted, "W107", "property_not_admitted", DiagnosticSeverity::warning},
    }};
    for (const ValueCodeCase& entry : cases) {
        require(diagnostic_code_label(entry.code) == entry.label, entry.name);
        require(diagnostic_code_name(entry.code) == entry.name, entry.name);
        require(diagnostic_severity(entry.code) == entry.severity, entry.name);
    }
}

} // namespace

int main() {
    test_value_codes();
    test_numbers_and_units();
    test_components();
    test_colours();
    test_resources();
    test_custom_properties();
    test_substitution_bound();
    test_constant_expressions();
    test_input_expressions();
    test_folding_bits();
    test_expression_refusals();
    test_expression_budgets();
    if (failures != 0) {
        std::cerr << failures << " theme value check(s) failed\n";
        return 1;
    }
    std::cout << "theme value tests passed\n";
    return 0;
}
