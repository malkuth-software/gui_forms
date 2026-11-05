#include "theme_tokenizer.hpp"

#include "../number/theme_number.hpp"
#include "../../xml/xml_characters/xml_characters.hpp"

#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

// One code point read at a byte position: its scalar value and byte count.
// length is 0 at the end of the text, and for bytes that are not
// well-formed UTF-8 (those are diagnosed by the encoding pass).
struct CodePoint final {
    std::uint32_t scalar{};
    std::size_t length{};
};

[[nodiscard]] CodePoint code_point_at(std::string_view text, std::size_t offset) noexcept {
    CodePoint result{};
    if (offset >= text.size()) return result;
    const unsigned char byte = static_cast<unsigned char>(text[offset]);
    if (byte < 0x80U) {
        result.scalar = byte;
        result.length = 1U;
        return result;
    }
    const detail::XmlDecodedCharacter decoded = detail::decode_xml_utf8(text, offset);
    result.scalar = decoded.scalar;
    result.length = decoded.length;
    return result;
}

[[nodiscard]] bool digit(std::uint32_t scalar) noexcept { return scalar >= '0' && scalar <= '9'; }

[[nodiscard]] bool letter(std::uint32_t scalar) noexcept {
    const bool lower = scalar >= 'a' && scalar <= 'z';
    const bool upper = scalar >= 'A' && scalar <= 'Z';
    return lower || upper;
}

// CSS Syntax §4.2: an ident-start code point is a letter, '_' or any
// non-ASCII code point; an ident code point adds digits and '-'.
[[nodiscard]] bool name_start(CodePoint point) noexcept {
    if (point.length == 0U) return false;
    const bool start = letter(point.scalar) || point.scalar == '_' || point.scalar >= 0x80U;
    return start;
}

[[nodiscard]] bool name_character(CodePoint point) noexcept {
    const bool character = name_start(point) || digit(point.scalar) || point.scalar == '-';
    return point.length != 0U && character;
}

[[nodiscard]] bool whitespace_byte(char byte) noexcept {
    const bool space = byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
    return space;
}

[[nodiscard]] bool newline_byte(char byte) noexcept {
    const bool newline = byte == '\n' || byte == '\r' || byte == '\f';
    return newline;
}

[[nodiscard]] bool hex_byte(char byte) noexcept {
    const bool hex = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
    return hex;
}

[[nodiscard]] std::uint32_t hex_value(char byte) noexcept {
    if (byte >= '0' && byte <= '9') return static_cast<std::uint32_t>(byte - '0');
    if (byte >= 'a' && byte <= 'f') return static_cast<std::uint32_t>(byte - 'a' + 10);
    return static_cast<std::uint32_t>(byte - 'A' + 10);
}

// One escape read at its backslash (CSS Syntax 4.3.7): the code point it
// names and the byte after it. A hex escape is one to six hex digits and
// one optional whitespace (CR LF counting as one); any other escape is the
// backslash and one code point. `valid` is false for an escape of 0, of a
// surrogate or of a value past U+10FFFF.
struct Escape final {
    std::uint32_t scalar{};
    std::size_t end{};
    bool valid{};
};

[[nodiscard]] Escape read_escape(std::string_view text, std::size_t backslash) noexcept {
    Escape result{};
    std::size_t end = backslash + 1U;
    if (end < text.size() && hex_byte(text[end])) {
        std::uint32_t value = 0U;
        std::size_t digits = 0U;
        while (end < text.size() && digits < 6U && hex_byte(text[end])) {
            value = value * 16U + hex_value(text[end]);
            ++end;
            ++digits;
        }
        if (end + 1U < text.size() && text[end] == '\r' && text[end + 1U] == '\n') {
            end += 2U;
        } else if (end < text.size() && whitespace_byte(text[end])) {
            ++end;
        }
        result.scalar = value;
        result.end = end;
        result.valid = value != 0U && (value < 0xD800U || value > 0xDFFFU) && value <= 0x10FFFFU;
        return result;
    }
    const CodePoint escaped = code_point_at(text, end);
    result.scalar = escaped.scalar;
    result.end = end + escaped.length;
    result.valid = escaped.length != 0U && escaped.scalar != 0U;
    return result;
}

