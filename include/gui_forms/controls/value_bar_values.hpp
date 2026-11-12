#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// ValueBar's vocabulary (the node engine and value bar study, section 3),
// apart from the class so its capabilities can name it.

// How a ValueBar presents its value (study 3.1): `fill`, the slider form,
// shows the value's proportion of the soft range as a fill under its text;
// `field`, the number form, shows no fill and step arrows at both ends
// while hovered.
enum class ValueBarStyle : std::uint8_t {
    fill,
    field,
};

// The unit a ValueBar's value is shown and typed in (study 3.3; owner
// question 11). The value is always held in the unit's base:
// - none: a plain number;
// - percent: a fraction, 0..1 shown as 0..100 %;
// - length: metres, shown in metres, typed in mm, cm, m, km, in, ft, yd or
//   mi;
// - angle: radians, shown in degrees, typed in deg (or °) or rad;
// - time: seconds, shown in seconds, typed in ms, s, min or h.
enum class ValueUnit : std::uint8_t {
    none,
    percent,
    length,
    angle,
    time,
};

// One suffix a unit may be typed with: one typed `suffix` is `factor` base
// units.
struct UnitSuffix final {
    std::string_view suffix{};
    double factor{1.0};
};

// A unit's formatting and parsing (study 3.2): the suffix its values are
// shown with (`display`, whose factor also scales a typed expression that
// names no unit at all), every suffix it accepts, and whether a space
// separates the number from the shown suffix ("1.50 m", but "50%" and
// "90°"). The views are static storage.
struct UnitFormat final {
    ValueUnit unit{ValueUnit::none};
    UnitSuffix display{};
    std::span<const UnitSuffix> suffixes{};
    bool space_before_suffix{};
};

// The built-in format of `unit` (static storage; any thread may read it).
[[nodiscard]] const UnitFormat& unit_format(ValueUnit unit) noexcept;

// Why a typed value was refused (study 3.5). The editor stays open and the
// bar's validation part shows the defect.
enum class ValueTextDefect : std::uint8_t {
    none,
    // Nothing but spaces.
    empty,
    // More than maximum_value_text_bytes bytes.
    too_long,
    // A character the grammar has no use for.
    unexpected_character,
    // A suffix the bar's unit does not accept.
    unknown_unit,
    // A name other than pi, sqrt, min and max.
    unknown_name,
    // An operator or function without its operand, or a missing `)`.
    missing_operand,
    // A `)` without its `(`, or text after the expression.
    unexpected_token,
    // Deeper than maximum_value_text_depth parentheses or signs.
    too_deep,
    // A power whose exponent is not a whole number within
    // ±maximum_value_text_exponent, or sqrt of a negative number.
    bad_operand,
    // More than the expression engine's operation budget
    // (maximum_expression_operations instructions).
    over_budget,
    // A division by zero, or a result that is not finite.
    not_finite,
};

[[nodiscard]] std::string_view value_text_defect_name(ValueTextDefect defect) noexcept;

// The bounds of typed text (study 3.5: bounded in length and steps).
inline constexpr std::size_t maximum_value_text_bytes = 256U;
inline constexpr std::size_t maximum_value_text_depth = 16U;
inline constexpr std::int32_t maximum_value_text_exponent = 64;
// The most decimals a ValueBar shows.
inline constexpr std::int32_t maximum_value_bar_precision = 12;

} // namespace gui_forms
