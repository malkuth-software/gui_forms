#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <system_error>

// Locale-independent numbers: parsing what the user types or an assistive
// client writes (audit F4), and writing what the controls publish
// (A-gf-controls-05-F5, 10-F10 and the locale findings after them). This is
// the one place that owns number text; std::to_string, printf and default
// streams follow a locale the program may set.
//
// Parsing: std::stod and std::stoll follow LC_NUMERIC, std::stod and std::stoll follow LC_NUMERIC,
// which an embedding application may change, and throw on ordinary bad input;
// these read the C-locale form with std::from_chars and report failure with a
// result instead.
//
// Accepted form, matching what the controls accepted from std::stod in the
// C locale: optional leading ASCII spaces or tabs, an optional '+' or '-'
// sign, then the number, which must use every remaining character. Infinity
// and NaN are refused.
namespace gui_forms::number_text {

[[nodiscard]] inline std::string_view without_leading_blanks(std::string_view text) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) ++first;
    return text.substr(first);
}

// Reads a finite decimal number. `value` is written only on success.
[[nodiscard]] inline bool parse_finite_decimal(std::string_view text, double& value) noexcept {
    std::string_view rest = without_leading_blanks(text);
    // from_chars accepts '-' but not '+'; a '+' followed by another sign is refused.
    if (!rest.empty() && rest.front() == '+') {
        rest.remove_prefix(1U);
        if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) return false;
    }
    if (rest.empty()) return false;
    const char* const first = rest.data();
    const char* const last = first + rest.size();
    double parsed{};
    const std::from_chars_result result = std::from_chars(first, last, parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

// Reads a hexadecimal integer as std::stoll(text, nullptr, 16) read it: an
// optional sign, an optional "0x" or "0X" prefix, then hexadecimal digits.
// `value` is written only on success; magnitudes beyond INT64_MAX are refused.
[[nodiscard]] inline bool parse_hexadecimal_integer(std::string_view text, std::int64_t& value) noexcept {
    std::string_view rest = without_leading_blanks(text);
    bool negative = false;
    if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
        negative = rest.front() == '-';
        rest.remove_prefix(1U);
    }
    if (rest.size() > 2U && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X')) rest.remove_prefix(2U);
    if (rest.empty() || rest.front() == '+' || rest.front() == '-') return false;
    const char* const first = rest.data();
    const char* const last = first + rest.size();
    std::int64_t magnitude{};
    const std::from_chars_result result = std::from_chars(first, last, magnitude, 16);
    if (result.ec != std::errc{} || result.ptr != last) return false;
    value = negative ? -magnitude : magnitude;
    return true;
}

// Writing: std::to_chars with a format and precision is specified to write
// what printf's matching conversion writes in the C locale, so "%.6g" is
// append_number_text(out, value, std::chars_format::general, 6) and "%.0f" is
// (..., std::chars_format::fixed, 0). Appends nothing when the text would not
// fit 64 bytes, which no general form and no fixed form of a value below 1e40
// reaches.
inline void append_number_text(std::string& out, double value, std::chars_format format, int precision) {
    std::array<char, 64> text{};
    char* const first = text.data();
    char* const last = first + text.size();
    const std::to_chars_result written = std::to_chars(first, last, value, format, precision);
    if (written.ec != std::errc{}) return;
    out.append(first, written.ptr);
}

// An integer in uppercase hexadecimal, as a stream with std::uppercase and
// std::hex writes it in the classic locale: a negative value is written as
// its two's complement bits (the stream formats it unsigned).
inline void append_hexadecimal_integer(std::string& out, std::int64_t value) {
    std::array<char, 32> text{};
    char* const first = text.data();
    char* const last = first + text.size();
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    const std::to_chars_result written = std::to_chars(first, last, bits, 16);
    if (written.ec != std::errc{}) return;
    for (char* digit = first; digit != written.ptr; ++digit) {
        if (*digit >= 'a' && *digit <= 'f') *digit = static_cast<char>(*digit - 'a' + 'A');
    }
    out.append(first, written.ptr);
}

} // namespace gui_forms::number_text
