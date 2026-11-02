#include "gui_forms/binding/number_format/number_format.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace gui_forms {
namespace {

// A finite value as its significant decimal digits and a point position:
// the value is 0.D1D2D3... times 10^point, so digits "12345" with point 3
// are 123.45 and digits "12" with point -2 are 0.0012. The digits have no
// leading or trailing zero; zero has no digits and point 0.
struct DecimalDigits final {
    bool negative{false};
    std::string digits;
    std::int32_t point{0};
};

constexpr std::uint32_t default_fraction_digits = 2U;
constexpr std::uint32_t default_exponential_digits = 6U;
// The exponent from which G writes a double without a precision in the
// scientific form (.NET writes 1E+15 but 100000000000000).
constexpr std::uint32_t general_double_threshold = 15U;
// G without a precision writes every digit of an integer: no int64 or
// uint64 has an exponent this large.
constexpr std::uint32_t general_integer_threshold = 20U;
// The longest text std::to_chars writes here: a double's 309 integer
// digits, a sign, a point and 101 fraction digits (P99 asks for two more
// than its precision), or a scientific form with 99 fraction digits.
constexpr std::size_t chars_capacity = 512U;
static_assert(chars_capacity != 0U, "the conversion buffers are never empty");
// U+00A4 CURRENCY SIGN, the invariant culture's currency symbol, in UTF-8.
constexpr std::string_view currency_sign = "\xC2\xA4";
constexpr std::string_view percent_suffix = " %";

bool is_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

// Removes the leading zeros (each lowers the point) and the trailing ones.
void normalize(DecimalDigits& value) {
    std::size_t leading = 0U;
    while (leading < value.digits.size() && value.digits[leading] == '0') {
        ++leading;
    }
    if (leading == value.digits.size()) {
        value.digits.clear();
        value.point = 0;
        return;
    }
    value.digits.erase(0U, leading);
    value.point -= static_cast<std::int32_t>(leading);
    std::size_t kept = value.digits.size();
    while (kept > 0U && value.digits[kept - 1U] == '0') {
        --kept;
    }
    value.digits.resize(kept);
}

// Reads std::to_chars' fixed or scientific text ("-123.4500", "1.234e+05").
DecimalDigits digits_from_chars(std::string_view text) {
    DecimalDigits result{};
    std::size_t index = 0U;
    if (index < text.size() && text[index] == '-') {
        result.negative = true;
        ++index;
    }
    std::int32_t integer_digits = 0;
    std::int32_t exponent = 0;
    bool after_point = false;
    while (index < text.size()) {
        const char character = text[index];
        if (character == 'e') {
            const bool exponent_negative = index + 1U < text.size() && text[index + 1U] == '-';
            const char* const first = text.data() + index + 2U;
            const char* const last = text.data() + text.size();
            const std::from_chars_result parsed = std::from_chars(first, last, exponent);
            if (parsed.ec != std::errc{}) {
                throw std::logic_error("std::to_chars wrote an exponent that does not parse");
            }
            if (exponent_negative) exponent = -exponent;
            break;
        }
        if (character == '.') {
            after_point = true;
        } else {
            result.digits.push_back(character);
            if (!after_point) ++integer_digits;
        }
        ++index;
    }
    result.point = integer_digits + exponent;
    normalize(result);
    return result;
}

DecimalDigits digits_from_magnitude(bool negative, std::uint64_t magnitude) {
    // 20 digits hold the largest uint64.
    std::array<char, 20> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude);
    DecimalDigits result{};
    result.negative = negative;
    result.digits.assign(buffer.data(), written.ptr);
    result.point = static_cast<std::int32_t>(result.digits.size());
    normalize(result);
    return result;
}

// Keeps `count` significant digits (at least one), a dropped half rounding
// away from zero; the rounding of an integer's exact digits.
void round_away_from_zero(DecimalDigits& value, std::size_t count) {
    if (value.digits.size() <= count) return;
    const bool up = value.digits[count] >= '5';
    value.digits.resize(count);
    if (up) {
        std::size_t index = count;
        bool carry = true;
        while (carry && index > 0U) {
            --index;
            if (value.digits[index] == '9') {
                value.digits[index] = '0';
            } else {
                ++value.digits[index];
                carry = false;
            }
        }
        if (carry) {
            // Every kept digit was 9: the value is the next power of ten.
            value.digits.insert(value.digits.begin(), '1');
            ++value.point;
        }
    }
    normalize(value);
}

