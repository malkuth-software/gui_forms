#pragma once

#include "../custom_properties/theme_custom_properties.hpp"
#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"

#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// A number's unit once read (PLAN.md section 1.6, A3). Each family has one
// unit: `ms` is read as seconds, `rad` and `turn` as degrees, and `x` as
// `dppx`. `em` is parsed here and lowered in P8.
enum class ValueUnit : std::uint8_t { none, px, percent, em, seconds, degrees, dppx };

[[nodiscard]] std::string_view value_unit_name(ValueUnit unit) noexcept;

enum class ValueComponentKind : std::uint8_t {
    // A number with its unit; a constant expression is folded into one.
    number,
    // A colour: hex, rgb(), rgba(), hsl(), hsla() or color-mix(). A named
    // colour, `transparent` included, stays a keyword (named_colour reads
    // it), since a keyword position may use the same word.
    colour,
    keyword,
    // A quoted string; `text` is its content, without quotes.
    string,
    // `url("resource:<name>")`; `text` is the name.
    resource,
    // An expression that reads an engine input, a published value, a state
    // or a parameter: `program` is its position in ThemeValue::programs.
    expression,
    // Any other function: `text` is its name and its arguments are its
    // children. The phases that lower properties judge it.
    function,
    comma,
    slash,
};

// One component of a value. Components are stored in written order, each
// function followed by its arguments, so a component's children are the
// components from its index + 1 up to `subtree_end` (index + 1 for all but
// a function). `token` is the sheet token that wrote it, for diagnostics.
// `text` borrows the sheet's text.
struct ValueComponent final {
    ValueComponentKind kind{ValueComponentKind::number};
    ValueUnit unit{ValueUnit::none};
    double number{};
    Color colour{};
    std::string_view text{};
    std::uint32_t token{};
    std::uint32_t program{};
    std::uint32_t subtree_end{};
    // A colour's role, where role() gave it (P8, EC4); `source` is none
    // for every other component, a literal colour included.
    ColourOrigin origin{};

    friend bool operator==(const ValueComponent& left, const ValueComponent& right) noexcept {
        return left.kind == right.kind && left.unit == right.unit && left.number == right.number &&
               left.colour == right.colour && left.text == right.text && left.token == right.token &&
               left.program == right.program && left.subtree_end == right.subtree_end &&
               left.origin == right.origin;
    }
};

// A compiled expression (E5): instructions [first, first + count) of
// ThemeValue::instructions, the engine's postfix program after constant
// folding, which the engine's compiler accepted; and its result's unit.
struct ValueProgram final {
    std::uint32_t first{};
    std::uint32_t count{};
    ValueUnit unit{ValueUnit::none};

    friend bool operator==(ValueProgram left, ValueProgram right) noexcept {
        return left.first == right.first && left.count == right.count && left.unit == right.unit;
    }
};

// Caller-owned storage one value is read into, reused from value to value:
// a read replaces the contents and keeps the capacity.
struct ThemeValue final {
    std::vector<ValueComponent> components{};
    std::vector<ValueProgram> programs{};
    std::vector<ExpressionInstruction> instructions{};
    // The colour roles the value's role()s read, one bit a role by its
    // position, mixed colours' included.
    std::uint32_t roles_read{};

    // The instructions of `program`, one of this value's programs.
    [[nodiscard]] std::span<const ExpressionInstruction> program_instructions(ValueProgram program) const& noexcept;
    std::span<const ExpressionInstruction> program_instructions(ValueProgram program) const&& = delete;
};

// The colour a CSS named colour or `transparent` names, in any ASCII case.
[[nodiscard]] std::optional<Color> named_colour(std::string_view name) noexcept;

static_assert(colour_role_count <= 32U, "ThemeValue::roles_read holds one bit a role");

// One role's colours in a palette: a pair's foreground and background, or
// a single role's colour as its foreground.
struct RoleColours final {
    Color foreground{};
    Color background{};

