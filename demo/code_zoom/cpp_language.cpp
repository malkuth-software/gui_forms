#include "cpp_language.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace code_highlight {
namespace {

// -- the state a line leaves --------------------------------------------------------------
//
// Bits 0-3: the mode. For a raw string, bits 4-8 hold the delimiter's
// length and bits 9-63 its bytes (at most kept_delimiter_bytes, the first
// lowest) or, for a longer one, a 55-bit FNV-1a hash of it.

enum class LineMode : std::uint8_t {
    code = 0,
    block_comment = 1,
    line_comment = 2,
    directive = 3,
    string = 4,
    raw_string = 5,
};

constexpr std::uint64_t mode_mask = 0xFU;
constexpr std::uint32_t delimiter_length_shift = 4U;
constexpr std::uint64_t delimiter_length_mask = 0x1FU;
constexpr std::uint32_t delimiter_shift = 9U;
constexpr std::size_t kept_delimiter_bytes = 6U;
constexpr std::size_t maximum_raw_delimiter = 16U;
constexpr std::uint64_t delimiter_hash_mask = (std::uint64_t{1} << 55U) - 1U;
constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

[[nodiscard]] LineMode mode_of(const LexState state) noexcept {
    const std::uint64_t bits = state.value & mode_mask;
    switch (bits) {
    case 1U: return LineMode::block_comment;
    case 2U: return LineMode::line_comment;
    case 3U: return LineMode::directive;
    case 4U: return LineMode::string;
    case 5U: return LineMode::raw_string;
    default: return LineMode::code;
    }
}

[[nodiscard]] LexState state_of(const LineMode mode) noexcept {
    const LexState state{static_cast<std::uint64_t>(mode)};
    return state;
}

// The state an open raw string with `delimiter` (at most
// maximum_raw_delimiter bytes) carries. Two delimiters give the same state
// exactly when they are equal, up to the hash for the long ones.
[[nodiscard]] LexState raw_state(const std::string_view delimiter) noexcept {
    std::uint64_t payload = 0U;
    if (delimiter.size() <= kept_delimiter_bytes) {
        for (std::size_t index = 0U; index < delimiter.size(); ++index) {
            const std::uint64_t byte = static_cast<unsigned char>(delimiter[index]);
            payload |= byte << (8U * index);
        }
    } else {
        std::uint64_t hash = fnv_offset;
        for (const char character : delimiter) {
            hash ^= static_cast<unsigned char>(character);
            hash *= fnv_prime;
        }
        payload = hash & delimiter_hash_mask;
    }
    const std::uint64_t length = static_cast<std::uint64_t>(delimiter.size()) & delimiter_length_mask;
    const LexState state{static_cast<std::uint64_t>(LineMode::raw_string) | (length << delimiter_length_shift) |
                         (payload << delimiter_shift)};
    return state;
}

[[nodiscard]] std::size_t delimiter_length_of(const LexState state) noexcept {
    const std::uint64_t bits = (state.value >> delimiter_length_shift) & delimiter_length_mask;
    const std::size_t length = static_cast<std::size_t>(bits);
    return length;
}

// -- character classes and names ----------------------------------------------------------

[[nodiscard]] bool is_digit(const char character) noexcept { return character >= '0' && character <= '9'; }

[[nodiscard]] bool is_identifier_start(const char character) noexcept {
    const unsigned char byte = static_cast<unsigned char>(character);
    const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    // Bytes of UTF-8 sequences take part in names, as C++23's identifiers allow.
    return letter || character == '_' || character == '$' || byte >= 0x80U;
}

[[nodiscard]] bool is_identifier_part(const char character) noexcept {
    return is_identifier_start(character) || is_digit(character);
}

[[nodiscard]] bool is_space(const char character) noexcept {
    return character == ' ' || character == '\t' || character == '\v' || character == '\f' || character == '\r';
}

[[nodiscard]] bool is_punctuation(const char character) noexcept {
    constexpr std::string_view punctuation = "{}()[];,";
    return punctuation.find(character) != std::string_view::npos;
}

[[nodiscard]] bool is_operator(const char character) noexcept {
    constexpr std::string_view operators = "+-*/%=<>!&|^~?:.#";
    return operators.find(character) != std::string_view::npos;
}

// A raw string delimiter's byte: any but space, the parentheses, the
// backslash and the control characters ([lex.string] d-char).
[[nodiscard]] bool is_delimiter_byte(const char character) noexcept {
    const unsigned char byte = static_cast<unsigned char>(character);
    return byte > 0x20U && byte < 0x7FU && character != '(' && character != ')' && character != '\\';
}

constexpr std::array<std::string_view, 82> keywords{
    "alignas",         "alignof",         "and",             "and_eq",          "asm",
    "auto",            "bitand",          "bitor",           "break",           "case",
    "catch",           "class",           "co_await",        "co_return",       "co_yield",
    "compl",           "concept",         "const",           "const_cast",      "consteval",
    "constexpr",       "constinit",       "continue",        "decltype",        "default",
    "delete",          "do",              "dynamic_cast",    "else",            "enum",
    "explicit",        "export",          "extern",          "false",           "final",
    "for",             "friend",          "goto",            "if",              "import",
    "inline",          "module",          "mutable",         "namespace",       "new",
    "noexcept",        "not",             "not_eq",          "nullptr",         "operator",
    "or",              "or_eq",           "override",        "private",         "protected",
    "public",          "register",        "reinterpret_cast","requires",        "return",
    "sizeof",          "static",          "static_assert",   "static_cast",     "struct",
    "switch",          "template",        "this",            "thread_local",    "throw",
    "true",            "try",             "typedef",         "typeid",          "typename",
    "union",           "using",           "virtual",         "volatile",        "while",
    "xor",             "xor_eq",
};

constexpr std::array<std::string_view, 41> type_names{
    "array",      "bool",       "byte",       "char",       "char16_t",   "char32_t",
    "char8_t",    "double",     "float",      "int",        "int16_t",    "int32_t",
    "int64_t",    "int8_t",     "intmax_t",   "intptr_t",   "long",       "max_align_t",
    "nullptr_t",  "optional",   "ptrdiff_t",  "shared_ptr", "short",      "signed",
    "size_t",     "span",       "string",     "string_view","uint16_t",   "uint32_t",
    "uint64_t",   "uint8_t",    "uintmax_t",  "uintptr_t",  "unique_ptr", "unsigned",
    "variant",    "vector",     "void",       "wchar_t",    "weak_ptr",
};

// Binary search needs them in order.
static_assert(std::is_sorted(keywords.begin(), keywords.end()));
static_assert(std::is_sorted(type_names.begin(), type_names.end()));

// A name the house style spells as a type: an upper-case first letter and a
// lower-case letter after it (Painter, FontSpec; not SIZE_MAX).
[[nodiscard]] bool capitalized_type_name(const std::string_view name) noexcept {
    if (name.empty() || name[0] < 'A' || name[0] > 'Z') return false;
    const std::size_t lower = name.find_first_of("abcdefghijklmnopqrstuvwxyz");
    return lower != std::string_view::npos;
}

[[nodiscard]] TokenKind name_kind(const std::string_view name) noexcept {
    if (std::binary_search(keywords.begin(), keywords.end(), name)) return TokenKind::keyword;
    if (std::binary_search(type_names.begin(), type_names.end(), name)) return TokenKind::type;
    if (capitalized_type_name(name)) return TokenKind::type;
    return TokenKind::identifier;
}

[[nodiscard]] bool string_prefix(const std::string_view name) noexcept {
    return name == "L" || name == "u" || name == "U" || name == "u8";
}

[[nodiscard]] bool raw_string_prefix(const std::string_view name) noexcept {
    return name == "R" || name == "LR" || name == "uR" || name == "UR" || name == "u8R";
}

// -- one line -----------------------------------------------------------------------------
//
// Each take_ member lexes one construct beginning at its first argument,
// appends its token and returns the position after it; one that runs to
// the line's end with the construct still open sets continued_ to the state
// the next line enters.

class LineLexer final {
public:
    LineLexer(std::string_view line, std::vector<LineToken>& tokens) noexcept : line_(line), tokens_(tokens) {}

