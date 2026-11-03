// The theme sheet parser (PLAN.md sections 1.3 and 3): tokens into a
// ThemeSyntaxTree. The grammar's own depth is fixed (sheet, at-rule block,
// rule block, declaration), so each level is a function; only groups, the
// parentheses, brackets and functions inside preludes and values, nest as
// deep as the input says, and those are walked with an explicit stack. A
// form document's objects (stage FD) also nest as deep as the input says:
// one loop walks them, their blocks on the same explicit stack.

#include "theme_syntax_tree.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms::theme_language {

namespace {

// How a scan over component values ended: at the token that ends the
// current item (next_ is left on it), at the end of the tokens, or at an
// error the scan has reported.
enum class ItemEnd : std::uint8_t { left_brace, semicolon, right_brace, end_of_sheet, stopped };

// What a group may contain. Inside a prelude or a value a group holds
// component values only; an unknown at-rule's block may hold anything that
// balances, since it is kept but not read.
enum class GroupScan : std::uint8_t { component_values, any_balanced };

[[nodiscard]] bool is_trivia(TokenKind kind) noexcept {
    return kind == TokenKind::whitespace || kind == TokenKind::comment || kind == TokenKind::byte_order_mark;
}

[[nodiscard]] bool opens_group(TokenKind kind) noexcept {
    return kind == TokenKind::left_paren || kind == TokenKind::left_square || kind == TokenKind::function;
}

[[nodiscard]] bool closes_group(TokenKind kind) noexcept {
    return kind == TokenKind::right_paren || kind == TokenKind::right_square;
}

[[nodiscard]] TokenKind closer_for(TokenKind opener) noexcept {
    if (opener == TokenKind::left_square) return TokenKind::right_square;
    if (opener == TokenKind::left_brace) return TokenKind::right_brace;
    return TokenKind::right_paren;
}

enum class AtRuleName : std::uint8_t { import_rule, media, keyframes, theme, unknown };

[[nodiscard]] AtRuleName at_rule_name(std::string_view name) noexcept {
    if (equals_ascii_folded(name, "import")) return AtRuleName::import_rule;
    if (equals_ascii_folded(name, "media")) return AtRuleName::media;
    if (equals_ascii_folded(name, "keyframes")) return AtRuleName::keyframes;
    if (equals_ascii_folded(name, "theme")) return AtRuleName::theme;
    return AtRuleName::unknown;
}

// One open block or group on the explicit stack: the closer it waits for
// and the token that opened it (where an unclosed one is reported).
struct OpenGroup final {
    TokenKind closer{TokenKind::right_paren};
    std::uint32_t opener{};
};

class SheetParser final {
public:
    SheetParser(const ThemeSource& source, const ThemeTokenBuffer& tokens, const LineStartTable& lines,
                ThemeSyntaxTree& tree, ThemeDiagnosticList& diagnostics, ThemeSheetForm form) noexcept;

    [[nodiscard]] ParseOutcome run();

private:
    // The levels of the grammar, each starting at next_.
    void parse_top_level_item();
    // An inline style (P9): its leading declarations, as a style rule with
    // no selector, then its blocks.
    void parse_inline_style();
    void parse_at_rule();
    void parse_import(std::uint32_t at);
    void parse_block_at_rule(std::uint32_t at, SyntaxNodeKind kind);
    void parse_unknown_at_rule(std::uint32_t at);
    // `@theme { ... }`: first in the sheet, and once.
    void parse_theme_header(std::uint32_t at);
    void parse_rule(SyntaxNodeKind kind, bool top_level);
    void parse_rule_list_block(SyntaxNodeKind child_kind);
    void parse_declaration_block(bool in_keyframes);
    void parse_declaration(bool in_keyframes);
    // A form document (stage FD): its objects, walked by one loop.
    void parse_form_document();
    // At an identifier inside an object's block: a declaration, a style
    // block or a nested object. Returns the object's node when one opened.
    [[nodiscard]] std::optional<std::uint32_t> parse_object_item(std::size_t& declarations);
    // At an object's first token: opens its node and its block. Returns the
    // node, or nothing after an error.
    [[nodiscard]] std::optional<std::uint32_t> open_object();
    // Whether the prelude tokens [first, end), trimmed, are a type and a
    // stable ID; reports the offending token otherwise.
    [[nodiscard]] bool object_prelude_valid(TokenRange prelude);
    void parse_style_block();

