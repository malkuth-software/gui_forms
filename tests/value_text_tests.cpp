// A ValueBar's typed and shown text (the node engine and value bar study,
// sections 3.5 and 4, "expression and unit parsing corpus with expected
// values and diagnostics"): arithmetic and precedence, folded powers and
// roots, units and mixed units, the display unit of a text naming none,
// every defect with its position, the bounds, and the formatted value.
// Headless and deterministic.

#include "gui_forms/controls/value_bar_values.hpp"
#include "../src/controls/range_control/value_bar/value_text.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

// The expected values are computed as the parser computes them, without
// fused multiply-add.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using gui_forms::value_text::ValueTextResult;
using gui_forms::value_text::format_value_text;
using gui_forms::value_text::parse_value_text;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

constexpr double pi_value = 3.14159265358979323846;

// One accepted text: its value must be exactly `expected` (every row is
// exact in double, or computed here by the same operations).
struct Accepted final {
    std::string_view text{};
    ValueUnit unit{ValueUnit::none};
    double expected{};
};

// One refused text: its defect and the byte offset reported.
struct Refused final {
    std::string_view text{};
    ValueUnit unit{ValueUnit::none};
    ValueTextDefect defect{ValueTextDefect::none};
    std::size_t position{};
};

void test_accepted() {
    const Accepted rows[] = {
        {.text = "42", .unit = ValueUnit::none, .expected = 42.0},
        {.text = "  2.5 ", .unit = ValueUnit::none, .expected = 2.5},
        {.text = ".5", .unit = ValueUnit::none, .expected = 0.5},
        {.text = "1e3", .unit = ValueUnit::none, .expected = 1000.0},
        {.text = "1 + 2 * 3", .unit = ValueUnit::none, .expected = 7.0},
        {.text = "(1 + 2) * 3", .unit = ValueUnit::none, .expected = 9.0},
        {.text = "10 - 4 - 3", .unit = ValueUnit::none, .expected = 3.0},
        {.text = "12 / 4 / 3", .unit = ValueUnit::none, .expected = 1.0},
        {.text = "-3 + 5", .unit = ValueUnit::none, .expected = 2.0},
        {.text = "--3", .unit = ValueUnit::none, .expected = 3.0},
        {.text = "+4", .unit = ValueUnit::none, .expected = 4.0},
        {.text = "2^10", .unit = ValueUnit::none, .expected = 1024.0},
        {.text = "2^3^2", .unit = ValueUnit::none, .expected = 512.0},
        {.text = "-2^2", .unit = ValueUnit::none, .expected = -4.0},
        {.text = "2^-2", .unit = ValueUnit::none, .expected = 0.25},
        {.text = "5^0", .unit = ValueUnit::none, .expected = 1.0},
        {.text = "sqrt(16) + 1", .unit = ValueUnit::none, .expected = 5.0},
        {.text = "min(3, 1, 2)", .unit = ValueUnit::none, .expected = 1.0},
        {.text = "max(3, 1 + 4)", .unit = ValueUnit::none, .expected = 5.0},
        {.text = "pi", .unit = ValueUnit::none, .expected = pi_value},
        {.text = "50", .unit = ValueUnit::percent, .expected = 0.5},
        {.text = "50%", .unit = ValueUnit::percent, .expected = 0.5},
        {.text = "25 * 2", .unit = ValueUnit::percent, .expected = 0.5},
        {.text = "10cm + 2mm", .unit = ValueUnit::length, .expected = 0.1 + 0.002},
        {.text = "1m 50cm", .unit = ValueUnit::length, .expected = 1.0 + 0.5},
        {.text = "2ft 6in", .unit = ValueUnit::length, .expected = 2.0 * 0.3048 + 6.0 * 0.0254},
        {.text = "3", .unit = ValueUnit::length, .expected = 3.0},
        {.text = "2 * 10 cm", .unit = ValueUnit::length, .expected = 2.0 * 0.1},
        {.text = "90", .unit = ValueUnit::angle, .expected = 90.0 * (pi_value / 180.0)},
        {.text = "90\xC2\xB0", .unit = ValueUnit::angle, .expected = 90.0 * (pi_value / 180.0)},
        {.text = "1 rad", .unit = ValueUnit::angle, .expected = 1.0},
        {.text = "1min 30s", .unit = ValueUnit::time, .expected = 90.0},
        {.text = "250ms", .unit = ValueUnit::time, .expected = 0.25},
    };
    for (const Accepted& row : rows) {
        const ValueTextResult result = parse_value_text(row.text, row.unit);
        const bool exact = result.defect == ValueTextDefect::none && result.value == row.expected;
        if (!exact) {
            std::cerr << "  text '" << row.text << "' gave " << result.value << " ("
                      << value_text_defect_name(result.defect) << "), expected " << row.expected << '\n';
        }
        require(exact, "an accepted text gives its value");
    }
}

