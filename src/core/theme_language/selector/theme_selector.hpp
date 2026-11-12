#pragma once

#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The derived selector limits (PLAN.md section 6; R2-14 starting values).
inline constexpr std::size_t maximum_is_arguments = 32U;
// Simple selectors in one expanded selector: its type, ID, classes, states,
// part and role, as written, `:is()` arguments included. It bounds the work
// of expanding a selector to a constant per expanded rule.
inline constexpr std::size_t maximum_simple_selectors = 32U;

// The lowered sheet's size (a derived limit, PLAN.md P5): at most
// maximum_lowered_bytes, counted as an estimate of the records' bytes, so
// that a selector list expanding into many rules, each copying its rule's
// names and declarations, builds a bounded amount. Each record counts
// lowered_record_bytes and its strings their bytes (lowered_selector_bytes
// for a selector).
inline constexpr std::size_t maximum_lowered_bytes = 16U * maximum_sheet_bytes;
inline constexpr std::size_t lowered_record_bytes = 64U;

// What the lowering of one sheet may still build: `rules` expanded rules,
// at most maximum_rules, and `bytes` lowered bytes, at most
// maximum_lowered_bytes.
struct LoweringBudget final {
    std::size_t rules{maximum_rules};
    std::size_t bytes{maximum_lowered_bytes};
};

// The lowered bytes one selector counts: a record, its names' bytes and one
// byte per state.
[[nodiscard]] std::size_t lowered_selector_bytes(const RuleSelector& selector) noexcept;

// One selector of a rule, expanded: the engine's selector, the specificity
// CSS gives it (with `:is()`, its most specific argument's, EC2), and its
// cascade level: theme_sheet, where specificity alone orders the rules,
// as CSS's does (selector step 1).
struct LoweredSelector final {
    RuleSelector selector{};
    RuleSpecificity specificity{};
    CascadeLevel level{CascadeLevel::theme_sheet};

    friend bool operator==(const LoweredSelector& left, const LoweredSelector& right) noexcept {
        return left.selector == right.selector && left.specificity == right.specificity &&
               left.level == right.level;
    }
};

// The names a selector may use: the anatomy of each control type a theme
// may name (manifests/control-schema.json lists the stock ones). A part
// named with a control type must be declared by that type, among its own
// parts or its item systems'; a part named without one by some type.
// `:state(name)` names a standard state first (AnatomyState), else a
// custom state of the selector's control type (R2-13): its anatomy's
// custom_states, or those of the item system declaring the selector's
// part. Each anatomy is expected to pass validate_control_anatomy, which
// refuses a custom state named as a standard one; one that was not checked
// cannot shadow a standard state, since the standard name is read first.
// Borrowed for the lowering.
struct SelectorVocabulary final {
    std::span<const ControlAnatomy> controls{};
};

// What one lowering reads, all borrowed for the call: the sheet, its
// tokens, the selector list's tokens (a style rule's content), the label
// diagnostics give the rule, and the cursor that locates diagnostics. The
// cursor is the caller's, so that lowering every rule of a sheet locates
// in one pass.
struct SelectorInput final {
    const ThemeSource& source;
    const ThemeTokenBuffer& tokens;
    TokenRange selectors{};
    std::string_view rule_label{};
    SourcePositionCursor& cursor;
};

enum class SelectorOutcome : std::uint8_t {
    // The selectors were appended to the output.
    lowered,
    // A name the vocabulary does not know: one warning, and the rule is
    // dropped (B3, R2-08). Nothing was appended. A part the selector's
    // control type lacks leaves out only that selector, with its own
    // warning, as CSS's `::part()` matches nothing for an unknown part; so
    // does a custom state its control type does not declare, or one in a
    // selector naming no control type (R2-13). A list left with no
    // selector is dropped.
    dropped,
    // An error: the theme is refused (B2). Nothing was appended.
    refused,
};

