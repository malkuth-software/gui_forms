// Selector lowering (PLAN.md section 1.4): a style rule's selector list,
// as tokens, into the engine's RuleSelector records. One pass reads the
// list left to right. Each compound selector being read is a frame on an
// explicit stack, one per open group (`:is()`, `:where()` or `:not()`); a
// compound is held as its alternatives, one per choice of its `:is()` and
// `:where()` arguments, so that closing one multiplies its compound's
// alternatives by the arguments' union. A `:not()` is one simple selector
// whose arguments become the expanded selector's exclusions.
// Every alternative count is bounded by the rule budget, and every
// alternative by maximum_simple_selectors, so expanding costs a constant
// per rule the list expands into; the bytes the expanded selectors copy are
// bounded by the byte budget.

#include "theme_selector.hpp"

#include "gui_forms/theme/types/theme_types.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

using selector_detail::Alternative;
using selector_detail::DecodedName;
using selector_detail::Frame;
using selector_detail::GroupKind;
using selector_detail::SelectorWorkspace;
using selector_detail::Simple;
using selector_detail::SimpleKind;

// A standard pseudo-class and the state it selects (PLAN.md section 1.4).
struct PseudoClassState final {
    std::string_view name{};
    AnatomyState state{AnatomyState::normal};
};

constexpr std::array<PseudoClassState, 12> standard_pseudo_classes{{
    {"hover", AnatomyState::hot},
    {"active", AnatomyState::pressed},
    {"focus", AnatomyState::focused},
    {"focus-visible", AnatomyState::focus_visible},
    {"checked", AnatomyState::checked},
    {"indeterminate", AnatomyState::indeterminate},
    {"disabled", AnatomyState::disabled},
    {"enabled", AnatomyState::enabled},
    {"read-only", AnatomyState::read_only},
    {"invalid", AnatomyState::invalid},
    {"open", AnatomyState::open},
    {"default", AnatomyState::preselect},
}};

// CSS's pseudo-classes outside the language (B1): an error, where a name
// CSS does not have is an unknown name (a warning).
constexpr std::array<std::string_view, 25> refused_pseudo_classes{{
    "has",          "nth-child",    "nth-last-child", "nth-of-type",
    "nth-last-of-type", "first-child", "last-child",  "only-child",   "first-of-type",  "last-of-type",
    "only-of-type", "empty",          "focus-within", "read-write",   "link",           "visited",
    "any-link",     "target",         "scope",        "host",         "lang",           "dir",
    "placeholder-shown", "required",  "optional",
}};

// The language's functional pseudo-classes, an error written without
// their arguments.
constexpr std::array<std::string_view, 6> argument_pseudo_classes{{"is", "where", "not", "family", "state", "item"}};

constexpr std::array<std::string_view, 9> refused_pseudo_elements{{
    "before", "after", "first-line", "first-letter", "marker", "placeholder", "selection", "backdrop",
    "file-selector-button",
}};

template <std::size_t Count>
[[nodiscard]] bool names_one_of(std::string_view name, const std::array<std::string_view, Count>& names) noexcept {
    for (const std::string_view candidate : names) {
        if (equals_ascii_folded(name, candidate)) return true;
    }
    return false;
}

// The registered (snake_case) name a kebab-case name writes, or an empty
// string when it is not one: a name written with '_', upper case or any
// other character names nothing.
[[nodiscard]] RuleSpecificity added(RuleSpecificity left, RuleSpecificity right) noexcept {
    RuleSpecificity sum{};
    sum.ids = left.ids + right.ids;
    sum.classes = left.classes + right.classes;
    sum.types = left.types + right.types;
    return sum;
}

// What one simple selector adds to its selector's specificity: the engine's
// count (rule_specificity) for one of its kind.
[[nodiscard]] RuleSpecificity specificity_of(SimpleKind kind) noexcept {
    RuleSpecificity one{};
    if (kind == SimpleKind::stable_id || kind == SimpleKind::item_id) one.ids = 1U;
    const bool pseudo_class = kind == SimpleKind::style_class || kind == SimpleKind::state ||
                              kind == SimpleKind::custom_state || kind == SimpleKind::family ||
                              kind == SimpleKind::item_class;
    if (pseudo_class) one.classes = 1U;
    if (kind == SimpleKind::control_type || kind == SimpleKind::surface) one.types = 1U;
    if (kind == SimpleKind::part || kind == SimpleKind::role) one.types = 1U;
    return one;
}

// The surface a registered name selects, or none: `window` and `popup`
// (owner decision 2026-11-04).
[[nodiscard]] SurfaceKind surface_named(std::string_view registered) noexcept {
    if (registered == "window") return SurfaceKind::window;
    if (registered == "popup") return SurfaceKind::popup;
    return SurfaceKind::none;
}

// The anatomy of the control type `registered`, or null when the
// vocabulary has none. The anatomy is the vocabulary's, borrowed.
[[nodiscard]] const ControlAnatomy* find_control(const SelectorVocabulary& vocabulary,
                                                 std::string_view registered) noexcept {
    for (const ControlAnatomy& anatomy : vocabulary.controls) {
        if (anatomy.control_id == registered) return &anatomy;
    }
    return nullptr;
}

// Whether `anatomy` declares the part `registered` among its own parts or
// its item systems'.
[[nodiscard]] bool declares_part(const ControlAnatomy& anatomy, std::string_view registered) noexcept {
    if (find_declared_part(anatomy.parts, registered).has_value()) return true;
    for (const ItemSystemDeclaration& system : anatomy.item_systems) {
        if (find_declared_part(system.parts, registered).has_value()) return true;
    }
    return false;
}

// The custom states a selector of `anatomy` reads (R2-13): those of the
// item system declaring `part` when the part is an item part, else the
// control type's own. A part of the control's own shares its states.
[[nodiscard]] std::span<const CustomStateDeclaration> custom_state_scope(const ControlAnatomy& anatomy,
                                                                         std::string_view part) noexcept {
    if (part.empty() || find_declared_part(anatomy.parts, part).has_value()) return anatomy.custom_states;
    for (const ItemSystemDeclaration& system : anatomy.item_systems) {
        if (find_declared_part(system.parts, part).has_value()) return system.custom_states;
    }
    return anatomy.custom_states;
}

// The position of the custom state `registered` in `declared`, or none.
[[nodiscard]] std::optional<CustomStateIndex> find_declared_custom_state(
    std::span<const CustomStateDeclaration> declared, std::string_view registered) noexcept {
    for (std::size_t index = 0U; index < declared.size(); ++index) {
        if (declared[index].name != registered) continue;
        const CustomStateIndex found{static_cast<std::uint8_t>(index)};
        return found;
    }
    return std::nullopt;
}