    // Scanning.
    [[nodiscard]] ItemEnd scan_to_item_end();
    [[nodiscard]] bool skip_group(GroupScan scan);
    [[nodiscard]] bool item_opens_block(std::uint32_t from) const noexcept;
    void skip_trivia() noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] TokenRange trimmed(std::uint32_t first, std::uint32_t end) const noexcept;
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;

    // The explicit stack.
    [[nodiscard]] bool push(std::uint32_t opener);
    void pop() noexcept;

    // Nodes.
    [[nodiscard]] std::uint32_t open_node(SyntaxNodeKind kind, std::uint32_t first);
    void close_node(std::uint32_t node, std::uint32_t end) noexcept;

    // Diagnostics. A failure records the one error and stops the parse; a
    // failure found at the end of the tokens is a lexical stop instead
    // when an invalid token ended them early.
    void report(DiagnosticCode code, std::size_t offset, std::string detail);
    void fail_at_token(DiagnosticCode code, std::uint32_t index, std::string detail);
    void fail_at_end(DiagnosticCode code, std::uint32_t opener, std::string detail);
    void fail_at_end_of_sheet();
    [[nodiscard]] bool stopped() const noexcept;

    const ThemeSource& source_;
    std::string_view text_{};
    const std::vector<Token>& tokens_;
    ThemeSyntaxTree& tree_;
    ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    // The parse reads tokens [0, limit_): limit_ is the first invalid
    // token's index, or the token count.
    std::uint32_t limit_{};
    std::uint32_t next_{};
    std::array<OpenGroup, maximum_nesting_depth> open_{};
    std::size_t depth_{};
    std::size_t rules_{};
    std::size_t imports_{};
    // Whether an item other than @import has been read.
    bool seen_other_item_{};
    bool seen_theme_header_{};
    ThemeSheetForm form_{ThemeSheetForm::sheet};
    // Whether the declarations being read are an inline style's leading
    // ones, which the end of the text may end.
    bool inline_declarations_{};
    // A form document's open objects, innermost last: each one's node, and
    // the declarations read in its block so far. Each holds an entry of
    // open_, so there are never more than it holds.
    std::array<std::uint32_t, maximum_nesting_depth> object_nodes_{};
    std::array<std::size_t, maximum_nesting_depth> object_declarations_{};
    std::size_t objects_{};
    ParseOutcome outcome_{ParseOutcome::tree_complete};
};

SheetParser::SheetParser(const ThemeSource& source, const ThemeTokenBuffer& tokens, const LineStartTable& lines,
                         ThemeSyntaxTree& tree, ThemeDiagnosticList& diagnostics, ThemeSheetForm form) noexcept
    : source_(source), text_(source.text), tokens_(tokens.tokens), tree_(tree), diagnostics_(diagnostics),
      cursor_(lines, source.text), form_(form) {}

ParseOutcome SheetParser::run() {
    tree_.nodes.clear();
    const std::uint32_t count = static_cast<std::uint32_t>(tokens_.size());
    std::size_t significant = 0U;
    limit_ = count;
    for (std::uint32_t index = 0U; index < count; ++index) {
        const TokenKind kind = tokens_[index].kind;
        if (kind == TokenKind::invalid && limit_ == count) limit_ = index;
        if (!is_trivia(kind)) ++significant;
    }
    // Each node but the sheet owns two significant tokens once complete;
    // the nodes open when a parse stops (at most one per grammar level) may
    // own one.
    // A form document's objects add one open node each.
    const std::size_t open_levels = form_ == ThemeSheetForm::form_document ? maximum_nesting_depth + 3U : 4U;
    tree_.nodes.reserve(significant / 2U + 1U + open_levels);

    const std::uint32_t root = open_node(SyntaxNodeKind::sheet, 0U);
    if (form_ == ThemeSheetForm::inline_style) {
        parse_inline_style();
    } else if (form_ == ThemeSheetForm::form_document) {
        parse_form_document();
    } else {
        while (!stopped()) {
            skip_trivia();
            if (at_end()) break;
            parse_top_level_item();
        }
    }
    if (outcome_ == ParseOutcome::tree_complete && limit_ < count) outcome_ = ParseOutcome::stopped_at_lexical_error;
    // The sheet spans every token, whatever stopped the parse.
    close_node(root, count);
    return outcome_;
}

void SheetParser::parse_top_level_item() {
    const TokenKind kind = tokens_[next_].kind;
    if (kind == TokenKind::at_keyword) {
        parse_at_rule();
        return;
    }
    if (kind == TokenKind::right_brace || closes_group(kind)) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
        return;
    }
    seen_other_item_ = true;
    parse_rule(SyntaxNodeKind::style_rule, true);
}

