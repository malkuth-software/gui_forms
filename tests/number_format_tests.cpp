// The invariant number formatter (gui_forms/binding/number_format): the
// format strings it reads, each format over doubles (ties to even on the
// exact binary value, the percent form rounded after the point moves,
// signed zero, values that are not finite, the largest precisions) and over
// integers (exact digits, ties away from zero where a precision drops
// digits), and plain_number_text, which turns the text a format writes back
// into plain decimal text, with the round trip through std::from_chars.

#include "gui_forms/binding/number_format/number_format.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using gui_forms::NumberFormat;
using gui_forms::NumberFormatKind;
using gui_forms::append_formatted_number;
using gui_forms::parse_number_format;
using gui_forms::plain_number_text;

int failures = 0;

void fail(std::string_view what, std::string_view format, std::string_view expected, std::string_view actual) {
    ++failures;
    std::printf("number_format: %.*s under \"%.*s\": expected \"%.*s\", got \"%.*s\"\n",
                static_cast<int>(what.size()), what.data(), static_cast<int>(format.size()), format.data(),
                static_cast<int>(expected.size()), expected.data(), static_cast<int>(actual.size()),
                actual.data());
}

NumberFormat format_of(std::string_view text) {
    const std::optional<NumberFormat> parsed = parse_number_format(text);
    if (!parsed) {
        fail("a format string", text, "a format", "none");
        return NumberFormat{};
    }
    return *parsed;
}

struct DoubleCase final {
    std::string_view format;
    double value{};
    std::string_view expected;
};

struct SignedCase final {
    std::string_view format;
    std::int64_t value{};
    std::string_view expected;
};

struct UnsignedCase final {
    std::string_view format;
    std::uint64_t value{};
    std::string_view expected;
};

struct PlainCase final {
    std::string_view format;
    std::string_view text;
    // Empty for a text that is not a number under the format.
    std::string_view expected;
};

constexpr double infinity = std::numeric_limits<double>::infinity();

constexpr DoubleCase double_cases[] = {
    // F: digits after the point; exact ties go to the even digit.
    {"F2", 1234.5, "1234.50"},
    {"F", 1234.5, "1234.50"},
    {"f1", 0.25, "0.2"},
    {"F0", 2.5, "2"},
    {"F0", 3.5, "4"},
    {"F0", -2.5, "-2"},
    {"F2", 0.125, "0.12"},
    {"F2", 0.375, "0.38"},
    // 1.005 is 1.00499999999999989... in binary.
    {"F2", 1.005, "1.00"},
    {"F12", 1.0 / 3.0, "0.333333333333"},
    {"F20", 0.1, "0.10000000000000000555"},
    // The sign follows the sign bit.
    {"F3", -0.0, "-0.000"},
    {"F2", -0.001, "-0.00"},
    // N: groups of three.
    {"N2", 1234567.891, "1,234,567.89"},
    {"N0", 999.5, "1,000"},
    {"N2", -1234.5, "-1,234.50"},
    {"N", 0.5, "0.50"},
    {"N1", 123.45, "123.5"},
    {"N0", 100.0, "100"},
    {"N0", 1000.0, "1,000"},
    // P: the exact hundredfold value, grouped, then " %".
    {"P", 0.1234, "12.34 %"},
    {"P1", 0.5, "50.0 %"},
    {"P0", -0.256, "-26 %"},
    {"P2", 0.29, "29.00 %"},
    {"P2", 12.345, "1,234.50 %"},
    {"p0", 0.0, "0 %"},
    // C: the currency sign; negatives in parentheses.
    {"C", 1234.5, "\xC2\xA4" "1,234.50"},
    {"C", -1234.5, "(\xC2\xA4" "1,234.50)"},
    {"C0", 0.5, "\xC2\xA4" "0"},
    {"C3", 1.0, "\xC2\xA4" "1.000"},
    // E: at least three exponent digits; the letter's case.
    {"E", 1234.5678, "1.234568E+003"},
    {"e2", 0.000123, "1.23e-004"},
    {"E0", 0.0, "0E+000"},
    {"E3", -1e300, "-1.000E+300"},
    {"E1", 0.25, "2.5E-001"},
    // G: the shortest text; scientific from an exponent of 15 or below -5.
    {"G", 1234.5, "1234.5"},
    {"G", 1e15, "1E+15"},
    {"G", 1e14, "100000000000000"},
    {"G", 0.0001, "0.0001"},
    {"G", 0.00001, "1E-05"},
    {"G", 0.1, "0.1"},
    {"g", 1.5e20, "1.5e+20"},
    {"G", 1.0 / 3.0, "0.3333333333333333"},
    {"G", -0.0, "-0"},
    {"G0", 2.0, "2"},
    // G with a precision: that many significant digits.
    {"G3", 1234.5, "1.23E+03"},
    {"G3", 0.000123456, "0.000123"},
    {"G5", 123.45, "123.45"},
    {"G17", 0.1, "0.10000000000000001"},
    {"G2", 100.0, "1E+02"},
    {"G3", 100.0, "100"},
    // Values that are not finite, whatever the format.
    {"F2", std::numeric_limits<double>::quiet_NaN(), "NaN"},
    {"C", infinity, "Infinity"},
    {"N", -infinity, "-Infinity"},
};