void append_utf8(std::uint32_t scalar, std::string& output) {
    if (scalar < 0x80U) {
        output.push_back(static_cast<char>(scalar));
        return;
    }
    if (scalar < 0x800U) {
        output.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
        return;
    }
    if (scalar < 0x10000U) {
        output.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
        return;
    }
    output.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
}

// The tokenizer over one sheet. It borrows the text, the output buffer and
// the diagnostics for one call of tokenize_theme_sheet; `cursor` borrows
// the line table, which outlives it.
class SheetTokenizer final {
public:
    SheetTokenizer(const ThemeSource& source, ThemeTokenBuffer& buffer, const LineStartTable& lines,
                   ThemeDiagnosticList& diagnostics, NameEscapes escapes) noexcept
        : source_(source), text_(source.text), buffer_(buffer), diagnostics_(diagnostics), cursor_(lines, text_),
          escapes_(escapes) {}

    void run();

private:
    [[nodiscard]] CodePoint at(std::size_t offset) const noexcept { return code_point_at(text_, offset); }
    [[nodiscard]] bool starts_with(std::size_t offset, std::string_view prefix) const noexcept {
        const bool matches = text_.substr(offset, prefix.size()) == prefix;
        return matches;
    }
    // CSS §4.3.9 and §4.3.10, with escapes where they are admitted.
    [[nodiscard]] bool starts_identifier(std::size_t offset) const noexcept;
    [[nodiscard]] bool starts_number(std::size_t offset) const noexcept;
    // Whether a valid escape's backslash is at `offset` (CSS 4.3.8): one
    // followed by a code point that is no line break, where escapes are
    // admitted.
    [[nodiscard]] bool starts_escape(std::size_t offset) const noexcept;
    // The end of the name starting at `offset`; `escaped` says whether it
    // holds an escape.
    [[nodiscard]] std::size_t name_end(std::size_t offset, bool& escaped) const noexcept;
    // Reports the first escape in [first, end) whose code point is not
    // admitted, and gives false then.
    [[nodiscard]] bool escapes_valid(std::size_t first, std::size_t end);
    // The end of the number starting at `offset` (CSS §4.3.12).
    [[nodiscard]] std::size_t number_end(std::size_t offset) const noexcept;

    void emit(TokenKind kind, std::size_t first, std::size_t end, std::uint8_t flags = 0U);
    void report(DiagnosticCode code, std::size_t offset, std::string detail);

    // Each consumes one token at `index_` and advances past it.
    void consume_whitespace();
    void consume_comment();
    void consume_string();
    void consume_hash();
    void consume_at_keyword();
    void consume_ident_like();
    void consume_bad_url(std::size_t first);
    void consume_numeric();
    void consume_invalid_bytes();

    const ThemeSource& source_;
    std::string_view text_{};
    ThemeTokenBuffer& buffer_;
    ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    NameEscapes escapes_{NameEscapes::refused};
    std::size_t index_{};
};

bool SheetTokenizer::starts_escape(std::size_t offset) const noexcept {
    if (escapes_ != NameEscapes::in_names) return false;
    if (offset + 1U >= text_.size() || text_[offset] != '\\') return false;
    const bool starts = !newline_byte(text_[offset + 1U]) && at(offset + 1U).length != 0U;
    return starts;
}

bool SheetTokenizer::starts_identifier(std::size_t offset) const noexcept {
    const CodePoint first = at(offset);
    if (first.length == 0U) return false;
    if (first.scalar == '-') {
        const CodePoint second = at(offset + 1U);
        const bool starts =
            name_start(second) || (second.length != 0U && second.scalar == '-') || starts_escape(offset + 1U);
        return starts;
    }
    return name_start(first) || starts_escape(offset);
}