void SheetParser::parse_inline_style() {
    // Empty declarations before the first, as CSS admits.
    skip_trivia();
    while (!at_end() && tokens_[next_].kind == TokenKind::semicolon) {
        ++next_;
        skip_trivia();
    }
    if (!at_end() && tokens_[next_].kind == TokenKind::ident) {
        // The control's own declarations: a rule whose selector, empty, is
        // the control.
        ++rules_;
        const std::uint32_t node = open_node(SyntaxNodeKind::style_rule, next_);
        tree_.nodes[node].content = TokenRange{TokenIndex{next_}, TokenIndex{next_}};
        inline_declarations_ = true;
        std::size_t declarations = 0U;
        while (!stopped()) {
            skip_trivia();
            if (at_end()) break;
            const TokenKind kind = tokens_[next_].kind;
            if (kind == TokenKind::semicolon) {
                ++next_;
                continue;
            }
            if (kind != TokenKind::ident) break;
            ++declarations;
            if (declarations > maximum_declarations_per_rule) {
                fail_at_token(DiagnosticCode::too_many_declarations, next_,
                              std::to_string(maximum_declarations_per_rule));
                break;
            }
            parse_declaration(false);
        }
        inline_declarations_ = false;
        close_node(node, trimmed(tree_.nodes[node].tokens.first.value, next_).end.value);
    }
    // Then the part and state blocks, the control's selector implied.
    while (!stopped()) {
        skip_trivia();
        if (at_end()) return;
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::at_keyword) {
            fail_at_token(DiagnosticCode::nesting_not_admitted, next_, clipped_theme_detail(token_text(next_)));
            return;
        }
        if (kind == TokenKind::ident) {
            // A declaration after a block.
            fail_at_token(DiagnosticCode::declaration_outside_rule, next_, {});
            return;
        }
        if (kind == TokenKind::right_brace || closes_group(kind)) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
            return;
        }
        parse_rule(SyntaxNodeKind::style_rule, true);
    }
}

void SheetParser::parse_at_rule() {
    const std::uint32_t at = next_;
    // The at-keyword's name follows its '@'.
    const std::string_view name = token_text(at).substr(1U);
    const AtRuleName known = at_rule_name(name);
    if (known == AtRuleName::import_rule) {
        parse_import(at);
        return;
    }
    if (known == AtRuleName::theme) {
        parse_theme_header(at);
        return;
    }
    seen_other_item_ = true;
    if (known == AtRuleName::media) {
        parse_block_at_rule(at, SyntaxNodeKind::media_rule);
        return;
    }
    if (known == AtRuleName::keyframes) {
        parse_block_at_rule(at, SyntaxNodeKind::keyframes_rule);
        return;
    }
    parse_unknown_at_rule(at);
}

void SheetParser::parse_import(std::uint32_t at) {
    if (seen_other_item_) {
        fail_at_token(DiagnosticCode::import_after_rules, at, {});
        return;
    }
    ++imports_;
    if (imports_ > maximum_imports) {
        fail_at_token(DiagnosticCode::too_many_imports, at, std::to_string(maximum_imports));
        return;
    }
    const std::uint32_t node = open_node(SyntaxNodeKind::import_rule, at);
    ++next_;
    const std::uint32_t prelude_first = next_;
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::semicolon) {
        const TokenRange content = trimmed(prelude_first, next_);
        const std::uint32_t written = content.end.value - content.first.value;
        const bool one_string = written == 1U && tokens_[content.first.value].kind == TokenKind::string;
        if (one_string) {
            tree_.nodes[node].content = content;
            ++next_;
        } else {
            // The first token that is not the one string, or the ';' when
            // nothing was written.
            std::uint32_t offending = content.first.value;
            if (written == 0U) {
                offending = next_;
            } else if (tokens_[offending].kind == TokenKind::string) {
                offending = trimmed(offending + 1U, content.end.value).first.value;
            }
            fail_at_token(DiagnosticCode::unexpected_token, offending,
                          std::string(token_kind_name(tokens_[offending].kind)));
        }
    } else if (end == ItemEnd::left_brace) {
        fail_at_token(DiagnosticCode::unexpected_token, next_, "left_brace");
    } else if (end == ItemEnd::right_brace) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
}

void SheetParser::parse_block_at_rule(std::uint32_t at, SyntaxNodeKind kind) {
    const std::uint32_t node = open_node(kind, at);
    ++next_;
    const std::uint32_t prelude_first = next_;
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::left_brace) {
        tree_.nodes[node].content = trimmed(prelude_first, next_);
        if (push(next_)) {
            ++next_;
            const SyntaxNodeKind child_kind =
                kind == SyntaxNodeKind::keyframes_rule ? SyntaxNodeKind::keyframe_block : SyntaxNodeKind::style_rule;
            parse_rule_list_block(child_kind);
        }
    } else if (end == ItemEnd::semicolon) {
        fail_at_token(DiagnosticCode::unexpected_token, next_, "semicolon");
    } else if (end == ItemEnd::right_brace) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
}