namespace selector_detail {

// One choice of every `:is()` argument in a compound: the simple
// selectors it is made of, as positions in the lowering's simple
// selector table. At most maximum_simple_selectors.
struct Alternative final {
    std::array<std::uint32_t, maximum_simple_selectors> simples{};
    std::uint32_t count{};
};

// A simple selector read from the list.
// `surface` is `window` or `popup` (owner decision 2026-11-04), written
// where a control type would be.
// `family` is `:family(<name>)`, a control visual family (selector step
// 2); `negation` is a whole `:not()`, its arguments held in
// SelectorWorkspace::negated. `custom_state` is a `:state(name)` that names
// no standard state: it is resolved against the selector's control type
// once the selector is expanded (R2-13), since an `:is()` may give each
// expansion its own type.
enum class SimpleKind : std::uint8_t {
    control_type,
    stable_id,
    style_class,
    state,
    custom_state,
    part,
    role,
    surface,
    family,
    negation,
};
struct Simple final {
    SimpleKind kind{SimpleKind::control_type};
    // The token that wrote it, for diagnostics.
    std::uint32_t token{};
    // The written name: the control type, ID, class, part or custom state
    // (in its written kebab-case for a type, part or custom state).
    std::string_view name{};
    AnatomyState state{AnatomyState::normal};
    PartRole role{PartRole::region};
    SurfaceKind surface{SurfaceKind::none};
    // A negation's arguments: `negated_count` alternatives from
    // `negated_first` in SelectorWorkspace::negated.
    std::uint32_t negated_first{};
    std::uint32_t negated_count{};
};

// What an open group is: `:is()`, which counts as its most specific
// argument; `:where()`, which counts nothing; or `:not()`, whose arguments
// exclude parts and count as its most specific one (selector step 2).
enum class GroupKind : std::uint8_t { is, where, negation };

// One compound selector being read, and, for an `:is()`, the union of
// its finished arguments.
struct Frame final {
    std::vector<Alternative> compound{};
    RuleSpecificity compound_specificity{};
    // Whether the compound has a simple selector or `*` yet, and whether
    // whitespace followed it (a descendant combinator if another simple
    // selector follows).
    bool compound_started{};
    bool space_pending{};
    std::vector<Alternative> arguments{};
    RuleSpecificity arguments_specificity{};
    std::size_t argument_count{};
    GroupKind group{GroupKind::is};
};

// The storage one lowering works in, kept from rule to rule: the simple
// selectors read, one frame per open `:is()` (and the list itself), and
// the scratch product of an `:is()` with its compound.
// An escaped name of the list being lowered (token_flag_escaped): its
// token and where its decoded name is in SelectorWorkspace::decoded.
struct DecodedName final {
    std::uint32_t token{};
    std::uint32_t offset{};
    std::uint32_t length{};
};

struct SelectorWorkspace final {
    std::vector<Simple> simples{};
    // The list's escaped names, decoded before it is read, so that the
    // names read from them stay valid while it is lowered.
    std::string decoded{};
    std::vector<DecodedName> decoded_names{};
    std::array<Frame, maximum_nesting_depth + 1U> frames{};
    std::vector<Alternative> product{};
    // The arguments of the list's `:not()`s, each negation a run of them.
    std::vector<Alternative> negated{};
    // The ancestor compounds of the selector being read, farthest first,
    // and what they count (selector step 4).
    std::vector<RuleAncestor> ancestors{};
    RuleSpecificity ancestors_specificity{};
};

} // namespace selector_detail

// Lowers selector lists (PLAN.md section 1.4): each compound selector of
// the list, with each `:is()` expanded into one selector per argument, is
// appended to the output in written order. The lowering keeps its working
// storage from rule to rule; one instance serves one compilation at a time.
class SelectorLowering final {
public:
    // Appends the rule's selectors to `output` and subtracts their count
    // and their lowered bytes from `budget`. A list that would expand past
    // `budget.rules` is refused with too_many_rules (the rules of a sheet,
    // expanded, are at most maximum_rules), and one whose selectors would
    // count more than `budget.bytes` with lowered_sheet_too_large. A
    // dropped rule's selectors count, since each is built. Its diagnostics
    // are appended to `diagnostics`. On `dropped` or `refused`, `output` and
    // `budget` are unchanged.
    [[nodiscard]] SelectorOutcome lower(const SelectorInput& input, const SelectorVocabulary& vocabulary,
                                        LoweringBudget& budget, std::vector<LoweredSelector>& output,
                                        ThemeDiagnosticList& diagnostics);

private:
    selector_detail::SelectorWorkspace workspace_{};
};

} // namespace gui_forms::theme_language