    [[nodiscard]] LexState run(LexState entry);

private:
    void emit(std::size_t start, std::size_t end, TokenKind kind);
    [[nodiscard]] bool ends_with_splice() const noexcept;
    [[nodiscard]] std::size_t take_name_end(std::size_t start) const noexcept;
    std::size_t take_block_comment(std::size_t start, std::size_t body);
    std::size_t take_line_comment(std::size_t start);
    std::size_t take_quoted(std::size_t start, std::size_t body, char quote, TokenKind kind);
    std::size_t take_raw_string(std::size_t start, std::size_t quote);
    std::size_t take_raw_body(std::size_t start, std::size_t body, LexState delimiter);
    std::size_t take_number(std::size_t start);
    std::size_t take_name(std::size_t start);
    std::size_t take_directive(std::size_t start);
    std::size_t take_operators(std::size_t start);
    // Lexes code from `start` to the line's end.
    void lex_code(std::size_t start, bool directive_allowed);

    std::string_view line_;
    std::vector<LineToken>& tokens_;
    bool continued_{};
    LexState continued_state_{};
    bool in_directive_{};
    bool in_attribute_{};
};

void LineLexer::emit(const std::size_t start, const std::size_t end, const TokenKind kind) {
    if (end <= start) return;
    const LineToken token{static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end - start), kind};
    tokens_.push_back(token);
}