void SheetParser::parse_theme_header(std::uint32_t at) {
    if (seen_other_item_ || seen_theme_header_ || imports_ != 0U) {
        fail_at_token(DiagnosticCode::theme_header_misplaced, at, {});
        return;
    }
    seen_theme_header_ = true;
    const std::uint32_t node = open_node(SyntaxNodeKind::theme_header, at);
    ++next_;
    const std::uint32_t prelude_first = next_;
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::left_brace) {
        const TokenRange prelude = trimmed(prelude_first, next_);
        if (prelude.end.value != prelude.first.value) {
            fail_at_token(DiagnosticCode::unexpected_token, prelude.first.value,
                          std::string(token_kind_name(tokens_[prelude.first.value].kind)));
        } else {
            tree_.nodes[node].content = prelude;
            if (push(next_)) {
                ++next_;
                parse_declaration_block(false);
            }
        }
    } else if (end == ItemEnd::semicolon) {
        fail_at_token(DiagnosticCode::unexpected_token, next_, "semicolon");
    } else if (end == ItemEnd::right_brace) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
}

void SheetParser::parse_unknown_at_rule(std::uint32_t at) {
    report(DiagnosticCode::unknown_at_rule, tokens_[at].offset.value, clipped_theme_detail(token_text(at)));
    const std::uint32_t node = open_node(SyntaxNodeKind::unknown_at_rule, at);
    ++next_;
    const std::uint32_t prelude_first = next_;
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::semicolon) {
        tree_.nodes[node].content = trimmed(prelude_first, next_);
        ++next_;
    } else if (end == ItemEnd::left_brace) {
        tree_.nodes[node].content = trimmed(prelude_first, next_);
        static_cast<void>(skip_group(GroupScan::any_balanced));
    } else if (end == ItemEnd::right_brace) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
}

void SheetParser::parse_rule(SyntaxNodeKind kind, bool top_level) {
    const std::uint32_t first = next_;
    ++rules_;
    if (rules_ > maximum_rules) {
        fail_at_token(DiagnosticCode::too_many_rules, first, std::to_string(maximum_rules));
        return;
    }
    const std::uint32_t node = open_node(kind, first);
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::left_brace) {
        tree_.nodes[node].content = trimmed(first, next_);
        if (push(next_)) {
            ++next_;
            parse_declaration_block(kind == SyntaxNodeKind::keyframe_block);
        }
    } else if (end == ItemEnd::semicolon) {
        fail_at_token(DiagnosticCode::declaration_outside_rule, first, {});
    } else if (end == ItemEnd::right_brace) {
        // At the top level a '}' closes nothing; inside a block it closes
        // the block and leaves this prelude without one.
        if (top_level) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
        } else {
            fail_at_token(DiagnosticCode::unexpected_token, next_, "right_brace");
        }
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
}

void SheetParser::parse_rule_list_block(SyntaxNodeKind child_kind) {
    while (!stopped()) {
        skip_trivia();
        if (at_end()) {
            fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            return;
        }
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::right_brace) {
            pop();
            ++next_;
            return;
        }
        if (kind == TokenKind::at_keyword) {
            fail_at_token(DiagnosticCode::nesting_not_admitted, next_, clipped_theme_detail(token_text(next_)));
            return;
        }
        if (closes_group(kind)) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
            return;
        }
        parse_rule(child_kind, false);
    }
}

void SheetParser::parse_declaration_block(bool in_keyframes) {
    std::size_t declarations = 0U;
    while (!stopped()) {
        skip_trivia();
        if (at_end()) {
            fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            return;
        }
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::right_brace) {
            pop();
            ++next_;
            return;
        }
        if (kind == TokenKind::semicolon) {
            // An empty declaration, as CSS admits.
            ++next_;
            continue;
        }
        if (kind == TokenKind::at_keyword) {
            fail_at_token(DiagnosticCode::nesting_not_admitted, next_, clipped_theme_detail(token_text(next_)));
            return;
        }
        if (kind != TokenKind::ident) {
            // A nested rule (`&:hover {`, `.x {`) or a stray token.
            const DiagnosticCode code =
                item_opens_block(next_) ? DiagnosticCode::nesting_not_admitted : DiagnosticCode::unexpected_token;
            fail_at_token(code, next_, std::string(token_kind_name(kind)));
            return;
        }
        ++declarations;
        if (declarations > maximum_declarations_per_rule) {
            fail_at_token(DiagnosticCode::too_many_declarations, next_,
                          std::to_string(maximum_declarations_per_rule));
            return;
        }
        parse_declaration(in_keyframes);
    }
}

