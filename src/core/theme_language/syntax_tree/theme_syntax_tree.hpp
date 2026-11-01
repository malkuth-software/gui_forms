#pragma once

#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../tokenizer/theme_tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// The structure limits (T3 decision E2). The rule limit counts style rules
// and keyframe blocks as written; P3 also counts the rules that selector
// lists and :is() expand into.
inline constexpr std::size_t maximum_nesting_depth = 32U;
inline constexpr std::size_t maximum_rules = 10'000U;
inline constexpr std::size_t maximum_declarations_per_rule = 64U;
// Imports written in one sheet; P10 counts them over the import closure.
inline constexpr std::size_t maximum_imports = 16U;

// A node's position in ThemeSyntaxTree::nodes.
struct NodeIndex final {
    std::uint32_t value{};

    friend constexpr bool operator==(NodeIndex left, NodeIndex right) noexcept {
        return left.value == right.value;
    }
};

// Tokens [first, end) of a ThemeTokenBuffer; empty when first == end.
struct TokenRange final {
    TokenIndex first{};
    TokenIndex end{};

    friend constexpr bool operator==(TokenRange left, TokenRange right) noexcept {
        return left.first == right.first && left.end == right.end;
    }
};

enum class SyntaxNodeKind : std::uint8_t {
    // The whole sheet; always node 0, covering every token.
    sheet,
    // `@import "<name>";`. Content: the string token.
    import_rule,
    // `@media <queries> { <style rules> }`. Content: the queries.
    media_rule,
    // `@keyframes <name> { <keyframe blocks> }`. Content: the name.
    keyframes_rule,
    // `@theme { <descriptors> }`, a user theme's header (T3 user themes):
    // the sheet's first item, its block of declarations read by
    // read_theme_header. Content: the empty prelude.
    theme_header,
    // Any other at-rule, kept for the round trip and otherwise skipped (a
    // W104 warning). Content: its prelude; its block, if any, is not parsed.
    unknown_at_rule,
    // `<selectors> { <declarations> }`. Content: the selectors.
    style_rule,
    // `from`, `to` or percentages, then `{ <declarations> }`, inside
    // @keyframes. Content: the keyframe selectors.
    keyframe_block,
    // `<property>: <value> [!important] [;]`. The property is the node's
    // first token. Content: the value, without `!important`.
    declaration,
};

[[nodiscard]] std::string_view syntax_node_kind_name(SyntaxNodeKind kind) noexcept;

// Node flags.
// What a text is: a theme sheet, or a control's inline style (P9).
enum class ThemeSheetForm : std::uint8_t { sheet, inline_style };

// A declaration followed by `!important`.
inline constexpr std::uint8_t syntax_flag_important = 0x01U;

// One node. `tokens` is everything the node spans: for a rule, its prelude
// through its closing `}`; for a declaration, its property through its
// `;` when one is written. `content` is the part the lowering reads (see
// SyntaxNodeKind), trimmed of whitespace and comments at both ends. Nodes
// are stored in document order, each followed by its descendants, so a
// node's children are the nodes from index + 1 up to `subtree_end`, and the
// next sibling of a child is at the child's own `subtree_end`. Whitespace
// and comments between nodes belong to the enclosing node's tokens.
struct SyntaxNode final {
    SyntaxNodeKind kind{SyntaxNodeKind::sheet};
    std::uint8_t flags{};
    TokenRange tokens{};
    TokenRange content{};
    NodeIndex subtree_end{};

    friend bool operator==(const SyntaxNode& left, const SyntaxNode& right) noexcept {
        return left.kind == right.kind && left.flags == right.flags && left.tokens == right.tokens &&
               left.content == right.content && left.subtree_end == right.subtree_end;
    }
};

// Caller-owned storage the parser fills; reused from sheet to sheet. Each
// node other than the sheet owns at least two tokens that are neither
// whitespace nor comments (a rule's braces, a declaration's property and
// colon, an at-rule's keyword and its `;` or braces), so a sheet of n such
// tokens has at most n / 2 + 1 nodes; the parser reserves that capacity
// before its loop.
struct ThemeSyntaxTree final {
    std::vector<SyntaxNode> nodes{};
};

enum class ParseOutcome : std::uint8_t {
    // The tree describes the whole sheet. Warnings, if any, are diagnostics.
    tree_complete,
    // A structural defect or a limit stopped the parse: one error, and the
    // tree holds only the nodes before it (B2: nothing of it is used).
    stopped_at_error,
    // The token buffer holds an `invalid` token; the tokenizer has already
    // reported it, so the parse stops there without another diagnostic.
    stopped_at_lexical_error,
};

// Parses the tokens of `source` into `tree`. It replaces the contents of
// `tree` (its capacity is kept) and appends its diagnostics to
// `diagnostics` in the order the parser meets them: warnings, then at most
// one error, which stops it. The nodes it has opened when it stops are
// closed where it stopped, so even a stopped tree nests correctly. At-rule
// names and `important` are matched ASCII case-insensitively, as in CSS.
// `tokens` and `lines` must be
// those tokenize_theme_sheet made from `source`; all three are borrowed for
// the call. The parser is iterative: the explicit stack of open blocks and
// groups holds at most maximum_nesting_depth entries, and no recursion
// depth follows the input.
//
// An inline style (ThemeSheetForm::inline_style, P9, R2-10) is a list of
// declarations for its control, then blocks whose selectors name the
// control's parts and states with the control's own selector implied
// (`::part(face):hover { ... }`). The declarations become a style rule
// whose selector is empty; the end of the text may end the last one. No
// at-rule is admitted (E206), and a declaration after a block is E203.
[[nodiscard]] ParseOutcome parse_theme_sheet(const ThemeSource& source, const ThemeTokenBuffer& tokens,
                                             const LineStartTable& lines, ThemeSyntaxTree& tree,
                                             ThemeDiagnosticList& diagnostics,
                                             ThemeSheetForm form = ThemeSheetForm::sheet);

enum class WriteOutcome : std::uint8_t {
    written,
    // A node's tokens lie outside its parent's or before an earlier
    // sibling's end: `output` is left empty.
    tree_inconsistent,
};

// Writes the sheet `tree` describes into `output` (replacing its contents):
// walking the nodes in order, it copies each gap between nodes and each
// childless node's bytes, so an untouched tree gives back `text` exactly
// (the byte-preserving round trip). A later editor replaces one node's
// bytes in this walk and copies every other byte.
[[nodiscard]] WriteOutcome write_theme_sheet(const ThemeSyntaxTree& tree, const ThemeTokenBuffer& tokens,
                                             std::string_view text, std::string& output);

} // namespace gui_forms::theme_language