void test_refused() {
    const Refused rows[] = {
        {.text = "", .unit = ValueUnit::none, .defect = ValueTextDefect::empty, .position = 0U},
        {.text = "   ", .unit = ValueUnit::none, .defect = ValueTextDefect::empty, .position = 0U},
        {.text = "1 $ 2", .unit = ValueUnit::none, .defect = ValueTextDefect::unexpected_character, .position = 2U},
        {.text = "3cm", .unit = ValueUnit::none, .defect = ValueTextDefect::unknown_unit, .position = 1U},
        {.text = "3 parsec", .unit = ValueUnit::length, .defect = ValueTextDefect::unknown_unit, .position = 2U},
        {.text = "2pi", .unit = ValueUnit::none, .defect = ValueTextDefect::unknown_unit, .position = 1U},
        {.text = "tau", .unit = ValueUnit::none, .defect = ValueTextDefect::unknown_name, .position = 0U},
        {.text = "1 +", .unit = ValueUnit::none, .defect = ValueTextDefect::missing_operand, .position = 3U},
        {.text = "(1 + 2", .unit = ValueUnit::none, .defect = ValueTextDefect::missing_operand, .position = 6U},
        {.text = "min(1)", .unit = ValueUnit::none, .defect = ValueTextDefect::missing_operand, .position = 5U},
        {.text = "sqrt 4", .unit = ValueUnit::none, .defect = ValueTextDefect::missing_operand, .position = 5U},
        {.text = "1 2", .unit = ValueUnit::none, .defect = ValueTextDefect::unexpected_token, .position = 2U},
        {.text = "1)", .unit = ValueUnit::none, .defect = ValueTextDefect::unexpected_token, .position = 1U},
        {.text = "2^0.5", .unit = ValueUnit::none, .defect = ValueTextDefect::bad_operand, .position = 1U},
        {.text = "2^65", .unit = ValueUnit::none, .defect = ValueTextDefect::bad_operand, .position = 1U},
        {.text = "sqrt(-1)", .unit = ValueUnit::none, .defect = ValueTextDefect::bad_operand, .position = 0U},
        {.text = "1 / 0", .unit = ValueUnit::none, .defect = ValueTextDefect::not_finite, .position = 5U},
        {.text = "1e999", .unit = ValueUnit::none, .defect = ValueTextDefect::not_finite, .position = 0U},
    };
    for (const Refused& row : rows) {
        const ValueTextResult result = parse_value_text(row.text, row.unit);
        const bool matched = result.defect == row.defect && result.position == row.position;
        if (!matched) {
            std::cerr << "  text '" << row.text << "' gave " << value_text_defect_name(result.defect) << " at "
                      << result.position << ", expected " << value_text_defect_name(row.defect) << " at "
                      << row.position << '\n';
        }
        require(matched, "a refused text gives its defect and position");
    }
}

