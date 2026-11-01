#pragma once

#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The derived custom-property limits (PLAN.md section 6; R2-14 starting
// values): distinct custom property names in one sheet, and the tokens a
// value holds once every var() in it is substituted.
inline constexpr std::size_t maximum_custom_properties = 1024U;
inline constexpr std::size_t maximum_substituted_tokens = 4096U;
// The most combinations of the definitions of the varying custom
// properties one rule reads (P12, PLAN.md section 1.11; E508), each a copy
// of the rule. Set from the token-heavy test theme
// (theme_custom_property_css_tests), whose most copied rule takes 24:
// more than four times that.
inline constexpr std::size_t maximum_variable_combinations = 128U;
// The most combinations of their candidate definitions one rule's copies
// are sought among (P12; E508 too): 32 for each copy the limit admits.
inline constexpr std::size_t maximum_variable_steps = 4096U;

// One definition, `--name: <tokens>`: the name as written (with its `--`),
// the value's tokens, the declaration's node, and the name's position among
// the sheet's distinct names in name order (from 0 to name_count() - 1);
// the style rule that declares it, the `@media` block holding that rule
// (node 0, the sheet, outside one), and whether it is `!important`.
struct CustomPropertyDefinition final {
    std::string_view name{};
    TokenRange value{};
    NodeIndex declaration{};
    std::uint32_t name_index{};
    NodeIndex rule{};
    NodeIndex media{};
    bool important{};

    friend bool operator==(const CustomPropertyDefinition& left, const CustomPropertyDefinition& right) noexcept {
        return left.name == right.name && left.value == right.value && left.declaration == right.declaration &&
               left.name_index == right.name_index && left.rule == right.rule && left.media == right.media &&
               left.important == right.important;
    }
};

// The names a theme reads but may not define (R2-06): the engine inputs,
// written `--pointer-x` and so on, and the values the application publishes
// that the theme's manifest lists, written `--<name>`. Borrowed.
struct ReservedInputs final {
    std::span<const std::string> published_values{};
};

// Where a reserved name reads from: an engine input, a published value, or
// neither (the name is not reserved).
enum class ReservedInputKind : std::uint8_t { none, engine_input, published_value };
struct ReservedInput final {
    ReservedInputKind kind{ReservedInputKind::none};
    // The EngineInput, or the published value's position in the list.
    std::uint32_t index{};
};

// The reserved input `name` (written with its `--`) names, if any.
[[nodiscard]] ReservedInput find_reserved_input(std::string_view name, const ReservedInputs& reserved) noexcept;

// A sheet's custom property definitions (PLAN.md section 1.6, R2-05),
// collected once per compilation and looked up by name. It keeps its
// storage from sheet to sheet.
class CustomPropertyTable final {
public:
    // Collects every custom property declared in a style rule of `tree`
    // (inside @media too; @keyframes blocks define none). Defining a
    // reserved input is an error (E701), and so is a sheet of more than
    // maximum_custom_properties distinct names (E511). Returns false after
    // an error, the table then empty. `tokens`, `tree` and `source` are
    // borrowed until the table is cleared or collects again.
    [[nodiscard]] bool collect(const ThemeSource& source, const ThemeTokenBuffer& tokens, const ThemeSyntaxTree& tree,
                               const ReservedInputs& reserved, SourcePositionCursor& cursor,
                               ThemeDiagnosticList& diagnostics);
    void clear() noexcept;

    // The definitions of `name` (written with its `--`), in document order.
    [[nodiscard]] std::span<const CustomPropertyDefinition> definitions_of(std::string_view name) const& noexcept;
    std::span<const CustomPropertyDefinition> definitions_of(std::string_view name) const&& = delete;
    // The definitions of the name at `name_index`, in document order.
    [[nodiscard]] std::span<const CustomPropertyDefinition> definitions_named(std::uint32_t name_index) const& noexcept;
    std::span<const CustomPropertyDefinition> definitions_named(std::uint32_t name_index) const&& = delete;
    // Every definition, by name_index, then in document order.
    [[nodiscard]] std::span<const CustomPropertyDefinition> definitions() const& noexcept;
    std::span<const CustomPropertyDefinition> definitions() const&& = delete;
    [[nodiscard]] std::size_t name_count() const noexcept;

private:
    // Sorted by name, then document order.
    std::vector<CustomPropertyDefinition> definitions_{};
    std::size_t name_count_{};
};

