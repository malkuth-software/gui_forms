// A form document's editor (stage FD, phase FD4). Each edit is one or two
// splices of the text, worked out from the syntax tree, then the whole
// edited text is read again as a document; only a valid one replaces the
// caller's.

#include "form_editor.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace gui_forms::form_language {

namespace {

using theme_language::NodeIndex;
using theme_language::ParseOutcome;
using theme_language::SyntaxNode;
using theme_language::SyntaxNodeKind;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSheetForm;
using theme_language::ThemeSource;
using theme_language::ThemeTokenBuffer;
using theme_language::Token;
using theme_language::TokenKind;
using theme_language::TokenizeOutcome;

// The spaces a block's first item is indented deeper than its object.
constexpr std::string_view block_indent = "  ";

// One replacement of the text: bytes [begin, end) become `text`.
struct Splice final {
    std::size_t begin{};
    std::size_t end{};
    std::string text{};
};

[[nodiscard]] bool is_trivia(TokenKind kind) noexcept {
    return kind == TokenKind::whitespace || kind == TokenKind::comment || kind == TokenKind::byte_order_mark;
}

[[nodiscard]] bool is_blank(char character) noexcept {
    return character == ' ' || character == '\t';
}

// Whether `name` is a kebab-case name: lowercase ASCII letters and digits
// in runs joined by single '-'.
[[nodiscard]] bool kebab_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '-' || name.back() == '-') return false;
    char previous = '\0';
    for (const char character : name) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (character == '-' && previous == '-') return false;
        if (!alphanumeric && character != '-') return false;
        previous = character;
    }
    return true;
}

// Whether `value` is the text of one declaration's value: it tokenizes
// cleanly, writes something, and ends no declaration or block.
[[nodiscard]] bool value_text_admitted(std::string_view value) {
    ThemeSource source{};
    source.file = "value";
    source.text = value;
    ThemeTokenBuffer tokens{};
    theme_language::LineStartTable lines{};
    ThemeDiagnosticList diagnostics{};
    const TokenizeOutcome outcome = tokenize_theme_sheet(source, tokens, lines, diagnostics);
    if (outcome != TokenizeOutcome::tokens_cover_sheet || diagnostics.error_count() != 0U) return false;
    bool written = false;
    for (const Token& token : tokens.tokens) {
        const bool ends_item = token.kind == TokenKind::semicolon || token.kind == TokenKind::left_brace ||
                               token.kind == TokenKind::right_brace || token.kind == TokenKind::invalid;
        if (ends_item) return false;
        if (!is_trivia(token.kind)) written = true;
    }
    return written;
}

// Reads a text into a document: tokens, syntax tree and, unless
// `syntax_only`, the model. Returns whether it is a valid document.
[[nodiscard]] bool read_document(FormDocument& document, const FormSchema& schema, bool syntax_only,
                                 ThemeDiagnosticList& diagnostics) {
    ThemeSource source{};
    source.file = document.file;
    source.text = document.text;
    const TokenizeOutcome tokenized = tokenize_theme_sheet(source, document.tokens, document.lines, diagnostics);
    if (tokenized != TokenizeOutcome::tokens_cover_sheet) return false;
    const ParseOutcome parsed = parse_theme_sheet(source, document.tokens, document.lines, document.tree, diagnostics,
                                                  ThemeSheetForm::form_document);
    if (parsed != ParseOutcome::tree_complete) return false;
    if (syntax_only) return true;
    const FormModelOutcome built =
        build_form_model(source, document.tokens, document.lines, document.tree, schema, document.model, diagnostics);
    return built == FormModelOutcome::model_built;
}

// Reads the document's file with `text` and, when valid, makes it the
// document.
[[nodiscard]] FormEditOutcome commit(FormDocument& document, std::string text, const FormSchema& schema,
                                     ThemeDiagnosticList& diagnostics) {
    FormDocument scratch{};
    scratch.file = document.file;
    scratch.text = std::move(text);
    if (!read_document(scratch, schema, false, diagnostics)) return FormEditOutcome::refused;
    document = std::move(scratch);
    return FormEditOutcome::edited;
}

[[nodiscard]] std::string spliced_text(std::string_view text, const Splice& splice) {
    std::string edited{};
    edited.reserve(text.size() - (splice.end - splice.begin) + splice.text.size());
    edited.append(text.substr(0U, splice.begin));
    edited.append(splice.text);
    edited.append(text.substr(splice.end));
    return edited;
}

// Byte positions and layout over one read document.
class TextLayout final {
public:
    explicit TextLayout(const FormDocument& document) noexcept : document_(document) {}