bool SheetTokenizer::starts_number(std::size_t offset) const noexcept {
    const CodePoint first = at(offset);
    if (first.length == 0U) return false;
    const CodePoint second = at(offset + 1U);
    if (first.scalar == '+' || first.scalar == '-') {
        if (second.length != 0U && digit(second.scalar)) return true;
        const CodePoint third = at(offset + 2U);
        const bool point_then_digit =
            second.length != 0U && second.scalar == '.' && third.length != 0U && digit(third.scalar);
        return point_then_digit;
    }
    if (first.scalar == '.') {
        const bool fraction = second.length != 0U && digit(second.scalar);
        return fraction;
    }
    return digit(first.scalar);
}

std::size_t SheetTokenizer::name_end(std::size_t offset, bool& escaped) const noexcept {
    std::size_t end = offset;
    escaped = false;
    while (end < text_.size()) {
        if (starts_escape(end)) {
            escaped = true;
            end = read_escape(text_, end).end;
            continue;
        }
        const CodePoint point = at(end);
        if (!name_character(point)) break;
        end += point.length;
    }
    return end;
}

bool SheetTokenizer::escapes_valid(std::size_t first, std::size_t end) {
    std::size_t index = first;
    while (index < end) {
        if (text_[index] != '\\') {
            ++index;
            continue;
        }
        const Escape escape = read_escape(text_, index);
        if (!escape.valid) {
            report(DiagnosticCode::escape_not_admitted, index, std::string(text_.substr(index, escape.end - index)));
            return false;
        }
        index = escape.end;
    }
    return true;
}

std::size_t SheetTokenizer::number_end(std::size_t offset) const noexcept {
    const std::size_t size = text_.size();
    std::size_t end = offset;
    if (end < size && (text_[end] == '+' || text_[end] == '-')) ++end;
    while (end < size && digit(static_cast<unsigned char>(text_[end]))) ++end;
    if (end + 1U < size && text_[end] == '.' && digit(static_cast<unsigned char>(text_[end + 1U]))) {
        end += 1U;
        while (end < size && digit(static_cast<unsigned char>(text_[end]))) ++end;
    }
    if (end < size && (text_[end] == 'e' || text_[end] == 'E')) {
        std::size_t exponent = end + 1U;
        if (exponent < size && (text_[exponent] == '+' || text_[exponent] == '-')) ++exponent;
        if (exponent < size && digit(static_cast<unsigned char>(text_[exponent]))) {
            end = exponent;
            while (end < size && digit(static_cast<unsigned char>(text_[end]))) ++end;
        }
    }
    return end;
}

void SheetTokenizer::emit(TokenKind kind, std::size_t first, std::size_t end, std::uint8_t flags) {
    Token token{};
    token.kind = kind;
    token.flags = flags;
    token.offset = ByteOffset{static_cast<std::uint32_t>(first)};
    token.length = ByteLength{static_cast<std::uint32_t>(end - first)};
    buffer_.tokens.push_back(token);
    index_ = end;
}

