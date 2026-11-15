#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>

namespace gui_forms::detail {

// Numbers published as text (accessibility values and descriptions, plot
// read-outs and tables) are written the same whatever locale the program
// sets: std::to_chars with a format and precision is specified to write what
// printf's matching conversion writes in the C locale, so "%.6g" becomes
// append_number_text(out, value, std::chars_format::general, 6) and "%.0f"
// becomes (..., std::chars_format::fixed, 0). Appends nothing when the text
// would not fit 64 bytes, which no general form and no fixed form of a value
// below 1e40 reaches.
inline void append_number_text(std::string& out, double value, std::chars_format format, int precision) {
    std::array<char, 64> text{};
    char* const first = text.data();
    char* const last = first + text.size();
    const std::to_chars_result written = std::to_chars(first, last, value, format, precision);
    if (written.ec != std::errc{}) return;
    out.append(first, written.ptr);
}

} // namespace gui_forms::detail
