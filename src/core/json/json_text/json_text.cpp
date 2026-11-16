// JSON text through a standard stream. See json_text.hpp.

#include "json_text.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <ios>
#include <iterator>
#include <locale>
#include <system_error>

namespace gui_forms::detail {
namespace {

// The classic locale's numeric output, except doubles: the shortest
// round-trip text (std::to_chars, never locale-dependent), or null. The
// stream's width, fill and precision are not used; no writer here sets
// them for a number.
class JsonNumberPut final : public std::num_put<char> {
public:
    JsonNumberPut() : std::num_put<char>(0U) {}

protected:
    iter_type do_put(iter_type out, std::ios_base& format, char fill, double value) const override {
        static_cast<void>(format);
        static_cast<void>(fill);
        // 32 characters hold the longest shortest form of a double
        // ("-2.2250738585072014e-308" is 24).
        std::array<char, 32> text{};
        std::size_t length = 0U;
        if (std::isfinite(value)) {
            const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
            if (written.ec == std::errc{}) length = static_cast<std::size_t>(written.ptr - text.data());
        }
        const std::string_view number = length != 0U ? std::string_view(text.data(), length) : std::string_view("null");
        for (const char character : number) {
            *out = character;
            ++out;
        }
        return out;
    }

    iter_type do_put(iter_type out, std::ios_base& format, char fill, long double value) const override {
        const iter_type written = do_put(out, format, fill, static_cast<double>(value));
        return written;
    }
};

constexpr std::string_view hex_digits = "0123456789abcdef";

} // namespace

void imbue_json_numbers(std::ostream& stream) {
    // std::locale takes ownership of a facet made with reference count 0
    // and deletes it with the last locale holding it.
    const std::locale json_locale(std::locale::classic(), new JsonNumberPut());
    static_cast<void>(stream.imbue(json_locale));
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
