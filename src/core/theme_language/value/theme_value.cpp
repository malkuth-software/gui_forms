// Value reading (PLAN.md section 1.6): a declaration's substituted tokens
// into components. Two readers share the token stream:
// - the component reader walks the tokens once, holding open functions on
//   an explicit stack; it turns rgb(), rgba(), hsl(), hsla() and
//   color-mix() into colours as each closes, so nested colour functions
//   need no recursion;
// - the expression reader, a shunting-yard pass with its own explicit
//   stack, compiles calc(), min(), max(), clamp() and the engine functions
//   into the engine's postfix program, folding each constant operation as
//   it is applied.

#include "theme_value.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

// Identical bits on every host need the same operations in the same order:
// no fused multiply-add for the colour arithmetic (T2 plan, "Numerics").
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::theme_language {

namespace {

// -- names --------------------------------------------------------------------

struct NamedColour final {
    std::string_view name{};
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

// The CSS Color 4 named colours (R2-21), sorted by name; `transparent` is
// read apart.
constexpr std::array<NamedColour, 148> named_colours{{
    {"aliceblue", 0xF0U, 0xF8U, 0xFFU},
    {"antiquewhite", 0xFAU, 0xEBU, 0xD7U},
    {"aqua", 0x00U, 0xFFU, 0xFFU},
    {"aquamarine", 0x7FU, 0xFFU, 0xD4U},
    {"azure", 0xF0U, 0xFFU, 0xFFU},
    {"beige", 0xF5U, 0xF5U, 0xDCU},
    {"bisque", 0xFFU, 0xE4U, 0xC4U},
    {"black", 0x00U, 0x00U, 0x00U},
    {"blanchedalmond", 0xFFU, 0xEBU, 0xCDU},
    {"blue", 0x00U, 0x00U, 0xFFU},
    {"blueviolet", 0x8AU, 0x2BU, 0xE2U},
    {"brown", 0xA5U, 0x2AU, 0x2AU},
    {"burlywood", 0xDEU, 0xB8U, 0x87U},
    {"cadetblue", 0x5FU, 0x9EU, 0xA0U},
    {"chartreuse", 0x7FU, 0xFFU, 0x00U},
    {"chocolate", 0xD2U, 0x69U, 0x1EU},
    {"coral", 0xFFU, 0x7FU, 0x50U},
    {"cornflowerblue", 0x64U, 0x95U, 0xEDU},
    {"cornsilk", 0xFFU, 0xF8U, 0xDCU},
    {"crimson", 0xDCU, 0x14U, 0x3CU},
    {"cyan", 0x00U, 0xFFU, 0xFFU},
    {"darkblue", 0x00U, 0x00U, 0x8BU},
    {"darkcyan", 0x00U, 0x8BU, 0x8BU},
    {"darkgoldenrod", 0xB8U, 0x86U, 0x0BU},
    {"darkgray", 0xA9U, 0xA9U, 0xA9U},
    {"darkgreen", 0x00U, 0x64U, 0x00U},
    {"darkgrey", 0xA9U, 0xA9U, 0xA9U},
    {"darkkhaki", 0xBDU, 0xB7U, 0x6BU},
    {"darkmagenta", 0x8BU, 0x00U, 0x8BU},
    {"darkolivegreen", 0x55U, 0x6BU, 0x2FU},
    {"darkorange", 0xFFU, 0x8CU, 0x00U},
    {"darkorchid", 0x99U, 0x32U, 0xCCU},
    {"darkred", 0x8BU, 0x00U, 0x00U},
    {"darksalmon", 0xE9U, 0x96U, 0x7AU},
    {"darkseagreen", 0x8FU, 0xBCU, 0x8FU},
    {"darkslateblue", 0x48U, 0x3DU, 0x8BU},
    {"darkslategray", 0x2FU, 0x4FU, 0x4FU},
    {"darkslategrey", 0x2FU, 0x4FU, 0x4FU},
    {"darkturquoise", 0x00U, 0xCEU, 0xD1U},
    {"darkviolet", 0x94U, 0x00U, 0xD3U},
    {"deeppink", 0xFFU, 0x14U, 0x93U},
    {"deepskyblue", 0x00U, 0xBFU, 0xFFU},
    {"dimgray", 0x69U, 0x69U, 0x69U},
    {"dimgrey", 0x69U, 0x69U, 0x69U},
    {"dodgerblue", 0x1EU, 0x90U, 0xFFU},
    {"firebrick", 0xB2U, 0x22U, 0x22U},
    {"floralwhite", 0xFFU, 0xFAU, 0xF0U},
    {"forestgreen", 0x22U, 0x8BU, 0x22U},
    {"fuchsia", 0xFFU, 0x00U, 0xFFU},
    {"gainsboro", 0xDCU, 0xDCU, 0xDCU},
    {"ghostwhite", 0xF8U, 0xF8U, 0xFFU},
    {"gold", 0xFFU, 0xD7U, 0x00U},
    {"goldenrod", 0xDAU, 0xA5U, 0x20U},
    {"gray", 0x80U, 0x80U, 0x80U},
    {"green", 0x00U, 0x80U, 0x00U},
    {"greenyellow", 0xADU, 0xFFU, 0x2FU},
    {"grey", 0x80U, 0x80U, 0x80U},
    {"honeydew", 0xF0U, 0xFFU, 0xF0U},
    {"hotpink", 0xFFU, 0x69U, 0xB4U},
    {"indianred", 0xCDU, 0x5CU, 0x5CU},
    {"indigo", 0x4BU, 0x00U, 0x82U},
    {"ivory", 0xFFU, 0xFFU, 0xF0U},
    {"khaki", 0xF0U, 0xE6U, 0x8CU},
    {"lavender", 0xE6U, 0xE6U, 0xFAU},
    {"lavenderblush", 0xFFU, 0xF0U, 0xF5U},
    {"lawngreen", 0x7CU, 0xFCU, 0x00U},
    {"lemonchiffon", 0xFFU, 0xFAU, 0xCDU},
    {"lightblue", 0xADU, 0xD8U, 0xE6U},
    {"lightcoral", 0xF0U, 0x80U, 0x80U},
    {"lightcyan", 0xE0U, 0xFFU, 0xFFU},
    {"lightgoldenrodyellow", 0xFAU, 0xFAU, 0xD2U},
    {"lightgray", 0xD3U, 0xD3U, 0xD3U},
    {"lightgreen", 0x90U, 0xEEU, 0x90U},
    {"lightgrey", 0xD3U, 0xD3U, 0xD3U},
    {"lightpink", 0xFFU, 0xB6U, 0xC1U},
    {"lightsalmon", 0xFFU, 0xA0U, 0x7AU},
    {"lightseagreen", 0x20U, 0xB2U, 0xAAU},
    {"lightskyblue", 0x87U, 0xCEU, 0xFAU},
    {"lightslategray", 0x77U, 0x88U, 0x99U},
    {"lightslategrey", 0x77U, 0x88U, 0x99U},
    {"lightsteelblue", 0xB0U, 0xC4U, 0xDEU},
    {"lightyellow", 0xFFU, 0xFFU, 0xE0U},
    {"lime", 0x00U, 0xFFU, 0x00U},
    {"limegreen", 0x32U, 0xCDU, 0x32U},
    {"linen", 0xFAU, 0xF0U, 0xE6U},
    {"magenta", 0xFFU, 0x00U, 0xFFU},
    {"maroon", 0x80U, 0x00U, 0x00U},
    {"mediumaquamarine", 0x66U, 0xCDU, 0xAAU},
    {"mediumblue", 0x00U, 0x00U, 0xCDU},
    {"mediumorchid", 0xBAU, 0x55U, 0xD3U},
    {"mediumpurple", 0x93U, 0x70U, 0xDBU},
    {"mediumseagreen", 0x3CU, 0xB3U, 0x71U},
    {"mediumslateblue", 0x7BU, 0x68U, 0xEEU},
    {"mediumspringgreen", 0x00U, 0xFAU, 0x9AU},
    {"mediumturquoise", 0x48U, 0xD1U, 0xCCU},
    {"mediumvioletred", 0xC7U, 0x15U, 0x85U},
    {"midnightblue", 0x19U, 0x19U, 0x70U},
    {"mintcream", 0xF5U, 0xFFU, 0xFAU},
    {"mistyrose", 0xFFU, 0xE4U, 0xE1U},
    {"moccasin", 0xFFU, 0xE4U, 0xB5U},
    {"navajowhite", 0xFFU, 0xDEU, 0xADU},
    {"navy", 0x00U, 0x00U, 0x80U},
    {"oldlace", 0xFDU, 0xF5U, 0xE6U},
    {"olive", 0x80U, 0x80U, 0x00U},
    {"olivedrab", 0x6BU, 0x8EU, 0x23U},
    {"orange", 0xFFU, 0xA5U, 0x00U},
    {"orangered", 0xFFU, 0x45U, 0x00U},
    {"orchid", 0xDAU, 0x70U, 0xD6U},
    {"palegoldenrod", 0xEEU, 0xE8U, 0xAAU},
    {"palegreen", 0x98U, 0xFBU, 0x98U},
    {"paleturquoise", 0xAFU, 0xEEU, 0xEEU},
    {"palevioletred", 0xDBU, 0x70U, 0x93U},
    {"papayawhip", 0xFFU, 0xEFU, 0xD5U},
    {"peachpuff", 0xFFU, 0xDAU, 0xB9U},
    {"peru", 0xCDU, 0x85U, 0x3FU},
    {"pink", 0xFFU, 0xC0U, 0xCBU},
    {"plum", 0xDDU, 0xA0U, 0xDDU},
    {"powderblue", 0xB0U, 0xE0U, 0xE6U},
    {"purple", 0x80U, 0x00U, 0x80U},
    {"rebeccapurple", 0x66U, 0x33U, 0x99U},
    {"red", 0xFFU, 0x00U, 0x00U},
    {"rosybrown", 0xBCU, 0x8FU, 0x8FU},
    {"royalblue", 0x41U, 0x69U, 0xE1U},
    {"saddlebrown", 0x8BU, 0x45U, 0x13U},
    {"salmon", 0xFAU, 0x80U, 0x72U},
    {"sandybrown", 0xF4U, 0xA4U, 0x60U},
    {"seagreen", 0x2EU, 0x8BU, 0x57U},
    {"seashell", 0xFFU, 0xF5U, 0xEEU},
    {"sienna", 0xA0U, 0x52U, 0x2DU},
    {"silver", 0xC0U, 0xC0U, 0xC0U},
    {"skyblue", 0x87U, 0xCEU, 0xEBU},
    {"slateblue", 0x6AU, 0x5AU, 0xCDU},
    {"slategray", 0x70U, 0x80U, 0x90U},
    {"slategrey", 0x70U, 0x80U, 0x90U},
    {"snow", 0xFFU, 0xFAU, 0xFAU},
    {"springgreen", 0x00U, 0xFFU, 0x7FU},
    {"steelblue", 0x46U, 0x82U, 0xB4U},
    {"tan", 0xD2U, 0xB4U, 0x8CU},
    {"teal", 0x00U, 0x80U, 0x80U},
    {"thistle", 0xD8U, 0xBFU, 0xD8U},
    {"tomato", 0xFFU, 0x63U, 0x47U},
    {"turquoise", 0x40U, 0xE0U, 0xD0U},
    {"violet", 0xEEU, 0x82U, 0xEEU},
    {"wheat", 0xF5U, 0xDEU, 0xB3U},
    {"white", 0xFFU, 0xFFU, 0xFFU},
    {"whitesmoke", 0xF5U, 0xF5U, 0xF5U},
    {"yellow", 0xFFU, 0xFFU, 0x00U},
    {"yellowgreen", 0x9AU, 0xCDU, 0x32U},
}};

// The longest name in the table, `lightgoldenrodyellow`.
constexpr std::size_t longest_colour_name = 20U;

struct NamedColourBefore final {
    bool operator()(const NamedColour& entry, std::string_view name) const noexcept { return entry.name < name; }
};

struct UnitName final {
    std::string_view name{};
    ValueUnit unit{ValueUnit::none};
    // The written number times `multiplier`, divided by `divisor`, is the
    // number in its family's unit; `ms` divides, so 250ms reads as the
    // double nearest 0.25s.
    double multiplier{1.0};
    double divisor{1.0};
};

constexpr double pi = 3.14159265358979323846;

// The admitted units (A3). Units match in any ASCII case, as in CSS; every
// other unit is an error.
constexpr std::array<UnitName, 9> admitted_units{{
    {"px", ValueUnit::px, 1.0, 1.0},
    {"em", ValueUnit::em, 1.0, 1.0},
    {"s", ValueUnit::seconds, 1.0, 1.0},
    {"ms", ValueUnit::seconds, 1.0, 1000.0},
    {"deg", ValueUnit::degrees, 1.0, 1.0},
    {"rad", ValueUnit::degrees, 180.0 / pi, 1.0},
    {"turn", ValueUnit::degrees, 360.0, 1.0},
    {"dppx", ValueUnit::dppx, 1.0, 1.0},
    {"x", ValueUnit::dppx, 1.0, 1.0},
}};

// The expression functions (PLAN.md section 1.6). The comparisons are in
// ExpressionOperation's order, less to not_equal.
enum class ExpressionFunction : std::uint8_t {
    none,
    calc,
    minimum,
    maximum,
    clamp,
    mix,
    smoothstep,
    choose,
    less,
    less_equal,
    greater,
    greater_equal,
    equal,
    not_equal,
    state,
    param,
};

struct ExpressionFunctionName final {
    std::string_view name{};
    ExpressionFunction function{ExpressionFunction::none};
};

constexpr std::array<ExpressionFunctionName, 15> expression_functions{{
    {"calc", ExpressionFunction::calc},
    {"min", ExpressionFunction::minimum},
    {"max", ExpressionFunction::maximum},
    {"clamp", ExpressionFunction::clamp},
    {"mix", ExpressionFunction::mix},
    {"smoothstep", ExpressionFunction::smoothstep},
    {"choose", ExpressionFunction::choose},
    {"less", ExpressionFunction::less},
    {"less-equal", ExpressionFunction::less_equal},
    {"greater", ExpressionFunction::greater},
    {"greater-equal", ExpressionFunction::greater_equal},
    {"equal", ExpressionFunction::equal},
    {"not-equal", ExpressionFunction::not_equal},
    {"state", ExpressionFunction::state},
    {"param", ExpressionFunction::param},
}};

// CSS functions outside the language (E405): the other colour spaces, the
// other math functions, and the functions that read outside the sheet.
// `env()` is admitted for the host's colour hints alone (colour_from_env).
constexpr std::array<std::string_view, 36> refused_functions{{
    "hwb",   "lab",       "lch",     "oklab",   "oklch",   "color",      "light-dark", "device-cmyk",
    "round", "mod",       "rem",     "sin",     "cos",     "tan",        "asin",       "acos",
    "atan",  "atan2",     "pow",     "sqrt",    "hypot",   "log",        "exp",        "abs",
    "sign",  "calc-size", "attr",    "if",      "image",   "image-set",  "cross-fade",
    "element", "paint",   "counter", "counters", "symbols",
}};

[[nodiscard]] ExpressionFunction expression_function(std::string_view name) noexcept {
    for (const ExpressionFunctionName& entry : expression_functions) {
        if (equals_ascii_folded(name, entry.name)) return entry.function;
    }
    return ExpressionFunction::none;
}

[[nodiscard]] bool refused_function(std::string_view name) noexcept {
    for (const std::string_view refused : refused_functions) {
        if (equals_ascii_folded(name, refused)) return true;
    }
    return false;
}

[[nodiscard]] bool is_number_token(TokenKind kind) noexcept {
    return kind == TokenKind::number || kind == TokenKind::percentage || kind == TokenKind::dimension;
}

// -- colour arithmetic ----------------------------------------------------------

[[nodiscard]] int hex_digit(char character) noexcept {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

// `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa`: `digits` is the text after
// the '#'.
[[nodiscard]] std::optional<Color> hex_colour(std::string_view digits) noexcept {
    const std::size_t count = digits.size();
    if (count != 3U && count != 4U && count != 6U && count != 8U) return std::nullopt;
    std::array<int, 8> values{};
    for (std::size_t index = 0U; index < count; ++index) {
        values[index] = hex_digit(digits[index]);
        if (values[index] < 0) return std::nullopt;
    }
    std::array<std::uint8_t, 4> channels{0U, 0U, 0U, 255U};
    const bool short_form = count <= 4U;
    const std::size_t channel_count = short_form ? count : count / 2U;
    for (std::size_t channel = 0U; channel < channel_count; ++channel) {
        // A short form's digit d stands for dd.
        const int high = short_form ? values[channel] : values[channel * 2U];
        const int low = short_form ? values[channel] : values[channel * 2U + 1U];
        channels[channel] = static_cast<std::uint8_t>(high * 16 + low);
    }
    const Color colour = Color::rgba(channels[0], channels[1], channels[2], channels[3]);
    return colour;
}

[[nodiscard]] double clamped(double value, double low, double high) noexcept {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

// A channel from 0 to 255, clamped, rounded half up.
[[nodiscard]] std::uint8_t channel_byte(double value) noexcept {
    const double bounded = clamped(value, 0.0, 255.0);
    const double rounded = std::floor(bounded + 0.5);
    return static_cast<std::uint8_t>(rounded);
}

// CSS Color 4's hsl-to-rgb for one channel: `offset` is 0 for red, 8 for
// green and 4 for blue; `hue` is in [0, 360), `saturation` and `lightness`
// in [0, 1]. The result is in [0, 1].
[[nodiscard]] double hsl_channel(double offset, double hue, double saturation, double lightness) noexcept {
    const double turned = offset + hue / 30.0;
    const double k = std::fmod(turned, 12.0);
    const double darker = lightness < 1.0 - lightness ? lightness : 1.0 - lightness;
    const double amplitude = saturation * darker;
    const double rising = k - 3.0;
    const double falling = 9.0 - k;
    const double nearer = rising < falling ? rising : falling;
    const double shape = clamped(nearer, -1.0, 1.0);
    const double channel = lightness - amplitude * shape;
    return channel;
}

// -- inputs ------------------------------------------------------------------------

[[nodiscard]] ValueUnit input_unit(const ValueToken& item) noexcept {
    if (item.source == ValueTokenSource::published_value) return ValueUnit::none;
    const EngineInput input = static_cast<EngineInput>(item.input);
    if (input == EngineInput::pointer_x || input == EngineInput::pointer_y) return ValueUnit::px;
    if (input == EngineInput::time_in_state) return ValueUnit::seconds;
    return ValueUnit::none;
}

[[nodiscard]] ExpressionInputIndex input_position(const ValueToken& item) noexcept {
    if (item.source == ValueTokenSource::published_value) return published_value_position(item.input);
    const ExpressionInputIndex position = engine_input_position(static_cast<EngineInput>(item.input));
    return position;
}

// -- the expression reader's stack ---------------------------------------------------

enum class PendingKind : std::uint8_t { add, subtract, multiply, divide, parenthesis, function };

// An operator waiting for its right operand, or an open parenthesis or
// function waiting for its ')'. `arguments` counts a function's commas so
// far.
struct Pending final {
    PendingKind kind{PendingKind::add};
    ExpressionFunction function{ExpressionFunction::none};
    std::uint32_t token{};
    std::uint32_t arguments{};
};

// Groups bind loosest, so popping operators stops at the nearest group.
[[nodiscard]] int precedence(PendingKind kind) noexcept {
    if (kind == PendingKind::multiply || kind == PendingKind::divide) return 2;
    if (kind == PendingKind::add || kind == PendingKind::subtract) return 1;
    return 0;
}

// Each nesting level holds its group and at most two operators, one of each
// precedence: an operator pops every one of equal or higher precedence
// before it is pushed.
constexpr std::size_t maximum_pending = maximum_nesting_depth * 3U;

using Operand = ValueReader::Operand;

struct NumberRead final {
    double value{};
    ValueUnit unit{ValueUnit::none};
};

// The direct children of a function component, by index. The longest
// admitted argument list is color-mix()'s nine.
struct ChildList final {
    std::array<std::uint32_t, 9> index{};
    std::size_t count{};
};

// A colour function's channels by component index: three, or four with an
// alpha; `legacy` when they are separated by commas.
struct ColourArguments final {
    std::array<std::uint32_t, 4> channels{};
    std::size_t count{};
    bool legacy{};
};

// One side of a color-mix(): its colour and its percentage, if written.
struct MixItem final {
    Color colour{};
    // The colour's role, where role() gave it.
    ColourOrigin origin{};
    bool has_colour{};
    bool has_percentage{};
    double percentage{};
};

// -- one read ----------------------------------------------------------------------

class ValuePass final {
public:
    ValuePass(const ValueInput& input, ThemeValue& value, ThemeDiagnosticList& diagnostics,
              std::vector<ExpressionInstruction>& unfolded, std::vector<Operand>& operands) noexcept;

    [[nodiscard]] ValueOutcome run();

private:
    // The component reader.
    [[nodiscard]] bool read_component();
    [[nodiscard]] bool number_of(std::size_t at, NumberRead& number);
    [[nodiscard]] bool read_resource(std::size_t at);
    [[nodiscard]] bool open_function(std::size_t at);
    [[nodiscard]] bool close_function();
    [[nodiscard]] bool children_of(std::uint32_t function, ChildList& children);
    [[nodiscard]] bool colour_arguments(std::uint32_t function, const ChildList& children,
                                        ColourArguments& arguments);
    [[nodiscard]] bool alpha_of(std::uint32_t component, double& alpha);
    [[nodiscard]] bool colour_from_rgb(std::uint32_t function);
    [[nodiscard]] bool colour_from_hsl(std::uint32_t function);
    [[nodiscard]] bool colour_from_mix(std::uint32_t function);
    [[nodiscard]] bool colour_from_role(std::uint32_t function);
    // `env(system-accent, <colour>)` or `env(system-highlight, <colour>)`
    // (T4 theme variants): the host's hint where the context gives one,
    // else the fallback colour, which is required.
    [[nodiscard]] bool colour_from_env(std::uint32_t function);
    [[nodiscard]] bool read_mix_item(const ChildList& children, std::size_t first, std::size_t end,
                                     std::uint32_t function, MixItem& item);
    void replace_with_colour(std::uint32_t function, Color colour);
    void push_component(ValueComponent component);

    // The expression reader: the expression that starts at `at` (a
    // reserved input, or an expression function's token) through its ')';
    // `next_` is left after it.
    [[nodiscard]] bool read_expression(std::size_t at);
    [[nodiscard]] bool read_operand(std::size_t& index, bool& expect_operand);
    [[nodiscard]] bool read_operator(std::size_t& index, bool& expect_operand);
    [[nodiscard]] bool open_group(PendingKind kind, ExpressionFunction function, std::uint32_t token);
    [[nodiscard]] bool apply_pending_operators();
    [[nodiscard]] bool apply_operator(const Pending& pending);
    [[nodiscard]] bool apply_function(const Pending& pending);
    [[nodiscard]] bool push_name_operand(std::size_t at, ExpressionFunction function);
    [[nodiscard]] bool check_same_units(ValueUnit left, ValueUnit right, std::uint32_t token);
    void report_units(ValueUnit left, std::string_view relation, ValueUnit right, std::uint32_t token);
    void push_operand(ExpressionInstruction instruction, ValueUnit unit, bool constant);
    void apply(ExpressionOperation operation, std::size_t operand_count, ValueUnit unit);
    void fold(Operand& operand);
    [[nodiscard]] bool finish_expression(std::uint32_t token);

    [[nodiscard]] bool is_sheet(std::size_t at) const noexcept;
    [[nodiscard]] TokenKind kind_of(std::size_t at) const noexcept;
    [[nodiscard]] std::string_view text_of(std::size_t at) const noexcept;
    [[nodiscard]] std::string_view function_name(std::size_t at) const noexcept;
    [[nodiscard]] bool spaced(std::uint32_t token) const noexcept;
    void report(DiagnosticCode code, std::uint32_t token, std::string detail);

    const ThemeSource& source_;
    const std::vector<Token>& tokens_;
    const std::vector<ThemeNumber>& numbers_;
    TokenRange written_{};
    std::span<const ValueToken> stream_{};
    const ValueContext& context_;
    SourcePositionCursor& cursor_;
    ThemeValue& value_;
    ThemeDiagnosticList& diagnostics_;
    std::vector<ExpressionInstruction>& unfolded_;
    std::vector<Operand>& operands_;
    // Compiles and evaluates each constant operation, and checks each
    // finished program.
    EngineExpression folder_{};
    std::size_t next_{};
    // The component reader's open functions, by component index.
    std::array<std::uint32_t, maximum_nesting_depth> open_{};
    std::size_t depth_{};
    // The expression reader's pending operators and open groups.
    std::array<Pending, maximum_pending> pending_{};
    std::size_t pending_count_{};
    std::size_t groups_{};
};

ValuePass::ValuePass(const ValueInput& input, ThemeValue& value, ThemeDiagnosticList& diagnostics,
                     std::vector<ExpressionInstruction>& unfolded, std::vector<Operand>& operands) noexcept
    : source_(input.source), tokens_(input.tokens.tokens), numbers_(input.tokens.numbers), written_(input.written),
      stream_(input.substituted), context_(input.context), cursor_(input.cursor), value_(value),
      diagnostics_(diagnostics), unfolded_(unfolded), operands_(operands) {}

ValueOutcome ValuePass::run() {
    if (stream_.empty()) {
        // An empty value's range is empty at where it would start (the
        // parser trims it there).
        const std::uint32_t last = static_cast<std::uint32_t>(tokens_.size() - 1U);
        const std::uint32_t at = written_.first.value < tokens_.size() ? written_.first.value : last;
        report(DiagnosticCode::value_syntax, at, "empty value");
        return ValueOutcome::refused;
    }
    while (next_ < stream_.size()) {
        if (!read_component()) return ValueOutcome::refused;
    }
    // A sheet's groups are balanced by the parser and a definition's tokens
    // are a balanced range, so every function is closed here.
    if (depth_ != 0U) {
        const ValueComponent& open = value_.components[open_[depth_ - 1U]];
        report(DiagnosticCode::value_syntax, open.token, "function not closed");
        return ValueOutcome::refused;
    }
    return ValueOutcome::read;
}

// -- the component reader ----------------------------------------------------------

bool ValuePass::read_component() {
    if (!is_sheet(next_)) return read_expression(next_);
    const std::uint32_t token = stream_[next_].token;
    const TokenKind kind = kind_of(next_);
    ValueComponent component{};
    component.token = token;
    if (is_number_token(kind)) {
        NumberRead number{};
        if (!number_of(next_, number)) return false;
        component.kind = ValueComponentKind::number;
        component.number = number.value;
        component.unit = number.unit;
    } else if (kind == TokenKind::ident) {
        component.kind = ValueComponentKind::keyword;
        component.text = text_of(next_);
    } else if (kind == TokenKind::string) {
        const std::string_view written = text_of(next_);
        component.kind = ValueComponentKind::string;
        component.text = written.substr(1U, written.size() - 2U);
    } else if (kind == TokenKind::hash) {
        const std::optional<Color> colour = hex_colour(text_of(next_).substr(1U));
        if (!colour) {
            report(DiagnosticCode::value_syntax, token, clipped_theme_detail(text_of(next_)));
            return false;
        }
        component.kind = ValueComponentKind::colour;
        component.colour = *colour;
    } else if (kind == TokenKind::comma) {
        component.kind = ValueComponentKind::comma;
    } else if (kind == TokenKind::delim && text_of(next_) == "/") {
        component.kind = ValueComponentKind::slash;
    } else if (kind == TokenKind::function) {
        return open_function(next_);
    } else if (kind == TokenKind::right_paren && depth_ != 0U) {
        ++next_;
        return close_function();
    } else {
        report(DiagnosticCode::value_syntax, token, clipped_theme_detail(text_of(next_)));
        return false;
    }
    push_component(component);
    ++next_;
    return true;
}

bool ValuePass::number_of(std::size_t at, NumberRead& number) {
    const std::uint32_t token = stream_[at].token;
    const Token& written = tokens_[token];
    const ThemeNumber& parsed = numbers_[written.number];
    number.value = parsed.value;
    number.unit = ValueUnit::none;
    if (written.kind == TokenKind::number) return true;
    if (written.kind == TokenKind::percentage) {
        number.unit = ValueUnit::percent;
        return true;
    }
    const std::string_view unit = text_of(at).substr(parsed.numeric_length.value);
    for (const UnitName& admitted : admitted_units) {
        if (!equals_ascii_folded(unit, admitted.name)) continue;
        const double scaled = parsed.value * admitted.multiplier;
        number.value = scaled / admitted.divisor;
        number.unit = admitted.unit;
        return true;
    }
    report(DiagnosticCode::unit_not_admitted, token, clipped_theme_detail(unit));
    return false;
}

bool ValuePass::open_function(std::size_t at) {
    const std::uint32_t token = stream_[at].token;
    const std::string_view name = function_name(at);
    if (expression_function(name) != ExpressionFunction::none) return read_expression(at);
    if (equals_ascii_folded(name, "url")) return read_resource(at);
    if (refused_function(name)) {
        report(DiagnosticCode::value_form_not_admitted, token, clipped_theme_detail(text_of(at)));
        return false;
    }
    if (depth_ == open_.size()) {
        report(DiagnosticCode::nesting_too_deep, token, std::to_string(maximum_nesting_depth));
        return false;
    }
    ValueComponent component{};
    component.kind = ValueComponentKind::function;
    component.text = name;
    component.token = token;
    open_[depth_] = static_cast<std::uint32_t>(value_.components.size());
    ++depth_;
    // Its subtree_end is set when it closes.
    value_.components.push_back(component);
    ++next_;
    return true;
}

bool ValuePass::read_resource(std::size_t at) {
    const std::uint32_t token = stream_[at].token;
    const std::size_t argument = at + 1U;
    const std::size_t close = at + 2U;
    const bool shaped = close < stream_.size() && is_sheet(argument) && is_sheet(close) &&
                        kind_of(argument) == TokenKind::string && kind_of(close) == TokenKind::right_paren;
    if (!shaped) {
        report(DiagnosticCode::value_syntax, token, "url() takes one string");
        return false;
    }
    const std::string_view quoted = text_of(argument);
    const std::string_view written = quoted.substr(1U, quoted.size() - 2U);
    constexpr std::string_view scheme = "resource:";
    if (written.substr(0U, scheme.size()) != scheme) {
        report(DiagnosticCode::value_form_not_admitted, stream_[argument].token, clipped_theme_detail(written));
        return false;
    }
    const std::string_view name = written.substr(scheme.size());
    bool listed = false;
    for (const std::string_view resource : context_.resources) {
        if (resource == name) listed = true;
    }
    if (name.empty() || !listed) {
        report(DiagnosticCode::resource_unknown, stream_[argument].token, clipped_theme_detail(name));
        return false;
    }
    ValueComponent component{};
    component.kind = ValueComponentKind::resource;
    component.text = name;
    component.token = token;
    push_component(component);
    next_ = close + 1U;
    return true;
}

bool ValuePass::close_function() {
    --depth_;
    const std::uint32_t function = open_[depth_];
    ValueComponent& closed = value_.components[function];
    closed.subtree_end = static_cast<std::uint32_t>(value_.components.size());
    const std::string_view name = closed.text;
    if (equals_ascii_folded(name, "rgb") || equals_ascii_folded(name, "rgba")) return colour_from_rgb(function);
    if (equals_ascii_folded(name, "hsl") || equals_ascii_folded(name, "hsla")) return colour_from_hsl(function);
    if (equals_ascii_folded(name, "color-mix")) return colour_from_mix(function);
    if (equals_ascii_folded(name, "role")) return colour_from_role(function);
    if (equals_ascii_folded(name, "env")) return colour_from_env(function);
    return true;
}

bool ValuePass::children_of(std::uint32_t function, ChildList& children) {
    children.count = 0U;
    const std::uint32_t end = value_.components[function].subtree_end;
    std::uint32_t child = function + 1U;
    while (child < end) {
        if (children.count == children.index.size()) {
            report(DiagnosticCode::value_syntax, value_.components[child].token, "too many arguments");
            return false;
        }
        children.index[children.count] = child;
        ++children.count;
        child = value_.components[child].subtree_end;
    }
    return true;
}

// rgb() and hsl() arguments: `a, b, c[, alpha]` (legacy) or
// `a b c[ / alpha]` (modern).
bool ValuePass::colour_arguments(std::uint32_t function, const ChildList& children, ColourArguments& arguments) {
    const ValueComponent& written = value_.components[function];
    bool commas = false;
    for (std::size_t child = 0U; child < children.count; ++child) {
        if (value_.components[children.index[child]].kind == ValueComponentKind::comma) commas = true;
    }
    if (children.count > 0U) {
        const ValueComponent& first = value_.components[children.index[0]];
        if (first.kind == ValueComponentKind::keyword && equals_ascii_folded(first.text, "from")) {
            report(DiagnosticCode::value_form_not_admitted, first.token, "relative colour");
            return false;
        }
    }
    arguments.legacy = commas;
    arguments.count = 0U;
    bool shaped = false;
    if (commas) {
        shaped = children.count == 5U || children.count == 7U;
        for (std::size_t child = 0U; shaped && child < children.count; ++child) {
            const bool comma = value_.components[children.index[child]].kind == ValueComponentKind::comma;
            const bool expected_comma = child % 2U == 1U;
            if (comma != expected_comma) shaped = false;
            if (!expected_comma) {
                arguments.channels[arguments.count] = children.index[child];
                ++arguments.count;
            }
        }
    } else {
        shaped = children.count == 3U ||
                 (children.count == 5U && value_.components[children.index[3]].kind == ValueComponentKind::slash);
        for (std::size_t child = 0U; shaped && child < children.count; ++child) {
            if (child == 3U) continue;
            arguments.channels[arguments.count] = children.index[child];
            ++arguments.count;
        }
    }
    if (!shaped) {
        report(DiagnosticCode::value_syntax, written.token, clipped_theme_detail(written.text));
        return false;
    }
    return true;
}

bool ValuePass::alpha_of(std::uint32_t component, double& alpha) {
    const ValueComponent& written = value_.components[component];
    const bool number = written.kind == ValueComponentKind::number &&
                        (written.unit == ValueUnit::none || written.unit == ValueUnit::percent);
    if (!number) {
        report(DiagnosticCode::value_wrong_kind, written.token, "alpha");
        return false;
    }
    const double fraction = written.unit == ValueUnit::percent ? written.number / 100.0 : written.number;
    alpha = clamped(fraction, 0.0, 1.0);
    return true;
}

bool ValuePass::colour_from_rgb(std::uint32_t function) {
    ChildList children{};
    if (!children_of(function, children)) return false;
    ColourArguments arguments{};
    if (!colour_arguments(function, children, arguments)) return false;
    std::array<double, 3> channels{};
    bool numbers = false;
    bool percentages = false;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const ValueComponent& written = value_.components[arguments.channels[channel]];
        const bool number = written.kind == ValueComponentKind::number &&
                            (written.unit == ValueUnit::none || written.unit == ValueUnit::percent);
        if (!number) {
            report(DiagnosticCode::value_wrong_kind, written.token, "colour channel");
            return false;
        }
        if (written.unit == ValueUnit::percent) {
            const double scaled = written.number * 255.0;
            channels[channel] = scaled / 100.0;
            percentages = true;
        } else {
            channels[channel] = written.number;
            numbers = true;
        }
    }
    if (arguments.legacy && numbers && percentages) {
        report(DiagnosticCode::value_syntax, value_.components[function].token,
               "rgb() with commas takes all numbers or all percentages");
        return false;
    }
    double alpha = 1.0;
    if (arguments.count == 4U && !alpha_of(arguments.channels[3], alpha)) return false;
    const Color colour = Color::rgba(channel_byte(channels[0]), channel_byte(channels[1]), channel_byte(channels[2]),
                                     channel_byte(alpha * 255.0));
    replace_with_colour(function, colour);
    return true;
}

bool ValuePass::colour_from_hsl(std::uint32_t function) {
    ChildList children{};
    if (!children_of(function, children)) return false;
    ColourArguments arguments{};
    if (!colour_arguments(function, children, arguments)) return false;
    const ValueComponent& hue = value_.components[arguments.channels[0]];
    const bool angle =
        hue.kind == ValueComponentKind::number && (hue.unit == ValueUnit::none || hue.unit == ValueUnit::degrees);
    if (!angle) {
        report(DiagnosticCode::value_wrong_kind, hue.token, "hue");
        return false;
    }
    std::array<double, 2> fractions{};
    for (std::size_t channel = 1U; channel < 3U; ++channel) {
        const ValueComponent& written = value_.components[arguments.channels[channel]];
        // The legacy form takes percentages; the modern one also takes
        // numbers, read as percentages.
        const bool admitted = written.kind == ValueComponentKind::number &&
                              (written.unit == ValueUnit::percent ||
                               (!arguments.legacy && written.unit == ValueUnit::none));
        if (!admitted) {
            report(DiagnosticCode::value_wrong_kind, written.token, "saturation or lightness");
            return false;
        }
        fractions[channel - 1U] = clamped(written.number / 100.0, 0.0, 1.0);
    }
    double alpha = 1.0;
    if (arguments.count == 4U && !alpha_of(arguments.channels[3], alpha)) return false;
    double turned = std::fmod(hue.number, 360.0);
    if (turned < 0.0) turned += 360.0;
    const double red = hsl_channel(0.0, turned, fractions[0], fractions[1]);
    const double green = hsl_channel(8.0, turned, fractions[0], fractions[1]);
    const double blue = hsl_channel(4.0, turned, fractions[0], fractions[1]);
    const Color colour = Color::rgba(channel_byte(red * 255.0), channel_byte(green * 255.0),
                                     channel_byte(blue * 255.0), channel_byte(alpha * 255.0));
    replace_with_colour(function, colour);
    return true;
}

bool ValuePass::read_mix_item(const ChildList& children, std::size_t first, std::size_t end, std::uint32_t function,
                              MixItem& item) {
    const std::size_t count = end - first;
    if (count == 0U || count > 2U) {
        report(DiagnosticCode::value_syntax, value_.components[function].token, "color-mix() side");
        return false;
    }
    for (std::size_t child = first; child < end; ++child) {
        const ValueComponent& written = value_.components[children.index[child]];
        std::optional<Color> colour{};
        if (written.kind == ValueComponentKind::colour) colour = written.colour;
        if (written.kind == ValueComponentKind::keyword) colour = named_colour(written.text);
        if (colour && !item.has_colour) {
            item.colour = *colour;
            item.origin = written.origin;
            item.has_colour = true;
            continue;
        }
        const bool percentage = written.kind == ValueComponentKind::number && written.unit == ValueUnit::percent;
        if (percentage && !item.has_percentage) {
            if (written.number < 0.0 || written.number > 100.0) {
                report(DiagnosticCode::value_out_of_range, written.token, "0% to 100%");
                return false;
            }
            item.percentage = written.number;
            item.has_percentage = true;
            continue;
        }
        report(DiagnosticCode::value_wrong_kind, written.token, "color-mix() takes a colour and a percentage");
        return false;
    }
    if (!item.has_colour) {
        report(DiagnosticCode::value_syntax, value_.components[function].token, "color-mix() side without a colour");
        return false;
    }
    return true;
}

// color-mix(in srgb, <colour> [<percentage>], <colour> [<percentage>]),
// interpolated with premultiplied alpha (CSS Color 5).
bool ValuePass::colour_from_mix(std::uint32_t function) {
    ChildList children{};
    if (!children_of(function, children)) return false;
    const std::uint32_t token = value_.components[function].token;
    if (children.count < 6U) {
        report(DiagnosticCode::value_syntax, token, "color-mix(in srgb, a, b)");
        return false;
    }
    const ValueComponent& in = value_.components[children.index[0]];
    const ValueComponent& space = value_.components[children.index[1]];
    const ValueComponent& after_space = value_.components[children.index[2]];
    const bool introduced = in.kind == ValueComponentKind::keyword && equals_ascii_folded(in.text, "in") &&
                            space.kind == ValueComponentKind::keyword && after_space.kind == ValueComponentKind::comma;
    if (!introduced) {
        report(DiagnosticCode::value_syntax, token, "color-mix(in srgb, a, b)");
        return false;
    }
    if (!equals_ascii_folded(space.text, "srgb")) {
        report(DiagnosticCode::value_form_not_admitted, space.token, clipped_theme_detail(space.text));
        return false;
    }
    std::size_t separator = 0U;
    for (std::size_t child = 3U; child < children.count; ++child) {
        const ValueComponent& written = value_.components[children.index[child]];
        if (written.kind != ValueComponentKind::comma) continue;
        if (separator != 0U) {
            report(DiagnosticCode::value_syntax, written.token, "','");
            return false;
        }
        separator = child;
    }
    if (separator == 0U) {
        report(DiagnosticCode::value_syntax, token, "color-mix(in srgb, a, b)");
        return false;
    }
    MixItem first{};
    MixItem second{};
    if (!read_mix_item(children, 3U, separator, function, first)) return false;
    if (!read_mix_item(children, separator + 1U, children.count, function, second)) return false;
    // The percentages: both omitted is 50% each; one omitted is the rest of
    // 100%; a sum below 100% scales the result's alpha.
    double first_share = 50.0;
    double second_share = 50.0;
    if (first.has_percentage && second.has_percentage) {
        first_share = first.percentage;
        second_share = second.percentage;
    } else if (first.has_percentage) {
        first_share = first.percentage;
        second_share = 100.0 - first.percentage;
    } else if (second.has_percentage) {
        second_share = second.percentage;
        first_share = 100.0 - second.percentage;
    }
    const double sum = first_share + second_share;
    if (sum == 0.0) {
        report(DiagnosticCode::value_out_of_range, token, "percentages sum to 0%");
        return false;
    }
    const double alpha_scale = sum < 100.0 ? sum / 100.0 : 1.0;
    const double first_weight = first_share / sum;
    const double second_weight = second_share / sum;
    const double first_alpha = static_cast<double>(first.colour.alpha) / 255.0;
    const double second_alpha = static_cast<double>(second.colour.alpha) / 255.0;
    const double first_coverage = first_alpha * first_weight;
    const double second_coverage = second_alpha * second_weight;
    const double alpha = first_coverage + second_coverage;
    const std::array<std::uint8_t, 3> first_channels{first.colour.red, first.colour.green, first.colour.blue};
    const std::array<std::uint8_t, 3> second_channels{second.colour.red, second.colour.green, second.colour.blue};
    // A fully transparent mix has no colour to keep: its channels are 0.
    std::array<std::uint8_t, 3> mixed{};
    for (std::size_t channel = 0U; channel < 3U && alpha != 0.0; ++channel) {
        const double first_part = static_cast<double>(first_channels[channel]) * first_coverage;
        const double second_part = static_cast<double>(second_channels[channel]) * second_coverage;
        const double premultiplied = first_part + second_part;
        mixed[channel] = channel_byte(premultiplied / alpha);
    }
    const double scaled_alpha = alpha * alpha_scale;
    const Color colour = Color::rgba(mixed[0], mixed[1], mixed[2], channel_byte(scaled_alpha * 255.0));
    replace_with_colour(function, colour);
    // A mix of roles keeps its first role (EC4); a literal in it makes the
    // mix a literal.
    const bool roles = first.origin.source == ColourSource::role && second.origin.source == ColourSource::role;
    if (roles) value_.components.back().origin = first.origin;
    return true;
}

// role(<role> [foreground | background]) (P8, R2-03): the palette's colour
// of the role's side, carrying the role.
bool ValuePass::colour_from_role(std::uint32_t function) {
    const std::uint32_t token = value_.components[function].token;
    ChildList children{};
    if (!children_of(function, children)) return false;
    bool shaped = children.count == 1U || children.count == 2U;
    for (std::size_t child = 0U; shaped && child < children.count; ++child) {
        if (value_.components[children.index[child]].kind != ValueComponentKind::keyword) shaped = false;
    }
    if (!shaped) {
        report(DiagnosticCode::value_syntax, token, "role(<role> [foreground | background])");
        return false;
    }
    const ValueComponent& written = value_.components[children.index[0]];
    std::string lower(written.text);
    for (char& character : lower) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    const std::optional<ColourRole> role = colour_role_from_name(lower);
    if (!role) {
        report(DiagnosticCode::keyword_unknown, written.token, clipped_theme_detail(written.text));
        return false;
    }
    const bool paired = colour_role_paired(*role);
    ColourRoleSide side = ColourRoleSide::foreground;
    if (children.count == 2U) {
        const ValueComponent& named = value_.components[children.index[1]];
        const bool foreground = equals_ascii_folded(named.text, "foreground");
        const bool background = equals_ascii_folded(named.text, "background");
        if (!foreground && !background) {
            report(DiagnosticCode::keyword_unknown, named.token, clipped_theme_detail(named.text));
            return false;
        }
        if (!paired) {
            report(DiagnosticCode::value_form_not_admitted, named.token, "a side of a single colour role");
            return false;
        }
        side = foreground ? ColourRoleSide::foreground : ColourRoleSide::background;
    } else if (paired) {
        if (!context_.bare_side) {
            report(DiagnosticCode::value_form_not_admitted, token, "role() of a pair without its side");
            return false;
        }
        side = *context_.bare_side;
    }
    if (context_.palette == nullptr) {
        report(DiagnosticCode::value_form_not_admitted, token, "role() in a palette");
        return false;
    }
    const std::size_t position = static_cast<std::size_t>(*role);
    const std::optional<RoleColours>& declared = (*context_.palette).roles[position];
    value_.roles_read |= 1U << position;
    if (!declared) {
        report(DiagnosticCode::colour_role_undefined, written.token, std::string(colour_role_name(*role)));
        return false;
    }
    const Color colour = side == ColourRoleSide::foreground ? (*declared).foreground : (*declared).background;
    replace_with_colour(function, colour);
    ColourOrigin origin{};
    origin.source = ColourSource::role;
    origin.role = *role;
    origin.side = side;
    value_.components.back().origin = origin;
    return true;
}

bool ValuePass::colour_from_env(std::uint32_t function) {
    const std::uint32_t token = value_.components[function].token;
    ChildList children{};
    if (!children_of(function, children)) return false;
    bool shaped = children.count == 3U;
    if (shaped) {
        const ValueComponent& named = value_.components[children.index[0]];
        const ValueComponent& separator = value_.components[children.index[1]];
        shaped = named.kind == ValueComponentKind::keyword && separator.kind == ValueComponentKind::comma;
    }
    std::optional<Color> fallback{};
    if (shaped) {
        const ValueComponent& written = value_.components[children.index[2]];
        if (written.kind == ValueComponentKind::colour) fallback = written.colour;
        if (written.kind == ValueComponentKind::keyword) fallback = named_colour(written.text);
    }
    if (!shaped || !fallback) {
        report(DiagnosticCode::value_syntax, token, "env(system-accent | system-highlight, <colour>)");
        return false;
    }
    const ValueComponent& named = value_.components[children.index[0]];
    const bool accent = equals_ascii_folded(named.text, "system-accent");
    const bool highlight = equals_ascii_folded(named.text, "system-highlight");
    if (!accent && !highlight) {
        report(DiagnosticCode::keyword_unknown, named.token, clipped_theme_detail(named.text));
        return false;
    }
    std::optional<Color> hint{};
    if (context_.system_colours != nullptr) {
        hint = accent ? (*context_.system_colours).accent : (*context_.system_colours).highlight;
    }
    const Color colour = hint ? *hint : *fallback;
    replace_with_colour(function, colour);
    return true;
}

void ValuePass::replace_with_colour(std::uint32_t function, Color colour) {
    const std::uint32_t token = value_.components[function].token;
    value_.components.resize(function);
    ValueComponent component{};
    component.kind = ValueComponentKind::colour;
    component.colour = colour;
    component.token = token;
    push_component(component);
}

void ValuePass::push_component(ValueComponent component) {
    component.subtree_end = static_cast<std::uint32_t>(value_.components.size() + 1U);
    value_.components.push_back(component);
}

// -- the expression reader ---------------------------------------------------------

bool ValuePass::read_expression(std::size_t at) {
    unfolded_.clear();
    operands_.clear();
    pending_count_ = 0U;
    groups_ = 0U;
    const std::uint32_t token = stream_[at].token;
    std::size_t index = at;
    bool expect_operand = true;
    do {
        if (index >= stream_.size()) {
            report(DiagnosticCode::value_syntax, token, "expression not closed");
            return false;
        }
        const bool stepped = expect_operand ? read_operand(index, expect_operand) : read_operator(index, expect_operand);
        if (!stepped) return false;
    } while (pending_count_ != 0U);
    next_ = index;
    return finish_expression(token);
}

bool ValuePass::read_operand(std::size_t& index, bool& expect_operand) {
    const ValueToken& item = stream_[index];
    if (!is_sheet(index)) {
        push_operand(ExpressionInstruction::input(input_position(item)), input_unit(item), false);
        ++index;
        expect_operand = false;
        return true;
    }
    const TokenKind kind = kind_of(index);
    if (is_number_token(kind)) {
        NumberRead number{};
        if (!number_of(index, number)) return false;
        push_operand(ExpressionInstruction::number(number.value), number.unit, true);
        ++index;
        expect_operand = false;
        return true;
    }
    if (kind == TokenKind::left_paren) {
        if (!open_group(PendingKind::parenthesis, ExpressionFunction::none, item.token)) return false;
        ++index;
        return true;
    }
    if (kind == TokenKind::function) {
        const std::string_view name = function_name(index);
        const ExpressionFunction function = expression_function(name);
        if (function == ExpressionFunction::state || function == ExpressionFunction::param) {
            if (!push_name_operand(index, function)) return false;
            index += 3U;
            expect_operand = false;
            return true;
        }
        if (function != ExpressionFunction::none) {
            if (!open_group(PendingKind::function, function, item.token)) return false;
            ++index;
            return true;
        }
        const DiagnosticCode code =
            refused_function(name) ? DiagnosticCode::value_form_not_admitted : DiagnosticCode::value_wrong_kind;
        report(code, item.token, clipped_theme_detail(text_of(index)));
        return false;
    }
    report(DiagnosticCode::value_syntax, item.token, "expected a number");
    return false;
}

bool ValuePass::read_operator(std::size_t& index, bool& expect_operand) {
    const std::uint32_t token = stream_[index].token;
    // A reserved input is an operand, never an operator; `invalid` stands
    // for it here, since a sheet with an invalid token never parses.
    const TokenKind kind = is_sheet(index) ? kind_of(index) : TokenKind::invalid;
    if (kind == TokenKind::delim) {
        const std::string_view written = text_of(index);
        PendingKind pending = PendingKind::add;
        if (written == "+") {
            pending = PendingKind::add;
        } else if (written == "-") {
            pending = PendingKind::subtract;
        } else if (written == "*") {
            pending = PendingKind::multiply;
        } else if (written == "/") {
            pending = PendingKind::divide;
        } else {
            report(DiagnosticCode::value_syntax, token, clipped_theme_detail(written));
            return false;
        }
        const bool additive = pending == PendingKind::add || pending == PendingKind::subtract;
        if (additive && !spaced(token)) {
            report(DiagnosticCode::value_syntax, token, "'+' and '-' need whitespace on both sides");
            return false;
        }
        while (pending_count_ != 0U && precedence(pending_[pending_count_ - 1U].kind) >= precedence(pending)) {
            --pending_count_;
            if (!apply_operator(pending_[pending_count_])) return false;
        }
        if (pending_count_ == pending_.size()) {
            report(DiagnosticCode::nesting_too_deep, token, std::to_string(maximum_nesting_depth));
            return false;
        }
        Pending pushed{};
        pushed.kind = pending;
        pushed.token = token;
        pending_[pending_count_] = pushed;
        ++pending_count_;
        ++index;
        expect_operand = true;
        return true;
    }
    if (kind != TokenKind::comma && kind != TokenKind::right_paren) {
        report(DiagnosticCode::value_syntax, token, "expected an operator");
        return false;
    }
    if (!apply_pending_operators()) return false;
    // Every operator follows an operand inside a group, so a group is on
    // top now.
    Pending& group = pending_[pending_count_ - 1U];
    if (kind == TokenKind::comma) {
        if (group.kind == PendingKind::parenthesis) {
            report(DiagnosticCode::value_syntax, token, "',' inside parentheses");
            return false;
        }
        ++group.arguments;
        ++index;
        expect_operand = true;
        return true;
    }
    const Pending closed = group;
    --pending_count_;
    --groups_;
    if (closed.kind == PendingKind::function && !apply_function(closed)) return false;
    ++index;
    expect_operand = false;
    return true;
}

bool ValuePass::open_group(PendingKind kind, ExpressionFunction function, std::uint32_t token) {
    if (groups_ == maximum_nesting_depth || pending_count_ == pending_.size()) {
        report(DiagnosticCode::nesting_too_deep, token, std::to_string(maximum_nesting_depth));
        return false;
    }
    Pending group{};
    group.kind = kind;
    group.function = function;
    group.token = token;
    pending_[pending_count_] = group;
    ++pending_count_;
    ++groups_;
    return true;
}

bool ValuePass::apply_pending_operators() {
    while (pending_count_ != 0U && precedence(pending_[pending_count_ - 1U].kind) > 0) {
        --pending_count_;
        if (!apply_operator(pending_[pending_count_])) return false;
    }
    return true;
}

void ValuePass::report_units(ValueUnit left, std::string_view relation, ValueUnit right, std::uint32_t token) {
    std::string detail(value_unit_name(left));
    detail += relation;
    detail += value_unit_name(right);
    report(DiagnosticCode::value_wrong_kind, token, detail);
}

bool ValuePass::check_same_units(ValueUnit left, ValueUnit right, std::uint32_t token) {
    if (left == right) return true;
    report_units(left, " and ", right, token);
    return false;
}

bool ValuePass::apply_operator(const Pending& pending) {
    const std::size_t count = operands_.size();
    const ValueUnit left = operands_[count - 2U].unit;
    const ValueUnit right = operands_[count - 1U].unit;
    if (pending.kind == PendingKind::add || pending.kind == PendingKind::subtract) {
        if (!check_same_units(left, right, pending.token)) return false;
        const ExpressionOperation operation =
            pending.kind == PendingKind::add ? ExpressionOperation::add : ExpressionOperation::subtract;
        apply(operation, 2U, left);
        return true;
    }
    if (pending.kind == PendingKind::multiply) {
        // One factor is a plain number; the product has the other's unit.
        if (left != ValueUnit::none && right != ValueUnit::none) {
            report_units(left, " * ", right, pending.token);
            return false;
        }
        const ValueUnit unit = left == ValueUnit::none ? right : left;
        apply(ExpressionOperation::multiply, 2U, unit);
        return true;
    }
    // Division by a plain number keeps the unit; by the same unit it gives a
    // plain number.
    if (right != ValueUnit::none && right != left) {
        report_units(left, " / ", right, pending.token);
        return false;
    }
    const ValueUnit unit = right == ValueUnit::none ? left : ValueUnit::none;
    apply(ExpressionOperation::divide, 2U, unit);
    return true;
}

bool ValuePass::apply_function(const Pending& pending) {
    const std::size_t arguments = static_cast<std::size_t>(pending.arguments) + 1U;
    const ExpressionFunction function = pending.function;
    const bool variadic = function == ExpressionFunction::minimum || function == ExpressionFunction::maximum;
    const bool comparison = function >= ExpressionFunction::less && function <= ExpressionFunction::not_equal;
    std::size_t expected = 3U;
    if (function == ExpressionFunction::calc) expected = 1U;
    if (comparison) expected = 2U;
    if (!variadic && arguments != expected) {
        std::string detail = std::to_string(expected);
        detail += expected == 1U ? " argument" : " arguments";
        report(DiagnosticCode::value_syntax, pending.token, detail);
        return false;
    }
    if (function == ExpressionFunction::calc) return true;
    if (variadic) {
        const ExpressionOperation operation =
            function == ExpressionFunction::minimum ? ExpressionOperation::minimum : ExpressionOperation::maximum;
        for (std::size_t applied = 1U; applied < arguments; ++applied) {
            const std::size_t live = operands_.size();
            const ValueUnit left = operands_[live - 2U].unit;
            const ValueUnit right = operands_[live - 1U].unit;
            if (!check_same_units(left, right, pending.token)) return false;
            apply(operation, 2U, left);
        }
        return true;
    }
    const std::size_t count = operands_.size();
    if (comparison) {
        const ValueUnit left = operands_[count - 2U].unit;
        const ValueUnit right = operands_[count - 1U].unit;
        if (!check_same_units(left, right, pending.token)) return false;
        const std::size_t offset =
            static_cast<std::size_t>(function) - static_cast<std::size_t>(ExpressionFunction::less);
        const std::size_t operation_value = static_cast<std::size_t>(ExpressionOperation::less) + offset;
        apply(static_cast<ExpressionOperation>(operation_value), 2U, ValueUnit::none);
        return true;
    }
    const ValueUnit first = operands_[count - 3U].unit;
    const ValueUnit second = operands_[count - 2U].unit;
    const ValueUnit third = operands_[count - 1U].unit;
    if (function == ExpressionFunction::clamp) {
        // CSS's clamp(MIN, VAL, MAX) is max(MIN, min(VAL, MAX)), so a MIN
        // above MAX gives MIN; the engine's clamp would give MAX.
        if (!check_same_units(first, second, pending.token)) return false;
        if (!check_same_units(first, third, pending.token)) return false;
        apply(ExpressionOperation::minimum, 2U, first);
        apply(ExpressionOperation::maximum, 2U, first);
        return true;
    }
    if (function == ExpressionFunction::mix) {
        if (!check_same_units(first, second, pending.token)) return false;
        if (!check_same_units(third, ValueUnit::none, pending.token)) return false;
        apply(ExpressionOperation::mix, 3U, first);
        return true;
    }
    if (function == ExpressionFunction::smoothstep) {
        if (!check_same_units(first, second, pending.token)) return false;
        if (!check_same_units(first, third, pending.token)) return false;
        apply(ExpressionOperation::smoothstep, 3U, ValueUnit::none);
        return true;
    }
    // choose(condition, if_true, if_false).
    if (!check_same_units(first, ValueUnit::none, pending.token)) return false;
    if (!check_same_units(second, third, pending.token)) return false;
    apply(ExpressionOperation::choose, 3U, second);
    return true;
}

bool ValuePass::push_name_operand(std::size_t at, ExpressionFunction function) {
    const std::uint32_t token = stream_[at].token;
    const std::size_t argument = at + 1U;
    const std::size_t close = at + 2U;
    const bool shaped = close < stream_.size() && is_sheet(argument) && is_sheet(close) &&
                        kind_of(argument) == TokenKind::ident && kind_of(close) == TokenKind::right_paren;
    if (!shaped) {
        const char* form = function == ExpressionFunction::state ? "state(<state>)" : "param(<parameter>)";
        report(DiagnosticCode::value_syntax, token, form);
        return false;
    }
    const std::string_view written = text_of(argument);
    const std::string registered = registered_name_from_kebab(written);
    if (function == ExpressionFunction::state) {
        const std::optional<AnatomyState> state =
            registered.empty() ? std::optional<AnatomyState>{} : anatomy_state_from_name(registered);
        if (!state) {
            report(DiagnosticCode::value_syntax, stream_[argument].token, clipped_theme_detail(written));
            return false;
        }
        push_operand(ExpressionInstruction::state(*state), ValueUnit::none, false);
        return true;
    }
    std::size_t position = context_.parameters.size();
    for (std::size_t parameter = 0U; parameter < context_.parameters.size(); ++parameter) {
        if (context_.parameters[parameter] == registered) position = parameter;
    }
    const bool found = !registered.empty() && position < context_.parameters.size();
    if (!found || position >= maximum_expression_parameters) {
        report(DiagnosticCode::value_syntax, stream_[argument].token, clipped_theme_detail(written));
        return false;
    }
    const ExpressionParameterIndex index{static_cast<std::uint8_t>(position)};
    push_operand(ExpressionInstruction::parameter(index), ValueUnit::none, false);
    return true;
}

void ValuePass::push_operand(ExpressionInstruction instruction, ValueUnit unit, bool constant) {
    Operand operand{};
    operand.first = static_cast<std::uint32_t>(unfolded_.size());
    operand.count = 1U;
    operand.unit = unit;
    operand.constant = constant;
    unfolded_.push_back(instruction);
    operands_.push_back(operand);
}

// The top `operand_count` operands are the operation's, adjacent at the end
// of the program in order; the operation joins them into one.
void ValuePass::apply(ExpressionOperation operation, std::size_t operand_count, ValueUnit unit) {
    const std::size_t base = operands_.size() - operand_count;
    bool constant = true;
    for (std::size_t operand = base; operand < operands_.size(); ++operand) {
        if (!operands_[operand].constant) constant = false;
    }
    Operand joined{};
    joined.first = operands_[base].first;
    joined.unit = unit;
    joined.constant = constant;
    unfolded_.push_back(ExpressionInstruction::apply(operation));
    joined.count = static_cast<std::uint32_t>(unfolded_.size() - joined.first);
    operands_.resize(base);
    if (constant && context_.fold_constants) fold(joined);
    operands_.push_back(joined);
}

// Replaces a constant operand's instructions with their value, computed by
// the engine's evaluator. An operation that gives the fallback (a division
// by zero, a value that is not finite) is left unfolded, so evaluating the
// program meets it as the unfolded program would.
void ValuePass::fold(Operand& operand) {
    const std::span<const ExpressionInstruction> program(unfolded_.data() + operand.first, operand.count);
    const ExpressionLimits limits{};
    const ExpressionCompilation compilation = folder_.compile(program, 0.0, limits);
    if (compilation.defect != ExpressionDefect::none) return;
    const ExpressionEnvironment environment{};
    const ExpressionResult result = folder_.evaluate(environment);
    if (result.fallback != ExpressionFallback::none) return;
    unfolded_.resize(operand.first);
    unfolded_.push_back(ExpressionInstruction::number(result.value));
    operand.count = 1U;
}

bool ValuePass::finish_expression(std::uint32_t token) {
    const Operand result = operands_.back();
    const std::span<const ExpressionInstruction> program(unfolded_.data() + result.first, result.count);
    ExpressionLimits limits{};
    if (!result.constant) {
        limits.inputs = engine_input_count + context_.published_values;
        limits.parameters = context_.parameters.size();
    }
    const ExpressionCompilation compilation = folder_.compile(program, 0.0, limits);
    const bool over_budget = compilation.defect == ExpressionDefect::over_budget ||
                             compilation.defect == ExpressionDefect::stack_too_deep;
    if (over_budget) {
        report(DiagnosticCode::expression_over_budget, token, expression_defect_name(compilation.defect));
        return false;
    }
    if (compilation.defect != ExpressionDefect::none) {
        report(DiagnosticCode::expression_invalid, token, expression_defect_name(compilation.defect));
        return false;
    }
    ValueComponent component{};
    component.token = token;
    component.unit = result.unit;
    if (result.constant) {
        // A value that reads nothing is a number (PLAN.md section 1.6).
        const ExpressionEnvironment environment{};
        const ExpressionResult evaluated = folder_.evaluate(environment);
        if (evaluated.fallback != ExpressionFallback::none) {
            report(DiagnosticCode::value_out_of_range, token, expression_fallback_name(evaluated.fallback));
            return false;
        }
        component.kind = ValueComponentKind::number;
        component.number = evaluated.value;
        push_component(component);
        return true;
    }
    ValueProgram compiled{};
    compiled.first = static_cast<std::uint32_t>(value_.instructions.size());
    compiled.count = result.count;
    compiled.unit = result.unit;
    value_.instructions.insert(value_.instructions.end(), program.begin(), program.end());
    component.kind = ValueComponentKind::expression;
    component.program = static_cast<std::uint32_t>(value_.programs.size());
    value_.programs.push_back(compiled);
    push_component(component);
    return true;
}

// -- tokens ----------------------------------------------------------------------------

bool ValuePass::is_sheet(std::size_t at) const noexcept {
    return stream_[at].source == ValueTokenSource::sheet;
}

TokenKind ValuePass::kind_of(std::size_t at) const noexcept {
    return tokens_[stream_[at].token].kind;
}

std::string_view ValuePass::text_of(std::size_t at) const noexcept {
    const Token& token = tokens_[stream_[at].token];
    const std::string_view text = source_.text.substr(token.offset.value, token.length.value);
    return text;
}

// A function token's name, without its '('.
std::string_view ValuePass::function_name(std::size_t at) const noexcept {
    const std::string_view written = text_of(at);
    const std::string_view name = written.substr(0U, written.size() - 1U);
    return name;
}

// CSS's rule for `+` and `-` in an expression: whitespace on both sides, so
// `1px -2px` is two numbers, not a subtraction.
bool ValuePass::spaced(std::uint32_t token) const noexcept {
    if (token == 0U || token + 1U >= tokens_.size()) return false;
    const bool before = tokens_[token - 1U].kind == TokenKind::whitespace;
    const bool after = tokens_[token + 1U].kind == TokenKind::whitespace;
    return before && after;
}

void ValuePass::report(DiagnosticCode code, std::uint32_t token, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(tokens_[token].offset);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

} // namespace

std::string_view value_unit_name(ValueUnit unit) noexcept {
    switch (unit) {
    case ValueUnit::none: return "number";
    case ValueUnit::px: return "px";
    case ValueUnit::percent: return "percent";
    case ValueUnit::em: return "em";
    case ValueUnit::seconds: return "seconds";
    case ValueUnit::degrees: return "degrees";
    case ValueUnit::dppx: return "dppx";
    }
    return "unknown";
}

std::optional<Color> named_colour(std::string_view name) noexcept {
    std::array<char, longest_colour_name> folded{};
    // A name longer than the longest named colour names none.
    if (name.size() > folded.size()) return std::nullopt;
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        const bool upper = character >= 'A' && character <= 'Z';
        folded[index] = upper ? static_cast<char>(character - 'A' + 'a') : character;
    }
    const std::string_view lower(folded.data(), name.size());
    if (lower == "transparent") return Color::rgba(0U, 0U, 0U, 0U);
    const std::array<NamedColour, named_colours.size()>::const_iterator found =
        std::lower_bound(named_colours.begin(), named_colours.end(), lower, NamedColourBefore{});
    if (found == named_colours.end() || (*found).name != lower) return std::nullopt;
    const Color colour = Color::rgba((*found).red, (*found).green, (*found).blue);
    return colour;
}

std::span<const ExpressionInstruction> ThemeValue::program_instructions(ValueProgram program) const& noexcept {
    const std::span<const ExpressionInstruction> found(instructions.data() + program.first, program.count);
    return found;
}

ValueOutcome ValueReader::read(const ValueInput& input, ThemeValue& value, ThemeDiagnosticList& diagnostics) {
    value.components.clear();
    value.programs.clear();
    value.instructions.clear();
    value.roles_read = 0U;
    value.components.reserve(input.substituted.size());
    ValuePass pass(input, value, diagnostics, unfolded_, operands_);
    const ValueOutcome outcome = pass.run();
    if (outcome == ValueOutcome::refused) {
        value.components.clear();
        value.programs.clear();
        value.instructions.clear();
    }
    return outcome;
}

} // namespace gui_forms::theme_language