// A registered name in the kebab case a sheet writes, for diagnostics.
[[nodiscard]] std::string kebab_case_of(std::string_view registered) {
    std::string written(registered);
    std::replace(written.begin(), written.end(), '_', '-');
    return written;
}

void reset_compound(Frame& frame) {
    frame.compound.clear();
    frame.compound.push_back(Alternative{});
    frame.compound_specificity = RuleSpecificity{};
    frame.compound_started = false;
    frame.space_pending = false;
}

// One function argument that is a single name, `(face)`: the name's token
// and the token after the ')'. `found` is false when the argument is
// anything else; `offending` is then the token that is not admitted.
struct NameArgument final {
    bool found{};
    std::uint32_t name{};
    std::uint32_t after{};
    std::uint32_t offending{};
};

class SelectorPass final {
public:
    SelectorPass(const SelectorInput& input, const SelectorVocabulary& vocabulary, LoweringBudget budget,
                 std::vector<LoweredSelector>& output, ThemeDiagnosticList& diagnostics,
                 SelectorWorkspace& workspace) noexcept;

    [[nodiscard]] SelectorOutcome run();
    // How many selectors run() expanded, and the bytes they count.
    [[nodiscard]] std::size_t emitted() const noexcept;
    [[nodiscard]] std::size_t emitted_bytes() const noexcept;

private:
    [[nodiscard]] bool read_simple();
    [[nodiscard]] bool read_pseudo();
    [[nodiscard]] bool read_pseudo_element(std::uint32_t name_token);
    [[nodiscard]] bool open_group(std::uint32_t function_token, GroupKind group);
    [[nodiscard]] bool close_group(std::uint32_t closer);
    [[nodiscard]] bool read_family(std::uint32_t function_token);
    [[nodiscard]] bool read_item(std::uint32_t function_token);
    [[nodiscard]] bool finish_compound(std::uint32_t at);
    [[nodiscard]] bool add_simple(const Simple& simple);
    // add_simple, counting `counted` toward the compound's specificity.
    [[nodiscard]] bool add_counted(const Simple& simple, RuleSpecificity counted);
    // A `:not()` argument's exclusion, appended to `selector` unless it holds
    // one already; false (reported) when it names a part, role or surface.
    // Its custom states are resolved when `resolve` is set; one its
    // control type (or the selector's) does not declare clears `declared`,
    // reported (R2-13), and the exclusion is not appended.
    [[nodiscard]] bool add_exclusion(const Alternative& argument, RuleSelector& selector, bool resolve,
                                     bool& declared);
    // Resolves the custom state written by `simple` (R2-13) against the
    // custom states of `control_type` (or of the item system declaring
    // `part`): its position, or none, reported (W106) with the element
    // checked, once per state token.
    [[nodiscard]] std::optional<CustomStateIndex> resolve_custom_state(const Simple& simple,
                                                                       std::string_view control_type,
                                                                       std::string_view part);
    // Ends the compound being read as an ancestor compound joined by
    // `combinator` to the next (selector step 4); false (reported) when it
    // is empty, expanded, or names more than a type, a stable ID and
    // classes, or when the selector would have too many ancestors.
    [[nodiscard]] bool push_ancestor(std::uint32_t at, AncestorCombinator combinator);
    [[nodiscard]] bool materialize(const Alternative& alternative, RuleSpecificity specificity);
    [[nodiscard]] NameArgument name_argument(std::uint32_t function_token) const noexcept;
    [[nodiscard]] std::uint32_t after_matching_paren(std::uint32_t function_token) const noexcept;
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;
    // A function token's name, without its '('.
    [[nodiscard]] std::string_view function_name(std::uint32_t index) const noexcept;
    void note_unknown(std::uint32_t token, std::string_view name);
    // Reports a part its selector's control type lacks (W106), once per
    // part token: only that selector is left out (materialize).
    void note_unknown_part(std::uint32_t token);
    void report(DiagnosticCode code, std::uint32_t token, std::string detail);

    const SelectorInput& input_;
    const SelectorVocabulary& vocabulary_;
    const std::vector<Token>& tokens_;
    LoweringBudget budget_{};
    std::vector<LoweredSelector>& output_;
    ThemeDiagnosticList& diagnostics_;
    SelectorWorkspace& workspace_;
    std::uint32_t index_{};
    std::uint32_t end_{};
    std::size_t depth_{};
    // How many open groups are `:not()`s: one may not open inside another.
    std::size_t negations_open_{};
    std::size_t emitted_{};
    std::size_t emitted_bytes_{};
    // Whether an unknown name has dropped the rule (its one warning is
    // reported); reading goes on, since an error later still refuses.
    bool dropped_{};
    // Selectors appended to the output, and the part token last reported
    // by note_unknown_part (zero for none: a part token never comes first).
    std::size_t appended_{};
    std::uint32_t reported_part_token_{};
    // The custom state token last reported by resolve_custom_state (zero
    // for none: a state token never comes first).
    std::uint32_t reported_state_token_{};
};

SelectorPass::SelectorPass(const SelectorInput& input, const SelectorVocabulary& vocabulary, LoweringBudget budget,
                           std::vector<LoweredSelector>& output, ThemeDiagnosticList& diagnostics,
                           SelectorWorkspace& workspace) noexcept
    : input_(input), vocabulary_(vocabulary), tokens_(input.tokens.tokens), budget_(budget), output_(output),
      diagnostics_(diagnostics), workspace_(workspace) {}

