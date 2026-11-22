#pragma once

// A highlighting language's vocabulary, in the shape of SwiftEdit's
// (swiftedit/src/highlight/language.hpp and highlight_types.hpp): a language
// lexes one line at a time from the state the line before left, and reports
// line-relative tokens whose kinds a presenter maps to colours of its own.
// The code-zoom demonstrator's C++ language (cpp_language.hpp) is written to
// it. Nothing here depends on GUI.Forms, only on the standard library, so
// the files move unchanged into STXlib (owner, 2026-11-22).

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace code_highlight {

// What a token is. A presenter may index a table of `token_kind_count`
// entries by the kind's value, so the order is part of the contract; a
// language never names a colour.
enum class TokenKind : std::uint8_t {
    plain,           // text no other kind describes; also every gap between tokens
    comment,         // // ... and /* ... */
    string,          // "...", R"(...)", a header name in #include <...>
    character,       // 'a', u8'a', '\n'
    number,          // 42, 0x1Fu, 1'000, 1.5e-3f, 0x1.8p3
    keyword,         // if, constexpr, return, nullptr, true
    type,            // int, char32_t, std::size_t's size_t, a capitalized type name
    preprocessor,    // #include, # define
    attribute,       // [[nodiscard]], [[deprecated("...")]]'s brackets and names
    punctuation,     // { } ( ) [ ] ; ,
    operator_symbol, // + -> :: <=> && ? :
    identifier,      // any other name
    invalid,         // an unterminated literal, a stray byte
};
inline constexpr std::size_t token_kind_count = 13U;

// One token: the bytes [column, column + length) of one line, and its kind.
// Columns are bytes, line-relative.
struct LineToken final {
    std::uint32_t column{};
    std::uint32_t length{};
    TokenKind kind{TokenKind::plain};
    friend constexpr bool operator==(const LineToken& left, const LineToken& right) noexcept {
        return left.column == right.column && left.length == right.length && left.kind == right.kind;
    }
};

// The lexer's state at a line boundary. Its bits belong to the language
// that produced it; a caller only stores and compares states.
struct LexState final {
    std::uint64_t value{};
    friend constexpr bool operator==(LexState left, LexState right) noexcept { return left.value == right.value; }
    friend constexpr bool operator!=(LexState left, LexState right) noexcept { return left.value != right.value; }
};

class Language {
public:
    Language() = default;
    Language(const Language&) = delete;
    Language& operator=(const Language&) = delete;
    virtual ~Language() = default;

    // The language's registry name, as "cpp". Stable.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    // What a person reads in a menu, as "C++".
    [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
    // The state the first line of a document starts in.
    [[nodiscard]] virtual LexState initial_state() const noexcept = 0;
    // Lexes one line (its bytes without the terminator) entered in `entry`.
    // Appends the line's tokens to `tokens` in increasing column order,
    // without overlap; bytes no token covers are plain. Returns the state
    // the next line starts in. Mutates nothing but `tokens`; a line longer
    // than UINT32_MAX bytes is the caller's to refuse.
    [[nodiscard]] virtual LexState lex_line(std::string_view line, LexState entry,
                                            std::vector<LineToken>& tokens) const = 0;
};

// The kind's name, as "operator_symbol"; "plain" for a value outside the
// enumeration.
[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;

} // namespace code_highlight