void SheetParser::parse_declaration(bool in_keyframes) {
    const std::uint32_t first = next_;
    const std::uint32_t node = open_node(SyntaxNodeKind::declaration, first);
    ++next_;
    skip_trivia();
    if (at_end()) {
        if (inline_declarations_) {
            fail_at_end_of_sheet();
        } else {
            fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
        }
        close_node(node, next_);
        return;
    }
    if (tokens_[next_].kind != TokenKind::colon) {
        // `a { ... }` or `a b { ... }` is a nested rule; anything else is a
        // stray token.
        const bool nested = item_opens_block(next_);
        const DiagnosticCode code = nested ? DiagnosticCode::nesting_not_admitted : DiagnosticCode::unexpected_token;
        const std::uint32_t at = nested ? first : next_;
        fail_at_token(code, at, std::string(token_kind_name(tokens_[at].kind)));
        close_node(node, next_);
        return;
    }
    ++next_;
    const std::uint32_t value_first = next_;
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::left_brace) {
        // `a:hover { ... }`: a nested rule.
        fail_at_token(DiagnosticCode::nesting_not_admitted, first, "ident");
        close_node(node, next_);
        return;
    }
    if (end == ItemEnd::end_of_sheet && !inline_declarations_) {
        fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
        close_node(node, next_);
        return;
    }
    if (end == ItemEnd::right_brace && inline_declarations_) {
        fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
        close_node(node, next_);
        return;
    }
    if (end == ItemEnd::stopped) {
        close_node(node, next_);
        return;
    }
    TokenRange content = trimmed(value_first, next_);
    // `! important` at the value's end, with any whitespace or comments
    // between the two.
    if (content.end.value > content.first.value) {
        const std::uint32_t last = content.end.value - 1U;
        const bool important_name =
            tokens_[last].kind == TokenKind::ident && equals_ascii_folded(token_text(last), "important");
        const TokenRange before = trimmed(content.first.value, last);
        const bool bang = before.end.value > before.first.value &&
                          tokens_[before.end.value - 1U].kind == TokenKind::delim &&
                          token_text(before.end.value - 1U) == "!";
        if (important_name && bang) {
            const std::uint32_t bang_index = before.end.value - 1U;
            if (in_keyframes) {
                fail_at_token(DiagnosticCode::important_in_keyframes, bang_index, {});
                close_node(node, next_);
                return;
            }
            tree_.nodes[node].flags = syntax_flag_important;
            content = trimmed(content.first.value, bang_index);
        }
    }
    tree_.nodes[node].content = content;
    if (end == ItemEnd::semicolon) {
        ++next_;
        close_node(node, next_);
        return;
    }
    // Ended by the block's '}', or an inline style's by the end of its
    // text: the declaration ends at its last written token, and the
    // whitespace after it is the rule's.
    const TokenRange spanned = trimmed(first, next_);
    close_node(node, spanned.end.value);
}

void SheetParser::parse_form_document() {
    while (!stopped()) {
        skip_trivia();
        if (at_end()) {
            if (objects_ != 0U) {
                fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            }
            break;
        }
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::right_brace) {
            if (objects_ == 0U) {
                fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
                break;
            }
            pop();
            ++next_;
            --objects_;
            close_node(object_nodes_[objects_], next_);
            continue;
        }
        if (kind == TokenKind::at_keyword) {
            fail_at_token(DiagnosticCode::nesting_not_admitted, next_, clipped_theme_detail(token_text(next_)));
            break;
        }
        if (kind == TokenKind::semicolon && objects_ != 0U) {
            // An empty declaration, as CSS admits.
            ++next_;
            continue;
        }
        if (kind != TokenKind::ident) {
            const DiagnosticCode code =
                closes_group(kind) ? DiagnosticCode::unbalanced_block : DiagnosticCode::unexpected_token;
            const std::string detail = closes_group(kind) ? std::string("no opening")
                                                          : std::string(token_kind_name(kind));
            fail_at_token(code, next_, detail);
            break;
        }
        std::optional<std::uint32_t> opened{};
        if (objects_ == 0U) {
            opened = open_object();
        } else {
            opened = parse_object_item(object_declarations_[objects_ - 1U]);
        }
        if (!opened) continue;
        object_nodes_[objects_] = *opened;
        object_declarations_[objects_] = 0U;
        ++objects_;
    }
    // The objects a stop leaves open end where it stopped.
    while (objects_ != 0U) {
        --objects_;
        close_node(object_nodes_[objects_], next_);
    }
}