SelectorOutcome SelectorPass::run() {
    index_ = input_.selectors.first.value;
    end_ = input_.selectors.end.value;
    workspace_.simples.clear();
    workspace_.simples.reserve(end_ - index_);
    workspace_.negated.clear();
    workspace_.ancestors.clear();
    workspace_.ancestors_specificity = RuleSpecificity{};
    // Escaped names (`#gallery\.heading`) are decoded first: a decoded
    // name is never longer than its text, so the reservation holds them
    // all and nothing read from `decoded` moves.
    workspace_.decoded.clear();
    workspace_.decoded_names.clear();
    std::size_t escaped_bytes = 0U;
    for (std::uint32_t token = index_; token < end_; ++token) {
        if ((tokens_[token].flags & token_flag_escaped) != 0U) escaped_bytes += tokens_[token].length.value;
    }
    workspace_.decoded.reserve(escaped_bytes);
    for (std::uint32_t token = index_; token < end_; ++token) {
        if ((tokens_[token].flags & token_flag_escaped) == 0U) continue;
        const std::size_t offset = workspace_.decoded.size();
        const Token& escaped = tokens_[token];
        const std::string_view written = input_.source.text.substr(escaped.offset.value, escaped.length.value);
        append_decoded_name(written, workspace_.decoded);
        workspace_.decoded_names.push_back(
            DecodedName{.token = token,
                        .offset = static_cast<std::uint32_t>(offset),
                        .length = static_cast<std::uint32_t>(workspace_.decoded.size() - offset)});
    }
    Frame& list = workspace_.frames[0];
    reset_compound(list);
    while (index_ < end_) {
        const TokenKind kind = tokens_[index_].kind;
        Frame& frame = workspace_.frames[depth_];
        if (kind == TokenKind::comment) {
            ++index_;
            continue;
        }
        if (kind == TokenKind::whitespace) {
            if (frame.compound_started) frame.space_pending = true;
            ++index_;
            continue;
        }
        if (kind == TokenKind::comma) {
            if (!finish_compound(index_)) return SelectorOutcome::refused;
            ++index_;
            continue;
        }
        if (kind == TokenKind::right_paren && depth_ != 0U) {
            if (!close_group(index_)) return SelectorOutcome::refused;
            ++index_;
            continue;
        }
        // Combinators join compounds at the list's level only (selector
        // step 4): `>` a child, whitespace before a compound a descendant.
        const bool child = kind == TokenKind::delim && token_text(index_) == ">";
        if (child && depth_ == 0U) {
            if (!push_ancestor(index_, AncestorCombinator::child)) return SelectorOutcome::refused;
            ++index_;
            continue;
        }
        if (frame.space_pending && depth_ == 0U) {
            if (!push_ancestor(index_, AncestorCombinator::descendant)) return SelectorOutcome::refused;
        } else if (frame.space_pending) {
            report(DiagnosticCode::combinator_not_admitted, index_, "whitespace inside a group");
            return SelectorOutcome::refused;
        }
        if (!read_simple()) return SelectorOutcome::refused;
    }
    // The parser balanced every group, so no group is open here.
    if (!finish_compound(end_)) return SelectorOutcome::refused;
    // A list whose every selector named a part its type lacks appended
    // nothing, and is dropped as one unknown name drops a rule.
    if (dropped_ || appended_ == 0U) return SelectorOutcome::dropped;
    return SelectorOutcome::lowered;
}

std::size_t SelectorPass::emitted() const noexcept {
    return emitted_;
}

std::size_t SelectorPass::emitted_bytes() const noexcept {
    return emitted_bytes_;
}

bool SelectorPass::read_simple() {
    const Token& token = tokens_[index_];
    Frame& frame = workspace_.frames[depth_];
    if (token.kind == TokenKind::ident) {
        if (frame.compound_started) {
            report(DiagnosticCode::selector_syntax, index_, "a control type after another simple selector");
            return false;
        }
        const std::string_view name = token_text(index_);
        const std::string registered = registered_name_from_kebab(name);
        frame.compound_started = true;
        // The surface selectors are not control types: no anatomy declares
        // them, and they come before the vocabulary is consulted.
        const SurfaceKind surface = surface_named(registered);
        if (surface != SurfaceKind::none) {
            Simple simple{};
            simple.kind = SimpleKind::surface;
            simple.token = index_;
            simple.name = name;
            simple.surface = surface;
            ++index_;
            return add_simple(simple);
        }
        const bool known = !registered.empty() && find_control(vocabulary_, registered) != nullptr;
        if (!known) {
            note_unknown(index_, name);
            ++index_;
            return true;
        }
        Simple simple{};
        simple.kind = SimpleKind::control_type;
        simple.token = index_;
        simple.name = name;
        ++index_;
        return add_simple(simple);
    }
    if (token.kind == TokenKind::hash) {
        if ((token.flags & token_flag_hash_identifier) == 0U) {
            report(DiagnosticCode::selector_syntax, index_, "a stable ID must be a name");
            return false;
        }
        Simple simple{};
        simple.kind = SimpleKind::stable_id;
        simple.token = index_;
        // The ID follows its '#' and keeps its own spelling.
        simple.name = token_text(index_).substr(1U);
        ++index_;
        return add_simple(simple);
    }
    if (token.kind == TokenKind::colon) return read_pseudo();
    if (token.kind == TokenKind::left_square) {
        report(DiagnosticCode::attribute_selector_not_admitted, index_, {});
        return false;
    }
    if (token.kind != TokenKind::delim) {
        report(DiagnosticCode::selector_syntax, index_, std::string(token_kind_name(token.kind)));
        return false;
    }
    const char written = token_text(index_)[0];
    if (written == '*') {
        if (frame.compound_started) {
            report(DiagnosticCode::selector_syntax, index_, "'*' after another simple selector");
            return false;
        }
        frame.compound_started = true;
        ++index_;
        return true;
    }
    if (written == '.') {
        const std::uint32_t name_token = index_ + 1U;
        if (name_token >= end_ || tokens_[name_token].kind != TokenKind::ident) {
            report(DiagnosticCode::selector_syntax, index_, "'.' without a class name");
            return false;
        }
        const std::string_view name = token_text(name_token);
        if (!valid_style_class_name(name)) {
            report(DiagnosticCode::selector_syntax, name_token, "not a style class name");
            return false;
        }
        Simple simple{};
        simple.kind = SimpleKind::style_class;
        simple.token = name_token;
        simple.name = name;
        index_ = name_token + 1U;
        return add_simple(simple);
    }
    const bool combinator = written == '>' || written == '+' || written == '~' || written == '&';
    const DiagnosticCode code = combinator ? DiagnosticCode::combinator_not_admitted : DiagnosticCode::selector_syntax;
    report(code, index_, std::string(token_text(index_)));
    return false;
}