// The bounds: length, nesting and the operation budget.
void test_bounds() {
    const std::string long_text(maximum_value_text_bytes + 1U, '1');
    require(parse_value_text(long_text, ValueUnit::none).defect == ValueTextDefect::too_long, "too long");
    std::string nested{};
    for (std::size_t level = 0U; level <= maximum_value_text_depth; ++level) nested.push_back('(');
    nested.push_back('1');
    for (std::size_t level = 0U; level <= maximum_value_text_depth; ++level) nested.push_back(')');
    require(parse_value_text(nested, ValueUnit::none).defect == ValueTextDefect::too_deep, "too deep");
    std::string shallow = "((((1))))";
    require(parse_value_text(shallow, ValueUnit::none).value == 1.0, "shallow nesting is accepted");
    // 1+1+...: two instructions a term, past the 64-instruction budget.
    std::string summed = "1";
    for (std::size_t term = 0U; term < 40U; ++term) summed.append("+1");
    require(parse_value_text(summed, ValueUnit::none).defect == ValueTextDefect::over_budget, "over budget");
    std::string within = "1";
    for (std::size_t term = 0U; term < 30U; ++term) within.append("+1");
    require(parse_value_text(within, ValueUnit::none).value == 31.0, "within the budget");
}

void test_format() {
    std::string text{};
    format_value_text(64.0, ValueUnit::none, 0, text);
    require(text == "64", "an integer at precision 0");
    format_value_text(0.125, ValueUnit::none, 3, text);
    require(text == "0.125", "three decimals");
    format_value_text(-0.001, ValueUnit::none, 2, text);
    require(text == "0.00", "a negative value rounding to zero shows no sign");
    format_value_text(-1.5, ValueUnit::none, 1, text);
    require(text == "-1.5", "a negative value keeps its sign");
    format_value_text(0.5, ValueUnit::percent, 0, text);
    require(text == "50%", "percent: 0..1 shown as 0..100 %");
    format_value_text(1.5, ValueUnit::length, 2, text);
    require(text == "1.50 m", "length in metres, spaced");
    format_value_text(pi_value / 2.0, ValueUnit::angle, 1, text);
    require(text == "90.0\xC2\xB0", "angle in degrees");
    format_value_text(2.0, ValueUnit::time, 1, text);
    require(text == "2.0 s", "time in seconds");
    format_value_text(1.0, ValueUnit::none, 40, text);
    require(text == "1.000000000000", "precision is clamped to 12");
    // The formatted value parses back to itself (round trip at the shown
    // precision).
    format_value_text(0.25, ValueUnit::percent, 0, text);
    require(parse_value_text(text, ValueUnit::percent).value == 0.25, "the shown text parses back");
    // The span form writes the same text without allocating, and nothing
    // into a buffer too short for it.
    std::array<char, 8> small{};
    const std::size_t written = format_value_text(1.5, ValueUnit::length, 2, std::span<char>(small));
    require(std::string_view(small.data(), written) == "1.50 m", "the span form writes the text");
    std::array<char, 4> tiny{};
    require(format_value_text(1.5, ValueUnit::length, 2, std::span<char>(tiny)) == 0U, "too short writes nothing");
    std::array<char, gui_forms::value_text::maximum_formatted_value_bytes> widest{};
    require(format_value_text(-1.0e308, ValueUnit::length, 12, std::span<char>(widest)) > 309U,
            "the bound holds the widest value");
}

void test_formats() {
    require(unit_format(ValueUnit::percent).display.suffix == "%", "percent shows %");
    require(unit_format(ValueUnit::length).suffixes.size() == 8U, "length has eight suffixes");
    require(unit_format(ValueUnit::none).suffixes.empty(), "none has no suffixes");
    require(value_text_defect_name(ValueTextDefect::over_budget) == "over_budget", "defect names");
}

} // namespace

int main() {
    test_accepted();
    test_refused();
    test_bounds();
    test_format();
    test_formats();
    if (failures != 0) {
        std::cerr << failures << " value text checks failed\n";
        return 1;
    }
    std::cout << "value text: arithmetic, units, defects, bounds and formatting passed\n";
    return 0;
}
