#include "theme_custom_properties.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

// The written spelling of each engine input, in EngineInput order.
constexpr std::array<std::string_view, engine_input_count> engine_input_names{{
    "--pointer-x",
    "--pointer-y",
    "--time-in-state",
    "--display-scale",
    "--text-scale",
}};

// Orders definitions by name; std::stable_sort keeps document order among
// equal names.
struct DefinitionNameLess final {
    bool operator()(const CustomPropertyDefinition& left, const CustomPropertyDefinition& right) const noexcept {
        return left.name < right.name;
    }
};

// Orders a definition against a name, for std::lower_bound and
// std::upper_bound.
struct DefinitionBeforeName final {
    bool operator()(const CustomPropertyDefinition& definition, std::string_view name) const noexcept {
        return definition.name < name;
    }
    bool operator()(std::string_view name, const CustomPropertyDefinition& definition) const noexcept {
        return name < definition.name;
    }
};

// Orders a definition against a name's position, likewise.
struct DefinitionBeforeIndex final {
    bool operator()(const CustomPropertyDefinition& definition, std::uint32_t name_index) const noexcept {
        return definition.name_index < name_index;
    }
    bool operator()(std::uint32_t name_index, const CustomPropertyDefinition& definition) const noexcept {
        return name_index < definition.name_index;
    }
};

[[nodiscard]] bool is_custom_property_name(std::string_view name) noexcept {
    return name.size() > 2U && name[0] == '-' && name[1] == '-';
}

[[nodiscard]] bool is_trivia(TokenKind kind) noexcept {
    return kind == TokenKind::whitespace || kind == TokenKind::comment || kind == TokenKind::byte_order_mark;
}

[[nodiscard]] std::string_view token_text(const ThemeSource& source, const Token& token) noexcept {
    const std::string_view text = source.text.substr(token.offset.value, token.length.value);
    return text;
}

void report(const ThemeSource& source, const Token& token, SourcePositionCursor& cursor, DiagnosticCode code,
            std::string detail, ThemeDiagnosticList& diagnostics) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source.file);
    diagnostic.position = cursor.locate(token.offset);
    diagnostic.detail = std::move(detail);
    diagnostics.add(std::move(diagnostic));
}

// A var() read at its function token: its name token, its fallback's
// tokens (empty when it has none) and the token after its ')'. `valid` is
// false when it is not `var(<custom property> [, <tokens>])`.
struct VariableReference final {
    bool valid{};
    std::uint32_t name{};
    bool has_fallback{};
    TokenRange fallback{};
    std::uint32_t after{};
};

[[nodiscard]] VariableReference read_variable(const ThemeSource& source, const std::vector<Token>& tokens,
                                              std::uint32_t function, std::uint32_t end) noexcept {
    VariableReference reference{};
    std::uint32_t index = function + 1U;
    while (index < end && is_trivia(tokens[index].kind)) ++index;
    if (index >= end || tokens[index].kind != TokenKind::ident) return reference;
    if (!is_custom_property_name(token_text(source, tokens[index]))) return reference;
    reference.name = index;
    ++index;
    while (index < end && is_trivia(tokens[index].kind)) ++index;
    if (index >= end) return reference;
    const TokenKind after_name = tokens[index].kind;
    if (after_name == TokenKind::right_paren) {
        reference.valid = true;
        reference.after = index + 1U;
        return reference;
    }
    if (after_name != TokenKind::comma) return reference;
    // The fallback runs to the ')' that closes the var(); the parser
    // balanced every group.
    const std::uint32_t fallback_first = index + 1U;
    std::size_t open = 1U;
    ++index;
    while (index < end) {
        const TokenKind kind = tokens[index].kind;
        const bool opens = kind == TokenKind::function || kind == TokenKind::left_paren || kind == TokenKind::left_square;
        const bool closes = kind == TokenKind::right_paren || kind == TokenKind::right_square;
        if (opens) ++open;
        if (closes) --open;
        if (open == 0U) break;
        ++index;
    }
    if (index >= end) return reference;
    reference.valid = true;
    reference.has_fallback = true;
    reference.fallback.first = TokenIndex{fallback_first};
    reference.fallback.end = TokenIndex{index};
    reference.after = index + 1U;
    return reference;
}

} // namespace