bool SelectorPass::read_pseudo() {
    const std::uint32_t colon = index_;
    const std::uint32_t next = colon + 1U;
    if (next >= end_) {
        report(DiagnosticCode::selector_syntax, colon, "':' without a name");
        return false;
    }
    Frame& frame = workspace_.frames[depth_];
    const TokenKind kind = tokens_[next].kind;
    if (kind == TokenKind::colon) return read_pseudo_element(next + 1U);
    if (kind == TokenKind::ident) {
        const std::string_view name = token_text(next);
        frame.compound_started = true;
        index_ = next + 1U;
        for (const PseudoClassState& entry : standard_pseudo_classes) {
            if (!equals_ascii_folded(name, entry.name)) continue;
            Simple simple{};
            simple.kind = SimpleKind::state;
            simple.token = next;
            simple.state = entry.state;
            return add_simple(simple);
        }
        if (names_one_of(name, refused_pseudo_classes)) {
            report(DiagnosticCode::pseudo_class_not_admitted, colon, clipped_theme_detail(name));
            return false;
        }
        if (names_one_of(name, argument_pseudo_classes)) {
            report(DiagnosticCode::selector_syntax, next, "':" + std::string(name) + "' takes arguments");
            return false;
        }
        note_unknown(next, name);
        return true;
    }
    if (kind != TokenKind::function) {
        report(DiagnosticCode::selector_syntax, next, std::string(token_kind_name(kind)));
        return false;
    }
    const std::string_view name = function_name(next);
    if (equals_ascii_folded(name, "is")) return open_group(next, GroupKind::is);
    if (equals_ascii_folded(name, "where")) return open_group(next, GroupKind::where);
    if (equals_ascii_folded(name, "not")) {
        if (negations_open_ != 0U) {
            report(DiagnosticCode::pseudo_class_not_admitted, colon, "':not()' inside ':not()'");
            return false;
        }
        return open_group(next, GroupKind::negation);
    }
    if (equals_ascii_folded(name, "family")) return read_family(next);
    if (equals_ascii_folded(name, "item")) return read_item(next);
    if (equals_ascii_folded(name, "state")) {
        const NameArgument argument = name_argument(next);
        if (!argument.found) {
            report(DiagnosticCode::selector_syntax, argument.offending, "':state()' takes one name");
            return false;
        }
        frame.compound_started = true;
        index_ = argument.after;
        const std::string_view written = token_text(argument.name);
        const std::string registered = registered_name_from_kebab(written);
        // A name written with '_', upper case or any other character
        // names nothing.
        if (registered.empty()) {
            note_unknown(argument.name, written);
            return true;
        }
        // Precedence (R2-13): a standard state first; otherwise a custom
        // state of the selector's control type, resolved once the
        // selector is expanded (materialize).
        const std::optional<AnatomyState> state = anatomy_state_from_name(registered);
        Simple simple{};
        simple.token = argument.name;
        simple.name = written;
        if (!state) {
            simple.kind = SimpleKind::custom_state;
            return add_simple(simple);
        }
        simple.kind = SimpleKind::state;
        simple.state = *state;
        return add_simple(simple);
    }
    if (names_one_of(name, refused_pseudo_classes)) {
        report(DiagnosticCode::pseudo_class_not_admitted, colon, clipped_theme_detail(name));
        return false;
    }
    frame.compound_started = true;
    note_unknown(next, name);
    index_ = after_matching_paren(next);
    return true;
}

// `::<name>` or `::<name>(...)`, with `name_token` after the second ':'.
bool SelectorPass::read_pseudo_element(std::uint32_t name_token) {
    const std::uint32_t colon = index_;
    if (name_token >= end_) {
        report(DiagnosticCode::selector_syntax, colon, "'::' without a name");
        return false;
    }
    Frame& frame = workspace_.frames[depth_];
    const TokenKind kind = tokens_[name_token].kind;
    if (kind == TokenKind::ident) {
        const std::string_view name = token_text(name_token);
        if (names_one_of(name, refused_pseudo_elements)) {
            report(DiagnosticCode::pseudo_class_not_admitted, colon, clipped_theme_detail(name));
            return false;
        }
        frame.compound_started = true;
        note_unknown(name_token, name);
        index_ = name_token + 1U;
        return true;
    }
    if (kind != TokenKind::function) {
        report(DiagnosticCode::selector_syntax, name_token, std::string(token_kind_name(kind)));
        return false;
    }
    const std::string_view name = function_name(name_token);
    const bool part = equals_ascii_folded(name, "part");
    const bool role = equals_ascii_folded(name, "role");
    if (!part && !role) {
        frame.compound_started = true;
        note_unknown(name_token, name);
        index_ = after_matching_paren(name_token);
        return true;
    }
    const NameArgument argument = name_argument(name_token);
    if (!argument.found) {
        report(DiagnosticCode::selector_syntax, argument.offending, part ? "'::part()' takes one name"
                                                                        : "'::role()' takes one name");
        return false;
    }
    frame.compound_started = true;
    index_ = argument.after;
    const std::string_view written = token_text(argument.name);
    const std::string registered = registered_name_from_kebab(written);
    if (registered.empty()) {
        note_unknown(argument.name, written);
        return true;
    }
    Simple simple{};
    simple.token = argument.name;
    simple.name = written;
    if (part) {
        // Checked against the control type once the selector is expanded.
        simple.kind = SimpleKind::part;
        return add_simple(simple);
    }
    const std::optional<PartRole> found = part_role_from_name(registered);
    if (!found) {
        note_unknown(argument.name, written);
        return true;
    }
    simple.kind = SimpleKind::role;
    simple.role = *found;
    return add_simple(simple);
}

bool SelectorPass::open_group(std::uint32_t function_token, GroupKind group) {
    Frame& parent = workspace_.frames[depth_];
    parent.compound_started = true;
    if (depth_ + 1U >= workspace_.frames.size()) {
        report(DiagnosticCode::nesting_too_deep, function_token, std::to_string(maximum_nesting_depth));
        return false;
    }
    ++depth_;
    Frame& frame = workspace_.frames[depth_];
    frame.arguments.clear();
    frame.arguments_specificity = RuleSpecificity{};
    frame.argument_count = 0U;
    frame.group = group;
    if (group == GroupKind::negation) ++negations_open_;
    reset_compound(frame);
    index_ = function_token + 1U;
    return true;
}