bool LineLexer::ends_with_splice() const noexcept { return !line_.empty() && line_.back() == '\\'; }

std::size_t LineLexer::take_name_end(std::size_t start) const noexcept {
    std::size_t end = start;
    while (end < line_.size() && is_identifier_part(line_[end])) ++end;
    return end;
}

std::size_t LineLexer::take_block_comment(const std::size_t start, const std::size_t body) {
    const std::size_t close = line_.find("*/", body);
    if (close == std::string_view::npos) {
        emit(start, line_.size(), TokenKind::comment);
        continued_ = true;
        continued_state_ = state_of(LineMode::block_comment);
        return line_.size();
    }
    const std::size_t end = close + 2U;
    emit(start, end, TokenKind::comment);
    return end;
}

std::size_t LineLexer::take_line_comment(const std::size_t start) {
    emit(start, line_.size(), TokenKind::comment);
    if (ends_with_splice()) {
        continued_ = true;
        continued_state_ = state_of(LineMode::line_comment);
    }
    return line_.size();
}

// `body` is the first byte after the opening quote. An escape skips the byte
// after its backslash; a backslash ending the line continues an ordinary
// string. A closed literal takes its user-defined suffix.
std::size_t LineLexer::take_quoted(const std::size_t start, const std::size_t body, const char quote,
                                   const TokenKind kind) {
    std::size_t index = body;
    while (index < line_.size()) {
        const char character = line_[index];
        if (character == '\\') {
            if (index + 1U == line_.size()) {
                emit(start, line_.size(), kind);
                continued_ = true;
                continued_state_ = state_of(LineMode::string);
                return line_.size();
            }
            index += 2U;
            continue;
        }
        if (character == quote) {
            const std::size_t end = take_name_end(index + 1U);
            emit(start, end, kind);
            return end;
        }
        index += 1U;
    }
    emit(start, line_.size(), TokenKind::invalid);
    return line_.size();
}

// `quote` is the opening '"' after the prefix. A delimiter that is too long
// or holds a byte a delimiter cannot, or no '(' after it, makes the rest of
// the line invalid.
std::size_t LineLexer::take_raw_string(const std::size_t start, const std::size_t quote) {
    std::size_t index = quote + 1U;
    while (index < line_.size() && index - quote - 1U <= maximum_raw_delimiter && is_delimiter_byte(line_[index])) {
        ++index;
    }
    const std::size_t length = index - quote - 1U;
    if (index >= line_.size() || line_[index] != '(' || length > maximum_raw_delimiter) {
        emit(start, line_.size(), TokenKind::invalid);
        return line_.size();
    }
    const LexState delimiter = raw_state(line_.substr(quote + 1U, length));
    const std::size_t end = take_raw_body(start, index + 1U, delimiter);
    return end;
}