void SheetTokenizer::report(DiagnosticCode code, std::size_t offset, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

void SheetTokenizer::consume_whitespace() {
    const std::size_t first = index_;
    std::size_t end = first;
    while (end < text_.size() && whitespace_byte(text_[end])) ++end;
    emit(TokenKind::whitespace, first, end);
}

void SheetTokenizer::consume_comment() {
    const std::size_t first = index_;
    const std::size_t close = text_.find("*/", first + 2U);
    if (close == std::string_view::npos) {
        report(DiagnosticCode::unterminated_comment, first, {});
        emit(TokenKind::invalid, first, text_.size());
        return;
    }
    emit(TokenKind::comment, first, close + 2U);
}

void SheetTokenizer::consume_string() {
    const std::size_t first = index_;
    const char quote = text_[first];
    const std::size_t size = text_.size();
    std::size_t end = first + 1U;
    bool closed = false;
    while (end < size) {
        const char byte = text_[end];
        if (byte == quote) {
            closed = true;
            break;
        }
        // An unescaped line break ends the string unterminated (CSS's bad
        // string), before the break.
        if (newline_byte(byte)) break;
        if (byte == '\\') {
            ++end;
            if (end >= size) break;
            // An escaped line break continues the string; CR LF is one.
            if (text_[end] == '\r' && end + 1U < size && text_[end + 1U] == '\n') {
                end += 2U;
                continue;
            }
            const CodePoint escaped = at(end);
            end += escaped.length == 0U ? 1U : escaped.length;
            continue;
        }
        ++end;
    }
    if (!closed) {
        report(DiagnosticCode::unterminated_string, first, {});
        emit(TokenKind::invalid, first, end);
        return;
    }
    const std::size_t content = end - first - 1U;
    if (content > maximum_string_bytes) {
        report(DiagnosticCode::string_too_long, first, std::to_string(content));
        emit(TokenKind::invalid, first, end + 1U);
        return;
    }
    emit(TokenKind::string, first, end + 1U);
}

void SheetTokenizer::consume_hash() {
    const std::size_t first = index_;
    const CodePoint next = at(first + 1U);
    if (!name_character(next) && !starts_escape(first + 1U)) {
        emit(TokenKind::delim, first, first + 1U);
        return;
    }
    std::uint8_t flags = starts_identifier(first + 1U) ? token_flag_hash_identifier : std::uint8_t{0U};
    bool escaped = false;
    const std::size_t end = name_end(first + 1U, escaped);
    if (escaped && !escapes_valid(first, end)) {
        emit(TokenKind::invalid, first, end);
        return;
    }
    if (escaped) flags = static_cast<std::uint8_t>(flags | token_flag_escaped);
    emit(TokenKind::hash, first, end, flags);
}

void SheetTokenizer::consume_at_keyword() {
    const std::size_t first = index_;
    if (!starts_identifier(first + 1U)) {
        emit(TokenKind::delim, first, first + 1U);
        return;
    }
    bool escaped = false;
    const std::size_t end = name_end(first + 1U, escaped);
    if (escaped && !escapes_valid(first, end)) {
        emit(TokenKind::invalid, first, end);
        return;
    }
    emit(TokenKind::at_keyword, first, end, escaped ? token_flag_escaped : std::uint8_t{0U});
}

void SheetTokenizer::consume_bad_url(std::size_t first) {
    // CSS §4.3.14: the remnants run to the closing parenthesis, or the end.
    const std::size_t close = text_.find(')', first);
    const std::size_t end = close == std::string_view::npos ? text_.size() : close + 1U;
    report(DiagnosticCode::bad_url, first, {});
    emit(TokenKind::invalid, first, end);
}

void SheetTokenizer::consume_ident_like() {
    const std::size_t first = index_;
    bool escaped = false;
    const std::size_t end = name_end(first, escaped);
    if (escaped && !escapes_valid(first, end)) {
        emit(TokenKind::invalid, first, end);
        return;
    }
    const std::uint8_t flags = escaped ? token_flag_escaped : std::uint8_t{0U};
    if (end >= text_.size() || text_[end] != '(') {
        emit(TokenKind::ident, first, end, flags);
        return;
    }
    const std::string_view name = text_.substr(first, end - first);
    const bool url = name.size() == 3U && (name[0] == 'u' || name[0] == 'U') && (name[1] == 'r' || name[1] == 'R') &&
                     (name[2] == 'l' || name[2] == 'L');
    if (url) {
        // Only url("...") is admitted (PLAN.md section 1.2): after the
        // parenthesis and any whitespace, a quote must follow.
        std::size_t next = end + 1U;
        while (next < text_.size() && whitespace_byte(text_[next])) ++next;
        const bool quoted = next < text_.size() && (text_[next] == '"' || text_[next] == '\'');
        if (!quoted) {
            consume_bad_url(first);
            return;
        }
    }
    emit(TokenKind::function, first, end + 1U, flags);
}

void SheetTokenizer::consume_numeric() {
    const std::size_t first = index_;
    const std::size_t numeric = number_end(first);
    const std::string_view number_text = text_.substr(first, numeric - first);
    const NumberConversion conversion = convert_theme_number(number_text);
    TokenKind kind = TokenKind::number;
    std::size_t end = numeric;
    bool escaped = false;
    if (starts_identifier(numeric)) {
        kind = TokenKind::dimension;
        end = name_end(numeric, escaped);
    }
    if (escaped && !escapes_valid(numeric, end)) {
        emit(TokenKind::invalid, first, end);
        return;
    } else if (numeric < text_.size() && text_[numeric] == '%') {
        kind = TokenKind::percentage;
        end = numeric + 1U;
    }
    if (conversion.defect != NumberDefect::none) {
        const DiagnosticCode code = conversion.defect == NumberDefect::too_precise ? DiagnosticCode::number_too_precise
                                                                                   : DiagnosticCode::number_out_of_range;
        report(code, first, std::string(number_text));
        emit(TokenKind::invalid, first, end);
        return;
    }
    ThemeNumber value{};
    value.value = conversion.value;
    value.numeric_length = ByteLength{static_cast<std::uint32_t>(numeric - first)};
    value.integer = conversion.integer;
    const std::uint32_t position = static_cast<std::uint32_t>(buffer_.numbers.size());
    buffer_.numbers.push_back(value);
    emit(kind, first, end, escaped ? token_flag_escaped : std::uint8_t{0U});
    buffer_.tokens.back().number = position;
}

void SheetTokenizer::consume_invalid_bytes() {
    // A run of bytes that are not well-formed UTF-8, or NUL bytes; the
    // encoding pass has reported them.
    const std::size_t first = index_;
    std::size_t end = first;
    while (end < text_.size()) {
        if (text_[end] == '\0') {
            ++end;
            continue;
        }
        const CodePoint point = at(end);
        if (point.length != 0U) break;
        ++end;
    }
    emit(TokenKind::invalid, first, end);
}

void SheetTokenizer::run() {
    const std::size_t size = text_.size();
    if (starts_with(0U, "\xEF\xBB\xBF")) emit(TokenKind::byte_order_mark, 0U, 3U);
    while (index_ < size) {
        const char byte = text_[index_];
        const std::size_t first = index_;
        if (whitespace_byte(byte)) {
            consume_whitespace();
        } else if (starts_with(first, "/*")) {
            consume_comment();
        } else if (byte == '"' || byte == '\'') {
            consume_string();
        } else if (byte == '#') {
            consume_hash();
        } else if (byte == '(') {
            emit(TokenKind::left_paren, first, first + 1U);
        } else if (byte == ')') {
            emit(TokenKind::right_paren, first, first + 1U);
        } else if (byte == '[') {
            emit(TokenKind::left_square, first, first + 1U);
        } else if (byte == ']') {
            emit(TokenKind::right_square, first, first + 1U);
        } else if (byte == '{') {
            emit(TokenKind::left_brace, first, first + 1U);
        } else if (byte == '}') {
            emit(TokenKind::right_brace, first, first + 1U);
        } else if (byte == ',') {
            emit(TokenKind::comma, first, first + 1U);
        } else if (byte == ':') {
            emit(TokenKind::colon, first, first + 1U);
        } else if (byte == ';') {
            emit(TokenKind::semicolon, first, first + 1U);
        } else if (starts_with(first, "<!--") || starts_with(first, "-->")) {
            const std::size_t length = byte == '<' ? 4U : 3U;
            report(DiagnosticCode::token_not_admitted, first, std::string(text_.substr(first, length)));
            emit(TokenKind::invalid, first, first + length);
        } else if (starts_number(first)) {
            consume_numeric();
        } else if (starts_identifier(first)) {
            consume_ident_like();
        } else if (byte == '@') {
            consume_at_keyword();
        } else if (byte == '\\') {
            report(DiagnosticCode::escape_not_admitted, first, {});
            emit(TokenKind::invalid, first, first + 1U);
        } else if (byte == '\0' || at(first).length == 0U) {
            consume_invalid_bytes();
        } else {
            // Every non-ASCII code point starts a name, so a delim is ASCII.
            emit(TokenKind::delim, first, first + 1U);
        }
    }
}

// The encoding pass (house style §8: validate the whole buffer before the
// kernel): one diagnostic per run of ill-formed UTF-8 bytes and per NUL.
void report_encoding_defects(const ThemeSource& source, const LineStartTable& lines,
                             ThemeDiagnosticList& diagnostics) {
    const std::string_view text = source.text;
    SourcePositionCursor cursor(lines, text);
    std::size_t index = 0U;
    bool in_invalid_run = false;
    while (index < text.size()) {
        if (text[index] == '\0') {
            ThemeDiagnostic diagnostic{};
            diagnostic.code = DiagnosticCode::nul_byte;
            diagnostic.file = std::string(source.file);
            diagnostic.position = cursor.locate(ByteOffset{static_cast<std::uint32_t>(index)});
            diagnostics.add(std::move(diagnostic));
            in_invalid_run = false;
            ++index;
            continue;
        }
        const CodePoint point = code_point_at(text, index);
        if (point.length != 0U) {
            in_invalid_run = false;
            index += point.length;
            continue;
        }
        if (!in_invalid_run) {
            ThemeDiagnostic diagnostic{};
            diagnostic.code = DiagnosticCode::invalid_utf8;
            diagnostic.file = std::string(source.file);
            diagnostic.position = cursor.locate(ByteOffset{static_cast<std::uint32_t>(index)});
            diagnostics.add(std::move(diagnostic));
            in_invalid_run = true;
        }
        ++index;
    }
}

} // namespace

