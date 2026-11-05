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
// CSS gives it (with `:is()`, its most specific argument's, EC2), and the
// cascade level that specificity puts a theme-sheet rule at (a stable ID
// at stable_id_rule, else a class at class_rule, else theme_sheet).
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
    // dropped (B3, R2-08). Nothing was appended.
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
// `surface` is `window` or `popup` (owner decision 2026-11-05), written
// where a control type would be.
enum class SimpleKind : std::uint8_t { control_type, stable_id, style_class, state, part, role, surface };
struct Simple final {
    SimpleKind kind{SimpleKind::control_type};
    // The token that wrote it, for diagnostics.
    std::uint32_t token{};
    // The written name: the control type, ID, class or part (in its
    // written kebab-case for a type or part).
    std::string_view name{};
    AnatomyState state{AnatomyState::normal};
    PartRole role{PartRole::region};
    SurfaceKind surface{SurfaceKind::none};
};

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