std::optional<std::uint32_t> SheetParser::parse_object_item(std::size_t& declarations) {
    // The token after the identifier tells the item: a ':' a declaration,
    // a '{' a style block, anything else an object's stable ID.
    std::uint32_t after = next_ + 1U;
    while (after < limit_ && is_trivia(tokens_[after].kind)) ++after;
    const TokenKind following = after < limit_ ? tokens_[after].kind : TokenKind::whitespace;
    if (following == TokenKind::colon) {
        ++declarations;
        if (declarations > maximum_declarations_per_rule) {
            fail_at_token(DiagnosticCode::too_many_declarations, next_, std::to_string(maximum_declarations_per_rule));
            return std::nullopt;
        }
        parse_declaration(false);
        return std::nullopt;
    }
    if (following == TokenKind::left_brace && equals_ascii_folded(token_text(next_), "style")) {
        parse_style_block();
        return std::nullopt;
    }
    return open_object();
}

std::optional<std::uint32_t> SheetParser::open_object() {
    const std::uint32_t first = next_;
    ++rules_;
    if (rules_ > maximum_rules) {
        fail_at_token(DiagnosticCode::too_many_rules, first, std::to_string(maximum_rules));
        return std::nullopt;
    }
    const std::uint32_t node = open_node(SyntaxNodeKind::object_rule, first);
    const ItemEnd end = scan_to_item_end();
    if (end == ItemEnd::left_brace) {
        const TokenRange prelude = trimmed(first, next_);
        tree_.nodes[node].content = prelude;
        if (object_prelude_valid(prelude) && push(next_)) {
            ++next_;
            return node;
        }
    } else if (end == ItemEnd::semicolon) {
        // `name;` or `a b;`: neither a declaration nor an object.
        const DiagnosticCode code =
            objects_ == 0U ? DiagnosticCode::declaration_outside_rule : DiagnosticCode::unexpected_token;
        fail_at_token(code, objects_ == 0U ? first : next_, objects_ == 0U ? std::string() : std::string("semicolon"));
    } else if (end == ItemEnd::right_brace) {
        if (objects_ == 0U) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
        } else {
            fail_at_token(DiagnosticCode::unexpected_token, next_, "right_brace");
        }
    } else if (end == ItemEnd::end_of_sheet) {
        fail_at_end_of_sheet();
    }
    close_node(node, next_);
    return std::nullopt;
}

bool SheetParser::object_prelude_valid(TokenRange prelude) {
    // The type: one identifier, then trivia.
    const std::uint32_t type = prelude.first.value;
    if (tokens_[type].kind != TokenKind::ident) {
        fail_at_token(DiagnosticCode::unexpected_token, type, std::string(token_kind_name(tokens_[type].kind)));
        return false;
    }
    const std::uint32_t after_type = type + 1U;
    if (after_type == prelude.end.value || !is_trivia(tokens_[after_type].kind)) {
        const std::uint32_t at = after_type == prelude.end.value ? next_ : after_type;
        fail_at_token(DiagnosticCode::unexpected_token, at, "expected stable id");
        return false;
    }
    // The stable ID: the tokens after the trivia, written without any,
    // whose bytes follow the stable ID rule.
    const TokenRange identity = trimmed(after_type, prelude.end.value);
    for (std::uint32_t index = identity.first.value; index < identity.end.value; ++index) {
        if (is_trivia(tokens_[index].kind)) {
            fail_at_token(DiagnosticCode::unexpected_token, index + 1U,
                          std::string(token_kind_name(tokens_[index + 1U].kind)));
            return false;
        }
    }
    const Token& first = tokens_[identity.first.value];
    const Token& last = tokens_[identity.end.value - 1U];
    const std::size_t stop = static_cast<std::size_t>(last.offset.value) + last.length.value;
    const std::string_view written = text_.substr(first.offset.value, stop - first.offset.value);
    if (!valid_stable_id(written)) {
        fail_at_token(DiagnosticCode::unexpected_token, identity.first.value, clipped_theme_detail(written));
        return false;
    }
    return true;
}