std::string_view token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::byte_order_mark: return "byte_order_mark";
    case TokenKind::whitespace: return "whitespace";
    case TokenKind::comment: return "comment";
    case TokenKind::ident: return "ident";
    case TokenKind::function: return "function";
    case TokenKind::at_keyword: return "at_keyword";
    case TokenKind::hash: return "hash";
    case TokenKind::string: return "string";
    case TokenKind::number: return "number";
    case TokenKind::percentage: return "percentage";
    case TokenKind::dimension: return "dimension";
    case TokenKind::delim: return "delim";
    case TokenKind::colon: return "colon";
    case TokenKind::semicolon: return "semicolon";
    case TokenKind::comma: return "comma";
    case TokenKind::left_square: return "left_square";
    case TokenKind::right_square: return "right_square";
    case TokenKind::left_paren: return "left_paren";
    case TokenKind::right_paren: return "right_paren";
    case TokenKind::left_brace: return "left_brace";
    case TokenKind::right_brace: return "right_brace";
    case TokenKind::invalid: return "invalid";
    }
    return "unknown";
}

void append_decoded_name(std::string_view written, std::string& output) {
    std::size_t index = 0U;
    while (index < written.size()) {
        const std::size_t backslash = written.find('\\', index);
        const std::size_t plain_end = backslash == std::string_view::npos ? written.size() : backslash;
        output.append(written.substr(index, plain_end - index));
        if (plain_end == written.size()) return;
        const Escape escape = read_escape(written, backslash);
        append_utf8(escape.scalar, output);
        index = escape.end;
    }
}

TokenizeOutcome tokenize_theme_sheet(const ThemeSource& source, ThemeTokenBuffer& buffer, LineStartTable& lines,
                                     ThemeDiagnosticList& diagnostics, NameEscapes escapes) {
    buffer.tokens.clear();
    buffer.numbers.clear();
    lines.clear();
    const std::string_view text = source.text;
    if (text.size() > maximum_sheet_bytes) {
        ThemeDiagnostic diagnostic{};
        diagnostic.code = DiagnosticCode::sheet_too_large;
        diagnostic.file = std::string(source.file);
        diagnostic.detail = std::to_string(text.size());
        diagnostics.add(std::move(diagnostic));
        return TokenizeOutcome::refused_too_large;
    }
    lines.reset(text);
    // Every token holds at least one byte; a number takes at least two
    // bytes but the first.
    buffer.tokens.reserve(text.size());
    buffer.numbers.reserve(text.size() / 2U + 1U);
    report_encoding_defects(source, lines, diagnostics);
    SheetTokenizer tokenizer(source, buffer, lines, diagnostics, escapes);
    tokenizer.run();
    return TokenizeOutcome::tokens_cover_sheet;
}

} // namespace gui_forms::theme_language