// Finds `)delim"` for the delimiter `delimiter` names, from `body`.
std::size_t LineLexer::take_raw_body(const std::size_t start, const std::size_t body, const LexState delimiter) {
    const std::size_t length = delimiter_length_of(delimiter);
    std::size_t close = line_.find(')', body);
    while (close != std::string_view::npos) {
        const std::size_t quote = close + 1U + length;
        if (quote < line_.size() && line_[quote] == '"' && raw_state(line_.substr(close + 1U, length)) == delimiter) {
            const std::size_t end = take_name_end(quote + 1U);
            emit(start, end, TokenKind::string);
            return end;
        }
        close = line_.find(')', close + 1U);
    }
    emit(start, line_.size(), TokenKind::string);
    continued_ = true;
    continued_state_ = delimiter;
    return line_.size();
}

// A pp-number: a digit (or a '.' before one), then digits, letters, '_',
// '.', an exponent's sign after e, E, p or P, and a digit separator before a
// letter or digit.
std::size_t LineLexer::take_number(const std::size_t start) {
    std::size_t index = start + 1U;
    while (index < line_.size()) {
        const char character = line_[index];
        const bool has_next = index + 1U < line_.size();
        const char next = has_next ? line_[index + 1U] : '\0';
        const bool exponent = character == 'e' || character == 'E' || character == 'p' || character == 'P';
        if (exponent && (next == '+' || next == '-')) {
            index += 2U;
        } else if (is_identifier_part(character) || character == '.') {
            index += 1U;
        } else if (character == '\'' && has_next && is_identifier_part(next)) {
            index += 2U;
        } else {
            break;
        }
    }
    emit(start, index, TokenKind::number);
    return index;
}

std::size_t LineLexer::take_name(const std::size_t start) {
    const std::size_t end = take_name_end(start);
    const std::string_view name = line_.substr(start, end - start);
    const char after = end < line_.size() ? line_[end] : '\0';
    if (after == '"' && raw_string_prefix(name)) return take_raw_string(start, end);
    if (after == '"' && string_prefix(name)) return take_quoted(start, end + 1U, '"', TokenKind::string);
    if (after == '\'' && string_prefix(name)) return take_quoted(start, end + 1U, '\'', TokenKind::character);
    const TokenKind kind = in_attribute_ ? TokenKind::attribute : name_kind(name);
    emit(start, end, kind);
    return end;
}

// '#', spaces and the directive's name; an #include's <header> follows as a
// string.
std::size_t LineLexer::take_directive(const std::size_t start) {
    std::size_t index = start + 1U;
    while (index < line_.size() && is_space(line_[index])) ++index;
    const std::size_t name_end = take_name_end(index);
    const std::string_view name = line_.substr(index, name_end - index);
    emit(start, name_end, TokenKind::preprocessor);
    in_directive_ = true;
    if (name != "include" && name != "include_next" && name != "import") return name_end;
    std::size_t header = name_end;
    while (header < line_.size() && is_space(line_[header])) ++header;
    if (header >= line_.size() || line_[header] != '<') return name_end;
    const std::size_t close = line_.find('>', header + 1U);
    if (close == std::string_view::npos) return name_end;
    emit(header, close + 1U, TokenKind::string);
    return close + 1U;
}

// A run of operator characters, ending before a comment's opening or a
// number's leading '.'.
std::size_t LineLexer::take_operators(const std::size_t start) {
    std::size_t index = start + 1U;
    while (index < line_.size() && is_operator(line_[index])) {
        const char character = line_[index];
        const char next = index + 1U < line_.size() ? line_[index + 1U] : '\0';
        const bool comment = character == '/' && (next == '/' || next == '*');
        const bool number = character == '.' && is_digit(next);
        if (comment || number) break;
        ++index;
    }
    emit(start, index, TokenKind::operator_symbol);
    return index;
}