bool SelectorPass::close_group(std::uint32_t closer) {
    if (!finish_compound(closer)) return false;
    const Frame& arguments = workspace_.frames[depth_];
    --depth_;
    if (arguments.group == GroupKind::negation) {
        // A `:not()` is one simple selector of its compound: its arguments
        // become the selector's exclusions when it is expanded.
        --negations_open_;
        if (workspace_.negated.size() + arguments.arguments.size() > budget_.rules) {
            report(DiagnosticCode::too_many_rules, closer, std::to_string(maximum_rules));
            return false;
        }
        Simple simple{};
        simple.kind = SimpleKind::negation;
        simple.token = closer;
        simple.negated_first = static_cast<std::uint32_t>(workspace_.negated.size());
        simple.negated_count = static_cast<std::uint32_t>(arguments.arguments.size());
        workspace_.negated.insert(workspace_.negated.end(), arguments.arguments.begin(), arguments.arguments.end());
        // CSS: a `:not()` counts as its most specific argument.
        return add_counted(simple, arguments.arguments_specificity);
    }
    Frame& parent = workspace_.frames[depth_];
    const std::size_t left = parent.compound.size();
    const std::size_t right = arguments.arguments.size();
    // Both counts are at most the budget, so the product cannot overflow.
    if (left * right > budget_.rules) {
        report(DiagnosticCode::too_many_rules, closer, std::to_string(maximum_rules));
        return false;
    }
    std::vector<Alternative>& product = workspace_.product;
    product.clear();
    product.reserve(left * right);
    for (const Alternative& base : parent.compound) {
        for (const Alternative& argument : arguments.arguments) {
            if (base.count + argument.count > maximum_simple_selectors) {
                report(DiagnosticCode::selector_too_long, closer, std::to_string(maximum_simple_selectors));
                return false;
            }
            Alternative merged = base;
            for (std::uint32_t simple = 0U; simple < argument.count; ++simple) {
                merged.simples[merged.count] = argument.simples[simple];
                ++merged.count;
            }
            product.push_back(merged);
        }
    }
    parent.compound.swap(product);
    // CSS: an `:is()` counts as its most specific argument, a `:where()`
    // as nothing.
    if (arguments.group == GroupKind::is) {
        parent.compound_specificity = added(parent.compound_specificity, arguments.arguments_specificity);
    }
    return true;
}

// `:family(<name>)`, a control visual family in kebab case: an unknown
// family is an unknown name (a warning that drops the rule).
bool SelectorPass::read_family(std::uint32_t function_token) {
    const NameArgument argument = name_argument(function_token);
    if (!argument.found) {
        report(DiagnosticCode::selector_syntax, argument.offending, "':family()' takes one name");
        return false;
    }
    Frame& frame = workspace_.frames[depth_];
    frame.compound_started = true;
    index_ = argument.after;
    const std::string_view written = token_text(argument.name);
    const std::string registered = registered_name_from_kebab(written);
    std::string_view family{};
    for (std::size_t index = 0U; index < control_visual_role_count; ++index) {
        // The role names are static strings, so the simple selector may
        // keep one.
        const std::string_view name = control_visual_role_name(static_cast<ControlVisualRole>(index));
        if (!registered.empty() && name == registered) family = name;
    }
    if (family.empty()) {
        note_unknown(argument.name, written);
        return true;
    }
    Simple simple{};
    simple.kind = SimpleKind::family;
    simple.token = argument.name;
    simple.name = family;
    return add_simple(simple);
}

// `:item(<compound>)` (cp31): one compound of the item's stable ID
// (`#id`, at most one) and classes (`.name`), written without spaces
// between them, such as `:item(#n42)`, `:item(.socket-float)` or
// `:item(#n42.warning)`. The part then matches only for an item that has
// them; each counts as an ID or a class does.
bool SelectorPass::read_item(std::uint32_t function_token) {
    std::uint32_t index = function_token + 1U;
    while (index < end_ && (tokens_[index].kind == TokenKind::whitespace || tokens_[index].kind == TokenKind::comment)) {
        ++index;
    }
    std::size_t read = 0U;
    while (index < end_) {
        const Token& token = tokens_[index];
        if (token.kind == TokenKind::hash) {
            if ((token.flags & token_flag_hash_identifier) == 0U) {
                report(DiagnosticCode::selector_syntax, index, "an item's stable ID must be a name");
                return false;
            }
            Simple simple{};
            simple.kind = SimpleKind::item_id;
            simple.token = index;
            simple.name = token_text(index).substr(1U);
            if (!add_simple(simple)) return false;
            read = read + 1U;
            ++index;
            continue;
        }
        const bool dot = token.kind == TokenKind::delim && token_text(index)[0] == '.';
        if (!dot) break;
        const std::uint32_t name_token = index + 1U;
        if (name_token >= end_ || tokens_[name_token].kind != TokenKind::ident) {
            report(DiagnosticCode::selector_syntax, index, "'.' without a class name");
            return false;
        }
        const std::string_view name = token_text(name_token);
        if (!valid_style_class_name(name)) {
            report(DiagnosticCode::selector_syntax, name_token, "not a style class name");
            return false;
        }
        Simple simple{};
        simple.kind = SimpleKind::item_class;
        simple.token = name_token;
        simple.name = name;
        if (!add_simple(simple)) return false;
        read = read + 1U;
        index = name_token + 1U;
    }
    while (index < end_ && (tokens_[index].kind == TokenKind::whitespace || tokens_[index].kind == TokenKind::comment)) {
        ++index;
    }
    if (read == 0U || index >= end_ || tokens_[index].kind != TokenKind::right_paren) {
        const std::uint32_t offending = index < end_ ? index : function_token;
        report(DiagnosticCode::selector_syntax, offending, "':item()' takes an item's #id and .classes");
        return false;
    }
    Frame& frame = workspace_.frames[depth_];
    frame.compound_started = true;
    index_ = index + 1U;
    return true;
}

// Ends the compound being read at token `at` (a ',', the ')' of an
// `:is()`, or the list's end): at the top level its alternatives are
// appended to the output, inside an `:is()` to the arguments' union.
bool SelectorPass::finish_compound(std::uint32_t at) {
    Frame& frame = workspace_.frames[depth_];
    if (!frame.compound_started) {
        // `at` is the list's end at the last token of the sheet.
        const std::size_t last = tokens_.size() - 1U;
        const std::uint32_t located = at < tokens_.size() ? at : static_cast<std::uint32_t>(last);
        report(DiagnosticCode::selector_syntax, located, "an empty selector");
        return false;
    }
    if (depth_ == 0U) {
        // CSS: every compound of a complex selector counts.
        const RuleSpecificity counted = added(frame.compound_specificity, workspace_.ancestors_specificity);
        for (const Alternative& alternative : frame.compound) {
            if (!materialize(alternative, counted)) return false;
        }
        workspace_.ancestors.clear();
        workspace_.ancestors_specificity = RuleSpecificity{};
    } else {
        ++frame.argument_count;
        if (frame.argument_count > maximum_is_arguments) {
            report(DiagnosticCode::too_many_is_arguments, at, std::to_string(maximum_is_arguments));
            return false;
        }
        if (frame.arguments.size() + frame.compound.size() > budget_.rules) {
            report(DiagnosticCode::too_many_rules, at, std::to_string(maximum_rules));
            return false;
        }
        frame.arguments.insert(frame.arguments.end(), frame.compound.begin(), frame.compound.end());
        if (frame.arguments_specificity < frame.compound_specificity) {
            frame.arguments_specificity = frame.compound_specificity;
        }
    }
    reset_compound(frame);
    return true;
}

