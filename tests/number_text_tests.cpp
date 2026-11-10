// Locale-independent number parsing (audit F4): the controls read typed and
// assistive-client numbers in the C-locale form whatever LC_NUMERIC says.

#include "../src/controls/basic/number_text.hpp"

#include <clocale>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_decimal_forms() {
    double value = -1.0;
    require(gui_forms::number_text::parse_finite_decimal("12.5", value) && value == 12.5, "plain decimal");
    require(gui_forms::number_text::parse_finite_decimal("  \t+3", value) && value == 3.0, "blanks and plus");
    require(gui_forms::number_text::parse_finite_decimal("-0.25", value) && value == -0.25, "minus");
    require(gui_forms::number_text::parse_finite_decimal("1e3", value) && value == 1000.0, "exponent");
    value = 7.0;
    require(!gui_forms::number_text::parse_finite_decimal("", value), "empty refused");
    require(!gui_forms::number_text::parse_finite_decimal("12,5", value), "a comma separator refused");
    require(!gui_forms::number_text::parse_finite_decimal("3 ", value), "trailing text refused");
    require(!gui_forms::number_text::parse_finite_decimal("+-3", value), "two signs refused");
    require(!gui_forms::number_text::parse_finite_decimal("inf", value), "infinity refused");
    require(!gui_forms::number_text::parse_finite_decimal("nan", value), "NaN refused");
    require(!gui_forms::number_text::parse_finite_decimal("1e999", value), "overflow refused");
    require(value == 7.0, "a refusal leaves the value");
}

void check_hexadecimal_forms() {
    std::int64_t value = 0;
    require(gui_forms::number_text::parse_hexadecimal_integer("ff", value) && value == 255, "plain hex");
    require(gui_forms::number_text::parse_hexadecimal_integer("0x1F", value) && value == 31, "prefixed hex");
    require(gui_forms::number_text::parse_hexadecimal_integer("-0x10", value) && value == -16, "negative hex");
    value = 9;
    require(!gui_forms::number_text::parse_hexadecimal_integer("0x", value), "a bare prefix refused");
    require(!gui_forms::number_text::parse_hexadecimal_integer("fg", value), "a bad digit refused");
    require(!gui_forms::number_text::parse_hexadecimal_integer("10000000000000000", value), "overflow refused");
    require(value == 9, "a refusal leaves the value");
}

} // namespace

int main() {
    try {
        check_decimal_forms();
        check_hexadecimal_forms();
        // Under a locale whose decimal separator is a comma, the same forms
        // still parse (std::stod would have read "12.5" as 12).
        const char* const candidates[] = {"de_DE.UTF-8", "de_DE", "German_Germany.1252", "de-DE"};
        bool switched = false;
        for (const char* const candidate : candidates) {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr) {
                switched = true;
                break;
            }
        }
        check_decimal_forms();
        check_hexadecimal_forms();
        static_cast<void>(std::setlocale(LC_NUMERIC, "C"));
        std::cout << "number text tests passed" << (switched ? " (also under a comma locale)" : "") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "number text tests failed: " << error.what() << '\n';
        return 1;
    }
}
