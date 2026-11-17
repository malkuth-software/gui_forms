#include "strftime_date_pattern.hpp"

#include <cstddef>

namespace gui_forms::host {
namespace {

bool ascii_letter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

// The picker text for the conversion `%conversion`; empty for one the
// picker does not draw.
std::string_view picker_conversion(char conversion) {
    switch (conversion) {
    case 'd': return "dd";
    case 'e': return "d";
    case 'm': return "MM";
    case 'b':
    case 'h': return "MMM";
    case 'B': return "MMMM";
    case 'y': return "yy";
    case 'Y': return "yyyy";
    case 'a': return "ddd";
    case 'A': return "dddd";
    case 'H': return "HH";
    case 'k': return "H";
    case 'I': return "hh";
    case 'l': return "h";
    case 'M': return "mm";
    case 'S': return "ss";
    case 'p':
    case 'P': return "tt";
    case 'D': return "MM/dd/yy";
    case 'F': return "yyyy-MM-dd";
    case 'T': return "HH:mm:ss";
    case 'R': return "HH:mm";
    case 'r': return "hh:mm:ss tt";
    case 'n':
    case 't': return " ";
    case '%': return "%";
    default: return {};
    }
}

// The picker text for `%conversion` under glibc's '-' (no padding) or '_'
// (space padding, which a picker pattern cannot draw: the nearest is no
// padding) flag; the padded numeric fields lose their padding, every other
// conversion is as picker_conversion gives it.
std::string_view unpadded_picker_conversion(char conversion) {
    switch (conversion) {
    case 'd': return "d";
    case 'm': return "M";
    case 'H': return "H";
    case 'I': return "h";
    case 'M': return "m";
    case 'S': return "s";
    default: return picker_conversion(conversion);
    }
}

bool trimmable(char character) {
    return character == ' ' || character == ',';
}

} // namespace

std::string picker_pattern_from_strftime(const std::string_view pattern) {
    std::string output;
    output.reserve(pattern.size() * 2U);
    std::size_t index = 0U;
    while (index < pattern.size()) {
        const char character = pattern[index];
        if (character == '%') {
            ++index;
            // Skip glibc's flags and width, and the E and O modifiers,
            // noting a flag that drops the padding.
            bool unpadded = false;
            while (index < pattern.size() &&
                   (pattern[index] == '_' || pattern[index] == '-' || pattern[index] == '0' ||
                    pattern[index] == '^' || pattern[index] == '#' ||
                    (pattern[index] >= '1' && pattern[index] <= '9') || pattern[index] == 'E' ||
                    pattern[index] == 'O')) {
                if (pattern[index] == '-' || pattern[index] == '_') unpadded = true;
                ++index;
            }
            if (index < pattern.size()) {
                const std::string_view conversion =
                    unpadded ? unpadded_picker_conversion(pattern[index]) : picker_conversion(pattern[index]);
                output += conversion;
                ++index;
            }
            continue;
        }
        if (ascii_letter(character)) {
            output.push_back('\'');
            while (index < pattern.size() && ascii_letter(pattern[index])) {
                output.push_back(pattern[index]);
                ++index;
            }
            output.push_back('\'');
            continue;
        }
        if (character == '\'' || character == '\\') output.push_back('\\');
        output.push_back(character);
        ++index;
    }
    std::size_t first = 0U;
    while (first < output.size() && trimmable(output[first])) ++first;
    std::size_t last = output.size();
    while (last > first && trimmable(output[last - 1U])) --last;
    const std::string trimmed = output.substr(first, last - first);
    return trimmed;
}

} // namespace gui_forms::host