void LineLexer::lex_code(const std::size_t start, const bool directive_allowed) {
    const std::size_t first_code = line_.find_first_not_of(" \t\v\f\r");
    std::size_t index = start;
    while (index < line_.size()) {
        const char character = line_[index];
        const char next = index + 1U < line_.size() ? line_[index + 1U] : '\0';
        if (is_space(character)) {
            index += 1U;
        } else if (character == '/' && next == '/') {
            index = take_line_comment(index);
        } else if (character == '/' && next == '*') {
            index = take_block_comment(index, index + 2U);
        } else if (character == '#' && directive_allowed && index == first_code) {
            index = take_directive(index);
        } else if (is_digit(character) || (character == '.' && is_digit(next))) {
            index = take_number(index);
        } else if (is_identifier_start(character)) {
            index = take_name(index);
        } else if (character == '"') {
            index = take_quoted(index, index + 1U, '"', TokenKind::string);
        } else if (character == '\'') {
            index = take_quoted(index, index + 1U, '\'', TokenKind::character);
        } else if (character == '[' && next == '[' && !in_attribute_) {
            emit(index, index + 2U, TokenKind::attribute);
            in_attribute_ = true;
            index += 2U;
        } else if (character == ']' && next == ']' && in_attribute_) {
            emit(index, index + 2U, TokenKind::attribute);
            in_attribute_ = false;
            index += 2U;
        } else if (is_punctuation(character)) {
            emit(index, index + 1U, TokenKind::punctuation);
            index += 1U;
        } else if (is_operator(character)) {
            index = take_operators(index);
        } else {
            // A splice's backslash, a backtick, '@' or a control byte.
            const TokenKind kind =
                character == '\\' && index + 1U == line_.size() ? TokenKind::operator_symbol : TokenKind::invalid;
            emit(index, index + 1U, kind);
            index += 1U;
        }
    }
}

LexState LineLexer::run(const LexState entry) {
    std::size_t start = 0U;
    bool directive_allowed = true;
    switch (mode_of(entry)) {
    case LineMode::block_comment:
        start = take_block_comment(0U, 0U);
        directive_allowed = false;
        break;
    case LineMode::line_comment:
        start = take_line_comment(0U);
        break;
    case LineMode::string:
        start = take_quoted(0U, 0U, '"', TokenKind::string);
        directive_allowed = false;
        break;
    case LineMode::raw_string:
        start = take_raw_body(0U, 0U, entry);
        directive_allowed = false;
        break;
    case LineMode::directive:
        in_directive_ = true;
        directive_allowed = false;
        break;
    default:
        break;
    }
    if (!continued_) lex_code(start, directive_allowed);
    if (continued_) return continued_state_;
    const LineMode exit = in_directive_ && ends_with_splice() ? LineMode::directive : LineMode::code;
    const LexState state = state_of(exit);
    return state;
}

} // namespace

LexState CppLanguage::lex_line(const std::string_view line, const LexState entry,
                               std::vector<LineToken>& tokens) const {
    LineLexer lexer(line, tokens);
    const LexState exit = lexer.run(entry);
    return exit;
}

const CppLanguage& cpp_language() noexcept {
    static const CppLanguage language{};
    return language;
}

std::string_view token_kind_name(const TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::comment: return "comment";
    case TokenKind::string: return "string";
    case TokenKind::character: return "character";
    case TokenKind::number: return "number";
    case TokenKind::keyword: return "keyword";
    case TokenKind::type: return "type";
    case TokenKind::preprocessor: return "preprocessor";
    case TokenKind::attribute: return "attribute";
    case TokenKind::punctuation: return "punctuation";
    case TokenKind::operator_symbol: return "operator_symbol";
    case TokenKind::identifier: return "identifier";
    case TokenKind::invalid: return "invalid";
    default: return "plain";
    }
}

} // namespace code_highlight