ReservedInput find_reserved_input(std::string_view name, const ReservedInputs& reserved) noexcept {
    ReservedInput found{};
    for (std::size_t input = 0U; input < engine_input_names.size(); ++input) {
        if (name != engine_input_names[input]) continue;
        found.kind = ReservedInputKind::engine_input;
        found.index = static_cast<std::uint32_t>(input);
        return found;
    }
    if (!is_custom_property_name(name)) return found;
    const std::string_view published = name.substr(2U);
    for (std::size_t value = 0U; value < reserved.published_values.size(); ++value) {
        if (published != reserved.published_values[value]) continue;
        found.kind = ReservedInputKind::published_value;
        found.index = static_cast<std::uint32_t>(value);
        return found;
    }
    return found;
}

bool CustomPropertyTable::collect(const ThemeSource& source, const ThemeTokenBuffer& tokens,
                                  const ThemeSyntaxTree& tree, const ReservedInputs& reserved,
                                  SourcePositionCursor& cursor, ThemeDiagnosticList& diagnostics) {
    clear();
    const std::vector<SyntaxNode>& nodes = tree.nodes;
    // A declaration's parent is the nearest style rule or keyframe block
    // before it whose subtree holds it; only style rules' declarations
    // define custom properties.
    std::uint32_t rule_end = 0U;
    std::uint32_t rule = 0U;
    bool in_style_rule = false;
    std::uint32_t media_end = 0U;
    std::uint32_t media = 0U;
    for (std::uint32_t index = 0U; index < nodes.size(); ++index) {
        const SyntaxNode& node = nodes[index];
        if (node.kind == SyntaxNodeKind::media_rule) {
            media = index;
            media_end = node.subtree_end.value;
            continue;
        }
        if (node.kind == SyntaxNodeKind::style_rule || node.kind == SyntaxNodeKind::keyframe_block) {
            in_style_rule = node.kind == SyntaxNodeKind::style_rule;
            rule = index;
            rule_end = node.subtree_end.value;
            continue;
        }
        if (node.kind != SyntaxNodeKind::declaration || index >= rule_end || !in_style_rule) continue;
        const Token& property = tokens.tokens[node.tokens.first.value];
        const std::string_view name = token_text(source, property);
        if (!is_custom_property_name(name)) continue;
        if (find_reserved_input(name, reserved).kind != ReservedInputKind::none) {
            report(source, property, cursor, DiagnosticCode::reserved_input_set, clipped_theme_detail(name),
                   diagnostics);
            clear();
            return false;
        }
        CustomPropertyDefinition definition{};
        definition.name = name;
        definition.value = node.content;
        definition.declaration = NodeIndex{index};
        definition.rule = NodeIndex{rule};
        definition.media = NodeIndex{index < media_end ? media : 0U};
        definition.important = (node.flags & syntax_flag_important) != 0U;
        definitions_.push_back(definition);
    }
    std::stable_sort(definitions_.begin(), definitions_.end(), DefinitionNameLess{});
    std::size_t names = 0U;
    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const bool first_of_name = index == 0U || definitions_[index].name != definitions_[index - 1U].name;
        if (first_of_name) ++names;
        // At most maximum_custom_properties + 1, checked below.
        definitions_[index].name_index = static_cast<std::uint32_t>(names - 1U);
        if (!first_of_name) continue;
        if (names > maximum_custom_properties) {
            const SyntaxNode& node = nodes[definitions_[index].declaration.value];
            report(source, tokens.tokens[node.tokens.first.value], cursor, DiagnosticCode::too_many_custom_properties,
                   std::to_string(maximum_custom_properties), diagnostics);
            clear();
            return false;
        }
    }
    name_count_ = names;
    return true;
}

void CustomPropertyTable::clear() noexcept {
    definitions_.clear();
    name_count_ = 0U;
}

