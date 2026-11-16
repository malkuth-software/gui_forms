// JSON text through a standard stream. See json_text.hpp.

#include "json_text.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <ios>
#include <locale>
#include <system_error>

namespace gui_forms::detail {
namespace {

constexpr std::string_view hex_digits = "0123456789abcdef";

} // namespace

void prepare_json_stream(std::ostream& stream) {
    static_cast<void>(stream.imbue(std::locale::classic()));
}

std::ostream& operator<<(std::ostream& stream, JsonNumber number) {
    // 32 characters hold the longest shortest form of a double
    // ("-2.2250738585072014e-308" is 24).
    std::array<char, 32> text{};
    if (!std::isfinite(number.value)) {
        stream << "null";
        return stream;
    }
    const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), number.value);
    if (written.ec != std::errc{}) {
        stream << "null";
        return stream;
    }
    stream.write(text.data(), static_cast<std::streamsize>(written.ptr - text.data()));
    return stream;
}

void append_json_string(std::ostream& stream, std::string_view value) {
    stream << '"';
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const unsigned char character = static_cast<unsigned char>(value[offset]);
        if (character >= 0x80U) {
            const Utf8ScalarDecode decoded = decode_utf8_scalar(value, Utf8Offset(offset));
            if (decoded.valid) {
                stream.write(value.data() + offset, static_cast<std::streamsize>(decoded.length));
            } else {
                stream << "\\ufffd";
            }
            offset = offset + decoded.length;
            continue;
        }
        switch (character) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20U) {
                const std::array<char, 6> escape{'\\', 'u', '0', '0', hex_digits[character >> 4U],
                                                 hex_digits[character & 0x0FU]};
                stream.write(escape.data(), static_cast<std::streamsize>(escape.size()));
            } else {
                stream << static_cast<char>(character);
            }
            break;
        }
        offset = offset + 1U;
    }
    stream << '"';
}

} // namespace gui_forms::detail
