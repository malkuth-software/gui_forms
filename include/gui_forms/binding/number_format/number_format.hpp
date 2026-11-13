#pragma once

// Invariant number formatting under .NET's standard numeric format strings
// "F", "N", "P", "C", "E" and "G", each with an optional precision of 0 to
// 99 ("F2", "N0", "E10"). It is the one number formatter of Binding's
// format_string and DataGridView's cell style format.
//
// The text is the invariant culture's on every host, whatever the process
// locale: "." before the fraction, "," between groups of three digits, "-"
// before a negative value, the currency sign U+00A4, and "NaN", "Infinity"
// and "-Infinity" for values that are not finite (whatever the format).
//
// Rounding is that of std::to_chars: the text is the representable result
// nearest to the exact binary value, and an exact tie goes to the even
// digit, as .NET Core 3.0 and later round. The sign follows the value's
// sign bit, so -0.0 and a negative value that rounds to zero keep their "-"
// ("-0.00"), as .NET Core 3.0 and later write them. An integer argument is
// formatted from its exact digits; where a precision drops digits of an
// integer ("E2", "G3"), a tie rounds away from zero, as .NET rounds
// integers.

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

// The format a format string's letter names; the letter's case chooses the
// exponent letter's case ("e" or "E") and nothing else.
enum class NumberFormatKind : std::uint8_t {
    // "F": `precision` digits after the point (default 2).
    fixed = 0,
    // "N": as fixed, with the integer digits in groups of three.
    number = 1,
    // "P": the value times 100, grouped, then " %"; negatives "-n %".
    percent = 2,
    // "C": the currency sign U+00A4, then the grouped number; negatives in
    // parentheses ("(¤1,234.50)"), the invariant culture's pattern.
    currency = 3,
    // "E": one digit, the point, `precision` digits (default 6), then the
    // exponent letter, its sign and at least three digits ("1.234500E+003").
    exponential = 4,
    // "G": the shorter of the fixed and scientific forms with `precision`
    // significant digits (see NumberFormat::precision), trailing zeros
    // removed; the scientific form's exponent has at least two digits
    // ("1.5E+20", "1E-05").
    general = 5,
};

// The largest precision a format string can state, as in .NET Framework.
constexpr std::uint32_t number_format_maximum_precision = 99U;

struct GUI_FORMS_API NumberFormat final {
    NumberFormatKind kind{NumberFormatKind::general};
    // Empty for the kind's default: 2 for F, N, P and C; 6 for E. For G an
    // empty or zero precision gives a double's shortest text that parses
    // back to the same value (scientific from an exponent of 15 or below
    // -5) and an integer's every digit.
    std::optional<std::uint32_t> precision;
    // The exponent letter's case: "E" for an upper-case format letter.
    bool upper_case{true};

    friend bool operator==(const NumberFormat& left, const NumberFormat& right) noexcept {
        return left.kind == right.kind && left.precision == right.precision &&
               left.upper_case == right.upper_case;
    }
};

// The format a format string names: one of the letters F, N, P, C, E and G
// in either case, then at most two decimal digits. Empty for any other
// text, the empty string included (an empty format string means "no
// format" to its callers, not a format).
[[nodiscard]] GUI_FORMS_API std::optional<NumberFormat> parse_number_format(std::string_view text) noexcept;

// Appends `value` under `format` to `output`; nothing else of `output`
// changes.
GUI_FORMS_API void append_formatted_number(std::string& output, double value, const NumberFormat& format);
GUI_FORMS_API void append_formatted_number(std::string& output, std::int64_t value, const NumberFormat& format);
GUI_FORMS_API void append_formatted_number(std::string& output, std::uint64_t value, const NumberFormat& format);

// The plain number in `text` written under `format`: the group separators,
// the currency sign and a percent sign removed, parentheses turned into a
// leading "-", and a percent value's exponent lowered by two ("12.5 %"
// gives "12.5e-2"), so that a number parser of plain decimal text (as
// Binding's and DataGridView's value conversions are) reads the value the
// text shows. Spaces at either end are ignored. Empty for text that is not
// a number under the format; a text this returns can still be refused by
// the parser (an integer kind refuses a fraction).
[[nodiscard]] GUI_FORMS_API std::optional<std::string> plain_number_text(std::string_view text, const NumberFormat& format);

} // namespace gui_forms