void SheetParser::parse_style_block() {
    const std::uint32_t node = open_node(SyntaxNodeKind::style_block, next_);
    tree_.nodes[node].content = TokenRange{TokenIndex{next_}, TokenIndex{next_ + 1U}};
    ++next_;
    skip_trivia();
    if (!push(next_)) {
        close_node(node, next_);
        return;
    }
    ++next_;
    // Empty declarations before the first, as CSS admits.
    skip_trivia();
    while (!at_end() && tokens_[next_].kind == TokenKind::semicolon) {
        ++next_;
        skip_trivia();
    }
    if (!at_end() && tokens_[next_].kind == TokenKind::ident) {
        // The object's own declarations: a rule whose selector, empty, is
        // the object, ended by the first block or the closing '}'.
        ++rules_;
        const std::uint32_t rule = open_node(SyntaxNodeKind::style_rule, next_);
        tree_.nodes[rule].content = TokenRange{TokenIndex{next_}, TokenIndex{next_}};
        std::size_t declarations = 0U;
        while (!stopped()) {
            skip_trivia();
            if (at_end()) break;
            const TokenKind kind = tokens_[next_].kind;
            if (kind == TokenKind::semicolon) {
                ++next_;
                continue;
            }
            if (kind != TokenKind::ident) break;
            ++declarations;
            if (declarations > maximum_declarations_per_rule) {
                fail_at_token(DiagnosticCode::too_many_declarations, next_,
                              std::to_string(maximum_declarations_per_rule));
                break;
            }
            parse_declaration(false);
        }
        close_node(rule, trimmed(tree_.nodes[rule].tokens.first.value, next_).end.value);
    }
    // Then the part and state blocks, the object's selector implied, to the
    // block's '}'.
    while (!stopped()) {
        skip_trivia();
        if (at_end()) {
            fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            break;
        }
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::right_brace) {
            pop();
            ++next_;
            break;
        }
        if (kind == TokenKind::at_keyword) {
            fail_at_token(DiagnosticCode::nesting_not_admitted, next_, clipped_theme_detail(token_text(next_)));
            break;
        }
        if (kind == TokenKind::ident) {
            // A declaration after a block.
            fail_at_token(DiagnosticCode::declaration_outside_rule, next_, {});
            break;
        }
        if (closes_group(kind)) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
            break;
        }
        parse_rule(SyntaxNodeKind::style_rule, false);
    }
    close_node(node, next_);
}

ItemEnd SheetParser::scan_to_item_end() {
    while (!stopped()) {
        if (at_end()) return ItemEnd::end_of_sheet;
        const TokenKind kind = tokens_[next_].kind;
        if (kind == TokenKind::left_brace) return ItemEnd::left_brace;
        if (kind == TokenKind::semicolon) return ItemEnd::semicolon;
        if (kind == TokenKind::right_brace) return ItemEnd::right_brace;
        if (closes_group(kind)) {
            fail_at_token(DiagnosticCode::unbalanced_block, next_, "no opening");
            return ItemEnd::stopped;
        }
        if (opens_group(kind)) {
            if (!skip_group(GroupScan::component_values)) return ItemEnd::stopped;
            continue;
        }
        ++next_;
    }
    return ItemEnd::stopped;
}

