// Selector lowering (PLAN.md section 1.4): a style rule's selector list,
// as tokens, into the engine's RuleSelector records. One pass reads the
// list left to right. Each compound selector being read is a frame on an
// explicit stack, one per open `:is()`; a compound is held as its
// alternatives, one per choice of its `:is()` arguments, so that closing an
// `:is()` multiplies its compound's alternatives by the arguments' union.
// Every alternative count is bounded by the rule budget, and every
// alternative by maximum_simple_selectors, so expanding costs a constant
// per rule the list expands into; the bytes the expanded selectors copy are
// bounded by the byte budget.

#include "theme_selector.hpp"

#include <optional>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

using selector_detail::Alternative;
using selector_detail::DecodedName;
using selector_detail::Frame;
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
constexpr std::array<std::string_view, 27> refused_pseudo_classes{{
    "not",          "where",          "has",          "nth-child",    "nth-last-child", "nth-of-type",
    "nth-last-of-type", "first-child", "last-child",  "only-child",   "first-of-type",  "last-of-type",
    "only-of-type", "empty",          "focus-within", "read-write",   "link",           "visited",
    "any-link",     "target",         "scope",        "host",         "lang",           "dir",
    "placeholder-shown", "required",  "optional",
}};

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
    if (kind == SimpleKind::stable_id) one.ids = 1U;
    if (kind == SimpleKind::style_class || kind == SimpleKind::state) one.classes = 1U;
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
    [[nodiscard]] bool open_is(std::uint32_t function_token);
    [[nodiscard]] bool close_is(std::uint32_t closer);
    [[nodiscard]] bool finish_compound(std::uint32_t at);
    [[nodiscard]] bool add_simple(const Simple& simple);
    [[nodiscard]] bool materialize(const Alternative& alternative, RuleSpecificity specificity);
    [[nodiscard]] NameArgument name_argument(std::uint32_t function_token) const noexcept;
    [[nodiscard]] std::uint32_t after_matching_paren(std::uint32_t function_token) const noexcept;
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;
    // A function token's name, without its '('.
    [[nodiscard]] std::string_view function_name(std::uint32_t index) const noexcept;
    void note_unknown(std::uint32_t token, std::string_view name);
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
    std::size_t emitted_{};
    std::size_t emitted_bytes_{};
    // Whether an unknown name has dropped the rule (its one warning is
    // reported); reading goes on, since an error later still refuses.
    bool dropped_{};
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
            if (!close_is(index_)) return SelectorOutcome::refused;
            ++index_;
            continue;
        }
        if (frame.space_pending) {
            report(DiagnosticCode::combinator_not_admitted, index_, "whitespace");
            return SelectorOutcome::refused;
        }
        if (!read_simple()) return SelectorOutcome::refused;
    }
    // The parser balanced every group, so no `:is()` is open here.
    if (!finish_compound(end_)) return SelectorOutcome::refused;
    if (dropped_) return SelectorOutcome::dropped;
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
        note_unknown(next, name);
        return true;
    }
    if (kind != TokenKind::function) {
        report(DiagnosticCode::selector_syntax, next, std::string(token_kind_name(kind)));
        return false;
    }
    const std::string_view name = function_name(next);
    if (equals_ascii_folded(name, "is")) return open_is(next);
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
        const std::optional<AnatomyState> state =
            registered.empty() ? std::optional<AnatomyState>{} : anatomy_state_from_name(registered);
        if (!state) {
            // A custom control's own state is unknown for now (R2-13).
            note_unknown(argument.name, written);
            return true;
        }
        Simple simple{};
        simple.kind = SimpleKind::state;
        simple.token = argument.name;
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

bool SelectorPass::open_is(std::uint32_t function_token) {
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
    reset_compound(frame);
    index_ = function_token + 1U;
    return true;
}

bool SelectorPass::close_is(std::uint32_t closer) {
    if (!finish_compound(closer)) return false;
    const Frame& arguments = workspace_.frames[depth_];
    --depth_;
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
    // CSS: an `:is()` counts as its most specific argument.
    parent.compound_specificity = added(parent.compound_specificity, arguments.arguments_specificity);
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
        for (const Alternative& alternative : frame.compound) {
            if (!materialize(alternative, frame.compound_specificity)) return false;
        }
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
    frame.compound_specificity = added(frame.compound_specificity, specificity_of(simple.kind));
    frame.compound_started = true;
    return true;
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
    for (std::uint32_t position = 0U; position < alternative.count; ++position) {
        const Simple& simple = workspace_.simples[alternative.simples[position]];
        if (simple.kind == SimpleKind::control_type || simple.kind == SimpleKind::part) {
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
        } else {
            bool present = false;
            for (const AnatomyState state : selector.states) {
                if (state == simple.state) present = true;
            }
            if (!present) selector.states.push_back(simple.state);
        }
    }
    // A surface selector names the surface and its root's classes and ID
    // only: its rules style no part of a control.
    const bool surface_with_more = selector.surface != SurfaceKind::none &&
                                   (!selector.control_type.empty() || !selector.part.empty() || selector.role ||
                                    !selector.states.empty());
    if (surface_with_more) {
        report(DiagnosticCode::selector_syntax, surface_token,
               "a window or popup selector takes only classes and a stable ID");
        return false;
    }
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
        if (!declared) note_unknown(part_token, token_text(part_token));
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
    if (!dropped_) output_.push_back(std::move(lowered));
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
                        selector.part.size() + selector.states.size();
    for (const std::string& name : selector.classes) bytes += name.size();
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
