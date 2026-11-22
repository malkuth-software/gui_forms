#pragma once

// C++ for highlighting (the code-zoom demonstrator, owner 2026-11-22), to
// the Language interface (code_highlight.hpp, SwiftEdit's shape). Standard
// library only, so it moves unchanged into STXlib.
//
// What it reports: keywords (C++23's, with the contextual override, final,
// import and module); types (the fundamental types, the standard's size and
// fixed-width names and a few class templates, and any name that begins
// with an upper-case letter and holds a lower-case one, as the house
// style's type names do, so an ALL_CAPS macro stays an identifier);
// preprocessor directives (the '#' and its name, the first code on a line,
// with an #include's <header> as a string); string literals with their
// encoding prefixes and user-defined suffixes, raw strings included;
// character literals; numbers as the standard's pp-numbers (digit
// separators, exponents with signs, suffixes); line and block comments;
// attributes (the [[ ]] brackets and the names between them); punctuation
// and runs of operator characters. An unterminated string or character
// literal is invalid to the end of its line, as is a stray byte.
//
// What a line carries to the next (LexState): an open block comment; a line
// comment, a directive or an ordinary string literal continued by a
// backslash ending the line; an open raw string with its delimiter. A
// delimiter of at most six bytes is kept exactly; a longer one (up to the
// standard's sixteen) as its length and a 55-bit hash, so a `)delim"` of
// that length hashing alike would close it: a colouring error, never a
// fault. Block-comment markers do not nest: the first */ ends the comment.
// Other line splices (a backslash-newline inside a name or an operator) are
// not joined.

#include "code_highlight.hpp"

#include <string_view>
#include <vector>

namespace code_highlight {

class CppLanguage final : public Language {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "cpp"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "C++"; }
    [[nodiscard]] LexState initial_state() const noexcept override { return LexState{}; }
    [[nodiscard]] LexState lex_line(std::string_view line, LexState entry,
                                    std::vector<LineToken>& tokens) const override;
};

// The language, with static storage duration.
[[nodiscard]] const CppLanguage& cpp_language() noexcept;

} // namespace code_highlight
