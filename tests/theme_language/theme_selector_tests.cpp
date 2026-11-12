// The theme language's selectors (stage T3, phase P3). Gate: one
// conformance fixture per selector form of PLAN.md section 1.4 and per
// refused form; the specificity and the level of each. Custom states
// (R2-13): resolved against the selector's control type or item system,
// a standard name first, and W106 where nothing declares them.

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
using gui_forms::AncestorCombinator;
using gui_forms::CascadeLevel;
using gui_forms::AnatomyDefect;
using gui_forms::ControlAnatomy;
using gui_forms::CustomStateDeclaration;
using gui_forms::CustomStateIndex;
using gui_forms::ItemSystemDeclaration;
using gui_forms::PartDeclaration;
using gui_forms::PartRole;
using gui_forms::RuleAncestor;
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
using gui_forms::validate_control_anatomy;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// -- the vocabulary: four control types ---------------------------------------
// The button declares the custom state danger, and the list view's items
// modified and pinned (R2-13); the check box declares none.

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
constexpr std::array<CustomStateDeclaration, 1> button_custom_states{{{.name = "danger"}}};
constexpr std::array<CustomStateDeclaration, 2> list_item_custom_states{{{.name = "modified"}, {.name = "pinned"}}};
constexpr std::array<ItemSystemDeclaration, 1> list_items{{
    {.name = "item", .parts = list_item_parts, .states = {}, .custom_states = list_item_custom_states},
}};
const std::array<ControlAnatomy, 4> controls{{
    {.control_id = "button",
     .parts = button_parts,
     .states = {},
     .custom_states = button_custom_states,
     .item_systems = {}},
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

// Lowers the selectors of the sheet `<selectors> {}` against `vocabulary`
// (by default the four control types above).
Lowering lower(std::string_view selectors, std::size_t budget = maximum_rules,
               std::span<const ControlAnatomy> named = controls) {
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
    vocabulary.controls = named;
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

// :where(), :not() and :family() (selector step 2).
void test_step_two() {
    const Lowering where = lower("button:where(.a, #b)");
    require(where.outcome == SelectorOutcome::lowered && where.selectors.size() == 2U, ":where() expands as :is()");
    if (where.selectors.size() == 2U) {
        require(where.selectors[0].specificity == specificity(0U, 0U, 1U) &&
                    where.selectors[1].specificity == specificity(0U, 0U, 1U) &&
                    where.selectors[1].selector.stable_id == "b",
                "a :where() counts nothing");
    }

    const LoweredSelector negated = lower_one("button:not(.a, :hover)");
    require(negated.selector.control_type == "button" && negated.selector.excluded.size() == 2U &&
                negated.specificity == specificity(0U, 1U, 1U),
            "a :not() is one selector excluding each argument, counting its most specific");
    if (negated.selector.excluded.size() == 2U) {
        require(negated.selector.excluded[0].classes == std::vector<std::string>{"a"} &&
                    negated.selector.excluded[1].states.size() == 1U,
                "each argument its own exclusion");
    }
    const LoweredSelector by_id = lower_one(":not(#x.y)");
    require(by_id.specificity == specificity(1U, 1U, 0U) && by_id.selector.excluded.size() == 1U &&
                by_id.selector.excluded[0].stable_id == "x",
            "a compound argument counts as a compound");
    const Lowering spread = lower(":not(:is(.a, .b))");
    require(spread.outcome == SelectorOutcome::lowered && spread.selectors.size() == 1U &&
                spread.selectors[0].selector.excluded.size() == 2U,
            "an :is() inside a :not() gives an exclusion each");

    const LoweredSelector family = lower_one(":family(accent-button):hover");
    require(family.selector.family == "accent_button" && family.specificity == specificity(0U, 2U, 0U),
            "a family counts as a pseudo-class");
    const LoweredSelector not_family = lower_one("button:not(:family(command-button))");
    require(not_family.selector.excluded.size() == 1U &&
                not_family.selector.excluded[0].family == "command_button",
            "a family inside a :not()");
}

// Descendant and child combinators (selector step 4).
void test_combinators() {
    const LoweredSelector descendant = lower_one("#form .row button:hover");
    require(descendant.selector.control_type == "button" && descendant.selector.ancestors.size() == 2U &&
                descendant.specificity == specificity(1U, 2U, 1U),
            "a descendant chain: two ancestor compounds, every compound counted");
    if (descendant.selector.ancestors.size() == 2U) {
        const RuleAncestor& nearest = descendant.selector.ancestors[0];
        const RuleAncestor& farthest = descendant.selector.ancestors[1];
        require(nearest.classes == std::vector<std::string>{"row"} &&
                    nearest.combinator == AncestorCombinator::descendant && farthest.stable_id == "form",
                "the ancestors nearest first");
    }
    const LoweredSelector child = lower_one("track-bar > button");
    require(child.selector.ancestors.size() == 1U && child.selector.ancestors[0].control_type == "track_bar" &&
                child.selector.ancestors[0].combinator == AncestorCombinator::child,
            "a child combinator, the type in its registered spelling");
    const LoweredSelector tight = lower_one("*>button");
    require(tight.selector.ancestors.size() == 1U && tight.selector.ancestors[0].control_type.empty(),
            "'*' as an ancestor, no whitespace needed around '>'");
    const Lowering list = lower(".a button, check-box");
    require(list.outcome == SelectorOutcome::lowered && list.selectors.size() == 2U &&
                list.selectors[1].selector.ancestors.empty(),
            "each selector of a list has its own ancestors");
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
    const std::array<RefusedCase, 23> cases{{
        {":is(button label)", DiagnosticCode::combinator_not_admitted, 12U},
        {"button > :hover > label", DiagnosticCode::selector_syntax, 11U},
        {"> label", DiagnosticCode::selector_syntax, 1U},
        {"button+check-box", DiagnosticCode::combinator_not_admitted, 7U},
        {"button ~ check-box", DiagnosticCode::combinator_not_admitted, 8U},
        {"&:hover", DiagnosticCode::combinator_not_admitted, 1U},
        {":not(:not(.a))", DiagnosticCode::pseudo_class_not_admitted, 6U},
        {":not(::part(face))", DiagnosticCode::selector_syntax, 13U},
        {"button:not", DiagnosticCode::selector_syntax, 8U},
        {":family(button, card)", DiagnosticCode::selector_syntax, 15U},
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
    const std::array<std::string_view, 11> unknown{{
        "slider",
        ":family(nothing)",
        ":not(slider)",
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
    // As CSS's `::part()` matches nothing for a part the element lacks, an
    // unknown part leaves out its own selector, with one warning each, and
    // the rest of the list applies.
    const Lowering part_in_list = lower("button::part(thumb), track-bar::part(thumb), check-box::part(face)");
    const bool kept_rest = part_in_list.outcome == SelectorOutcome::lowered &&
                           part_in_list.selectors.size() == 1U &&
                           part_in_list.selectors[0].selector.control_type == "track_bar" &&
                           part_in_list.selectors[0].selector.part == "thumb" &&
                           part_in_list.diagnostics.size() == 2U &&
                           part_in_list.diagnostics[0].code == DiagnosticCode::unknown_selector_name &&
                           part_in_list.diagnostics[0].detail == "thumb" &&
                           part_in_list.diagnostics[1].detail == "face" &&
                           part_in_list.budget_left < maximum_rules;
    require(kept_rest, "a part the type lacks leaves out only its own selector");
    const Lowering part_in_group = lower(":is(button, track-bar)::part(thumb)");
    require(part_in_group.outcome == SelectorOutcome::lowered && part_in_group.selectors.size() == 1U &&
                part_in_group.selectors[0].selector.control_type == "track_bar" &&
                part_in_group.diagnostics.size() == 1U,
            "an expanded alternative whose type lacks the part is left out, warned once");
    const Lowering only_unknown_parts = lower("button::part(thumb), check-box::part(face)");
    require(only_unknown_parts.outcome == SelectorOutcome::dropped && only_unknown_parts.selectors.empty() &&
                only_unknown_parts.diagnostics.size() == 2U && only_unknown_parts.budget_left == maximum_rules,
            "a list left with no selector is dropped");
    const Lowering then_error = lower("slider + a");
    require(then_error.outcome == SelectorOutcome::refused && then_error.diagnostics.size() == 2U &&
                then_error.diagnostics[1].code == DiagnosticCode::combinator_not_admitted,
            "an error after an unknown name still refuses");
}

// -- custom states (R2-13) ------------------------------------------------------

// Whether `result` dropped its rule with one W106 whose detail contains
// `checked`.
bool dropped_naming(const Lowering& result, std::string_view checked) {
    const bool dropped = result.outcome == SelectorOutcome::dropped && result.selectors.empty() &&
                         result.diagnostics.size() == 1U &&
                         result.diagnostics[0].code == DiagnosticCode::unknown_selector_name &&
                         result.diagnostics[0].detail.find(checked) != std::string::npos &&
                         result.budget_left == maximum_rules;
    return dropped;
}

void test_custom_states() {
    const std::vector<CustomStateIndex> first{CustomStateIndex{0U}};
    const std::vector<CustomStateIndex> second{CustomStateIndex{1U}};

    const LoweredSelector danger = lower_one("button:state(danger)");
    require(danger.selector.custom_states == first && danger.selector.states.empty() &&
                danger.specificity == specificity(0U, 1U, 1U),
            "a custom state of the type, at its position, counting as a pseudo-class");
    const LoweredSelector face = lower_one("button::part(face):state(danger):hover");
    require(face.selector.part == "face" && face.selector.custom_states == first &&
                face.selector.states == std::vector<AnatomyState>{AnatomyState::hot} &&
                face.specificity == specificity(0U, 2U, 2U),
            "a part shares its control's custom states, beside a standard state");
    const LoweredSelector pinned = lower_one("list-view::part(row):state(pinned)");
    require(pinned.selector.custom_states == second, "an item part reads its item system's custom states");
    const LoweredSelector negated = lower_one("button:not(:state(danger))");
    require(negated.selector.excluded.size() == 1U && negated.selector.excluded[0].custom_states == first &&
                negated.selector.custom_states.empty(),
            "a custom state in :not() is read against the selector's type");
    const LoweredSelector repeated = lower_one("button:state(danger):state(danger)");
    require(repeated.selector.custom_states == first, "a custom state written twice is held once");

    // Not declared by the type checked: W106 naming it.
    require(dropped_naming(lower("check-box:state(danger)"), "check-box"),
            "a custom state the type does not declare is W106 naming the type");
    require(dropped_naming(lower("list-view:state(pinned)"), "list-view"),
            "an item system's custom state is not the control's own");
    require(dropped_naming(lower("button:not(:state(pinned))"), "button"),
            "an undeclared custom state in :not() is W106 too");
    require(dropped_naming(lower("track-bar::part(thumb):not(button:state(nothing))"), "button"),
            "a :not() argument with its own type reads that type's custom states");

    // No element: nothing to declare it.
    require(dropped_naming(lower("::part(face):state(danger)"), "no control type"),
            "a selector naming no type cannot name a custom state");
    require(dropped_naming(lower(".primary:state(danger)"), "no control type"),
            "a class alone names no type");
    require(dropped_naming(lower(":family(button):state(danger)"), "no control type"),
            "a family names no type");

    // As an unknown part, an undeclared custom state leaves out only its
    // own selector.
    const Lowering in_list = lower("button:state(danger), check-box:state(danger)");
    require(in_list.outcome == SelectorOutcome::lowered && in_list.selectors.size() == 1U &&
                in_list.selectors[0].selector.control_type == "button" && in_list.diagnostics.size() == 1U &&
                in_list.diagnostics[0].detail.find("check-box") != std::string::npos,
            "the type declaring it keeps its selector; the other is left out with W106");
    const Lowering in_group = lower(":is(button, check-box):state(danger)");
    require(in_group.outcome == SelectorOutcome::lowered && in_group.selectors.size() == 1U &&
                in_group.selectors[0].selector.custom_states == first && in_group.diagnostics.size() == 1U,
            "each expansion reads its own type's custom states");
    const Lowering in_ancestor = lower("button:state(danger) check-box");
    require(in_ancestor.outcome == SelectorOutcome::refused,
            "an ancestor compound takes no custom state, as it takes no state");

    // Precedence and collision: a standard name is read first, so a custom
    // state named as a standard one never shadows it; the anatomy carrying
    // it is refused by validate_control_anatomy.
    constexpr std::array<CustomStateDeclaration, 2> gauge_custom_states{{{.name = "hot"}, {.name = "lit"}}};
    const std::array<ControlAnatomy, 1> gauge{{
        {.control_id = "gauge", .parts = {}, .states = {}, .custom_states = gauge_custom_states, .item_systems = {}},
    }};
    require(validate_control_anatomy(gauge[0]).defect == AnatomyDefect::custom_state_shadows_standard_state,
            "a custom state named as a standard one is refused with the anatomy");
    const Lowering standard = lower("gauge:state(hot)", maximum_rules, gauge);
    require(standard.outcome == SelectorOutcome::lowered && standard.selectors.size() == 1U &&
                standard.selectors[0].selector.states == std::vector<AnatomyState>{AnatomyState::hot} &&
                standard.selectors[0].selector.custom_states.empty(),
            "the standard state wins a colliding name");
    const Lowering lit = lower("gauge:state(lit)", maximum_rules, gauge);
    require(lit.outcome == SelectorOutcome::lowered && lit.selectors.size() == 1U &&
                lit.selectors[0].selector.custom_states == second,
            "a custom state's position is its declaration's, whatever precedes it");
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
    test_step_two();
    test_combinators();
    test_refused();
    test_dropped();
    test_custom_states();
    test_appending();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme selector tests passed\n";
    return 0;
}
