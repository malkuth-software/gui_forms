#include "theme_syntax_tree.hpp"

#include <array>

namespace gui_forms::theme_language {

namespace {

// Nodes nest at most this deep. A theme sheet nests four deep (the sheet,
// an at-rule, a rule and a declaration). In a form document every object
// and style block holds one entry of the parser's explicit stack, so the
// deepest node is the sheet, objects and a style block filling the stack,
// the style block's unbraced rule of leading declarations, and a
// declaration: three more than the stack.
constexpr std::size_t maximum_node_depth = maximum_nesting_depth + 3U;

// The bytes of tokens [first, end), which lie contiguously in the text.
// Returns false when the tokens do not lie within the text.
[[nodiscard]] bool append_tokens(const std::vector<Token>& tokens, std::string_view text, std::uint32_t first,
                                 std::uint32_t end, std::string& output) {
    if (first >= end) return true;
    const Token& last = tokens[end - 1U];
    const std::size_t begin = tokens[first].offset.value;
    const std::size_t stop = static_cast<std::size_t>(last.offset.value) + last.length.value;
    if (stop > text.size() || begin > stop) return false;
    output.append(text.substr(begin, stop - begin));
    return true;
}

} // namespace

std::string_view syntax_node_kind_name(SyntaxNodeKind kind) noexcept {
    switch (kind) {
    case SyntaxNodeKind::sheet: return "sheet";
    case SyntaxNodeKind::import_rule: return "import_rule";
    case SyntaxNodeKind::media_rule: return "media_rule";
    case SyntaxNodeKind::keyframes_rule: return "keyframes_rule";
    case SyntaxNodeKind::theme_header: return "theme_header";
    case SyntaxNodeKind::unknown_at_rule: return "unknown_at_rule";
    case SyntaxNodeKind::style_rule: return "style_rule";
    case SyntaxNodeKind::keyframe_block: return "keyframe_block";
    case SyntaxNodeKind::declaration: return "declaration";
    case SyntaxNodeKind::object_rule: return "object_rule";
    case SyntaxNodeKind::style_block: return "style_block";
    }
    return "unknown";
}

WriteOutcome write_theme_sheet(const ThemeSyntaxTree& tree, const ThemeTokenBuffer& tokens, std::string_view text,
                               std::string& output) {
    output.clear();
    const std::vector<SyntaxNode>& nodes = tree.nodes;
    const std::uint32_t token_count = static_cast<std::uint32_t>(tokens.tokens.size());
    if (nodes.empty()) return token_count == 0U ? WriteOutcome::written : WriteOutcome::tree_inconsistent;
    const SyntaxNode& root = nodes[0];
    const bool root_spans_sheet = root.kind == SyntaxNodeKind::sheet && root.tokens.first.value == 0U &&
                                  root.tokens.end.value == token_count && root.subtree_end.value == nodes.size();
    if (!root_spans_sheet) return WriteOutcome::tree_inconsistent;
    output.reserve(text.size());

    // The open ancestors of the node being visited, innermost last.
    std::array<std::uint32_t, maximum_node_depth> ancestors{};
    std::size_t depth = 1U;
    // Every token before `cursor` has been written.
    std::uint32_t cursor = 0U;
    const std::uint32_t count = static_cast<std::uint32_t>(nodes.size());
    for (std::uint32_t index = 1U; index < count; ++index) {
        while (depth > 1U && nodes[ancestors[depth - 1U]].subtree_end.value <= index) {
            const SyntaxNode& closed = nodes[ancestors[depth - 1U]];
            if (cursor > closed.tokens.end.value) {
                output.clear();
                return WriteOutcome::tree_inconsistent;
            }
            --depth;
        }
        const SyntaxNode& parent = nodes[ancestors[depth - 1U]];
        const SyntaxNode& node = nodes[index];
        const bool nested = node.tokens.first.value >= cursor && node.tokens.first.value <= node.tokens.end.value &&
                            node.tokens.end.value <= parent.tokens.end.value && node.subtree_end.value > index &&
                            node.subtree_end.value <= parent.subtree_end.value;
        if (!nested || !append_tokens(tokens.tokens, text, cursor, node.tokens.first.value, output)) {
            output.clear();
            return WriteOutcome::tree_inconsistent;
        }
        cursor = node.tokens.first.value;
        if (node.subtree_end.value == index + 1U) {
            // A childless node: its own bytes. An editor substitutes here.
            if (!append_tokens(tokens.tokens, text, cursor, node.tokens.end.value, output)) {
                output.clear();
                return WriteOutcome::tree_inconsistent;
            }
            cursor = node.tokens.end.value;
        } else {
            if (depth == maximum_node_depth) {
                output.clear();
                return WriteOutcome::tree_inconsistent;
            }
            ancestors[depth] = index;
            ++depth;
        }
    }
    while (depth > 1U) {
        if (cursor > nodes[ancestors[depth - 1U]].tokens.end.value) {
            output.clear();
            return WriteOutcome::tree_inconsistent;
        }
        --depth;
    }
    if (!append_tokens(tokens.tokens, text, cursor, root.tokens.end.value, output)) {
        output.clear();
        return WriteOutcome::tree_inconsistent;
    }
    return WriteOutcome::written;
}

} // namespace gui_forms::theme_language
