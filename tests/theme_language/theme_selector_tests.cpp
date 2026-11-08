// The theme language's selectors (stage T3, phase P3). Gate: one
// conformance fixture per selector form of PLAN.md section 1.4 and per
// refused form; the specificity and the level of each.

#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/selector/theme_selector.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::AnatomyState;
using gui_forms::CascadeLevel;
using gui_forms::ControlAnatomy;
using gui_forms::ItemSystemDeclaration;
using gui_forms::PartDeclaration;
using gui_forms::PartRole;
using gui_forms::RuleSpecificity;
using gui_forms::theme_language::ColumnNumber;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::LineStartTable;
using gui_forms::theme_language::LoweringBudget;
using gui_forms::theme_language::LoweredSelector;
using gui_forms::theme_language::ParseOutcome;
using gui_forms::theme_language::SelectorInput;
using gui_forms::theme_language::SelectorLowering;
using gui_forms::theme_language::SelectorOutcome;
using gui_forms::theme_language::SelectorVocabulary;
using gui_forms::theme_language::SourcePositionCursor;
using gui_forms::theme_language::SyntaxNodeKind;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;
using gui_forms::theme_language::ThemeSyntaxTree;
using gui_forms::theme_language::ThemeTokenBuffer;
using gui_forms::theme_language::TokenizeOutcome;
using gui_forms::theme_language::diagnostic_code_name;
using gui_forms::theme_language::lowered_selector_bytes;
using gui_forms::theme_language::maximum_is_arguments;
using gui_forms::theme_language::maximum_lowered_bytes;
using gui_forms::theme_language::maximum_rules;
using gui_forms::theme_language::maximum_simple_selectors;
using gui_forms::theme_language::parse_theme_sheet;
using gui_forms::theme_language::tokenize_theme_sheet;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// -- the vocabulary: four control types ---------------------------------------

