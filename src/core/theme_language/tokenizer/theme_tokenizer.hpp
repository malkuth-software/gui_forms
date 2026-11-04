#pragma once

#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The theme language's tokens (PLAN.md section 1.2): CSS Syntax Level 3's
// token kinds, restricted. Every byte of a sheet belongs to exactly one
// token, whitespace and comments included, so the tokens in order give back
// the sheet's bytes exactly (the byte-preserving round trip). Bytes that
// form no admitted token are an `invalid` token, and a diagnostic says why.
enum class TokenKind : std::uint8_t {
    // A leading U+FEFF, kept for the round trip and otherwise ignored.
    byte_order_mark,
    // A run of space, tab, LF, CR and form feed.
    whitespace,
    // `/* ... */`.
    comment,
    // A name: `button`, `--glow`, `-gf-x`.
    ident,
    // A name followed by `(`; the token includes the `(`.
    function,
    // `@` and a name: `@media`.
    at_keyword,
    // `#` and name characters: `#player-play`, `#fff`.
    hash,
    // `"..."` or `'...'`, quotes included.
    string,
    number,
    // A number followed by `%`.
    percentage,
    // A number followed by a name: `12px`, `90ms`.
    dimension,
    // One code point that is no other token: `.`, `*`, `!`, `>`.
    delim,
    colon,
    semicolon,
    comma,
    left_square,
    right_square,
    left_paren,
    right_paren,
    left_brace,
    right_brace,
    invalid,
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;

// Token flags.
// A hash whose name would start an identifier (CSS's "id" type flag): the
// form a stable ID selector needs. `#fff` lacks it; `#player-play` has it.
inline constexpr std::uint8_t token_flag_hash_identifier = 0x01U;
// A name (an ident, function, hash, at-keyword or dimension unit) written
// with CSS escapes (`#gallery\.heading`): its text is not its name, which
// append_decoded_name gives. Set only where escapes are admitted
// (NameEscapes::in_names).
inline constexpr std::uint8_t token_flag_escaped = 0x02U;

// Whether a tokenization admits CSS backslash escapes in names (CSS Syntax
// 4.3.7, owner 2026-11-04): a theme sheet does, so that a selector can
// name a stable ID or class holding a character no name may (the
// Gallery's dotted IDs); the compiler refuses an escape anywhere but a
// selector. Elsewhere (form documents, layout styles) a backslash is
// escape_not_admitted. An escape of code point 0, of a surrogate or of a
// value past U+10FFFF is escape_not_admitted either way.
enum class NameEscapes : std::uint8_t { refused, in_names };

// Appends to `output` the name `written` spells: each escape replaced by
// the code point it names, in UTF-8. `written` is a name the tokenizer
// admitted (its escapes valid). The result is never longer than `written`.
void append_decoded_name(std::string_view written, std::string& output);

// One token: its kind and its bytes [offset, offset + length). For a
// number, percentage or dimension, `number` is the position of its value in
// ThemeTokenBuffer::numbers; for every other kind it is 0 and means nothing.
struct Token final {
    TokenKind kind{TokenKind::invalid};
    std::uint8_t flags{};
    ByteOffset offset{};
    ByteLength length{};
    std::uint32_t number{};

    friend bool operator==(const Token& left, const Token& right) noexcept {
        return left.kind == right.kind && left.flags == right.flags && left.offset == right.offset &&
               left.length == right.length && left.number == right.number;
    }
};

// A numeric token's value, converted once (theme_number.hpp).
// `numeric_length` is the length of the number's own text at the token's
// start; a dimension's unit, or a percentage's '%', follows it.
struct ThemeNumber final {
    double value{};
    ByteLength numeric_length{};
    bool integer{};

    friend bool operator==(const ThemeNumber& left, const ThemeNumber& right) noexcept {
        return left.value == right.value && left.numeric_length == right.numeric_length &&
               left.integer == right.integer;
    }
};

// Caller-owned storage the tokenizer fills; reused from sheet to sheet
// (house style §1, §6). `tokens` in order cover the sheet's bytes; numbers
// are in the order of their tokens. A sheet of n bytes has at most n tokens,
// so the tokenizer reserves that capacity before its loop and never grows
// the storage while it runs.
struct ThemeTokenBuffer final {
    std::vector<Token> tokens{};
    std::vector<ThemeNumber> numbers{};
};

enum class TokenizeOutcome : std::uint8_t {
    // The tokens cover the sheet. Lexical defects, if any, are diagnostics
    // (all errors) and `invalid` tokens.
    tokens_cover_sheet,
    // The sheet is over maximum_sheet_bytes: one sheet_too_large error, and
    // the buffer and line table are left empty.
    refused_too_large,
};

// Tokenizes `source`. It replaces the contents of `buffer` and `lines` (their
// capacity is kept) and appends its diagnostics to `diagnostics` in two
// runs, each in the order of its offsets: first the encoding defects
// (invalid UTF-8 and NUL bytes, found by one pass over the whole sheet
// before tokenizing), then the token defects. `source` is borrowed for the
// call.
[[nodiscard]] TokenizeOutcome tokenize_theme_sheet(const ThemeSource& source, ThemeTokenBuffer& buffer,
                                                   LineStartTable& lines, ThemeDiagnosticList& diagnostics,
                                                   NameEscapes escapes = NameEscapes::refused);

} // namespace gui_forms::theme_language
