#pragma once

// A ValueBar's typed and shown text (the node engine and value bar study,
// section 3.5): a bounded arithmetic language with unit suffixes, compiled
// to the expression engine's postfix program (EngineExpression) and
// evaluated there, and the formatted value. Internal to the controls
// library; the tests include it.
//
// The grammar, in order of binding:
//   expression := term { ("+" | "-") term | unit-number }
//   term       := signed { ("*" | "/") signed }
//   signed     := ("-" | "+") signed | power
//   power      := primary [ "^" signed ]
//   primary    := number [ suffix ] | "pi" | "(" expression ")"
//               | ("min" | "max") "(" expression { "," expression } ")"
//               | "sqrt" "(" expression ")"
// A number with a suffix directly after another one is added to it
// ("1m 50cm"). Spaces may separate any two tokens.
//
// Units: a suffix multiplies its number by the suffix's factor (base units
// per typed unit). A text that names no unit at all is in the display unit:
// its result is multiplied by the display factor ("50" in percent is 0.5).
// A text that names one takes its unsuffixed numbers as plain numbers.
//
// The expression engine has no power and no square root, so `^` and sqrt
// are folded while parsing: their operands are evaluated by the engine, the
// power by repeated multiplication (a whole exponent, at most
// maximum_value_text_exponent multiplications) and the root by std::sqrt
// (correctly rounded under IEEE 754, so the same on every host), and the
// result stands in the program as a constant. Everything else is the
// engine's: + - * / negation, min and max.
//
// Bounds: maximum_value_text_bytes of text, maximum_value_text_depth of
// nesting (the parser's recursion depth), maximum_expression_operations
// instructions. Nothing is allocated while parsing.

#include "gui_forms/controls/range_control/value_bar/value_bar_values/value_bar_values.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::value_text {

// A parse's outcome: the value in the unit's base, or the defect and the
// byte offset in the text where it was found.
struct ValueTextResult final {
    double value{};
    ValueTextDefect defect{ValueTextDefect::none};
    std::size_t position{};
};

// Parses and evaluates `text` in `unit`.
[[nodiscard]] ValueTextResult parse_value_text(std::string_view text, ValueUnit unit) noexcept;

// Replaces `out` with `value` in `unit`'s display unit: fixed, `precision`
// decimals (clamped to 0..maximum_value_bar_precision), never "-0", then the
// display suffix. Locale-independent (std::to_chars).
void format_value_text(double value, ValueUnit unit, std::int32_t precision, std::string& out);

} // namespace gui_forms::value_text