constexpr SignedCase signed_cases[] = {
    {"F2", 1234, "1234.00"},
    {"N0", 1234567, "1,234,567"},
    {"N", -1234567, "-1,234,567.00"},
    {"P", 1, "100.00 %"},
    {"P0", 0, "0 %"},
    {"C", -5, "(\xC2\xA4" "5.00)"},
    {"E2", 125, "1.25E+002"},
    // A dropped half rounds away from zero for an integer.
    {"E1", 125, "1.3E+002"},
    {"E1", -125, "-1.3E+002"},
    {"E0", 25, "3E+001"},
    {"E2", 0, "0.00E+000"},
    {"G", std::numeric_limits<std::int64_t>::min(), "-9223372036854775808"},
    {"G", 0, "0"},
    {"G3", 12345, "1.23E+04"},
    {"G2", 99, "99"},
    {"G1", 99, "1E+02"},
    {"G1", 95, "1E+02"},
    {"G1", 94, "9E+01"},
};

constexpr UnsignedCase unsigned_cases[] = {
    {"G", std::numeric_limits<std::uint64_t>::max(), "18446744073709551615"},
    {"N0", std::numeric_limits<std::uint64_t>::max(), "18,446,744,073,709,551,615"},
    {"F1", 7U, "7.0"},
    {"e3", 1000U, "1.000e+003"},
};

constexpr PlainCase plain_cases[] = {
    {"N2", "1,234.50", "1234.50"},
    {"N0", "1,234,567", "1234567"},
    {"N2", "-1,234.50", "-1234.50"},
    {"C", "(\xC2\xA4" "1,234.50)", "-1234.50"},
    {"C", "\xC2\xA4" "5", "5"},
    {"C", "-\xC2\xA4" "5", "-5"},
    {"C", "5.25", "5.25"},
    {"P", "12.5 %", "12.5e-2"},
    {"P", "12.5%", "12.5e-2"},
    {"P", "-12.5 %", "-12.5e-2"},
    {"P", "40", "40e-2"},
    {"F2", " 3.25 ", "3.25"},
    {"E", "1.5E+003", "1.5E+003"},
    {"G", "NaN", "nan"},
    {"G", "Infinity", "inf"},
    {"G", "-Infinity", "-inf"},
    {"N", ".5", ".5"},
    // Refused: misplaced separators, signs, symbols of another format.
    {"N", "1,,234", ""},
    {"N", ",123", ""},
    {"N", "123,", ""},
    {"N", "1.5,0", ""},
    {"N", "--5", ""},
    {"N", "(5)", ""},
    {"N", "\xC2\xA4" "5", ""},
    {"C", "(-5)", ""},
    {"P", "1e5 %", ""},
    {"F2", "", ""},
    {"F2", "   ", ""},
    {"F2", "abc", ""},
    {"F2", "-", ""},
};

void check_format_strings() {
    struct FormatStringCase final {
        std::string_view text;
        bool accepted{};
        NumberFormatKind kind{NumberFormatKind::general};
        std::optional<std::uint32_t> precision;
        bool upper_case{};
    };
    const FormatStringCase cases[] = {
        {"F2", true, NumberFormatKind::fixed, 2U, true},
        {"f", true, NumberFormatKind::fixed, std::nullopt, false},
        {"N99", true, NumberFormatKind::number, 99U, true},
        {"p0", true, NumberFormatKind::percent, 0U, false},
        {"C", true, NumberFormatKind::currency, std::nullopt, true},
        {"e10", true, NumberFormatKind::exponential, 10U, false},
        {"G0", true, NumberFormatKind::general, 0U, true},
        {"N100", false},
        {"X2", false},
        {"D", false},
        {"", false},
        {"F-1", false},
        {"F 2", false},
        {"2F", false},
        {"FF", false},
    };
    for (const FormatStringCase& entry : cases) {
        const std::optional<NumberFormat> parsed = parse_number_format(entry.text);
        if (parsed.has_value() != entry.accepted) {
            fail("acceptance", entry.text, entry.accepted ? "accepted" : "refused",
                 parsed ? "accepted" : "refused");
            continue;
        }
        if (!parsed) continue;
        const NumberFormat expected{entry.kind, entry.precision, entry.upper_case};
        if (!(*parsed == expected)) fail("the parsed format", entry.text, "its kind, precision and case", "another");
    }
}