std::span<const CustomPropertyDefinition> CustomPropertyTable::definitions_of(std::string_view name) const& noexcept {
    const std::vector<CustomPropertyDefinition>::const_iterator first =
        std::lower_bound(definitions_.begin(), definitions_.end(), name, DefinitionBeforeName{});
    const std::vector<CustomPropertyDefinition>::const_iterator last =
        std::upper_bound(first, definitions_.end(), name, DefinitionBeforeName{});
    const std::span<const CustomPropertyDefinition> found(first, last);
    return found;
}

std::span<const CustomPropertyDefinition> CustomPropertyTable::definitions_named(std::uint32_t name_index) const& noexcept {
    const std::vector<CustomPropertyDefinition>::const_iterator first =
        std::lower_bound(definitions_.begin(), definitions_.end(), name_index, DefinitionBeforeIndex{});
    const std::vector<CustomPropertyDefinition>::const_iterator last =
        std::upper_bound(first, definitions_.end(), name_index, DefinitionBeforeIndex{});
    const std::span<const CustomPropertyDefinition> found(first, last);
    return found;
}

std::span<const CustomPropertyDefinition> CustomPropertyTable::definitions() const& noexcept {
    const std::span<const CustomPropertyDefinition> all(definitions_);
    return all;
}

void append_variable_names(const ThemeSource& source, const std::vector<Token>& tokens, TokenRange range,
                           const CustomPropertyTable& table, std::vector<std::uint32_t>& names) {
    // A fallback's var()s are inside the range, so one scan finds them.
    for (std::uint32_t index = range.first.value; index < range.end.value; ++index) {
        const Token& token = tokens[index];
        if (token.kind != TokenKind::function || !equals_ascii_folded(token_text(source, token), "var(")) continue;
        const VariableReference reference = read_variable(source, tokens, index, range.end.value);
        if (!reference.valid) continue;
        const std::span<const CustomPropertyDefinition> found =
            table.definitions_of(token_text(source, tokens[reference.name]));
        if (!found.empty()) names.push_back(found[0].name_index);
    }
}

std::size_t CustomPropertyTable::name_count() const noexcept {
    return name_count_;
}