    friend bool operator==(RoleColours left, RoleColours right) noexcept {
        return left.foreground == right.foreground && left.background == right.background;
    }
};

// The colours role() reads (P8, R2-03): each role the theme's `:root`
// palette declares, by the role's position.
struct RolePalette final {
    std::array<std::optional<RoleColours>, colour_role_count> roles{};
};

// What a value may read besides constants, all borrowed for the read:
// - `parameters`: the registered names of all the primitive's parameters,
//   in declaration order, for `param()`, which reads one by its position
//   (empty outside a primitive's parameters; the lowering refuses a
//   `param()` of a colour or a choice);
// - `published_values`: how many values the theme's manifest publishes, so
//   inputs are addressed up to engine_input_count + published_values;
// - `resources`: the resource names the manifest lists, for `url()`.
// `fold_constants` is true except in the tests that compare folded and
// unfolded programs.
// - `palette`: the colours role() reads; null where role() is not
//   admitted (a palette's own declarations);
// - `bare_side`: the colour role(<pair>) without a side gives, its
//   foreground in `color` and its background in `background` and
//   `background-color`; none elsewhere, where a pair needs its side.
struct ValueContext final {
    std::span<const std::string_view> parameters{};
    std::size_t published_values{};
    std::span<const std::string_view> resources{};
    bool fold_constants{true};
    const RolePalette* palette{};
    std::optional<ColourRoleSide> bare_side{};
    // The host's colour hints env() reads (T4 theme variants), or null:
    // every env() then reads its fallback.
    const SystemColourHints* system_colours{};
};

// What one read takes, all borrowed for the call. `written` is the
// declaration's value as written (SyntaxNode::content), where a diagnostic
// about the whole value points; `substituted` is that value once its var()s
// are substituted, naming tokens of `tokens`.
struct ValueInput final {
    const ThemeSource& source;
    const ThemeTokenBuffer& tokens;
    TokenRange written;
    std::span<const ValueToken> substituted;
    const ValueContext& context;
    SourcePositionCursor& cursor;
};

enum class ValueOutcome : std::uint8_t { read, refused };

// Reads values: a declaration's substituted tokens (VariableSubstitution)
// into components. It keeps its working storage from value to value.
class ValueReader final {
public:
    // Fills `value` (replacing its contents) from `input.substituted`.
    // Errors stop the read at the first one:
    // - a value with no component, or a malformed one (E401);
    // - an operand or argument of the wrong kind, such as units that do not
    //   agree or an expression in a colour (E402);
    // - a colour-mix() percentage outside 0% to 100%, or a constant
    //   expression whose evaluation gives its fallback (E403);
    // - a unit outside the language (E404) or a CSS form outside it (E405);
    // - an expression the engine's compiler refuses (E408), or one over its
    //   budget (E507);
    // - a resource the context does not list (E413);
    // - role() of a role the vocabulary lacks or with an unknown side
    //   (E406), of a pair without a side where the property gives none, of
    //   a single role with a side, or where no palette is given (E405), or
    //   of a role the palette does not declare (E415);
    // - groups nested past maximum_nesting_depth (E504).
    // On `refused`, `value` is empty. Each constant operation of an
    // expression is evaluated by the engine's own evaluator as it is read,
    // so a folded program gives the bits the unfolded one would.
    [[nodiscard]] ValueOutcome read(const ValueInput& input, ThemeValue& value, ThemeDiagnosticList& diagnostics);

    // One operand of the expression being compiled: instructions [first,
    // first + count) of the program being built, the unit of its value, and
    // whether it reads nothing but constants.
    struct Operand final {
        std::uint32_t first{};
        std::uint32_t count{};
        ValueUnit unit{ValueUnit::none};
        bool constant{};
    };

private:
    std::vector<ExpressionInstruction> unfolded_{};
    std::vector<Operand> operands_{};
};

} // namespace gui_forms::theme_language
