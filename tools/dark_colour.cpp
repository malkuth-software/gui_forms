// The dark colour rule as a filter (stage T4, variants stage B), for
// tools/relocate_colours.py: it seeds the dark value of each colour it moves
// into a theme sheet by the rule the derived dark variant uses
// (src/core/theme/dark_variant/dark_variant.hpp).
//
// Each input line is `<class> <colour>`: a DarkColourClass name in snake case
// (ink, stock_surface, ...) and a colour written #rgb, #rrggbb or #rrggbbaa.
// Each output line is the dark colour, #rrggbb when it is opaque and
// #rrggbbaa otherwise, in input order. A malformed line stops the filter
// with a message on stderr and exit status 1; lines before it are written.
#include "../src/core/theme/dark_variant/dark_variant.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using gui_forms::Color;
using gui_forms::detail::DarkColourClass;

struct ClassName final {
    std::string_view name;
    DarkColourClass kind;
};

constexpr std::array<ClassName, 13> class_names{{
    {"chrome_surface", DarkColourClass::chrome_surface},
    {"stock_surface", DarkColourClass::stock_surface},
    {"well_surface", DarkColourClass::well_surface},
    {"accent_surface", DarkColourClass::accent_surface},
    {"ink", DarkColourClass::ink},
    {"light_ink", DarkColourClass::light_ink},
    {"disabled_ink", DarkColourClass::disabled_ink},
    {"line", DarkColourClass::line},
    {"deep_line", DarkColourClass::deep_line},
    {"cue", DarkColourClass::cue},
    {"accent_line", DarkColourClass::accent_line},
    {"highlight", DarkColourClass::highlight},
    {"shade", DarkColourClass::shade},
}};

std::optional<DarkColourClass> class_of(std::string_view name) {
    for (const ClassName& entry : class_names) {
        if (entry.name == name) return entry.kind;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> hex_digit(char written) {
    if (written >= '0' && written <= '9') return static_cast<std::uint8_t>(written - '0');
    if (written >= 'a' && written <= 'f') return static_cast<std::uint8_t>(written - 'a' + 10);
    if (written >= 'A' && written <= 'F') return static_cast<std::uint8_t>(written - 'A' + 10);
    return std::nullopt;
}

// `text` as a colour: # then 3, 6 or 8 hexadecimal digits.
std::optional<Color> colour_of(std::string_view text) {
    if (text.empty() || text[0] != '#') return std::nullopt;
    const std::string_view digits = text.substr(1U);
    if (digits.size() != 3U && digits.size() != 6U && digits.size() != 8U) return std::nullopt;
    std::array<std::uint8_t, 8> values{};
    for (std::size_t index = 0U; index < digits.size(); ++index) {
        const std::optional<std::uint8_t> value = hex_digit(digits[index]);
        if (!value) return std::nullopt;
        values[index] = *value;
    }
    if (digits.size() == 3U) {
        const std::uint8_t red = static_cast<std::uint8_t>(values[0] * 17U);
        const std::uint8_t green = static_cast<std::uint8_t>(values[1] * 17U);
        const std::uint8_t blue = static_cast<std::uint8_t>(values[2] * 17U);
        return Color::rgba(red, green, blue, 255U);
    }
    const std::uint8_t red = static_cast<std::uint8_t>(values[0] * 16U + values[1]);
    const std::uint8_t green = static_cast<std::uint8_t>(values[2] * 16U + values[3]);
    const std::uint8_t blue = static_cast<std::uint8_t>(values[4] * 16U + values[5]);
    std::uint8_t alpha = 255U;
    if (digits.size() == 8U) alpha = static_cast<std::uint8_t>(values[6] * 16U + values[7]);
    return Color::rgba(red, green, blue, alpha);
}

void write_channel(std::string& output, std::uint8_t channel) {
    constexpr std::string_view digits = "0123456789abcdef";
    output.push_back(digits[channel / 16U]);
    output.push_back(digits[channel % 16U]);
}

std::string written(Color colour) {
    std::string output = "#";
    write_channel(output, colour.red);
    write_channel(output, colour.green);
    write_channel(output, colour.blue);
    if (colour.alpha != 255U) write_channel(output, colour.alpha);
    return output;
}

} // namespace

int main() {
    std::string line;
    std::size_t number = 0U;
    while (std::getline(std::cin, line)) {
        number = number + 1U;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) {
            std::cerr << "dark_colour: line " << number << ": expected `<class> <colour>`\n";
            return 1;
        }
        const std::string_view text = line;
        const std::optional<DarkColourClass> kind = class_of(text.substr(0U, space));
        const std::optional<Color> light = colour_of(text.substr(space + 1U));
        if (!kind || !light) {
            std::cerr << "dark_colour: line " << number << ": unknown class or colour\n";
            return 1;
        }
        const Color dark = gui_forms::detail::dark_colour(*light, *kind);
        std::cout << written(dark) << '\n';
    }
    return 0;
}