// The integer digits, in groups of three when `grouped`, then `fraction`
// digits after a point (none, and no point, for zero).
void append_fixed(std::string& output, const DecimalDigits& value, std::uint32_t fraction, bool grouped) {
    const std::int64_t size = static_cast<std::int64_t>(value.digits.size());
    const std::int64_t integer_count = value.point > 0 ? value.point : 0;
    if (integer_count == 0) {
        output.push_back('0');
    }
    for (std::int64_t index = 0; index < integer_count; ++index) {
        if (grouped && index > 0 && (integer_count - index) % 3 == 0) {
            output.push_back(',');
        }
        const char digit = index < size ? value.digits[static_cast<std::size_t>(index)] : '0';
        output.push_back(digit);
    }
    if (fraction == 0U) return;
    output.push_back('.');
    for (std::int64_t offset = 0; offset < static_cast<std::int64_t>(fraction); ++offset) {
        const std::int64_t index = value.point + offset;
        const bool present = index >= 0 && index < size;
        const char digit = present ? value.digits[static_cast<std::size_t>(index)] : '0';
        output.push_back(digit);
    }
}

// One digit, `fraction` more after a point (no point for none), the
// exponent letter, its sign and at least `exponent_digits` digits.
void append_scientific(std::string& output, const DecimalDigits& value, std::uint32_t fraction, bool upper_case,
                       std::uint32_t exponent_digits) {
    const std::size_t size = value.digits.size();
    output.push_back(size == 0U ? '0' : value.digits[0]);
    if (fraction > 0U) {
        output.push_back('.');
        for (std::size_t index = 1U; index <= fraction; ++index) {
            const char digit = index < size ? value.digits[index] : '0';
            output.push_back(digit);
        }
    }
    const std::int32_t exponent = size == 0U ? 0 : value.point - 1;
    output.push_back(upper_case ? 'E' : 'e');
    output.push_back(exponent < 0 ? '-' : '+');
    const std::uint32_t magnitude = static_cast<std::uint32_t>(exponent < 0 ? -exponent : exponent);
    std::array<char, 10> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude);
    const std::size_t written_count = static_cast<std::size_t>(written.ptr - buffer.data());
    if (written_count < exponent_digits) {
        output.append(exponent_digits - written_count, '0');
    }
    output.append(buffer.data(), written_count);
}

// G: the fixed form when the exponent is above -5 and below `threshold`,
// else the scientific form; either without trailing zeros.
void append_general(std::string& output, const DecimalDigits& value, bool upper_case, std::uint32_t threshold) {
    const std::int32_t size = static_cast<std::int32_t>(value.digits.size());
    const std::int32_t exponent = size == 0 ? 0 : value.point - 1;
    if (exponent > -5 && exponent < static_cast<std::int32_t>(threshold)) {
        const std::int32_t fraction = size > value.point ? size - value.point : 0;
        append_fixed(output, value, static_cast<std::uint32_t>(fraction), false);
        return;
    }
    const std::uint32_t fraction = size > 1 ? static_cast<std::uint32_t>(size - 1) : 0U;
    append_scientific(output, value, fraction, upper_case, 2U);
}

// The prepared digits under the format's sign pattern. `fraction` is the
// digits after the point of F, N, P and C and the precision of E; G uses
// `general_threshold`.
void append_digits(std::string& output, const DecimalDigits& value, const NumberFormat& format,
                   std::uint32_t fraction, std::uint32_t general_threshold) {
    const NumberFormatKind kind = format.kind;
    if (kind == NumberFormatKind::currency) {
        if (value.negative) output.push_back('(');
        output.append(currency_sign);
        append_fixed(output, value, fraction, true);
        if (value.negative) output.push_back(')');
        return;
    }
    if (value.negative) output.push_back('-');
    if (kind == NumberFormatKind::fixed) {
        append_fixed(output, value, fraction, false);
    } else if (kind == NumberFormatKind::number) {
        append_fixed(output, value, fraction, true);
    } else if (kind == NumberFormatKind::percent) {
        append_fixed(output, value, fraction, true);
        output.append(percent_suffix);
    } else if (kind == NumberFormatKind::exponential) {
        append_scientific(output, value, fraction, format.upper_case, 3U);
    } else {
        append_general(output, value, format.upper_case, general_threshold);
    }
}