void check_doubles() {
    for (const DoubleCase& entry : double_cases) {
        std::string text{};
        append_formatted_number(text, entry.value, format_of(entry.format));
        if (text != entry.expected) fail("a double", entry.format, entry.expected, text);
    }
    // The text is appended: what the string held stays.
    std::string appended = "x=";
    append_formatted_number(appended, 1.5, format_of("F1"));
    if (appended != "x=1.5") fail("an append", "F1", "x=1.5", appended);
    // The largest precisions at the largest magnitudes fit.
    std::string widest{};
    append_formatted_number(widest, 1e308, format_of("P99"));
    const std::string_view widest_view = widest;
    // 311 digits in 104 groups, the point, 99 digits and " %".
    const bool widest_shape = widest.size() == 516U && widest_view.substr(0U, 7U) == "10,000," &&
                              widest_view.substr(widest.size() - 4U) == "00 %";
    if (!widest_shape) fail("1e308", "P99", "516 bytes, \"10,000,\" ... \"00 %\"", widest);
    std::string largest{};
    append_formatted_number(largest, -std::numeric_limits<double>::max(), format_of("F99"));
    // A sign, 309 digits, the point and 99 digits.
    if (largest.size() != 410U) fail("-DBL_MAX", "F99", "410 bytes", largest);
    std::string tiny{};
    append_formatted_number(tiny, std::numeric_limits<double>::denorm_min(), format_of("E99"));
    if (std::string_view(tiny).substr(0U, 8U) != "4.940656" || std::string_view(tiny).substr(tiny.size() - 5U) != "E-324") {
        fail("the smallest subnormal", "E99", "4.940656...E-324", tiny);
    }
}

void check_integers() {
    for (const SignedCase& entry : signed_cases) {
        std::string text{};
        append_formatted_number(text, entry.value, format_of(entry.format));
        if (text != entry.expected) fail("an int64", entry.format, entry.expected, text);
    }
    for (const UnsignedCase& entry : unsigned_cases) {
        std::string text{};
        append_formatted_number(text, entry.value, format_of(entry.format));
        if (text != entry.expected) fail("a uint64", entry.format, entry.expected, text);
    }
}

void check_plain_text() {
    for (const PlainCase& entry : plain_cases) {
        const std::optional<std::string> plain = plain_number_text(entry.text, format_of(entry.format));
        const std::string_view actual = plain ? std::string_view(*plain) : std::string_view("(refused)");
        const std::string_view expected = entry.expected.empty() ? std::string_view("(refused)") : entry.expected;
        if (actual != expected) fail(entry.text, entry.format, expected, actual);
    }
}

// Text a format writes, made plain and parsed, is the value it shows.
void check_round_trips() {
    struct RoundTrip final {
        std::string_view format;
        double value{};
    };
    const RoundTrip cases[] = {
        {"N2", 1234567.25}, {"N0", -42.0}, {"C", -1234.5}, {"C2", 0.75}, {"P1", 0.125},
        {"P0", -3.0}, {"F3", 2.5}, {"E4", 12345.0}, {"G", 1e-7}, {"G", 6.02214076e23},
    };
    for (const RoundTrip& entry : cases) {
        const NumberFormat format = format_of(entry.format);
        std::string text{};
        append_formatted_number(text, entry.value, format);
        const std::optional<std::string> plain = plain_number_text(text, format);
        if (!plain) {
            fail("a round trip", entry.format, "plain text", text);
            continue;
        }
        double parsed = 0.0;
        const char* const last = (*plain).data() + (*plain).size();
        const std::from_chars_result read = std::from_chars((*plain).data(), last, parsed);
        if (read.ec != std::errc{} || read.ptr != last || parsed != entry.value) {
            fail("a round trip", entry.format, text, *plain);
        }
    }
}

} // namespace

int main() {
    check_format_strings();
    check_doubles();
    check_integers();
    check_plain_text();
    check_round_trips();
    if (failures != 0) {
        std::printf("number_format: %d failures\n", failures);
        return 1;
    }
    std::puts("number_format: format strings, doubles, integers, plain text and round trips hold");
    return 0;
}