bool SelectorPass::add_simple(const Simple& simple) {
    const bool added_simple = add_counted(simple, specificity_of(simple.kind));
    return added_simple;
}

bool SelectorPass::add_counted(const Simple& simple, RuleSpecificity counted) {
    const std::uint32_t position = static_cast<std::uint32_t>(workspace_.simples.size());
    workspace_.simples.push_back(simple);
    Frame& frame = workspace_.frames[depth_];
    for (Alternative& alternative : frame.compound) {
        if (alternative.count == maximum_simple_selectors) {
            report(DiagnosticCode::selector_too_long, simple.token, std::to_string(maximum_simple_selectors));
            return false;
        }
        alternative.simples[alternative.count] = position;
        ++alternative.count;
    }
    frame.compound_specificity = added(frame.compound_specificity, counted);
    frame.compound_started = true;
    return true;
}

bool SelectorPass::push_ancestor(std::uint32_t at, AncestorCombinator combinator) {
    Frame& frame = workspace_.frames[0];
    if (!frame.compound_started) {
        report(DiagnosticCode::selector_syntax, at, "a combinator without a compound before it");
        return false;
    }
    if (frame.compound.size() != 1U) {
        report(DiagnosticCode::selector_syntax, at, "an ancestor compound takes no :is() or :where()");
        return false;
    }
    if (workspace_.ancestors.size() == maximum_rule_ancestors) {
        report(DiagnosticCode::selector_too_long, at, std::to_string(maximum_rule_ancestors) + " ancestor compounds");
        return false;
    }
    RuleAncestor ancestor{};
    ancestor.combinator = combinator;
    const Alternative& compound = frame.compound.front();
    for (std::uint32_t position = 0U; position < compound.count; ++position) {
        const Simple& simple = workspace_.simples[compound.simples[position]];
        if (simple.kind == SimpleKind::control_type) {
            ancestor.control_type = registered_name_from_kebab(simple.name);
        } else if (simple.kind == SimpleKind::stable_id) {
            ancestor.stable_id = std::string(simple.name);
        } else if (simple.kind == SimpleKind::style_class) {
            if (std::find(ancestor.classes.begin(), ancestor.classes.end(), simple.name) == ancestor.classes.end()) {
                ancestor.classes.push_back(std::string(simple.name));
            }
        } else {
            // A state, family or part of an ancestor would re-resolve a
            // subtree on every hover; a surface is no control's ancestor.
            report(DiagnosticCode::selector_syntax, simple.token,
                   "an ancestor compound takes only a type, a stable ID and classes");
            return false;
        }
    }
    workspace_.ancestors.push_back(std::move(ancestor));
    workspace_.ancestors_specificity = added(workspace_.ancestors_specificity, frame.compound_specificity);
    reset_compound(frame);
    return true;
}

bool SelectorPass::add_exclusion(const Alternative& argument, RuleSelector& selector, bool resolve,
                                 bool& declared) {
    RuleExclusion exclusion{};
    for (std::uint32_t position = 0U; position < argument.count; ++position) {
        const Simple& simple = workspace_.simples[argument.simples[position]];
        const bool local = simple.kind == SimpleKind::control_type || simple.kind == SimpleKind::stable_id ||
                           simple.kind == SimpleKind::style_class || simple.kind == SimpleKind::state ||
                           simple.kind == SimpleKind::custom_state || simple.kind == SimpleKind::family;
        if (!local) {
            report(DiagnosticCode::selector_syntax, simple.token, "':not()' takes no part, role or surface");
            return false;
        }
        if (simple.kind == SimpleKind::control_type) {
            exclusion.control_type = registered_name_from_kebab(simple.name);
        } else if (simple.kind == SimpleKind::stable_id) {
            exclusion.stable_id = std::string(simple.name);
        } else if (simple.kind == SimpleKind::family) {
            exclusion.family = std::string(simple.name);
        } else if (simple.kind == SimpleKind::style_class) {
            if (std::find(exclusion.classes.begin(), exclusion.classes.end(), simple.name) == exclusion.classes.end()) {
                exclusion.classes.push_back(std::string(simple.name));
            }
        } else if (simple.kind == SimpleKind::state) {
            if (std::find(exclusion.states.begin(), exclusion.states.end(), simple.state) == exclusion.states.end()) {
                exclusion.states.push_back(simple.state);
            }
        }
    }
    // The custom states once the argument's type is known: its own, else
    // the selector's, whose part then chooses the item system.
    const bool own_type = !exclusion.control_type.empty() && exclusion.control_type != selector.control_type;
    const std::string_view scope_type = own_type ? std::string_view(exclusion.control_type)
                                                 : std::string_view(selector.control_type);
    const std::string_view scope_part = own_type ? std::string_view{} : std::string_view(selector.part);
    for (std::uint32_t position = 0U; position < argument.count; ++position) {
        const Simple& simple = workspace_.simples[argument.simples[position]];
        if (simple.kind != SimpleKind::custom_state || !resolve) continue;
        const std::optional<CustomStateIndex> found = resolve_custom_state(simple, scope_type, scope_part);
        if (!found) {
            declared = false;
            return true;
        }
        const std::vector<CustomStateIndex>& held = exclusion.custom_states;
        if (std::find(held.begin(), held.end(), *found) == held.end()) exclusion.custom_states.push_back(*found);
    }
    if (std::find(selector.excluded.begin(), selector.excluded.end(), exclusion) == selector.excluded.end()) {
        selector.excluded.push_back(std::move(exclusion));
    }
    return true;
}