    [[nodiscard]] std::size_t token_begin(std::uint32_t token) const noexcept {
        return document_.tokens.tokens[token].offset.value;
    }
    [[nodiscard]] std::size_t token_end(std::uint32_t token) const noexcept {
        const Token& written = document_.tokens.tokens[token];
        return static_cast<std::size_t>(written.offset.value) + written.length.value;
    }
    [[nodiscard]] std::size_t node_begin(NodeIndex node) const noexcept {
        return token_begin(document_.tree.nodes[node.value].tokens.first.value);
    }
    [[nodiscard]] std::size_t node_end(NodeIndex node) const noexcept {
        return token_end(document_.tree.nodes[node.value].tokens.end.value - 1U);
    }
    // The line ending the document writes.
    [[nodiscard]] std::string_view newline() const& noexcept {
        return document_.text.find("\r\n") != std::string::npos ? std::string_view("\r\n") : std::string_view("\n");
    }
    std::string_view newline() const&& = delete;
    [[nodiscard]] std::size_t line_start(std::size_t offset) const noexcept {
        const std::size_t previous = document_.text.rfind('\n', offset == 0U ? 0U : offset - 1U);
        if (offset == 0U || previous == std::string::npos) return 0U;
        return previous + 1U;
    }
    // The blanks before `offset` on its line, when nothing else is there.
    [[nodiscard]] std::optional<std::string_view> indentation(std::size_t offset) const noexcept {
        const std::size_t start = line_start(offset);
        for (std::size_t index = start; index < offset; ++index) {
            if (!is_blank(document_.text[index])) return std::nullopt;
        }
        return std::string_view(document_.text).substr(start, offset - start);
    }
    // Where the line after `offset` begins, when only blanks and the line
    // ending follow it; the text's end when it ends there.
    [[nodiscard]] std::optional<std::size_t> blank_rest_of_line(std::size_t offset) const noexcept {
        std::size_t index = offset;
        while (index < document_.text.size() && is_blank(document_.text[index])) ++index;
        if (index == document_.text.size()) return index;
        if (document_.text[index] == '\n') return index + 1U;
        if (document_.text[index] == '\r' && index + 1U < document_.text.size() && document_.text[index + 1U] == '\n') {
            return index + 2U;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool same_line(std::size_t first, std::size_t second) const noexcept {
        const std::string_view between = std::string_view(document_.text).substr(first, second - first);
        return between.find('\n') == std::string_view::npos;
    }

private:
    const FormDocument& document_;
};

// The stable ID an object's prelude writes.
[[nodiscard]] std::string_view object_identity(const FormDocument& document, const SyntaxNode& object) {
    const TextLayout layout(document);
    std::uint32_t first = object.content.first.value + 1U;
    while (first < object.content.end.value && is_trivia(document.tokens.tokens[first].kind)) ++first;
    if (first >= object.content.end.value) return {};
    const std::size_t begin = layout.token_begin(first);
    const std::size_t end = layout.token_end(object.content.end.value - 1U);
    return std::string_view(document.text).substr(begin, end - begin);
}

// Whether an object node is a `span` block (study 10.9): a region of its
// container's grid, which is no object of the document.
[[nodiscard]] bool span_block(const FormDocument& document, const SyntaxNode& node) {
    const TextLayout layout(document);
    const std::uint32_t type_token = node.content.first.value;
    const std::size_t begin = layout.token_begin(type_token);
    const std::size_t end = layout.token_end(type_token);
    const bool span = std::string_view(document.text).substr(begin, end - begin) == form_span_type_name;
    return span;
}

[[nodiscard]] std::optional<NodeIndex> find_object(const FormDocument& document, std::string_view stable_id) {
    const std::vector<SyntaxNode>& nodes = document.tree.nodes;
    for (std::uint32_t index = 1U; index < nodes.size(); ++index) {
        if (nodes[index].kind != SyntaxNodeKind::object_rule || span_block(document, nodes[index])) continue;
        if (object_identity(document, nodes[index]) == stable_id) return NodeIndex{index};
    }
    return std::nullopt;
}

// The object's direct children of `kind`, in order; all its direct
// children when `kind` is none. Span blocks are not objects: they are left
// out when `kind` is object_rule.
[[nodiscard]] std::vector<NodeIndex> direct_children(const FormDocument& document, NodeIndex object,
                                                     std::optional<SyntaxNodeKind> kind) {
    std::vector<NodeIndex> children{};
    const std::vector<SyntaxNode>& nodes = document.tree.nodes;
    std::uint32_t child = object.value + 1U;
    while (child < nodes[object.value].subtree_end.value) {
        const bool span = kind && *kind == SyntaxNodeKind::object_rule && span_block(document, nodes[child]);
        if (!kind || (nodes[child].kind == *kind && !span)) children.push_back(NodeIndex{child});
        child = nodes[child].subtree_end.value;
    }
    return children;
}

// The object's `{`: the first token after its prelude that is not trivia.
[[nodiscard]] std::uint32_t open_brace(const FormDocument& document, NodeIndex object) {
    std::uint32_t token = document.tree.nodes[object.value].content.end.value;
    while (is_trivia(document.tokens.tokens[token].kind)) ++token;
    return token;
}

// Places `item` first in the object's block.
[[nodiscard]] Splice insert_first(const FormDocument& document, NodeIndex object, std::string_view item) {
    const TextLayout layout(document);
    const std::size_t brace = layout.token_end(open_brace(document, object));
    const std::size_t close = layout.node_end(object) - 1U;
    Splice splice{};
    splice.begin = brace;
    splice.end = brace;
    if (layout.same_line(brace, close)) {
        splice.text = " " + std::string(item);
        return splice;
    }
    const std::optional<std::string_view> object_indent = layout.indentation(layout.node_begin(object));
    splice.text = std::string(layout.newline()) + std::string(object_indent ? *object_indent : std::string_view{}) +
                  std::string(block_indent) + std::string(item);
    return splice;
}

// Places `item` after the sibling node `after`.
[[nodiscard]] Splice insert_after(const FormDocument& document, NodeIndex object, NodeIndex after,
                                  std::string_view item) {
    const TextLayout layout(document);
    const SyntaxNode& sibling = document.tree.nodes[after.value];
    Splice splice{};
    splice.begin = layout.node_end(after);
    splice.end = splice.begin;
    // A declaration that the block's `}` ended needs its `;` before a
    // sibling follows it.
    const bool unterminated = sibling.kind == SyntaxNodeKind::declaration &&
                              document.tokens.tokens[sibling.tokens.end.value - 1U].kind != TokenKind::semicolon;
    const std::string terminator = unterminated ? std::string(";") : std::string();
    const std::size_t brace = layout.token_begin(open_brace(document, object));
    const std::optional<std::string_view> sibling_indent = layout.indentation(layout.node_begin(after));
    if (layout.same_line(brace, splice.begin) || !sibling_indent) {
        splice.text = terminator + " " + std::string(item);
        return splice;
    }
    splice.text = terminator + std::string(layout.newline()) + std::string(*sibling_indent) + std::string(item);
    return splice;
}

// Places `item` before the sibling node `before`.
[[nodiscard]] Splice insert_before(const FormDocument& document, NodeIndex before, std::string_view item) {
    const TextLayout layout(document);
    Splice splice{};
    splice.begin = layout.node_begin(before);
    splice.end = splice.begin;
    const std::optional<std::string_view> indent = layout.indentation(splice.begin);
    if (!indent) {
        splice.text = std::string(item) + " ";
        return splice;
    }
    splice.text = std::string(item) + std::string(layout.newline()) + std::string(*indent);
    return splice;
}

// Removes a node: with its line when it is alone on it, else with the
// blanks before it.
[[nodiscard]] Splice remove_node(const FormDocument& document, NodeIndex node) {
    const TextLayout layout(document);
    const std::size_t begin = layout.node_begin(node);
    const std::size_t end = layout.node_end(node);
    Splice splice{};
    const std::optional<std::string_view> indent = layout.indentation(begin);
    const std::optional<std::size_t> rest = layout.blank_rest_of_line(end);
    if (indent && rest) {
        splice.begin = layout.line_start(begin);
        splice.end = *rest;
        return splice;
    }
    std::size_t from = begin;
    while (from > 0U && is_blank(document.text[from - 1U])) --from;
    splice.begin = from;
    splice.end = end;
    return splice;
}

// Where an object item goes among `parent`'s children at `position`; none
// when the position is past them.
[[nodiscard]] std::optional<Splice> place_object(const FormDocument& document, NodeIndex parent,
                                                 std::size_t position, std::string_view item) {
    const std::vector<NodeIndex> objects = direct_children(document, parent, SyntaxNodeKind::object_rule);
    if (position > objects.size()) return std::nullopt;
    if (position < objects.size()) return insert_before(document, objects[position], item);
    const std::vector<NodeIndex> children = direct_children(document, parent, std::nullopt);
    if (children.empty()) return insert_first(document, parent, item);
    return insert_after(document, parent, children.back(), item);
}

// The object's declaration of `property`, if written.
[[nodiscard]] std::optional<NodeIndex> find_declaration(const FormDocument& document, NodeIndex object,
                                                        std::string_view property) {
    const TextLayout layout(document);
    for (const NodeIndex declaration : direct_children(document, object, SyntaxNodeKind::declaration)) {
        const std::uint32_t name = document.tree.nodes[declaration.value].tokens.first.value;
        const std::size_t begin = layout.token_begin(name);
        const std::string_view written = std::string_view(document.text).substr(begin, layout.token_end(name) - begin);
        if (written == property) return declaration;
    }
    return std::nullopt;
}

} // namespace

FormEditOutcome open_form_document(FormDocument& document, std::string file, std::string text,
                                   const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    FormDocument scratch{};
    scratch.file = std::move(file);
    scratch.text = std::move(text);
    if (!read_document(scratch, schema, false, diagnostics)) return FormEditOutcome::refused;
    document = std::move(scratch);
    return FormEditOutcome::edited;
}

FormEditOutcome set_form_property(FormDocument& document, std::string_view object_id, std::string_view property,
                                  std::string_view value, const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    if (!kebab_name(property) || !value_text_admitted(value)) return FormEditOutcome::not_admitted;
    const std::optional<NodeIndex> object = find_object(document, object_id);
    if (!object) return FormEditOutcome::object_missing;
    const std::optional<NodeIndex> declaration = find_declaration(document, *object, property);
    Splice splice{};
    if (declaration) {
        // Only the value's bytes change.
        const TextLayout layout(document);
        const SyntaxNode& written = document.tree.nodes[(*declaration).value];
        splice.begin = layout.token_begin(written.content.first.value);
        splice.end = layout.token_end(written.content.end.value - 1U);
        splice.text = std::string(value);
    } else {
        const std::string item = std::string(property) + ": " + std::string(value) + ";";
        const std::vector<NodeIndex> declarations =
            direct_children(document, *object, SyntaxNodeKind::declaration);
        splice = declarations.empty() ? insert_first(document, *object, item)
                                      : insert_after(document, *object, declarations.back(), item);
    }
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome reset_form_property(FormDocument& document, std::string_view object_id, std::string_view property,
                                    const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    const std::optional<NodeIndex> object = find_object(document, object_id);
    if (!object) return FormEditOutcome::object_missing;
    const std::optional<NodeIndex> declaration = find_declaration(document, *object, property);
    if (!declaration) return FormEditOutcome::unchanged;
    const Splice splice = remove_node(document, *declaration);
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome add_form_object(FormDocument& document, std::string_view parent_id, std::size_t position,
                                std::string_view type, std::string_view stable_id, const FormSchema& schema,
                                ThemeDiagnosticList& diagnostics) {
    if (!kebab_name(type) || !valid_stable_id(stable_id)) return FormEditOutcome::not_admitted;
    const std::optional<NodeIndex> parent = find_object(document, parent_id);
    if (!parent) return FormEditOutcome::object_missing;
    const std::string item = std::string(type) + " " + std::string(stable_id) + " { }";
    const std::optional<Splice> splice = place_object(document, *parent, position, item);
    if (!splice) return FormEditOutcome::not_admitted;
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, *splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome remove_form_object(FormDocument& document, std::string_view object_id, const FormSchema& schema,
                                   ThemeDiagnosticList& diagnostics) {
    const std::optional<NodeIndex> object = find_object(document, object_id);
    if (!object) return FormEditOutcome::object_missing;
    if ((*object).value == 1U) return FormEditOutcome::not_admitted;
    const Splice splice = remove_node(document, *object);
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome move_form_object(FormDocument& document, std::string_view object_id, std::string_view parent_id,
                                 std::size_t position, const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    const std::optional<NodeIndex> object = find_object(document, object_id);
    const std::optional<NodeIndex> parent = find_object(document, parent_id);
    if (!object || !parent) return FormEditOutcome::object_missing;
    const SyntaxNode& moved = document.tree.nodes[(*object).value];
    const bool into_itself = (*parent).value >= (*object).value && (*parent).value < moved.subtree_end.value;
    if ((*object).value == 1U || into_itself) return FormEditOutcome::not_admitted;
    const TextLayout layout(document);
    const std::size_t begin = layout.node_begin(*object);
    const std::string item = document.text.substr(begin, layout.node_end(*object) - begin);

    // First the object leaves its place; the parent is found again in the
    // text without it. That text need only be read as syntax: a document
    // may name the moved object elsewhere.
    FormDocument without{};
    without.file = document.file;
    without.text = spliced_text(document.text, remove_node(document, *object));
    if (!read_document(without, schema, true, diagnostics)) return FormEditOutcome::refused;
    const std::optional<NodeIndex> new_parent = find_object(without, parent_id);
    if (!new_parent) return FormEditOutcome::object_missing;
    const std::optional<Splice> splice = place_object(without, *new_parent, position, item);
    if (!splice) return FormEditOutcome::not_admitted;
    const FormEditOutcome outcome = commit(document, spliced_text(without.text, *splice), schema, diagnostics);
    return outcome;
}

} // namespace gui_forms::form_language
