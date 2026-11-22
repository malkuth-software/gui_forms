#pragma once

// The C# the WinForms converter reads (stage MCP G7): a lexer for the
// subset a Windows Forms designer writes and a form class declares. It
// reads; it never compiles, runs or resolves anything.
//
// Tokens: identifiers (a verbatim `@name` is the identifier `name`;
// keywords are identifiers the readers recognise by their text), numbers
// (decimal, hexadecimal, with a fraction, an exponent and a type suffix),
// strings (regular, with C#'s escapes decoded, and verbatim `@"..."`),
// characters, and punctuation (one or two characters: `+=`, `-=`, `::`,
// `=>`, `==`, `&&`, `||` and the single ones). Comments and whitespace
// separate tokens and are dropped; a preprocessor line (`#region`,
// `#pragma`, `#if`) is dropped whole, so code under a false `#if` is read
// as if it were live (designer files have none).
//
// Refused (ConversionCode::csharp_lexical, with the line and column):
// an unterminated string, character or comment; an interpolated string
// (`$"..."`), a raw string (`"""`), whose expressions the converter cannot
// read; and a byte that begins no token. A refusal leaves `tokens` empty.

#include "conversion_diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

// The largest C# or .resx file the converter reads, and the most tokens
// one file may hold.
inline constexpr std::size_t maximum_source_bytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maximum_csharp_tokens = 2U * 1024U * 1024U;

enum class CSharpTokenKind : std::uint8_t {
    identifier,
    number,
    string,
    character,
    punctuation,
};

struct CSharpToken final {
    CSharpTokenKind kind{CSharpTokenKind::punctuation};
    // The token's bytes in the source, as written (a string's quotes
    // included; a verbatim identifier without its `@`).
    std::uint32_t offset{};
    std::uint32_t length{};
    // From 1; a column counts bytes from the start of the line.
    std::uint32_t line{};
    std::uint32_t column{};
    // A string's or character's decoded text; a number's digits without
    // its type suffix or digit separators; empty for the other kinds.
    std::string value{};
};

// One read file: its text and its tokens, the tokens' offsets into the
// text. Caller-owned and reused.
struct CSharpSource final {
    std::string file{};
    std::string text{};
    std::vector<CSharpToken> tokens{};

    // The token's text as written. The view borrows `text`.
    [[nodiscard]] std::string_view spelling(const CSharpToken& token) const& noexcept {
        const std::string_view all(text);
        const std::string_view written = all.substr(token.offset, token.length);
        return written;
    }
    std::string_view spelling(const CSharpToken& token) const&& = delete;
};

enum class CSharpLexOutcome : std::uint8_t { lexed, refused };

// Lexes `source.text` into `source.tokens` (replaced whole). A refusal adds
// one csharp_lexical diagnostic, naming `source.file`, and leaves the
// tokens empty. A text over maximum_source_bytes, or with more than
// maximum_csharp_tokens tokens, is file_too_large.
[[nodiscard]] CSharpLexOutcome lex_csharp(CSharpSource& source, ConversionDiagnosticList& diagnostics);

// Whether `token` is the punctuation or identifier spelled `text`.
[[nodiscard]] bool is_punctuation(const CSharpSource& source, const CSharpToken& token, std::string_view text) noexcept;
[[nodiscard]] bool is_identifier(const CSharpSource& source, const CSharpToken& token, std::string_view text) noexcept;

} // namespace gui_forms::winforms_converter