std::optional<CustomStateIndex> SelectorPass::resolve_custom_state(const Simple& simple,
                                                                   std::string_view control_type,
                                                                   std::string_view part) {
    const std::string registered = registered_name_from_kebab(simple.name);
    // The type was found in the vocabulary when it was read.
    const ControlAnatomy* anatomy = control_type.empty() ? nullptr : find_control(vocabulary_, control_type);
    if (anatomy != nullptr) {
        const std::span<const CustomStateDeclaration> declared = custom_state_scope(*anatomy, part);
        const std::optional<CustomStateIndex> found = find_declared_custom_state(declared, registered);
        if (found) return found;
    }
    if (reported_state_token_ == simple.token) return std::nullopt;
    reported_state_token_ = simple.token;
    // The element checked first: a detail is clipped to
    // maximum_detail_bytes, and a long state name must not hide it.
    std::string detail{};
    if (anatomy == nullptr) {
        detail = "no control type to declare ";
    } else {
        detail = kebab_case_of(control_type);
        const bool item_part = !part.empty() && !find_declared_part((*anatomy).parts, part).has_value();
        if (item_part) {
            detail += "::part(";
            detail += kebab_case_of(part);
            detail += ") items";
        }
        detail += item_part ? " declare no custom state " : " declares no custom state ";
    }
    detail += simple.name;
    report(DiagnosticCode::unknown_selector_name, simple.token, clipped_theme_detail(detail));
    return std::nullopt;
}

// One expanded selector into the output. Two different control types, IDs,
// parts or roles in one expansion are an error; a part its control type
// does not declare is an unknown name.
bool SelectorPass::materialize(const Alternative& alternative, RuleSpecificity specificity) {
    if (emitted_ == budget_.rules) {
        report(DiagnosticCode::too_many_rules, input_.selectors.first.value, std::to_string(maximum_rules));
        return false;
    }
    LoweredSelector lowered{};
    RuleSelector& selector = lowered.selector;
    std::uint32_t part_token = 0U;
    std::uint32_t surface_token = 0U;
    bool names_custom_state = false;
    // Custom states and `:not()`s wait for the second walk below, once the
    // expansion's control type and part are known.
    for (std::uint32_t position = 0U; position < alternative.count; ++position) {
        const Simple& simple = workspace_.simples[alternative.simples[position]];
        if (simple.kind == SimpleKind::custom_state) {
            names_custom_state = true;
        } else if (simple.kind == SimpleKind::negation) {
            continue;
        } else if (simple.kind == SimpleKind::control_type || simple.kind == SimpleKind::part) {
            const std::string registered = registered_name_from_kebab(simple.name);
            std::string& field = simple.kind == SimpleKind::part ? selector.part : selector.control_type;
            if (!field.empty() && field != registered) {
                report(DiagnosticCode::selector_syntax, simple.token,
                       simple.kind == SimpleKind::part ? "two parts" : "two control types");
                return false;
            }
            field = registered;
            if (simple.kind == SimpleKind::part) part_token = simple.token;
        } else if (simple.kind == SimpleKind::surface) {
            if (selector.surface != SurfaceKind::none && selector.surface != simple.surface) {
                report(DiagnosticCode::selector_syntax, simple.token, "two surfaces");
                return false;
            }
            selector.surface = simple.surface;
            surface_token = simple.token;
        } else if (simple.kind == SimpleKind::stable_id) {
            if (!selector.stable_id.empty() && selector.stable_id != simple.name) {
                report(DiagnosticCode::selector_syntax, simple.token, "two stable IDs");
                return false;
            }
            selector.stable_id = std::string(simple.name);
        } else if (simple.kind == SimpleKind::role) {
            if (selector.role && *selector.role != simple.role) {
                report(DiagnosticCode::selector_syntax, simple.token, "two part roles");
                return false;
            }
            selector.role = simple.role;
        } else if (simple.kind == SimpleKind::style_class) {
            bool present = false;
            for (const std::string& name : selector.classes) {
                if (name == simple.name) present = true;
            }
            if (!present) selector.classes.push_back(std::string(simple.name));
        } else if (simple.kind == SimpleKind::family) {
            if (!selector.family.empty() && selector.family != simple.name) {
                report(DiagnosticCode::selector_syntax, simple.token, "two families");
                return false;
            }
            selector.family = std::string(simple.name);
        } else if (simple.kind == SimpleKind::item_id) {
            if (!selector.item_id.empty() && selector.item_id != simple.name) {
                report(DiagnosticCode::selector_syntax, simple.token, "two item IDs");
                return false;
            }
            selector.item_id = std::string(simple.name);
        } else if (simple.kind == SimpleKind::item_class) {
            bool present = false;
            for (const std::string& name : selector.item_classes) {
                if (name == simple.name) present = true;
            }
            if (!present) selector.item_classes.push_back(std::string(simple.name));
        } else {
            bool present = false;
            for (const AnatomyState state : selector.states) {
                if (state == simple.state) present = true;
            }
            if (!present) selector.states.push_back(simple.state);
        }
    }
    // The ancestor compounds, nearest first.
    selector.ancestors.assign(workspace_.ancestors.rbegin(), workspace_.ancestors.rend());
    if (selector.surface != SurfaceKind::none && !selector.ancestors.empty()) {
        report(DiagnosticCode::selector_syntax, surface_token, "a window or popup has no ancestors");
        return false;
    }
    // A surface selector names the surface and its root's classes and ID
    // only: its rules style no part of a control.
    const bool surface_with_more = selector.surface != SurfaceKind::none &&
                                   (!selector.control_type.empty() || !selector.part.empty() || selector.role ||
                                    !selector.states.empty() || names_custom_state ||
                                    !selector.item_id.empty() || !selector.item_classes.empty());
    if (surface_with_more) {
        report(DiagnosticCode::selector_syntax, surface_token,
               "a window or popup selector takes only classes and a stable ID");
        return false;
    }
    // A part is checked against the selector's type: as CSS's `::part()`
    // takes any name and matches nothing for a part the element lacks, an
    // unknown part leaves out its own selector, with W106, and the rest of
    // the list applies. Any other unknown name drops the whole rule.
    bool part_declared = true;
    if (!selector.part.empty() && !dropped_) {
        bool declared = false;
        if (!selector.control_type.empty()) {
            // The type was found when it was read.
            const ControlAnatomy* anatomy = find_control(vocabulary_, selector.control_type);
            declared = anatomy != nullptr && declares_part(*anatomy, selector.part);
        } else {
            for (const ControlAnatomy& anatomy : vocabulary_.controls) {
                if (declares_part(anatomy, selector.part)) declared = true;
            }
        }
        if (!declared) {
            note_unknown_part(part_token);
            part_declared = false;
        }
    }
    // The custom states and `:not()`s (R2-13). A custom state its type does
    // not declare leaves out its own selector, as an unknown part does:
    // CSS's `:state()` matches nothing on an element without the state.
    // They are resolved only for a selector that may still be appended.
    const bool resolve = !dropped_ && part_declared;
    bool states_declared = true;
    for (std::uint32_t position = 0U; position < alternative.count; ++position) {
        const Simple& simple = workspace_.simples[alternative.simples[position]];
        if (simple.kind == SimpleKind::negation) {
            for (std::uint32_t argument = 0U; argument < simple.negated_count; ++argument) {
                const Alternative& negated = workspace_.negated[simple.negated_first + argument];
                if (!add_exclusion(negated, selector, resolve && states_declared, states_declared)) return false;
            }
            continue;
        }
        if (simple.kind != SimpleKind::custom_state || !resolve || !states_declared) continue;
        const std::optional<CustomStateIndex> found =
            resolve_custom_state(simple, selector.control_type, selector.part);
        if (!found) {
            states_declared = false;
            continue;
        }
        const std::vector<CustomStateIndex>& held = selector.custom_states;
        if (std::find(held.begin(), held.end(), *found) == held.end()) selector.custom_states.push_back(*found);
    }
    // A dropped rule's selectors count too: each was built.
    const std::size_t bytes = lowered_selector_bytes(selector);
    if (bytes > budget_.bytes - emitted_bytes_) {
        report(DiagnosticCode::lowered_sheet_too_large, input_.selectors.first.value,
               std::to_string(maximum_lowered_bytes));
        return false;
    }
    emitted_bytes_ += bytes;
    lowered.specificity = specificity;
    // Every theme-sheet rule at one level, ordered there by its CSS
    // specificity alone (selector step 1).
    lowered.level = CascadeLevel::theme_sheet;
    if (!dropped_ && part_declared && states_declared) {
        output_.push_back(std::move(lowered));
        ++appended_;
    }
    ++emitted_;
    return true;
}