SubstitutionOutcome VariableSubstitution::substitute(const SubstitutionInput& input, TokenRange value,
                                                     std::vector<ValueToken>& output,
                                                     ThemeDiagnosticList& diagnostics) {
    output.clear();
    frames_.clear();
    active_.assign(input.table.name_count(), 0U);
    visited_ = 0U;
    const std::vector<Token>& tokens = input.tokens.tokens;
    Frame top{};
    top.cursor = value.first.value;
    top.end = value.end.value;
    frames_.push_back(top);
    while (!frames_.empty()) {
        Frame& frame = frames_.back();
        if (frame.cursor >= frame.end) {
            if (frame.defines) active_[frame.name_index] = 0U;
            frames_.pop_back();
            continue;
        }
        const std::uint32_t index = frame.cursor;
        const Token& token = tokens[index];
        if (!visit(input, 1U, index, diagnostics)) {
            output.clear();
            return SubstitutionOutcome::refused;
        }
        if (is_trivia(token.kind)) {
            ++frame.cursor;
            continue;
        }
        const bool variable = token.kind == TokenKind::function &&
                              equals_ascii_folded(token_text(input.source, token), "var(");
        if (!variable) {
            ValueToken copied{};
            copied.token = index;
            output.push_back(copied);
            ++frame.cursor;
            continue;
        }
        const VariableReference reference = read_variable(input.source, tokens, index, frame.end);
        if (!reference.valid) {
            report(input.source, token, input.cursor, DiagnosticCode::value_syntax, "var() takes a custom property",
                   diagnostics);
            output.clear();
            return SubstitutionOutcome::refused;
        }
        // The var() is read; what replaces it is pushed above this frame.
        // Its tokens after the function token count as visited, but for a
        // fallback that is read, whose tokens count as they are read: every
        // token the substitution passes over counts once, so its work stays
        // within a constant of the bound.
        frame.cursor = reference.after;
        const std::size_t inside = reference.after - index - 1U;
        const std::size_t fallback_tokens =
            reference.has_fallback ? reference.fallback.end.value - reference.fallback.first.value : 0U;
        const std::string_view name = token_text(input.source, tokens[reference.name]);
        const ReservedInput reserved = find_reserved_input(name, input.reserved);
        const std::span<const CustomPropertyDefinition> definitions = input.table.definitions_of(name);
        std::uint32_t choice = variable_undefined;
        if (!definitions.empty()) {
            const std::uint32_t name_index = definitions[0].name_index;
            if (!input.choices.empty()) {
                choice = input.choices[name_index];
            } else {
                choice = definitions.size() == 1U ? 0U : variable_unassigned;
            }
        }
        // A name the theme in effect defines, read by an inline style
        // compiled alone (P12b): where the style does not define it, the
        // theme's definition holds, so neither the fallback nor E409 does.
        const bool theme_named = !input.theme_names.empty() &&
                                 std::binary_search(input.theme_names.begin(), input.theme_names.end(), name);
        if (theme_named) theme_read_ = true;
        const bool deferred = theme_named && definitions.empty();
        const bool reads_fallback =
            reserved.kind == ReservedInputKind::none && choice == variable_undefined && !deferred;
        const std::size_t passed = reads_fallback ? inside - fallback_tokens : inside;
        if (!visit(input, passed, index, diagnostics)) {
            output.clear();
            return SubstitutionOutcome::refused;
        }
        if (reserved.kind != ReservedInputKind::none) {
            ValueToken read{};
            read.source = reserved.kind == ReservedInputKind::engine_input ? ValueTokenSource::engine_input
                                                                           : ValueTokenSource::published_value;
            read.token = index;
            read.input = reserved.index;
            output.push_back(read);
            continue;
        }
        if (deferred) {
            output.clear();
            return SubstitutionOutcome::undefined;
        }
        if (choice == variable_undefined) {
            if (reference.has_fallback) {
                Frame fallback{};
                fallback.cursor = reference.fallback.first.value;
                fallback.end = reference.fallback.end.value;
                frames_.push_back(fallback);
                continue;
            }
            output.clear();
            // A name some rule defines, though none holds here: the
            // declaration is not declared, as CSS's invalid at computed
            // value time.
            if (!definitions.empty()) return SubstitutionOutcome::undefined;
            report(input.source, tokens[reference.name], input.cursor, DiagnosticCode::variable_undefined,
                   clipped_theme_detail(name), diagnostics);
            return SubstitutionOutcome::refused;
        }
        if (choice == variable_unassigned || choice >= definitions.size()) {
            report(input.source, tokens[reference.name], input.cursor, DiagnosticCode::variable_cannot_vary,
                   clipped_theme_detail(name), diagnostics);
            output.clear();
            return SubstitutionOutcome::refused;
        }
        const CustomPropertyDefinition& definition = definitions[choice];
        const std::uint32_t name_index = definition.name_index;
        if (active_[name_index] != 0U) {
            report(input.source, tokens[reference.name], input.cursor, DiagnosticCode::variable_cycle,
                   clipped_theme_detail(name), diagnostics);
            output.clear();
            return SubstitutionOutcome::refused;
        }
        Frame substituted{};
        substituted.cursor = definition.value.first.value;
        substituted.end = definition.value.end.value;
        substituted.defines = true;
        substituted.name_index = name_index;
        active_[name_index] = 1U;
        frames_.push_back(substituted);
    }
    return SubstitutionOutcome::substituted;
}

bool VariableSubstitution::visit(const SubstitutionInput& input, std::size_t added, std::uint32_t token,
                                 ThemeDiagnosticList& diagnostics) {
    visited_ += added;
    if (visited_ <= maximum_substituted_tokens) return true;
    report(input.source, input.tokens.tokens[token], input.cursor, DiagnosticCode::substitution_too_long,
           std::to_string(maximum_substituted_tokens), diagnostics);
    return false;
}

} // namespace gui_forms::theme_language