std::uint32_t fraction_digits(const NumberFormat& format) noexcept {
    if (format.precision) return *format.precision;
    if (format.kind == NumberFormatKind::exponential) return default_exponential_digits;
    return default_fraction_digits;
}

// G's significant digits; zero for the shortest form (or every digit).
std::uint32_t general_digits(const NumberFormat& format) noexcept {
    const std::uint32_t result = format.precision ? *format.precision : 0U;
    return result;
}

bool uses_fraction_digits(NumberFormatKind kind) noexcept {
    return kind == NumberFormatKind::fixed || kind == NumberFormatKind::number ||
           kind == NumberFormatKind::currency;
}

void append_integer(std::string& output, bool negative, std::uint64_t magnitude, const NumberFormat& format) {
    DecimalDigits value = digits_from_magnitude(negative, magnitude);
    const NumberFormatKind kind = format.kind;
    if (kind == NumberFormatKind::general) {
        const std::uint32_t digits = general_digits(format);
        if (digits == 0U) {
            append_digits(output, value, format, 0U, general_integer_threshold);
            return;
        }
        round_away_from_zero(value, digits);
        append_digits(output, value, format, 0U, digits);
        return;
    }
    const std::uint32_t fraction = fraction_digits(format);
    if (kind == NumberFormatKind::exponential) {
        round_away_from_zero(value, static_cast<std::size_t>(fraction) + 1U);
    } else if (kind == NumberFormatKind::percent && !value.digits.empty()) {
        value.point += 2;
    }
    append_digits(output, value, format, fraction, 0U);
}

// Removes ASCII spaces at both ends.
std::string_view trimmed_spaces(std::string_view text) noexcept {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1U);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1U);
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0U, prefix.size()) == prefix;
}

} // namespace

std::optional<NumberFormat> parse_number_format(std::string_view text) noexcept {
    if (text.empty() || text.size() > 3U) return std::nullopt;
    NumberFormat result{};
    const char letter = text.front();
    result.upper_case = letter >= 'A' && letter <= 'Z';
    const char upper = result.upper_case ? letter : static_cast<char>(letter - 'a' + 'A');
    if (upper == 'F') {
        result.kind = NumberFormatKind::fixed;
    } else if (upper == 'N') {
        result.kind = NumberFormatKind::number;
    } else if (upper == 'P') {
        result.kind = NumberFormatKind::percent;
    } else if (upper == 'C') {
        result.kind = NumberFormatKind::currency;
    } else if (upper == 'E') {
        result.kind = NumberFormatKind::exponential;
    } else if (upper == 'G') {
        result.kind = NumberFormatKind::general;
    } else {
        return std::nullopt;
    }
    if (text.size() == 1U) return result;
    std::uint32_t precision = 0U;
    for (std::size_t index = 1U; index < text.size(); ++index) {
        if (!is_digit(text[index])) return std::nullopt;
        precision = precision * 10U + static_cast<std::uint32_t>(text[index] - '0');
    }
    // Two digits at most, so the precision is at most 99.
    result.precision = precision;
    return result;
}