NameArgument SelectorPass::name_argument(std::uint32_t function_token) const noexcept {
    NameArgument argument{};
    std::uint32_t index = function_token + 1U;
    while (index < end_ && (tokens_[index].kind == TokenKind::whitespace || tokens_[index].kind == TokenKind::comment)) {
        ++index;
    }
    if (index >= end_ || tokens_[index].kind != TokenKind::ident) {
        argument.offending = index < end_ ? index : function_token;
        return argument;
    }
    const std::uint32_t name = index;
    ++index;
    while (index < end_ && (tokens_[index].kind == TokenKind::whitespace || tokens_[index].kind == TokenKind::comment)) {
        ++index;
    }
    if (index >= end_ || tokens_[index].kind != TokenKind::right_paren) {
        argument.offending = index < end_ ? index : function_token;
        return argument;
    }
    argument.found = true;
    argument.name = name;
    argument.after = index + 1U;
    return argument;
}

// The token after the ')' that closes the function at `function_token`;
// the parser has balanced every group.
std::uint32_t SelectorPass::after_matching_paren(std::uint32_t function_token) const noexcept {
    std::size_t open = 1U;
    std::uint32_t index = function_token + 1U;
    while (index < end_ && open != 0U) {
        const TokenKind kind = tokens_[index].kind;
        const bool opens = kind == TokenKind::function || kind == TokenKind::left_paren || kind == TokenKind::left_square;
        const bool closes = kind == TokenKind::right_paren || kind == TokenKind::right_square;
        if (opens) ++open;
        if (closes) --open;
        ++index;
    }
    return index;
}

std::string_view SelectorPass::token_text(std::uint32_t index) const noexcept {
    const Token& token = tokens_[index];
    if ((token.flags & token_flag_escaped) != 0U) {
        // The name it spells (run() decoded it).
        for (const DecodedName& name : workspace_.decoded_names) {
            if (name.token != index) continue;
            const std::string_view decoded = std::string_view(workspace_.decoded).substr(name.offset, name.length);
            return decoded;
        }
    }
    const std::string_view text = input_.source.text.substr(token.offset.value, token.length.value);
    return text;
}

std::string_view SelectorPass::function_name(std::uint32_t index) const noexcept {
    const std::string_view text = token_text(index);
    const std::string_view name = text.substr(0U, text.size() - 1U);
    return name;
}

void SelectorPass::note_unknown(std::uint32_t token, std::string_view name) {
    if (dropped_) return;
    dropped_ = true;
    report(DiagnosticCode::unknown_selector_name, token, clipped_theme_detail(name));
}

void SelectorPass::note_unknown_part(std::uint32_t token) {
    if (reported_part_token_ == token) return;
    reported_part_token_ = token;
    report(DiagnosticCode::unknown_selector_name, token, clipped_theme_detail(token_text(token)));
}

void SelectorPass::report(DiagnosticCode code, std::uint32_t token, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(input_.source.file);
    diagnostic.position = input_.cursor.locate(tokens_[token].offset);
    diagnostic.rule = clipped_theme_detail(input_.rule_label);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

} // namespace

std::size_t lowered_selector_bytes(const RuleSelector& selector) noexcept {
    std::size_t bytes = lowered_record_bytes + selector.control_type.size() + selector.stable_id.size() +
                        selector.part.size() + selector.states.size() + selector.custom_states.size() +
                        selector.family.size() + selector.item_id.size();
    for (const std::string& name : selector.classes) bytes += name.size();
    for (const std::string& name : selector.item_classes) bytes += name.size();
    for (const RuleExclusion& exclusion : selector.excluded) {
        bytes += lowered_record_bytes + exclusion.control_type.size() + exclusion.stable_id.size() +
                 exclusion.states.size() + exclusion.custom_states.size() + exclusion.family.size();
        for (const std::string& name : exclusion.classes) bytes += name.size();
    }
    for (const RuleAncestor& ancestor : selector.ancestors) {
        bytes += lowered_record_bytes + ancestor.control_type.size() + ancestor.stable_id.size();
        for (const std::string& name : ancestor.classes) bytes += name.size();
    }
    return bytes;
}

SelectorOutcome SelectorLowering::lower(const SelectorInput& input, const SelectorVocabulary& vocabulary,
                                        LoweringBudget& budget, std::vector<LoweredSelector>& output,
                                        ThemeDiagnosticList& diagnostics) {
    const std::size_t kept = output.size();
    SelectorPass pass(input, vocabulary, budget, output, diagnostics, workspace_);
    const SelectorOutcome outcome = pass.run();
    if (outcome != SelectorOutcome::lowered) {
        output.resize(kept);
        return outcome;
    }
    budget.rules -= pass.emitted();
    budget.bytes -= pass.emitted_bytes();
    return outcome;
}

} // namespace gui_forms::theme_language