// One token of a value once its var()s are substituted: a token of the
// sheet, or a reserved input that a var() read (`token` is then the var()'s
// function token, for diagnostics).
enum class ValueTokenSource : std::uint8_t { sheet, engine_input, published_value };
struct ValueToken final {
    ValueTokenSource source{ValueTokenSource::sheet};
    std::uint32_t token{};
    // The EngineInput or published value position, for a reserved input.
    std::uint32_t input{};

    friend bool operator==(ValueToken left, ValueToken right) noexcept {
        return left.source == right.source && left.token == right.token && left.input == right.input;
    }
};

// Appends to `names` the name_index of each name `range` reads with
// var() that `table` defines, fallbacks included, in token order.
void append_variable_names(const ThemeSource& source, const std::vector<Token>& tokens, TokenRange range,
                           const CustomPropertyTable& table, std::vector<std::uint32_t>& names);

// A substitution's choice of a name's definition (P12, PLAN.md section
// 1.11): its position among the name's definitions, or one of these.
// - variable_unassigned: the name varies (it is defined in more than one
//   place) and the value is read where it cannot (E416);
// - variable_undefined: no definition holds, so var() reads its fallback,
//   or, without one, the declaration is not declared.
inline constexpr std::uint32_t variable_unassigned = 0xFFFF'FFFEU;
inline constexpr std::uint32_t variable_undefined = 0xFFFF'FFFFU;

// What a substitution reads, all borrowed for the call. `choices` holds a
// choice for each of the table's names, by name_index; when it is empty,
// a name defined once reads its definition and any other is unassigned.
struct SubstitutionInput final {
    const ThemeSource& source;
    const ThemeTokenBuffer& tokens;
    const CustomPropertyTable& table;
    const ReservedInputs& reserved;
    SourcePositionCursor& cursor;
    std::span<const std::uint32_t> choices{};
    // An inline style compiled alone (P12b): the custom property names the
    // theme in effect defines, sorted. A var() of one the style does not
    // define is left undeclared (`undefined`, not E409, its fallback
    // unread), and any var() of one is noted (theme_read): the style is
    // then lowered with the theme's text.
    std::span<const std::string> theme_names{};
};

// `undefined`: a var() read a name whose choice is variable_undefined,
// without a fallback; nothing is reported.
enum class SubstitutionOutcome : std::uint8_t { substituted, refused, undefined };

// Substitutes the var()s of declaration values (PLAN.md section 1.6). It
// keeps its working storage from value to value.
class VariableSubstitution final {
public:
    // Fills `output` (replacing its contents) with the tokens of `value`,
    // each `var(--name)` or `var(--name, fallback)` replaced by the name's
    // value (substituted in turn), by a reserved input, or by the fallback
    // when no rule defines the name. A name's definition is the one its
    // choice names. Whitespace and comments are left out: tokens already
    // separate the components. Errors: a name undefined without a fallback
    // (E409), an unassigned choice (E416), a cycle (E410), a malformed
    // var() (E401), and a substitution that passes over
    // more than maximum_substituted_tokens tokens (E512). Every token it
    // passes over counts once: whitespace and comments, each var()'s own
    // tokens and a fallback it skips. So its work stays within a constant
    // of the bound however the definitions nest. On `refused` and
    // `undefined`, `output` is empty.
    [[nodiscard]] SubstitutionOutcome substitute(const SubstitutionInput& input, TokenRange value,
                                                 std::vector<ValueToken>& output, ThemeDiagnosticList& diagnostics);

    // One text being read: the rest of its tokens, and, when it is a
    // definition's value (`defines`), the position of the name it defines,
    // active until the text ends.
    struct Frame final {
        std::uint32_t cursor{};
        std::uint32_t end{};
        bool defines{};
        std::uint32_t name_index{};
    };

    // Whether a var() substituted since clear_theme_read() named one of
    // SubstitutionInput::theme_names (P12b).
    [[nodiscard]] bool theme_read() const noexcept { return theme_read_; }
    void clear_theme_read() noexcept { theme_read_ = false; }

private:
    // Counts `added` more tokens visited; false, after reporting E512 at
    // `token`, past the bound.
    [[nodiscard]] bool visit(const SubstitutionInput& input, std::size_t added, std::uint32_t token,
                             ThemeDiagnosticList& diagnostics);

    std::vector<Frame> frames_{};
    // One flag per distinct name: set while its definition is being read,
    // so a cycle is found without searching the open frames.
    std::vector<std::uint8_t> active_{};
    std::size_t visited_{};
    bool theme_read_{};
};

} // namespace gui_forms::theme_language