void append_formatted_number(std::string& output, double value, const NumberFormat& format) {
    if (std::isnan(value)) {
        output.append("NaN");
        return;
    }
    if (std::isinf(value)) {
        output.append(value < 0.0 ? "-Infinity" : "Infinity");
        return;
    }
    std::array<char, chars_capacity> buffer{};
    char* const first = buffer.data();
    char* const last = first + chars_capacity;
    const NumberFormatKind kind = format.kind;
    std::to_chars_result written{};
    std::uint32_t fraction = 0U;
    std::uint32_t threshold = 0U;
    std::int32_t point_shift = 0;
    if (uses_fraction_digits(kind)) {
        fraction = fraction_digits(format);
        written = std::to_chars(first, last, value, std::chars_format::fixed, static_cast<int>(fraction));
    } else if (kind == NumberFormatKind::percent) {
        // Rounding the value at two more fraction digits, then moving the
        // point, rounds the exact hundredfold value (a multiplication by
        // 100 would round first).
        fraction = fraction_digits(format);
        written = std::to_chars(first, last, value, std::chars_format::fixed, static_cast<int>(fraction + 2U));
        point_shift = 2;
    } else if (kind == NumberFormatKind::exponential) {
        fraction = fraction_digits(format);
        written = std::to_chars(first, last, value, std::chars_format::scientific, static_cast<int>(fraction));
    } else {
        const std::uint32_t digits = general_digits(format);
        if (digits == 0U) {
            threshold = general_double_threshold;
            written = std::to_chars(first, last, value, std::chars_format::scientific);
        } else {
            threshold = digits;
            written = std::to_chars(first, last, value, std::chars_format::scientific, static_cast<int>(digits - 1U));
        }
    }
    if (written.ec != std::errc{}) {
        throw std::logic_error("the number formatter's buffer is shorter than its longest text");
    }
    DecimalDigits digits = digits_from_chars(std::string_view(first, static_cast<std::size_t>(written.ptr - first)));
    if (!digits.digits.empty()) digits.point += point_shift;
    append_digits(output, digits, format, fraction, threshold);
}

void append_formatted_number(std::string& output, std::int64_t value, const NumberFormat& format) {
    const bool negative = value < 0;
    // The magnitude in unsigned arithmetic, which also holds -INT64_MIN.
    const std::uint64_t magnitude = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                             : static_cast<std::uint64_t>(value);
    append_integer(output, negative, magnitude, format);
}

void append_formatted_number(std::string& output, std::uint64_t value, const NumberFormat& format) {
    append_integer(output, false, value, format);
}

std::optional<std::string> plain_number_text(std::string_view text, const NumberFormat& format) {
    std::string_view rest = trimmed_spaces(text);
    if (rest == "NaN") return std::string("nan");
    if (rest == "Infinity") return std::string("inf");
    if (rest == "-Infinity") return std::string("-inf");
    const NumberFormatKind kind = format.kind;
    bool negative = false;
    if (kind == NumberFormatKind::currency && rest.size() >= 2U && rest.front() == '(' && rest.back() == ')') {
        negative = true;
        rest = trimmed_spaces(rest.substr(1U, rest.size() - 2U));
    }
    if (!rest.empty() && rest.front() == '-') {
        if (negative) return std::nullopt;
        negative = true;
        rest.remove_prefix(1U);
    }
    if (kind == NumberFormatKind::currency && starts_with(rest, currency_sign)) {
        rest.remove_prefix(currency_sign.size());
    }
    if (kind == NumberFormatKind::percent && !rest.empty() && rest.back() == '%') {
        rest.remove_suffix(1U);
        rest = trimmed_spaces(rest);
    }
    if (rest.empty() || !(is_digit(rest.front()) || rest.front() == '.')) return std::nullopt;
    std::string result{};
    result.reserve(rest.size() + 4U);
    if (negative) result.push_back('-');
    bool in_integer_part = true;
    bool has_exponent = false;
    bool has_digit = false;
    for (std::size_t index = 0U; index < rest.size(); ++index) {
        const char character = rest[index];
        if (character == ',') {
            // A group separator stands between two integer digits.
            const bool between_digits = in_integer_part && index > 0U && index + 1U < rest.size() &&
                                        is_digit(rest[index - 1U]) && is_digit(rest[index + 1U]);
            if (!between_digits) return std::nullopt;
            continue;
        }
        if (character == '.') in_integer_part = false;
        if (character == 'e' || character == 'E') {
            in_integer_part = false;
            has_exponent = true;
        }
        if (is_digit(character) && !has_exponent) has_digit = true;
        result.push_back(character);
    }
    if (!has_digit) return std::nullopt;
    if (kind == NumberFormatKind::percent) {
        // The text is the value times 100; a percent text has no exponent.
        if (has_exponent) return std::nullopt;
        result.append("e-2");
    }
    return result;
}

} // namespace gui_forms