// Consumes the group opened at next_ through its closer. Returns false
// after reporting an error.
bool SheetParser::skip_group(GroupScan scan) {
    const std::size_t base = depth_;
    if (!push(next_)) return false;
    ++next_;
    while (depth_ > base) {
        if (at_end()) {
            fail_at_end(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            return false;
        }
        const TokenKind kind = tokens_[next_].kind;
        const bool block_token =
            kind == TokenKind::left_brace || kind == TokenKind::right_brace || kind == TokenKind::semicolon;
        if (scan == GroupScan::component_values && block_token) {
            // A group inside a prelude or a value ends before the item does.
            fail_at_token(DiagnosticCode::unbalanced_block, open_[depth_ - 1U].opener, "not closed");
            return false;
        }
        if (opens_group(kind) || kind == TokenKind::left_brace) {
            if (!push(next_)) return false;
            ++next_;
            continue;
        }
        if (closes_group(kind) || kind == TokenKind::right_brace) {
            const TokenKind expected = open_[depth_ - 1U].closer;
            if (kind != expected) {
                fail_at_token(DiagnosticCode::unbalanced_block, next_,
                              std::string("expected ") + std::string(token_kind_name(expected)));
                return false;
            }
            pop();
            ++next_;
            continue;
        }
        ++next_;
    }
    return true;
}

// Whether the item starting at `from` reaches a '{' before its ';' or '}':
// a lookahead that counts group depth only, used once before an error to
// name it.
bool SheetParser::item_opens_block(std::uint32_t from) const noexcept {
    std::size_t groups = 0U;
    for (std::uint32_t index = from; index < limit_; ++index) {
        const TokenKind kind = tokens_[index].kind;
        if (opens_group(kind)) {
            ++groups;
        } else if (closes_group(kind)) {
            if (groups == 0U) return false;
            --groups;
        } else if (groups == 0U) {
            if (kind == TokenKind::left_brace) return true;
            if (kind == TokenKind::semicolon || kind == TokenKind::right_brace) return false;
        }
    }
    return false;
}

void SheetParser::skip_trivia() noexcept {
    while (next_ < limit_ && is_trivia(tokens_[next_].kind)) ++next_;
}

bool SheetParser::at_end() const noexcept {
    return next_ >= limit_;
}

// Tokens [first, end) without the whitespace and comments at either end. A
// range of nothing else is empty at `first`, so it stays within any node
// that starts by `first`.
TokenRange SheetParser::trimmed(std::uint32_t first, std::uint32_t end) const noexcept {
    std::uint32_t begin = first;
    while (begin < end && is_trivia(tokens_[begin].kind)) ++begin;
    TokenRange range{};
    if (begin == end) {
        range.first = TokenIndex{first};
        range.end = TokenIndex{first};
        return range;
    }
    // tokens_[begin] is not trivia, so this stops above it.
    std::uint32_t stop = end;
    while (is_trivia(tokens_[stop - 1U].kind)) --stop;
    range.first = TokenIndex{begin};
    range.end = TokenIndex{stop};
    return range;
}

std::string_view SheetParser::token_text(std::uint32_t index) const noexcept {
    const Token& token = tokens_[index];
    const std::string_view text = text_.substr(token.offset.value, token.length.value);
    return text;
}

bool SheetParser::push(std::uint32_t opener) {
    if (depth_ == maximum_nesting_depth) {
        fail_at_token(DiagnosticCode::nesting_too_deep, opener, std::to_string(maximum_nesting_depth));
        return false;
    }
    OpenGroup group{};
    group.closer = closer_for(tokens_[opener].kind);
    group.opener = opener;
    open_[depth_] = group;
    ++depth_;
    return true;
}

void SheetParser::pop() noexcept {
    --depth_;
}

std::uint32_t SheetParser::open_node(SyntaxNodeKind kind, std::uint32_t first) {
    SyntaxNode node{};
    node.kind = kind;
    node.tokens.first = TokenIndex{first};
    node.tokens.end = TokenIndex{first};
    node.content = node.tokens;
    const std::uint32_t index = static_cast<std::uint32_t>(tree_.nodes.size());
    tree_.nodes.push_back(node);
    return index;
}

// Ends a node at token `end` (exclusive); its subtree is every node added
// since it opened.
void SheetParser::close_node(std::uint32_t node, std::uint32_t end) noexcept {
    SyntaxNode& closed = tree_.nodes[node];
    closed.tokens.end = TokenIndex{end};
    closed.subtree_end = NodeIndex{static_cast<std::uint32_t>(tree_.nodes.size())};
}

void SheetParser::report(DiagnosticCode code, std::size_t offset, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

void SheetParser::fail_at_token(DiagnosticCode code, std::uint32_t index, std::string detail) {
    report(code, tokens_[index].offset.value, std::move(detail));
    outcome_ = ParseOutcome::stopped_at_error;
}

void SheetParser::fail_at_end(DiagnosticCode code, std::uint32_t opener, std::string detail) {
    if (limit_ < tokens_.size()) {
        outcome_ = ParseOutcome::stopped_at_lexical_error;
        return;
    }
    fail_at_token(code, opener, std::move(detail));
}

// An item that the end of the sheet cuts off before its ';' or block.
void SheetParser::fail_at_end_of_sheet() {
    if (limit_ < tokens_.size()) {
        outcome_ = ParseOutcome::stopped_at_lexical_error;
        return;
    }
    report(DiagnosticCode::unexpected_token, text_.size(), "end of sheet");
    outcome_ = ParseOutcome::stopped_at_error;
}

bool SheetParser::stopped() const noexcept {
    return outcome_ != ParseOutcome::tree_complete;
}

} // namespace

ParseOutcome parse_theme_sheet(const ThemeSource& source, const ThemeTokenBuffer& tokens, const LineStartTable& lines,
                               ThemeSyntaxTree& tree, ThemeDiagnosticList& diagnostics, ThemeSheetForm form) {
    SheetParser parser(source, tokens, lines, tree, diagnostics, form);
    const ParseOutcome outcome = parser.run();
    return outcome;
}

} // namespace gui_forms::theme_language
