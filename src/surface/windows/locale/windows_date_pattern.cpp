#include "windows_date_pattern.hpp"

#include <cstddef>

namespace gui_forms::host {
namespace {

bool ascii_letter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

// Whether `letter` is a Windows format picture letter (fields the picker
// draws, and the era, which it drops).
bool picture_letter(char letter) {
    switch (letter) {
    case 'd':
    case 'M':
    case 'y':
    case 'g':
    case 'h':
    case 'H':
    case 'm':
    case 's':
    case 't':
        return true;
    default:
        return false;
    }
}

// The picker field for a run of `count` (at least 1) copies of the picture
// letter `letter`; empty for the era, which the picker does not draw.
std::string_view picker_field(char letter, std::size_t count) {
    switch (letter) {
    case 'd':
        if (count == 1U) return "d";
        if (count == 2U) return "dd";
        if (count == 3U) return "ddd";
        return "dddd";
    case 'M':
        if (count == 1U) return "M";
        if (count == 2U) return "MM";
        if (count == 3U) return "MMM";
        return "MMMM";
    case 'y':
        return count <= 2U ? std::string_view{"yy"} : std::string_view{"yyyy"};
    case 'h':
        return count == 1U ? std::string_view{"h"} : std::string_view{"hh"};
    case 'H':
        return count == 1U ? std::string_view{"H"} : std::string_view{"HH"};
    case 'm':
        return count == 1U ? std::string_view{"m"} : std::string_view{"mm"};
    case 's':
        return count == 1U ? std::string_view{"s"} : std::string_view{"ss"};
    case 't':
        return count == 1U ? std::string_view{"t"} : std::string_view{"tt"};
    default:
        return {};
    }
}

// Appends `character` as picker quoted-text content.
void append_quoted_character(std::string& output, char character) {
    if (character == '\'' || character == '\\') output.push_back('\\');
    output.push_back(character);
}

bool trimmable(char character) {
    return character == ' ' || character == ',';
}

} // namespace

std::string picker_pattern_from_windows(const std::string_view pattern) {
    std::string output;
    output.reserve(pattern.size() + 8U);
    std::size_t index = 0U;
    while (index < pattern.size()) {
        const char character = pattern[index];
        if (character == '\'') {
            // '' outside quotes is one apostrophe; otherwise quoted text runs
            // to the next lone quote, '' inside it being one apostrophe.
            if (index + 1U < pattern.size() && pattern[index + 1U] == '\'') {
                output += "'\\''";
                index += 2U;
                continue;
            }
            std::string text;
            ++index;
            while (index < pattern.size()) {
                if (pattern[index] == '\'') {
                    if (index + 1U < pattern.size() && pattern[index + 1U] == '\'') {
                        append_quoted_character(text, '\'');
                        index += 2U;
                        continue;
                    }
                    ++index;
                    break;
                }
                append_quoted_character(text, pattern[index]);
                ++index;
            }
            if (!text.empty()) {
                output.push_back('\'');
                output += text;
                output.push_back('\'');
            }
            continue;
        }
        if (ascii_letter(character) && picture_letter(character)) {
            std::size_t count = 1U;
            while (index + count < pattern.size() && pattern[index + count] == character) ++count;
            output += picker_field(character, count);
            index += count;
            continue;
        }
        if (ascii_letter(character)) {
            // A letter that is no picture: text, quoted for the picker.
            output.push_back('\'');
            while (index < pattern.size() && ascii_letter(pattern[index]) && !picture_letter(pattern[index])) {
                output.push_back(pattern[index]);
                ++index;
            }
            output.push_back('\'');
            continue;
        }
        if (character == '\\') output.push_back('\\');
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
