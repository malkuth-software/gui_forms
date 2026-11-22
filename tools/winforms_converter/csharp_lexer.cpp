#include "csharp_lexer.hpp"

#include <utility>

namespace gui_forms::winforms_converter {

namespace {

// The punctuation of two characters, longest match first.
constexpr std::string_view two_character_punctuation[] = {"+=", "-=", "::", "=>", "==", "!=", "&&", "||",
                                                          "<=", ">=", "|=", "&=", "++", "--", "??", "?."};

[[nodiscard]] bool identifier_start(unsigned char byte) noexcept {
    const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    // A byte of a UTF-8 sequence: C# admits Unicode letters in names.
    const bool start = letter || byte == '_' || byte >= 0x80U;
    return start;
}

[[nodiscard]] bool identifier_part(unsigned char byte) noexcept {
    const bool part = identifier_start(byte) || (byte >= '0' && byte <= '9');
    return part;
}

[[nodiscard]] bool digit(unsigned char byte) noexcept {
    const bool decimal = byte >= '0' && byte <= '9';
    return decimal;
}

[[nodiscard]] bool hexadecimal_digit(unsigned char byte) noexcept {
    const bool hexadecimal = digit(byte) || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
    return hexadecimal;
}

// Appends the UTF-8 encoding of `scalar`; false for a surrogate or a value
// past U+10FFFF, appending nothing.
[[nodiscard]] bool append_utf8(std::string& out, std::uint32_t scalar) {
    if (scalar >= 0xD800U && scalar <= 0xDFFFU) return false;
    if (scalar > 0x10FFFFU) return false;
    if (scalar < 0x80U) {
        out.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else if (scalar < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    }
    return true;
}

// The lexer's position: a byte offset with its line and column, moved
// forward only by advance().
class Cursor final {
public:
    explicit Cursor(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] bool done() const noexcept { return offset_ >= text_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint32_t line() const noexcept { return line_; }
    [[nodiscard]] std::uint32_t column() const noexcept { return column_; }
    // The byte `ahead` places on, or zero past the end.
    [[nodiscard]] unsigned char peek(std::size_t ahead = 0U) const noexcept {
        const std::size_t at = offset_ + ahead;
        if (at >= text_.size()) return 0U;
        const unsigned char byte = static_cast<unsigned char>(text_[at]);
        return byte;
    }
    [[nodiscard]] bool starts_with(std::string_view prefix) const noexcept {
        const bool starts = text_.substr(offset_).starts_with(prefix);
        return starts;
    }
    void advance(std::size_t count = 1U) noexcept {
        for (std::size_t step = 0U; step < count && offset_ < text_.size(); ++step) {
            const unsigned char byte = static_cast<unsigned char>(text_[offset_]);
            offset_ = offset_ + 1U;
            // CR LF is one line end: the CR ends it, the LF only advances.
            const bool line_end = byte == '\n' ? (offset_ < 2U || text_[offset_ - 2U] != '\r') : byte == '\r';
            if (line_end) {
                line_ = line_ + 1U;
                column_ = 1U;
            } else if (byte != '\n') {
                column_ = column_ + 1U;
            }
        }
    }

private:
    std::string_view text_;
    std::size_t offset_{};
    std::uint32_t line_{1U};
    std::uint32_t column_{1U};
};

// A refusal: where, and what was found.
struct LexRefusal final {
    std::uint32_t line{};
    std::uint32_t column{};
    std::string detail{};
};

class Lexer final {
public:
    Lexer(const std::string& text, std::vector<CSharpToken>& tokens) : cursor_(text), tokens_(tokens) {}

    // Every token, or the first refusal.
    [[nodiscard]] bool run(LexRefusal& refusal) {
        while (true) {
            skip_trivia(refusal);
            if (!refusal.detail.empty()) return false;
            if (cursor_.done()) return true;
            if (tokens_.size() >= maximum_csharp_tokens) {
                refusal = here("more tokens than the converter reads");
                return false;
            }
            const bool lexed = token(refusal);
            if (!lexed) return false;
        }
    }

private:
    [[nodiscard]] LexRefusal here(std::string detail) const {
        LexRefusal refusal{};
        refusal.line = cursor_.line();
        refusal.column = cursor_.column();
        refusal.detail = std::move(detail);
        return refusal;
    }

    // Whitespace, comments and preprocessor lines.
    void skip_trivia(LexRefusal& refusal) {
        bool line_start = tokens_.empty() || cursor_.offset() == 0U;
        while (!cursor_.done()) {
            const unsigned char byte = cursor_.peek();
            if (byte == ' ' || byte == '\t' || byte == '\f' || byte == '\v') {
                cursor_.advance();
            } else if (byte == '\r' || byte == '\n') {
                cursor_.advance();
                line_start = true;
            } else if (byte == 0xEFU && cursor_.peek(1U) == 0xBBU && cursor_.peek(2U) == 0xBFU &&
                       cursor_.offset() == 0U) {
                // A byte order mark.
                cursor_.advance(3U);
            } else if (cursor_.starts_with("//")) {
                while (!cursor_.done() && cursor_.peek() != '\n' && cursor_.peek() != '\r') cursor_.advance();
            } else if (cursor_.starts_with("/*")) {
                const LexRefusal opened = here("unterminated comment");
                cursor_.advance(2U);
                while (!cursor_.done() && !cursor_.starts_with("*/")) cursor_.advance();
                if (cursor_.done()) {
                    refusal = opened;
                    return;
                }
                cursor_.advance(2U);
            } else if (byte == '#' && line_start) {
                while (!cursor_.done() && cursor_.peek() != '\n' && cursor_.peek() != '\r') cursor_.advance();
            } else {
                return;
            }
        }
    }

    void push(CSharpTokenKind kind, std::size_t offset, std::uint32_t line, std::uint32_t column, std::string value) {
        CSharpToken token{};
        token.kind = kind;
        token.offset = static_cast<std::uint32_t>(offset);
        token.length = static_cast<std::uint32_t>(cursor_.offset() - offset);
        token.line = line;
        token.column = column;
        token.value = std::move(value);
        tokens_.push_back(std::move(token));
    }

    [[nodiscard]] bool token(LexRefusal& refusal) {
        const std::size_t offset = cursor_.offset();
        const std::uint32_t line = cursor_.line();
        const std::uint32_t column = cursor_.column();
        const unsigned char byte = cursor_.peek();
        if (byte == '$') {
            refusal = here("an interpolated string, whose expressions the converter does not read");
            return false;
        }
        if (cursor_.starts_with("\"\"\"")) {
            refusal = here("a raw string literal");
            return false;
        }
        if (byte == '@' && cursor_.peek(1U) == '"') {
            cursor_.advance(2U);
            std::string value{};
            const bool closed = verbatim_string(value);
            if (!closed) {
                refusal = LexRefusal{line, column, "unterminated verbatim string"};
                return false;
            }
            push(CSharpTokenKind::string, offset, line, column, std::move(value));
            return true;
        }
        if (byte == '@' && identifier_start(cursor_.peek(1U))) {
            cursor_.advance();
            const std::size_t name = cursor_.offset();
            while (identifier_part(cursor_.peek())) cursor_.advance();
            push(CSharpTokenKind::identifier, name, line, column, std::string());
            return true;
        }
        if (identifier_start(byte)) {
            while (identifier_part(cursor_.peek())) cursor_.advance();
            push(CSharpTokenKind::identifier, offset, line, column, std::string());
            return true;
        }
        if (digit(byte) || (byte == '.' && digit(cursor_.peek(1U)))) {
            std::string value{};
            number(value);
            push(CSharpTokenKind::number, offset, line, column, std::move(value));
            return true;
        }
        if (byte == '"' || byte == '\'') {
            cursor_.advance();
            std::string value{};
            const unsigned char quote = byte;
            const bool read = quoted(quote, value, refusal);
            if (!read) return false;
            const CSharpTokenKind kind = quote == '"' ? CSharpTokenKind::string : CSharpTokenKind::character;
            push(kind, offset, line, column, std::move(value));
            return true;
        }
        for (const std::string_view pair : two_character_punctuation) {
            if (cursor_.starts_with(pair)) {
                cursor_.advance(2U);
                push(CSharpTokenKind::punctuation, offset, line, column, std::string());
                return true;
            }
        }
        constexpr std::string_view single = ".,;:()[]{}=+-*/%|&^!~<>?";
        if (single.find(static_cast<char>(byte)) != std::string_view::npos) {
            cursor_.advance();
            push(CSharpTokenKind::punctuation, offset, line, column, std::string());
            return true;
        }
        refusal = here("a byte that begins no C# token");
        return false;
    }

    // A verbatim string's body after `@"`: `""` is one quote, nothing else
    // is an escape. False when the input ends first.
    [[nodiscard]] bool verbatim_string(std::string& value) {
        while (!cursor_.done()) {
            const unsigned char byte = cursor_.peek();
            if (byte == '"') {
                if (cursor_.peek(1U) == '"') {
                    value.push_back('"');
                    cursor_.advance(2U);
                    continue;
                }
                cursor_.advance();
                return true;
            }
            value.push_back(static_cast<char>(byte));
            cursor_.advance();
        }
        return false;
    }

    // A regular string's or character's body after its opening quote, with
    // the escapes decoded; a line end inside is unterminated.
    [[nodiscard]] bool quoted(unsigned char quote, std::string& value, LexRefusal& refusal) {
        const char* const what = quote == '"' ? "unterminated string" : "unterminated character literal";
        while (true) {
            if (cursor_.done() || cursor_.peek() == '\n' || cursor_.peek() == '\r') {
                refusal = here(what);
                return false;
            }
            const unsigned char byte = cursor_.peek();
            if (byte == quote) {
                cursor_.advance();
                return true;
            }
            if (byte != '\\') {
                value.push_back(static_cast<char>(byte));
                cursor_.advance();
                continue;
            }
            const bool escaped = escape(value, refusal);
            if (!escaped) return false;
        }
    }

    // One escape at the backslash.
    [[nodiscard]] bool escape(std::string& value, LexRefusal& refusal) {
        const LexRefusal at = here("an escape C# does not have");
        cursor_.advance();
        const unsigned char letter = cursor_.peek();
        cursor_.advance();
        switch (letter) {
        case '\'': value.push_back('\''); return true;
        case '"': value.push_back('"'); return true;
        case '\\': value.push_back('\\'); return true;
        case '0': value.push_back('\0'); return true;
        case 'a': value.push_back('\a'); return true;
        case 'b': value.push_back('\b'); return true;
        case 'f': value.push_back('\f'); return true;
        case 'n': value.push_back('\n'); return true;
        case 'r': value.push_back('\r'); return true;
        case 't': value.push_back('\t'); return true;
        case 'v': value.push_back('\v'); return true;
        case 'u':
        case 'U':
        case 'x': {
            // \u has four digits, \U eight, \x one to four.
            const std::size_t most = letter == 'U' ? 8U : 4U;
            const std::size_t least = letter == 'x' ? 1U : most;
            std::uint32_t scalar = 0U;
            std::size_t count = 0U;
            while (count < most && hexadecimal_digit(cursor_.peek())) {
                const unsigned char hex = cursor_.peek();
                const std::uint32_t nibble = digit(hex) ? static_cast<std::uint32_t>(hex - '0')
                                                        : static_cast<std::uint32_t>((hex | 0x20U) - 'a' + 10U);
                scalar = (scalar << 4U) | nibble;
                count = count + 1U;
                cursor_.advance();
            }
            if (count < least || !append_utf8(value, scalar)) {
                refusal = at;
                return false;
            }
            return true;
        }
        default:
            refusal = at;
            return false;
        }
    }

    // A number: digits with '_' separators, a fraction, an exponent and a
    // suffix; `value` gets the digits without separators or suffix.
    void number(std::string& value) {
        if (cursor_.peek() == '0' && (cursor_.peek(1U) == 'x' || cursor_.peek(1U) == 'X')) {
            value.append("0x");
            cursor_.advance(2U);
            while (hexadecimal_digit(cursor_.peek()) || cursor_.peek() == '_') {
                if (cursor_.peek() != '_') value.push_back(static_cast<char>(cursor_.peek()));
                cursor_.advance();
            }
        } else {
            while (digit(cursor_.peek()) || cursor_.peek() == '_' ||
                   (cursor_.peek() == '.' && digit(cursor_.peek(1U)))) {
                if (cursor_.peek() != '_') value.push_back(static_cast<char>(cursor_.peek()));
                cursor_.advance();
            }
            const unsigned char exponent = cursor_.peek();
            const unsigned char after = cursor_.peek(1U);
            const bool signed_exponent = (after == '+' || after == '-') && digit(cursor_.peek(2U));
            if ((exponent == 'e' || exponent == 'E') && (digit(after) || signed_exponent)) {
                value.push_back('e');
                cursor_.advance();
                if (signed_exponent) {
                    value.push_back(static_cast<char>(after));
                    cursor_.advance();
                }
                while (digit(cursor_.peek())) {
                    value.push_back(static_cast<char>(cursor_.peek()));
                    cursor_.advance();
                }
            }
        }
        // The type suffix: F, D, M, L, U, UL and their lowercase.
        constexpr std::string_view suffixes = "fFdDmMlLuU";
        while (suffixes.find(static_cast<char>(cursor_.peek())) != std::string_view::npos && cursor_.peek() != 0U) {
            cursor_.advance();
        }
    }

    Cursor cursor_;
    std::vector<CSharpToken>& tokens_;
};

} // namespace

CSharpLexOutcome lex_csharp(CSharpSource& source, ConversionDiagnosticList& diagnostics) {
    source.tokens.clear();
    if (source.text.size() > maximum_source_bytes) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = ConversionCode::file_too_large;
        diagnostic.file = source.file;
        diagnostic.detail = std::to_string(source.text.size()) + " bytes; the converter reads at most " +
                            std::to_string(maximum_source_bytes);
        diagnostics.add(std::move(diagnostic));
        return CSharpLexOutcome::refused;
    }
    std::vector<CSharpToken> tokens{};
    Lexer lexer(source.text, tokens);
    LexRefusal refusal{};
    const bool lexed = lexer.run(refusal);
    if (!lexed) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = ConversionCode::csharp_lexical;
        diagnostic.file = source.file;
        diagnostic.line = refusal.line;
        diagnostic.column = refusal.column;
        diagnostic.detail = std::move(refusal.detail);
        diagnostics.add(std::move(diagnostic));
        return CSharpLexOutcome::refused;
    }
    source.tokens = std::move(tokens);
    return CSharpLexOutcome::lexed;
}

bool is_punctuation(const CSharpSource& source, const CSharpToken& token, std::string_view text) noexcept {
    const bool matches = token.kind == CSharpTokenKind::punctuation && source.spelling(token) == text;
    return matches;
}

bool is_identifier(const CSharpSource& source, const CSharpToken& token, std::string_view text) noexcept {
    const bool matches = token.kind == CSharpTokenKind::identifier && source.spelling(token) == text;
    return matches;
}

} // namespace gui_forms::winforms_converter
