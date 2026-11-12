#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

// Locale-independent parsing of numbers typed by the user or written by an
// assistive client (audit F4). std::stod and std::stoll follow LC_NUMERIC,
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

} // namespace gui_forms::number_text