constexpr std::array<PartDeclaration, 3> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "label", .role = PartRole::label, .composed_from = {}, .states = {}},
    {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 2> check_box_parts{{
    {.name = "box", .role = PartRole::well, .composed_from = {}, .states = {}},
    {.name = "check", .role = PartRole::indicator, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 2> track_bar_parts{{
    {.name = "track", .role = PartRole::track, .composed_from = {}, .states = {}},
    {.name = "thumb", .role = PartRole::thumb, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 1> list_item_parts{{
    {.name = "row", .role = PartRole::selection, .composed_from = {}, .states = {}},
}};
constexpr std::array<ItemSystemDeclaration, 1> list_items{{
    {.name = "item", .parts = list_item_parts, .states = {}, .custom_states = {}},
}};
const std::array<ControlAnatomy, 4> controls{{
    {.control_id = "button", .parts = button_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "check_box", .parts = check_box_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "track_bar", .parts = track_bar_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "list_view", .parts = {}, .states = {}, .custom_states = {}, .item_systems = list_items},
}};

// -- one rule's selectors, lowered --------------------------------------------

struct Lowering final {
    SelectorOutcome outcome{SelectorOutcome::refused};
    std::vector<LoweredSelector> selectors{};
    std::vector<ThemeDiagnostic> diagnostics{};
    std::size_t budget_left{};
    std::size_t bytes_left{};
};

// Lowers the selectors of the sheet `<selectors> {}`.
Lowering lower(std::string_view selectors, std::size_t budget = maximum_rules) {
    const std::string sheet = std::string(selectors) + " {}";
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = sheet;
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeDiagnosticList diagnostics{};
    ThemeSyntaxTree tree{};
    Lowering result{};
    const TokenizeOutcome tokenized = tokenize_theme_sheet(source, tokens, lines, diagnostics);
    const ParseOutcome parsed = parse_theme_sheet(source, tokens, lines, tree, diagnostics);
    const bool one_rule = tokenized == TokenizeOutcome::tokens_cover_sheet && parsed == ParseOutcome::tree_complete &&
                          tree.nodes.size() == 2U && tree.nodes[1].kind == SyntaxNodeKind::style_rule;
    require(one_rule, std::string("one rule parses: ") + std::string(selectors));
    if (!one_rule) return result;
    SourcePositionCursor cursor(lines, source.text);
    const SelectorInput input{.source = source,
                              .tokens = tokens,
                              .selectors = tree.nodes[1].content,
                              .rule_label = selectors,
                              .cursor = cursor};
    SelectorVocabulary vocabulary{};
    vocabulary.controls = controls;
    SelectorLowering lowering{};
    LoweringBudget budget_left{};
    budget_left.rules = budget;
    result.outcome = lowering.lower(input, vocabulary, budget_left, result.selectors, diagnostics);
    result.budget_left = budget_left.rules;
    result.bytes_left = budget_left.bytes;
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    return result;
}

// CSS's three columns: stable IDs; classes and states; types and parts.
RuleSpecificity specificity(std::uint32_t ids, std::uint32_t classes, std::uint32_t types) {
    RuleSpecificity result{};
    result.ids = ids;
    result.classes = classes;
    result.types = types;
    return result;
}

// Lowers `selectors` to exactly one selector and returns it.
LoweredSelector lower_one(std::string_view selectors) {
    const Lowering result = lower(selectors);
    const bool one = result.outcome == SelectorOutcome::lowered && result.selectors.size() == 1U &&
                     result.diagnostics.empty();
    require(one, std::string("lowers to one selector: ") + std::string(selectors));
    if (!one) return LoweredSelector{};
    return result.selectors[0];
}

// -- each admitted form -------------------------------------------------------

void test_forms() {
    const LoweredSelector type = lower_one("track-bar");
    require(type.selector.control_type == "track_bar" && type.specificity == specificity(0U, 0U, 1U) &&
                type.level == CascadeLevel::theme_sheet,
            "a control type, written kebab-case");

    const LoweredSelector any = lower_one("*");
    require(any.selector == gui_forms::RuleSelector{} && any.specificity == RuleSpecificity{} &&
                any.level == CascadeLevel::theme_sheet,
            "any control");

    const LoweredSelector id = lower_one("#player-play");
    require(id.selector.stable_id == "player-play" && id.specificity == specificity(1U, 0U, 0U) &&
                id.level == CascadeLevel::theme_sheet,
            "a stable ID keeps its spelling, at the sheet level");

    const LoweredSelector style_class = lower_one(".primary");
    require(style_class.selector.classes == std::vector<std::string>{"primary"} &&
                style_class.specificity == specificity(0U, 1U, 0U) &&
                style_class.level == CascadeLevel::theme_sheet,
            "a class, at the sheet level");

    const LoweredSelector part = lower_one("button::part(face)");
    require(part.selector.control_type == "button" && part.selector.part == "face" &&
                part.specificity == specificity(0U, 0U, 2U),
            "a part of a control type");

    const LoweredSelector item_part = lower_one("list-view::part(row)");
    require(item_part.selector.part == "row", "an item system's part");

    const LoweredSelector any_part = lower_one("::part(thumb)");
    require(any_part.selector.control_type.empty() && any_part.selector.part == "thumb",
            "a part without a type, declared by some type");

    const LoweredSelector role = lower_one("::role(face)");
    require(role.selector.role == PartRole::face && role.specificity == specificity(0U, 0U, 1U),
            "a part role");

    const LoweredSelector custom = lower_one("button:state(drop-accepted)");
    require(custom.selector.states == std::vector<AnatomyState>{AnatomyState::drop_accepted} &&
                custom.specificity == specificity(0U, 1U, 1U),
            "a published state through :state()");

    struct StandardCase final {
        std::string_view selector{};
        AnatomyState state{};
    };
    const std::array<StandardCase, 13> standard{{
        {"button:hover", AnatomyState::hot},
        {"button:active", AnatomyState::pressed},
        {"button:focus", AnatomyState::focused},
        {"button:focus-visible", AnatomyState::focus_visible},
        {"button:checked", AnatomyState::checked},
        {"button:indeterminate", AnatomyState::indeterminate},
        {"button:disabled", AnatomyState::disabled},
        {"button:enabled", AnatomyState::enabled},
        {"button:read-only", AnatomyState::read_only},
        {"button:invalid", AnatomyState::invalid},
        {"button:open", AnatomyState::open},
        {"button:default", AnatomyState::preselect},
        {"button:HOVER", AnatomyState::hot},
    }};
    for (const StandardCase& entry : standard) {
        const LoweredSelector lowered = lower_one(entry.selector);
        require(lowered.selector.states == std::vector<AnatomyState>{entry.state} &&
                    lowered.specificity == specificity(0U, 1U, 1U),
                std::string("a standard pseudo-class: ") + std::string(entry.selector));
    }

    const LoweredSelector full = lower_one("button#save.primary.loud::part(face):hover:active");
    require(full.selector.stable_id == "save" && full.selector.classes.size() == 2U &&
                full.selector.states.size() == 2U && full.specificity == specificity(1U, 4U, 2U) &&
                full.level == CascadeLevel::theme_sheet,
            "every simple selector in one compound");

    const LoweredSelector repeated = lower_one(".a.a:hover:hover");
    require(repeated.selector.classes.size() == 1U && repeated.selector.states.size() == 1U &&
                repeated.specificity == specificity(0U, 4U, 0U),
            "a repeated class or state is kept once and counted as written, as CSS counts it");

    const LoweredSelector commented = lower_one("button/* a */:hover");
    require(commented.selector.states.size() == 1U, "a comment is not a combinator");
}

// -- :is() and selector lists ---------------------------------------------------

void test_expansion() {
    const Lowering either = lower("button:is(:hover, :focus-visible)::part(face)");
    require(either.outcome == SelectorOutcome::lowered && either.selectors.size() == 2U,
            ":is() expands into one rule per argument");
    if (either.selectors.size() == 2U) {
        require(either.selectors[0].selector.states == std::vector<AnatomyState>{AnatomyState::hot} &&
                    either.selectors[1].selector.states == std::vector<AnatomyState>{AnatomyState::focus_visible},
                "in argument order");
        require(either.selectors[0].specificity == specificity(0U, 1U, 2U) &&
                    either.selectors[1].specificity == specificity(0U, 1U, 2U),
                "each with the type, the part and one state");
    }

    const Lowering most = lower(":is(#a, button)");
    require(most.outcome == SelectorOutcome::lowered && most.selectors.size() == 2U, "an ID and a type");
    if (most.selectors.size() == 2U) {
        require(most.selectors[1].selector.control_type == "button" && most.selectors[1].selector.stable_id.empty() &&
                    most.selectors[1].specificity == specificity(1U, 0U, 0U) &&
                    most.selectors[1].level == CascadeLevel::theme_sheet,
                "each argument takes the most specific argument's specificity and its level (EC2)");
    }

    const Lowering list = lower("button, check-box::part(check):checked");
    require(list.outcome == SelectorOutcome::lowered && list.selectors.size() == 2U, "a selector list");
    if (list.selectors.size() == 2U) {
        require(list.selectors[0].specificity == specificity(0U, 0U, 1U) &&
                    list.selectors[1].specificity == specificity(0U, 1U, 2U),
                "each selector of a list has its own specificity (R2-15)");
    }

    const Lowering nested = lower(":is(:is(.a, .b), .c.d)");
    require(nested.outcome == SelectorOutcome::lowered && nested.selectors.size() == 3U, "nested :is()");
    if (nested.selectors.size() == 3U) {
        require(nested.selectors[0].specificity == specificity(0U, 2U, 0U) &&
                    nested.selectors[0].selector.classes == std::vector<std::string>{"a"},
                "the outer :is() counts its most specific argument, .c.d");
    }

    const Lowering product = lower(".x:is(.a, .b):is(:hover, :active, :focus)");
    require(product.outcome == SelectorOutcome::lowered && product.selectors.size() == 6U &&
                product.budget_left == maximum_rules - 6U,
            "two :is() multiply, and the budget counts the expansion");

    std::string widest = "button:is(";
    for (std::size_t index = 0U; index < maximum_is_arguments; ++index) {
        if (index != 0U) widest += ", ";
        widest += ".c" + std::to_string(index);
    }
    widest += ")";
    const Lowering at_limit = lower(widest);
    require(at_limit.outcome == SelectorOutcome::lowered && at_limit.selectors.size() == maximum_is_arguments,
            "32 arguments in one :is()");
}

// -- refused and dropped forms --------------------------------------------------

struct RefusedCase final {
    std::string_view selector{};
    DiagnosticCode code{};
    std::uint32_t column{};
};

void test_refused() {
    std::string too_wide = "button:is(";
    for (std::size_t index = 0U; index <= maximum_is_arguments; ++index) {
        if (index != 0U) too_wide += ",";
        too_wide += ".c";
    }
    too_wide += ")";
    std::string too_long{};
    for (std::size_t index = 0U; index <= maximum_simple_selectors; ++index) too_long += ".c";
    const std::array<RefusedCase, 21> cases{{
        {"button label", DiagnosticCode::combinator_not_admitted, 8U},
        {"button > label", DiagnosticCode::combinator_not_admitted, 8U},
        {"button>label", DiagnosticCode::combinator_not_admitted, 7U},
        {"button+check-box", DiagnosticCode::combinator_not_admitted, 7U},
        {"button ~ check-box", DiagnosticCode::combinator_not_admitted, 8U},
        {"&:hover", DiagnosticCode::combinator_not_admitted, 1U},
        {"button:not(:hover)", DiagnosticCode::pseudo_class_not_admitted, 7U},
        {":where(.a)", DiagnosticCode::pseudo_class_not_admitted, 1U},
        {"button:has(.a)", DiagnosticCode::pseudo_class_not_admitted, 7U},
        {"button:first-child", DiagnosticCode::pseudo_class_not_admitted, 7U},
        {"button::before", DiagnosticCode::pseudo_class_not_admitted, 7U},
        {"button[checked]", DiagnosticCode::attribute_selector_not_admitted, 7U},
        {"#1a", DiagnosticCode::selector_syntax, 1U},
        {".Primary", DiagnosticCode::selector_syntax, 2U},
        {"button:is(check-box)", DiagnosticCode::selector_syntax, 11U},
        {"#a#b", DiagnosticCode::selector_syntax, 3U},
        {"button,", DiagnosticCode::selector_syntax, 8U},
        {"button:is()", DiagnosticCode::selector_syntax, 11U},
        {"button::part(face label)", DiagnosticCode::selector_syntax, 19U},
        {std::string_view(too_wide), DiagnosticCode::too_many_is_arguments, 109U},
        {std::string_view(too_long), DiagnosticCode::selector_too_long, 66U},
    }};
    for (const RefusedCase& entry : cases) {
        const Lowering result = lower(entry.selector);
        const std::string what = std::string(diagnostic_code_name(entry.code)) + ": " + std::string(entry.selector);
        require(result.outcome == SelectorOutcome::refused && result.selectors.empty() &&
                    result.budget_left == maximum_rules,
                what + ": refused, nothing appended");
        const bool one = result.diagnostics.size() == 1U;
        require(one, what + ": one diagnostic");
        if (!one) continue;
        const ThemeDiagnostic& diagnostic = result.diagnostics[0];
        const bool located = diagnostic.code == entry.code && diagnostic.position.column == ColumnNumber{entry.column};
        if (!located) {
            std::cerr << "  got " << diagnostic_code_name(diagnostic.code) << " at column "
                      << diagnostic.position.column.value << '\n';
        }
        require(located, what + ": code and column");
        require(diagnostic.rule == entry.selector.substr(0U, 64U), what + ": names the rule");
    }

    const Lowering over_budget = lower(":is(.a, .b):is(.c, .d)", 3U);
    require(over_budget.outcome == SelectorOutcome::refused && over_budget.diagnostics.size() == 1U &&
                over_budget.diagnostics[0].code == DiagnosticCode::too_many_rules && over_budget.budget_left == 3U,
            "an expansion past the rule budget is refused");
    const Lowering list_over = lower(".a, .b, .c", 2U);
    require(list_over.outcome == SelectorOutcome::refused && list_over.diagnostics.size() == 1U &&
                list_over.diagnostics[0].code == DiagnosticCode::too_many_rules,
            "a list past the rule budget is refused");

    // The expanded selectors' bytes are counted: 320 copies of a 60,000-byte
    // stable ID pass the byte budget, as each copy is built.
    std::string long_id(60'000U, 'a');
    std::string twenty = ":is(.a0";
    for (int index = 1; index < 20; ++index) twenty += ", .a" + std::to_string(index);
    twenty += ")";
    std::string sixteen = ":is(.b0";
    for (int index = 1; index < 16; ++index) sixteen += ", .b" + std::to_string(index);
    sixteen += ")";
    const Lowering too_large = lower("#" + long_id + twenty + sixteen);
    require(too_large.outcome == SelectorOutcome::refused && too_large.diagnostics.size() == 1U &&
                too_large.diagnostics[0].code == DiagnosticCode::lowered_sheet_too_large &&
                too_large.diagnostics[0].detail == std::to_string(maximum_lowered_bytes) &&
                too_large.bytes_left == maximum_lowered_bytes,
            "an expansion past the byte budget is refused, and the budget is unchanged");
    const Lowering within = lower("#" + long_id + sixteen);
    std::size_t counted = 0U;
    for (const LoweredSelector& lowered : within.selectors) counted += lowered_selector_bytes(lowered.selector);
    require(within.outcome == SelectorOutcome::lowered && within.selectors.size() == 16U &&
                within.bytes_left == maximum_lowered_bytes - counted && counted > 16U * 60'000U,
            "the lowered selectors' bytes are subtracted from the budget");
}

void test_dropped() {
    const std::array<std::string_view, 9> unknown{{
        "slider",
        "Button",
        "track_bar",
        "button:frobnicate",
        "button:state(custom-thing)",
        "::role(nonsense)",
        "button::part(thumb)",
        "::part(nothing)",
        "button::shadow-root",
    }};
    for (const std::string_view selector : unknown) {
        const Lowering result = lower(selector);
        const bool dropped = result.outcome == SelectorOutcome::dropped && result.selectors.empty() &&
                             result.diagnostics.size() == 1U &&
                             result.diagnostics[0].code == DiagnosticCode::unknown_selector_name &&
                             result.budget_left == maximum_rules;
        require(dropped, std::string("an unknown name drops the rule: ") + std::string(selector));
    }
    const Lowering in_list = lower("button, slider:hover, .a");
    require(in_list.outcome == SelectorOutcome::dropped && in_list.selectors.empty(),
            "an unknown name drops the whole rule, not one selector");
    const Lowering then_error = lower("slider > a");
    require(then_error.outcome == SelectorOutcome::refused && then_error.diagnostics.size() == 2U &&
                then_error.diagnostics[1].code == DiagnosticCode::combinator_not_admitted,
            "an error after an unknown name still refuses");
}

// The output and budget are the caller's, appended to and kept.
void test_appending() {
    ThemeSource source{};
    source.file = "test.theme.css";
    const std::string sheet = "button {} slider {} .a, .b {}";
    source.text = sheet;
    ThemeTokenBuffer tokens{};
    LineStartTable lines{};
    ThemeDiagnosticList diagnostics{};
    ThemeSyntaxTree tree{};
    static_cast<void>(tokenize_theme_sheet(source, tokens, lines, diagnostics));
    static_cast<void>(parse_theme_sheet(source, tokens, lines, tree, diagnostics));
    require(tree.nodes.size() == 4U, "three rules");
    if (tree.nodes.size() != 4U) return;
    SourcePositionCursor cursor(lines, source.text);
    SelectorVocabulary vocabulary{};
    vocabulary.controls = controls;
    SelectorLowering lowering{};
    std::vector<LoweredSelector> output{};
    LoweringBudget budget{};
    std::array<SelectorOutcome, 3> outcomes{};
    for (std::size_t rule = 0U; rule < 3U; ++rule) {
        const SelectorInput input{.source = source,
                                  .tokens = tokens,
                                  .selectors = tree.nodes[rule + 1U].content,
                                  .rule_label = {},
                                  .cursor = cursor};
        outcomes[rule] = lowering.lower(input, vocabulary, budget, output, diagnostics);
    }
    require(outcomes[0] == SelectorOutcome::lowered && outcomes[1] == SelectorOutcome::dropped &&
                outcomes[2] == SelectorOutcome::lowered,
            "each rule's outcome");
    require(output.size() == 3U && budget.rules == maximum_rules - 3U,
            "the lowered rules append; the dropped one adds none");
    require(diagnostics.records().size() == 1U && diagnostics.records()[0].position.column == ColumnNumber{11U},
            "the shared cursor locates each rule's diagnostics");
}

} // namespace

int main() {
    test_forms();
    test_expansion();
    test_refused();
    test_dropped();
    test_appending();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme selector tests passed\n";
    return 0;
}
