// A form document's model (stage FD, phase FD2). One pass over the syntax
// tree in document order builds a scratch model and reports every defect;
// the caller's model is replaced only when there were none.

#include "form_model.hpp"

#include "gui_forms/control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace gui_forms::form_language {

namespace {

bool hexadecimal_digit(const char character) noexcept {
    const bool decimal = character >= '0' && character <= '9';
    const bool lower = character >= 'a' && character <= 'f';
    const bool upper = character >= 'A' && character <= 'F';
    return decimal || lower || upper;
}

std::uint32_t hexadecimal_value(const char character) noexcept {
    if (character >= '0' && character <= '9') return static_cast<std::uint32_t>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<std::uint32_t>(character - 'a') + 10U;
    return static_cast<std::uint32_t>(character - 'A') + 10U;
}

// Appends the UTF-8 encoding of the Unicode scalar `scalar` (no surrogate,
// at most U+10FFFF) to `output`.
void append_scalar_utf8(const std::uint32_t scalar, std::string& output) {
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

// The byte a one-character escape `\<escaped>` stands for, or NUL when
// `escaped` begins no such escape.
char single_escape_meaning(const char escaped) noexcept {
    switch (escaped) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '"':
    case '\'':
    case '\\': return escaped;
    default: return '\0';
    }
}

// The end of the `\u{H}` escape whose 'u' is at `u`, and its scalar; `end`
// is 0 when the escape is malformed or its scalar is refused.
struct ScalarEscape final {
    std::size_t end{};
    std::uint32_t scalar{};
};

ScalarEscape read_scalar_escape(const std::string_view content, const std::size_t u) noexcept {
    ScalarEscape escape{};
    std::size_t index = u + 1U;
    if (index >= content.size() || content[index] != '{') return escape;
    ++index;
    std::uint32_t value = 0U;
    std::size_t digits = 0U;
    while (index < content.size() && digits < 7U && hexadecimal_digit(content[index])) {
        value = value * 16U + hexadecimal_value(content[index]);
        ++index;
        ++digits;
    }
    if (digits == 0U || digits > 6U || index >= content.size() || content[index] != '}') return escape;
    const bool admitted = value != 0U && (value < 0xD800U || value > 0xDFFFU) && value <= 0x10FFFFU;
    if (!admitted) return escape;
    escape.end = index + 1U;
    escape.scalar = value;
    return escape;
}

// The refused escape that begins at the backslash `backslash`, as written:
// the backslash and the code point after it, or through the closing brace
// of a malformed `\u{...}` when there is one.
std::string refused_escape_text(const std::string_view content, const std::size_t backslash) {
    std::size_t end = backslash + 1U;
    if (end >= content.size()) return std::string(content.substr(backslash));
    if (content[end] == 'u') {
        const std::size_t close = content.find('}', end);
        end = close == std::string_view::npos ? end + 1U : close + 1U;
        return std::string(content.substr(backslash, end - backslash));
    }
    ++end;
    while (end < content.size() && (static_cast<unsigned char>(content[end]) & 0xC0U) == 0x80U) ++end;
    return std::string(content.substr(backslash, end - backslash));
}

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::LineStartTable;
using theme_language::NodeIndex;
using theme_language::SourcePositionCursor;
using theme_language::SyntaxNode;
using theme_language::SyntaxNodeKind;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeNumber;
using theme_language::ThemeSource;
using theme_language::ThemeSyntaxTree;
using theme_language::ThemeTokenBuffer;
using theme_language::Token;
using theme_language::TokenKind;
using theme_language::TokenRange;

// The most value tokens any kind reads: a rectangle's or insets' four, a
// font's four, a flags enumeration's choices, a text collection's items.
constexpr std::size_t maximum_value_tokens = 256U;

// The prefix that makes a declaration a handler reference.
constexpr std::string_view handler_prefix = "on-";

[[nodiscard]] bool is_trivia(TokenKind kind) noexcept {
    return kind == TokenKind::whitespace || kind == TokenKind::comment || kind == TokenKind::byte_order_mark;
}

// The registered name a kebab-case name spells: lowercase ASCII letters and
// digits in runs joined by single '-', each '-' becoming '_'. None for any
// other spelling.
[[nodiscard]] std::optional<std::string> registered_from_kebab(std::string_view written) {
    if (written.empty() || written.front() == '-' || written.back() == '-') return std::nullopt;
    std::string registered{};
    registered.reserve(written.size());
    char previous = '\0';
    for (const char character : written) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (character == '-') {
            if (previous == '-') return std::nullopt;
            registered.push_back('_');
        } else if (alphanumeric) {
            registered.push_back(character);
        } else {
            return std::nullopt;
        }
        previous = character;
    }
    return registered;
}

// The position of the descriptor registered as `registered` in
// `descriptors`; none if no descriptor has that name.
[[nodiscard]] std::optional<std::size_t> descriptor_position(std::span<const PropertyDescriptor> descriptors,
                                                             std::string_view registered) {
    for (std::size_t candidate = 0U; candidate < descriptors.size(); ++candidate) {
        if (descriptors[candidate].name == registered) return candidate;
    }
    return std::nullopt;
}

// Whether `name` is one C++ identifier: an ASCII letter or '_', then
// letters, digits and '_'.
[[nodiscard]] bool cpp_identifier(std::string_view name) noexcept {
    if (name.empty()) return false;
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                            character == '_';
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !(digit && index != 0U)) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::uint8_t> hex_digit(char character) noexcept {
    if (character >= '0' && character <= '9') return static_cast<std::uint8_t>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<std::uint8_t>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F') return static_cast<std::uint8_t>(character - 'A' + 10);
    return std::nullopt;
}

// The value tokens of a declaration: its content without trivia, at most
// maximum_value_tokens of them.
struct ValueTokens final {
    std::array<std::uint32_t, maximum_value_tokens> indices{};
    std::size_t count{};
    bool overflowed{};
};

// What reading one value found: the value, or the token it stopped at.
struct ValueReading final {
    std::optional<BindingValue> value{};
    // The offending token, when the value is not of the kind's forms.
    std::uint32_t offending{};
    // The property's kind cannot be written yet (E999).
    bool not_expressible{};
    // A string's refused escape, as written (E815), when that stopped it.
    std::string refused_escape{};
};

// The members one object's block has written so far.
struct WrittenNames final {
    std::unordered_set<std::string> properties{};
    std::unordered_set<std::string> handlers{};
};

class ModelBuilder final {
public:
    ModelBuilder(const ThemeSource& source, const ThemeTokenBuffer& tokens, const LineStartTable& lines,
                 const ThemeSyntaxTree& tree, const FormSchema& schema, ThemeDiagnosticList& diagnostics) noexcept;

    // Fills `scratch`; returns whether no defect was found.
    [[nodiscard]] bool run(FormModel& scratch);
    void check_form_root(const FormModel& scratch);
    // The spans' conflicts, and the placements against their parent's
    // spans (study 10.9): after the walk, since they need all of it.
    void check_spans(const FormModel& scratch);
    // Puts each object's parent-provided properties after its own, in the
    // parent's table order (ProvidedPropertyOrder).
    void order_provided_properties(FormModel& scratch) const;

private:
    void read_object(NodeIndex node, std::optional<ObjectIndex> parent, FormModel& scratch);
    // A `span` block of `owner`, whose type holds spans; `type_token` is
    // its reserved type's token.
    void read_span(NodeIndex node, ObjectIndex owner, std::uint32_t type_token, FormModel& scratch);
    // One whole number of `value`'s token `position`, within [minimum,
    // maximum_grid_span_extent - 1] (a cell coordinate) or [1,
    // maximum_grid_span_extent] (an extent).
    [[nodiscard]] std::optional<std::uint32_t> span_number(const ValueTokens& value, std::size_t position,
                                                           std::uint32_t minimum) const;
    // `written_names` holds the registered names already written in the
    // owner's block, properties and handlers apart.
    void read_declaration(NodeIndex node, ObjectIndex owner, WrittenNames& written_names, FormModel& scratch);
    void read_handler(NodeIndex node, ObjectIndex owner, std::string_view written, WrittenNames& written_names,
                      FormModel& scratch);
    [[nodiscard]] ValueReading read_value(const SyntaxNode& node, const PropertyDescriptor& descriptor) const;
    // The value `value` writes for the descriptor's kind, before the
    // descriptor's own rules; may move `reading`'s offending token.
    [[nodiscard]] std::optional<BindingValue> candidate_value(const ValueTokens& value,
                                                              const PropertyDescriptor& descriptor,
                                                              ValueReading& reading) const;
    [[nodiscard]] std::optional<BindingValue> boolean_value(const ValueTokens& value) const;
    [[nodiscard]] std::optional<BindingValue> integer_value(const ValueTokens& value, BindingValueKind kind) const;
    [[nodiscard]] std::optional<BindingValue> font_value(const ValueTokens& value, ValueReading& reading) const;
    [[nodiscard]] std::optional<BindingValue> text_collection_value(const ValueTokens& value,
                                                                    const PropertyDescriptor& descriptor,
                                                                    ValueReading& reading) const;
    [[nodiscard]] std::optional<BindingValue> geometry_value(const ValueTokens& value, BindingValueKind kind,
                                                             ValueReading& reading) const;
    [[nodiscard]] ValueTokens value_tokens(TokenRange content) const noexcept;
    [[nodiscard]] std::optional<double> number_at(std::uint32_t index) const noexcept;
    [[nodiscard]] std::optional<Color> color_at(std::uint32_t index) const;
    [[nodiscard]] std::optional<BindingValue> enumeration_from(const ValueTokens& value,
                                                              const PropertyDescriptor& descriptor,
                                                              std::uint32_t& offending) const;
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;
    // The bytes of tokens [first, end).
    [[nodiscard]] std::string_view range_text(std::uint32_t first, std::uint32_t end) const noexcept;
    void report(DiagnosticCode code, std::uint32_t token, std::string_view rule, std::string_view property,
                std::string detail);

    const ThemeSource& source_;
    std::string_view text_{};
    const std::vector<Token>& tokens_;
    const std::vector<ThemeNumber>& numbers_;
    const ThemeSyntaxTree& tree_;
    const FormSchema& schema_;
    ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    std::unordered_set<std::string> identities_{};
    bool defective_{};
};

ModelBuilder::ModelBuilder(const ThemeSource& source, const ThemeTokenBuffer& tokens, const LineStartTable& lines,
                           const ThemeSyntaxTree& tree, const FormSchema& schema,
                           ThemeDiagnosticList& diagnostics) noexcept
    : source_(source), text_(source.text), tokens_(tokens.tokens), numbers_(tokens.numbers), tree_(tree),
      schema_(schema), diagnostics_(diagnostics), cursor_(lines, source.text) {}

bool ModelBuilder::run(FormModel& scratch) {
    const std::vector<SyntaxNode>& nodes = tree_.nodes;
    const std::uint32_t count = static_cast<std::uint32_t>(nodes.size());
    // The top level: exactly one object, the root.
    std::size_t roots = 0U;
    std::uint32_t index = 1U;
    while (index < count) {
        if (nodes[index].kind == SyntaxNodeKind::object_rule) {
            ++roots;
            if (roots == 2U) {
                report(DiagnosticCode::form_root_invalid, nodes[index].tokens.first.value, {}, {}, "second root");
            }
        }
        index = nodes[index].subtree_end.value;
    }
    if (roots == 0U) {
        report(DiagnosticCode::form_root_invalid, static_cast<std::uint32_t>(tokens_.size()), {}, {}, "no root");
        return false;
    }
    if (roots > 1U) return false;
    read_object(NodeIndex{1U}, std::nullopt, scratch);
    check_form_root(scratch);
    check_spans(scratch);
    order_provided_properties(scratch);
    return !defective_;
}

namespace {

// Orders one object's properties: its own first, in document order, then
// those its parent provides, in the parent's table order. A table lists a
// value another depends on first (a table's cell before its spans), so the
// loader and the generator apply them in an order the parent admits.
struct ProvidedPropertyOrder final {
    bool operator()(const FormProperty& left, const FormProperty& right) const noexcept {
        const bool left_provided = left.provider == FormPropertyProvider::parent_type;
        const bool right_provided = right.provider == FormPropertyProvider::parent_type;
        if (left_provided != right_provided) return right_provided;
        if (!left_provided) return false;
        return left.schema_property < right.schema_property;
    }
};

} // namespace

void ModelBuilder::order_provided_properties(FormModel& scratch) const {
    std::vector<FormProperty>& properties = scratch.properties;
    std::size_t start = 0U;
    while (start < properties.size()) {
        std::size_t end = start + 1U;
        while (end < properties.size() && properties[end].object == properties[start].object) ++end;
        const std::vector<FormProperty>::iterator first = properties.begin() + static_cast<std::ptrdiff_t>(start);
        const std::vector<FormProperty>::iterator last = properties.begin() + static_cast<std::ptrdiff_t>(end);
        std::stable_sort(first, last, ProvidedPropertyOrder{});
        start = end;
    }
}

// A form root: one control among its children, and its button properties
// naming objects of the document. Reported after the walk, since they need
// all of it.
void ModelBuilder::check_form_root(const FormModel& scratch) {
    const FormObject& root = scratch.objects[0];
    if (root.schema_type >= schema_.types.size() || schema_.types[root.schema_type].name != form_root_type_name) {
        return;
    }
    std::size_t controls = 0U;
    for (const ObjectIndex child : root.children) {
        const std::size_t type = scratch.objects[child.value].schema_type;
        if (type < schema_.types.size() && schema_.types[type].holds_children) ++controls;
    }
    const SyntaxNode& root_node = tree_.nodes[root.node.value];
    if (controls != 1U) {
        report(DiagnosticCode::form_root_invalid, root_node.content.first.value, root.stable_id, {},
               "a form holds one control");
    }
    const FormSchemaType& form = schema_.types[root.schema_type];
    for (const FormProperty& property : scratch.properties) {
        if (property.object.value != 0U) continue;
        const std::string& name = form.properties[property.schema_property].name;
        if (name != "accept_button" && name != "cancel_button") continue;
        const std::string& named = std::get<std::string>(property.value);
        if (identities_.count(named) != 0U) continue;
        const SyntaxNode& declaration = tree_.nodes[property.node.value];
        report(DiagnosticCode::form_value_invalid, declaration.content.first.value, root.stable_id, name,
               "names no object of the document");
    }
}

// Reads the object at `node` and, in document order, its declarations,
// style block and objects. Objects nest at most maximum_nesting_depth deep
// (the parser's bound), so this recursion's depth is bounded by it.
void ModelBuilder::read_object(NodeIndex node, std::optional<ObjectIndex> parent, FormModel& scratch) {
    const SyntaxNode& object_node = tree_.nodes[node.value];
    const std::uint32_t type_token = object_node.content.first.value;
    // The stable ID: the prelude's tokens after the type and its trivia.
    std::uint32_t identity_first = type_token + 1U;
    while (identity_first < object_node.content.end.value && is_trivia(tokens_[identity_first].kind)) {
        ++identity_first;
    }
    const std::string stable_id(range_text(identity_first, object_node.content.end.value));
    const std::string_view written_type = token_text(type_token);
    if (parent && written_type == form_span_type_name) {
        read_span(node, *parent, type_token, scratch);
        return;
    }

    std::optional<std::size_t> schema_type{};
    const std::optional<std::string> registered_type = registered_from_kebab(written_type);
    if (registered_type) {
        for (std::size_t candidate = 0U; candidate < schema_.types.size(); ++candidate) {
            if (schema_.types[candidate].name == *registered_type) {
                schema_type = candidate;
                break;
            }
        }
    }
    if (!schema_type) report(DiagnosticCode::form_type_unknown, type_token, stable_id, {}, std::string(written_type));
    if (!identities_.insert(stable_id).second) {
        report(DiagnosticCode::form_identity_repeated, identity_first, stable_id, {}, stable_id);
    }
    if (parent && schema_type && schema_.types[*schema_type].name == form_root_type_name) {
        report(DiagnosticCode::form_root_invalid, type_token, stable_id, {}, "a form is only the root");
    }
    if (parent && scratch.objects[(*parent).value].schema_type < schema_.types.size()) {
        const FormSchemaType& parent_type = schema_.types[scratch.objects[(*parent).value].schema_type];
        if (!parent_type.holds_children) {
            report(DiagnosticCode::form_children_not_admitted, type_token, stable_id, {}, parent_type.name);
        }
    }

    const ObjectIndex self{static_cast<std::uint32_t>(scratch.objects.size())};
    FormObject object{};
    // An unknown type is recorded past the schema's types, so that its
    // members are reported as nothing else.
    object.schema_type = schema_type ? *schema_type : schema_.types.size();
    object.stable_id = stable_id;
    object.parent = parent;
    object.node = node;
    scratch.objects.push_back(std::move(object));
    if (parent) scratch.objects[(*parent).value].children.push_back(self);

    WrittenNames written{};
    std::uint32_t child = node.value + 1U;
    while (child < object_node.subtree_end.value) {
        const SyntaxNode& item = tree_.nodes[child];
        const std::uint32_t next = item.subtree_end.value;
        if (item.kind == SyntaxNodeKind::declaration) {
            read_declaration(NodeIndex{child}, self, written, scratch);
        } else if (item.kind == SyntaxNodeKind::style_block) {
            report(DiagnosticCode::form_style_block, item.tokens.first.value, stable_id, {},
                   "move its rules to the project theme, on a class the object names in `classes`");
        } else {
            // An object: the only other node an object's block holds.
            read_object(NodeIndex{child}, self, scratch);
        }
        child = next;
    }
}

void ModelBuilder::read_span(NodeIndex node, ObjectIndex owner, std::uint32_t type_token, FormModel& scratch) {
    const SyntaxNode& span_node = tree_.nodes[node.value];
    const std::string owner_id = scratch.objects[owner.value].stable_id;
    const std::size_t owner_type = scratch.objects[owner.value].schema_type;
    // An unknown owner type has been reported once.
    if (owner_type >= schema_.types.size()) return;
    if (!schema_.types[owner_type].holds_spans) {
        report(DiagnosticCode::form_children_not_admitted, type_token, owner_id, form_span_type_name,
               schema_.types[owner_type].name + " holds no spans");
        return;
    }
    // The name: the prelude's tokens after the type, none for an anonymous
    // span; lowercase letters, digits and single '-' (as a type's name).
    std::uint32_t name_first = type_token + 1U;
    while (name_first < span_node.content.end.value && is_trivia(tokens_[name_first].kind)) ++name_first;
    const std::string name(range_text(name_first, span_node.content.end.value));
    bool valid = true;
    if (!name.empty() && !registered_from_kebab(name)) {
        report(DiagnosticCode::form_value_invalid, name_first, owner_id, form_span_type_name,
               "a span's name is lowercase letters, digits and single '-'");
        valid = false;
    }
    GridSpan span{};
    span.name = name;
    bool anchored = false;
    std::unordered_set<std::string> written{};
    std::uint32_t child = node.value + 1U;
    while (child < span_node.subtree_end.value) {
        const SyntaxNode& item = tree_.nodes[child];
        const std::uint32_t next = item.subtree_end.value;
        child = next;
        if (item.kind != SyntaxNodeKind::declaration) {
            report(DiagnosticCode::form_children_not_admitted, item.tokens.first.value, owner_id,
                   form_span_type_name, "a span holds only anchor, rows and columns");
            valid = false;
            continue;
        }
        const std::uint32_t name_token = item.tokens.first.value;
        const std::string member(token_text(name_token));
        if (member != "anchor" && member != "rows" && member != "columns") {
            report(DiagnosticCode::form_property_unknown, name_token, owner_id, member, "span");
            valid = false;
            continue;
        }
        if (!written.insert(member).second) {
            report(DiagnosticCode::form_member_repeated, name_token, owner_id, member, {});
            valid = false;
            continue;
        }
        const ValueTokens value = value_tokens(item.content);
        const bool important = (item.flags & theme_language::syntax_flag_important) != 0U;
        const std::uint32_t offending = value.count == 0U ? name_token : value.indices[0];
        if (member == "anchor") {
            const std::optional<std::uint32_t> row = value.count == 2U ? span_number(value, 0U, 0U) : std::nullopt;
            const std::optional<std::uint32_t> column = value.count == 2U ? span_number(value, 1U, 0U) : std::nullopt;
            if (!row || !column || important || value.overflowed) {
                report(DiagnosticCode::form_value_invalid, offending, owner_id, member, "two cell numbers, row column");
                valid = false;
                continue;
            }
            span.anchor = GridCell{*row, *column};
            anchored = true;
            continue;
        }
        const std::optional<std::uint32_t> extent = value.count == 1U ? span_number(value, 0U, 1U) : std::nullopt;
        if (!extent || important) {
            report(DiagnosticCode::form_value_invalid, offending, owner_id, member, "one whole number from 1");
            valid = false;
            continue;
        }
        if (member == "rows") {
            span.rows = *extent;
        } else {
            span.columns = *extent;
        }
    }
    if (!anchored && valid) {
        report(DiagnosticCode::form_value_invalid, type_token, owner_id, "anchor", "a span needs an anchor");
        valid = false;
    }
    if (!valid) return;
    FormSpan record{};
    record.owner = owner;
    record.span = std::move(span);
    record.node = node;
    scratch.spans.push_back(std::move(record));
}

std::optional<std::uint32_t> ModelBuilder::span_number(const ValueTokens& value, std::size_t position,
                                                       std::uint32_t minimum) const {
    const std::uint32_t index = value.indices[position];
    if (tokens_[index].kind != TokenKind::number) return std::nullopt;
    const ThemeNumber& number = numbers_[tokens_[index].number];
    // A coordinate ends before the limit; an extent may reach it.
    const double maximum = static_cast<double>(maximum_grid_span_extent) - (minimum == 0U ? 1.0 : 0.0);
    if (!number.integer || number.value < static_cast<double>(minimum) || number.value > maximum) {
        return std::nullopt;
    }
    const std::uint32_t whole = static_cast<std::uint32_t>(number.value);
    return whole;
}

void ModelBuilder::check_spans(const FormModel& scratch) {
    const std::vector<FormSpan>& spans = scratch.spans;
    for (std::size_t later = 0U; later < spans.size(); ++later) {
        const FormSpan& span = spans[later];
        const std::string& owner_id = scratch.objects[span.owner.value].stable_id;
        const std::uint32_t token = tree_.nodes[span.node.value].tokens.first.value;
        if (!grid_span_extent_valid(span.span)) {
            report(DiagnosticCode::form_span_conflict, token, owner_id, span.span.name,
                   "it ends past the grid's limit");
            continue;
        }
        for (std::size_t earlier = 0U; earlier < later; ++earlier) {
            const FormSpan& other = spans[earlier];
            if (!(other.owner == span.owner) || !grid_span_extent_valid(other.span)) continue;
            if (!span.span.name.empty() && span.span.name == other.span.name) {
                report(DiagnosticCode::form_member_repeated, token, owner_id, span.span.name, "a span's name");
            } else if (grid_spans_overlap(span.span, other.span)) {
                report(DiagnosticCode::form_span_conflict, token, owner_id, span.span.name,
                       "it shares a cell with an earlier span");
            }
        }
    }
    for (const FormProperty& property : scratch.properties) {
        if (property.provider != FormPropertyProvider::parent_type) continue;
        const FormObject& object = scratch.objects[property.object.value];
        const ObjectIndex parent = *object.parent;
        if (!schema_.types[scratch.objects[parent.value].schema_type].holds_spans) continue;
        const std::string& name = form_property_descriptor(schema_, scratch, property).name;
        const std::uint32_t token = tree_.nodes[property.node.value].content.first.value;
        if (name == "area") {
            const std::string& area = std::get<std::string>(property.value);
            if (area.empty()) continue;
            bool named = false;
            for (const FormSpan& span : spans) {
                if (span.owner == parent && span.span.name == area) named = true;
            }
            if (!named) {
                report(DiagnosticCode::form_span_conflict, token, object.stable_id, name,
                       "names no span of the parent");
            }
            continue;
        }
        if (name != "cell") continue;
        const Point point = std::get<Point>(property.value);
        const bool whole = point.x >= 0.0 && point.y >= 0.0 && point.x < maximum_grid_span_extent &&
                           point.y < maximum_grid_span_extent && std::floor(point.x) == point.x &&
                           std::floor(point.y) == point.y;
        if (!whole) {
            report(DiagnosticCode::form_value_invalid, token, object.stable_id, name, "two cell numbers, row column");
            continue;
        }
        const GridCell cell{static_cast<std::uint32_t>(point.x), static_cast<std::uint32_t>(point.y)};
        for (const FormSpan& span : spans) {
            if (!(span.owner == parent) || !grid_span_extent_valid(span.span)) continue;
            if (grid_span_interior(span.span, cell)) {
                report(DiagnosticCode::form_span_conflict, token, object.stable_id, name,
                       "it lies inside a span; place the child at the span's anchor");
            }
        }
    }
    // A span over a cell a child is placed at is the same conflict as
    // that cell inside the span, reported once above at the placement.
}

void ModelBuilder::read_declaration(NodeIndex node, ObjectIndex owner, WrittenNames& written_names,
                                    FormModel& scratch) {
    const SyntaxNode& declaration = tree_.nodes[node.value];
    const std::uint32_t name_token = declaration.tokens.first.value;
    const std::string_view written = token_text(name_token);
    const FormObject& object = scratch.objects[owner.value];
    const std::string stable_id = object.stable_id;
    if (written.starts_with(handler_prefix)) {
        read_handler(node, owner, written, written_names, scratch);
        return;
    }
    // An object of an unknown type has been reported once; its members
    // are not checked.
    if (object.schema_type >= schema_.types.size()) return;
    const FormSchemaType& type = schema_.types[object.schema_type];
    // A name the object's type lacks may be one its parent provides to its
    // children (attached properties, study section 10). A root has no
    // parent, and a parent of an unknown type provides nothing.
    const FormSchemaType* parent_type = nullptr;
    if (object.parent) {
        const std::size_t parent_schema_type = scratch.objects[(*object.parent).value].schema_type;
        if (parent_schema_type < schema_.types.size()) parent_type = &schema_.types[parent_schema_type];
    }
    const std::optional<std::string> registered = registered_from_kebab(written);
    std::optional<std::size_t> property{};
    FormPropertyProvider provider = FormPropertyProvider::object_type;
    if (registered) {
        property = descriptor_position(type.properties, *registered);
        if (!property && parent_type != nullptr) {
            property = descriptor_position((*parent_type).child_properties, *registered);
            provider = FormPropertyProvider::parent_type;
        }
    }
    if (!property) {
        const std::string detail = parent_type == nullptr
            ? type.name
            : "neither " + type.name + " nor its parent's type " + (*parent_type).name + " provides it";
        report(DiagnosticCode::form_property_unknown, name_token, stable_id, written, detail);
        return;
    }
    const PropertyDescriptor& descriptor = provider == FormPropertyProvider::object_type
        ? type.properties[*property]
        : (*parent_type).child_properties[*property];
    if (!descriptor.writable) {
        report(DiagnosticCode::form_property_read_only, name_token, stable_id, written, {});
        return;
    }
    // A form is ontological: a font or a colour belongs to the project
    // theme, reached through `classes` (owner, 2026-11-04). A content value
    // (the font a TypographySpecimen shows) is not presentation.
    if (!descriptor.content_value && descriptor.kind == BindingValueKind::font) {
        report(DiagnosticCode::form_presentation_property, name_token, stable_id, written,
               "set it in the project theme (font-family, font-size, font-weight, font-style, "
               "letter-spacing) on a class the object names in `classes`");
        return;
    }
    if (!descriptor.content_value && descriptor.kind == BindingValueKind::color) {
        report(DiagnosticCode::form_presentation_property, name_token, stable_id, written,
               "set it in the project theme (color, background, border-color) on a class the object "
               "names in `classes`");
        return;
    }
    if (!written_names.properties.insert(*registered).second) {
        report(DiagnosticCode::form_member_repeated, name_token, stable_id, written, {});
        return;
    }
    if ((declaration.flags & theme_language::syntax_flag_important) != 0U) {
        report(DiagnosticCode::form_value_invalid, name_token, stable_id, written, "!important");
        return;
    }
    const ValueReading reading = read_value(declaration, descriptor);
    if (reading.not_expressible) {
        report(DiagnosticCode::not_yet_built, declaration.content.first.value, stable_id, written,
               "a value of this kind");
        return;
    }
    if (!reading.value && !reading.refused_escape.empty()) {
        report(DiagnosticCode::form_escape_invalid, reading.offending, stable_id, written, reading.refused_escape);
        return;
    }
    if (!reading.value) {
        report(DiagnosticCode::form_value_invalid, reading.offending, stable_id, written, {});
        return;
    }
    FormProperty record{};
    record.object = owner;
    record.provider = provider;
    record.schema_property = *property;
    record.node = node;
    record.value = *reading.value;
    scratch.properties.push_back(std::move(record));
}

void ModelBuilder::read_handler(NodeIndex node, ObjectIndex owner, std::string_view written,
                                WrittenNames& written_names, FormModel& scratch) {
    const SyntaxNode& declaration = tree_.nodes[node.value];
    const std::uint32_t name_token = declaration.tokens.first.value;
    const FormObject& object = scratch.objects[owner.value];
    const std::string stable_id = object.stable_id;
    if (object.schema_type >= schema_.types.size()) return;
    const FormSchemaType& type = schema_.types[object.schema_type];
    const std::optional<std::string> interaction = registered_from_kebab(written.substr(handler_prefix.size()));
    bool known = false;
    if (interaction) {
        for (const std::string& candidate : type.interactions) {
            if (candidate == *interaction) {
                known = true;
                break;
            }
        }
    }
    if (!known) {
        report(DiagnosticCode::form_property_unknown, name_token, stable_id, written, type.name);
        return;
    }
    if (!written_names.handlers.insert(*interaction).second) {
        report(DiagnosticCode::form_member_repeated, name_token, stable_id, written, {});
        return;
    }
    const ValueTokens value = value_tokens(declaration.content);
    const bool one_ident = value.count == 1U && !value.overflowed && tokens_[value.indices[0]].kind == TokenKind::ident;
    const std::string_view handler = one_ident ? token_text(value.indices[0]) : std::string_view{};
    const bool important = (declaration.flags & theme_language::syntax_flag_important) != 0U;
    if (!one_ident || !cpp_identifier(handler) || important) {
        const std::uint32_t offending = value.count == 0U ? name_token : value.indices[0];
        report(DiagnosticCode::form_handler_invalid, offending, stable_id, written, {});
        return;
    }
    FormHandler record{};
    record.object = owner;
    record.interaction = *interaction;
    record.handler = std::string(handler);
    record.node = node;
    scratch.handlers.push_back(std::move(record));
}

ValueTokens ModelBuilder::value_tokens(TokenRange content) const noexcept {
    ValueTokens value{};
    for (std::uint32_t index = content.first.value; index < content.end.value; ++index) {
        if (is_trivia(tokens_[index].kind)) continue;
        if (value.count == maximum_value_tokens) {
            value.overflowed = true;
            return value;
        }
        value.indices[value.count] = index;
        ++value.count;
    }
    return value;
}

std::optional<double> ModelBuilder::number_at(std::uint32_t index) const noexcept {
    const Token& token = tokens_[index];
    if (token.kind != TokenKind::number) return std::nullopt;
    const double value = numbers_[token.number].value;
    return value;
}

std::optional<Color> ModelBuilder::color_at(std::uint32_t index) const {
    const Token& token = tokens_[index];
    if (token.kind != TokenKind::hash) return std::nullopt;
    // The digits after '#'.
    const std::string_view digits = token_text(index).substr(1U);
    const std::size_t length = digits.size();
    const bool short_form = length == 3U || length == 4U;
    const bool long_form = length == 6U || length == 8U;
    if (!short_form && !long_form) return std::nullopt;
    std::array<std::uint8_t, 4> channels{0U, 0U, 0U, 255U};
    const std::size_t channel_count = short_form ? length : length / 2U;
    for (std::size_t channel = 0U; channel < channel_count; ++channel) {
        if (short_form) {
            const std::optional<std::uint8_t> digit = hex_digit(digits[channel]);
            if (!digit) return std::nullopt;
            channels[channel] = static_cast<std::uint8_t>(*digit * 17U);
        } else {
            const std::optional<std::uint8_t> high = hex_digit(digits[channel * 2U]);
            const std::optional<std::uint8_t> low = hex_digit(digits[channel * 2U + 1U]);
            if (!high || !low) return std::nullopt;
            channels[channel] = static_cast<std::uint8_t>(*high * 16U + *low);
        }
    }
    const Color color{channels[0], channels[1], channels[2], channels[3]};
    return color;
}

std::optional<BindingValue> ModelBuilder::enumeration_from(const ValueTokens& value,
                                                           const PropertyDescriptor& descriptor,
                                                           std::uint32_t& offending) const {
    const PropertyEnumDescriptor& enumeration = *descriptor.enumeration;
    if (value.count > 1U && !enumeration.flags) {
        offending = value.indices[1];
        return std::nullopt;
    }
    std::int64_t combined{};
    for (std::size_t position = 0U; position < value.count; ++position) {
        const std::uint32_t index = value.indices[position];
        offending = index;
        if (tokens_[index].kind != TokenKind::ident) return std::nullopt;
        const std::optional<std::string> registered = registered_from_kebab(token_text(index));
        if (!registered) return std::nullopt;
        const PropertyEnumChoice* found = nullptr;
        for (const PropertyEnumChoice& choice : enumeration.choices) {
            if (choice.name == *registered) {
                found = &choice;
                break;
            }
        }
        if (found == nullptr) return std::nullopt;
        combined = value.count == 1U ? (*found).value : (combined | (*found).value);
    }
    const std::optional<BindingValue> converted = convert_property_value(BindingValue{combined}, descriptor);
    return converted;
}

ValueReading ModelBuilder::read_value(const SyntaxNode& node, const PropertyDescriptor& descriptor) const {
    ValueReading reading{};
    const ValueTokens value = value_tokens(node.content);
    reading.offending = value.count == 0U ? node.tokens.first.value : value.indices[0];
    if (value.count == 0U || value.overflowed) return reading;
    const std::optional<BindingValue> candidate = candidate_value(value, descriptor, reading);
    if (!candidate) return reading;
    // The descriptor's own rules: ranges, standard values.
    reading.value = convert_property_value(*candidate, descriptor);
    return reading;
}

std::optional<BindingValue> ModelBuilder::candidate_value(const ValueTokens& value,
                                                          const PropertyDescriptor& descriptor,
                                                          ValueReading& reading) const {
    const std::uint32_t first = value.indices[0];
    switch (descriptor.kind) {
    case BindingValueKind::boolean: return boolean_value(value);
    case BindingValueKind::signed_integer:
    case BindingValueKind::unsigned_integer: return integer_value(value, descriptor.kind);
    case BindingValueKind::number: {
        if (value.count != 1U) return std::nullopt;
        const std::optional<double> number = number_at(first);
        if (!number) return std::nullopt;
        return BindingValue{*number};
    }
    case BindingValueKind::text: {
        if (value.count != 1U || tokens_[first].kind != TokenKind::string) return std::nullopt;
        FormStringReading text = read_form_string(token_text(first));
        if (!text.valid) {
            reading.refused_escape = std::move(text.escape);
            return std::nullopt;
        }
        return BindingValue{std::move(text.text)};
    }
    case BindingValueKind::point:
    case BindingValueKind::size:
    case BindingValueKind::rectangle:
    case BindingValueKind::insets: return geometry_value(value, descriptor.kind, reading);
    case BindingValueKind::color: {
        if (value.count != 1U) return std::nullopt;
        const std::optional<Color> color = color_at(first);
        if (!color) return std::nullopt;
        return BindingValue{*color};
    }
    case BindingValueKind::enumeration: {
        if (!descriptor.enumeration) return std::nullopt;
        std::uint32_t offending = first;
        const std::optional<BindingValue> choice = enumeration_from(value, descriptor, offending);
        reading.offending = offending;
        return choice;
    }
    case BindingValueKind::font: return font_value(value, reading);
    case BindingValueKind::collection: return text_collection_value(value, descriptor, reading);
    case BindingValueKind::null:
    case BindingValueKind::image:
    case BindingValueKind::object:
        reading.not_expressible = true;
        return std::nullopt;
    }
    // A kind outside the enumeration: no value of it can be written.
    return std::nullopt;
}

std::optional<BindingValue> ModelBuilder::boolean_value(const ValueTokens& value) const {
    const std::uint32_t first = value.indices[0];
    if (value.count != 1U || tokens_[first].kind != TokenKind::ident) return std::nullopt;
    const std::string_view word = token_text(first);
    if (word == "true") return BindingValue{true};
    if (word == "false") return BindingValue{false};
    return std::nullopt;
}

std::optional<BindingValue> ModelBuilder::integer_value(const ValueTokens& value, BindingValueKind kind) const {
    const std::uint32_t first = value.indices[0];
    if (value.count != 1U || tokens_[first].kind != TokenKind::number) return std::nullopt;
    const ThemeNumber& number = numbers_[tokens_[first].number];
    // The tokenizer's significant-digit bound keeps an integer exact in a
    // double; this range keeps it within both integer types.
    const bool in_range = number.value >= -9.0e15 && number.value <= 9.0e15;
    if (!number.integer || !in_range) return std::nullopt;
    const std::int64_t integer = static_cast<std::int64_t>(number.value);
    if (kind == BindingValueKind::signed_integer) return BindingValue{integer};
    if (integer < 0) return std::nullopt;
    return BindingValue{static_cast<std::uint64_t>(integer)};
}

// A font (owner decision FD6-3): `<role> <size> <weight> [italic]
// [spacing <n>]`, the role one of control, content and monospace, the size
// a positive number of logical pixels, the weight an integer from 1 to
// 1000, the letter spacing (FD6.3) a number of logical pixels from minus a
// quarter of the size to the size (valid_font_spec).
std::optional<BindingValue> ModelBuilder::font_value(const ValueTokens& value, ValueReading& reading) const {
    if (value.count < 3U || value.count > 6U) {
        reading.offending = value.indices[value.count - 1U];
        return std::nullopt;
    }
    FontSpec font{};
    const std::uint32_t role = value.indices[0];
    reading.offending = role;
    if (tokens_[role].kind != TokenKind::ident) return std::nullopt;
    const std::string_view role_word = token_text(role);
    if (role_word == "control") {
        font.role = FontRole::control;
    } else if (role_word == "content") {
        font.role = FontRole::content;
    } else if (role_word == "monospace") {
        font.role = FontRole::monospace;
    } else {
        return std::nullopt;
    }
    reading.offending = value.indices[1];
    const std::optional<double> size = number_at(value.indices[1]);
    if (!size || !(*size > 0.0) || *size > 4096.0) return std::nullopt;
    font.size = *size;
    reading.offending = value.indices[2];
    const Token& weight_token = tokens_[value.indices[2]];
    if (weight_token.kind != TokenKind::number) return std::nullopt;
    const ThemeNumber& weight = numbers_[weight_token.number];
    if (!weight.integer || weight.value < 1.0 || weight.value > 1000.0) return std::nullopt;
    font.weight = static_cast<std::uint16_t>(weight.value);
    std::size_t next = 3U;
    if (next < value.count) {
        const std::uint32_t word = value.indices[next];
        reading.offending = word;
        if (tokens_[word].kind != TokenKind::ident) return std::nullopt;
        if (token_text(word) == "italic") {
            font.italic = true;
            ++next;
        }
    }
    if (next < value.count) {
        const std::uint32_t word = value.indices[next];
        reading.offending = word;
        if (tokens_[word].kind != TokenKind::ident || token_text(word) != "spacing") return std::nullopt;
        if (next + 2U != value.count) {
            reading.offending = value.indices[value.count - 1U];
            return std::nullopt;
        }
        reading.offending = value.indices[next + 1U];
        const std::optional<double> spacing = number_at(value.indices[next + 1U]);
        if (!spacing) return std::nullopt;
        font.letter_spacing = *spacing;
        next += 2U;
    }
    if (next != value.count) {
        reading.offending = value.indices[next];
        return std::nullopt;
    }
    if (!valid_font_spec(font)) return std::nullopt;
    return BindingValue{font};
}

// A collection of text (owner decision FD6-4): one or more strings, for a
// property whose default is a text collection; its item type is the
// default's.
std::optional<BindingValue> ModelBuilder::text_collection_value(const ValueTokens& value,
                                                                const PropertyDescriptor& descriptor,
                                                                ValueReading& reading) const {
    const PropertyCollectionValue* model = nullptr;
    if (descriptor.default_value) model = std::get_if<PropertyCollectionValue>(&*descriptor.default_value);
    if (model == nullptr || !*model || (*model).item_kind() != BindingValueKind::text) {
        reading.not_expressible = true;
        return std::nullopt;
    }
    std::vector<BindingValue> items{};
    items.reserve(value.count);
    for (std::size_t position = 0U; position < value.count; ++position) {
        const std::uint32_t index = value.indices[position];
        if (tokens_[index].kind != TokenKind::string) {
            reading.offending = index;
            return std::nullopt;
        }
        FormStringReading item = read_form_string(token_text(index));
        if (!item.valid) {
            reading.offending = index;
            reading.refused_escape = std::move(item.escape);
            return std::nullopt;
        }
        items.emplace_back(std::move(item.text));
    }
    BindingValue collection{
        make_property_collection(std::string((*model).item_type_name()), BindingValueKind::text, std::move(items))};
    return collection;
}

std::optional<BindingValue> ModelBuilder::geometry_value(const ValueTokens& value, BindingValueKind kind,
                                                         ValueReading& reading) const {
    std::array<double, 4> numbers{};
    for (std::size_t position = 0U; position < value.count && position < numbers.size(); ++position) {
        const std::optional<double> number = number_at(value.indices[position]);
        if (!number) {
            reading.offending = value.indices[position];
            return std::nullopt;
        }
        numbers[position] = *number;
    }
    if (kind == BindingValueKind::point && value.count == 2U) return BindingValue{Point{numbers[0], numbers[1]}};
    if (kind == BindingValueKind::size && value.count == 2U) return BindingValue{Size{numbers[0], numbers[1]}};
    if (kind == BindingValueKind::rectangle && value.count == 4U) {
        return BindingValue{Rect{numbers[0], numbers[1], numbers[2], numbers[3]}};
    }
    // Insets in CSS order: all; vertical and horizontal; top right bottom
    // left.
    if (kind == BindingValueKind::insets && value.count == 1U) {
        return BindingValue{Insets{numbers[0], numbers[0], numbers[0], numbers[0]}};
    }
    if (kind == BindingValueKind::insets && value.count == 2U) {
        return BindingValue{Insets{numbers[1], numbers[0], numbers[1], numbers[0]}};
    }
    if (kind == BindingValueKind::insets && value.count == 4U) {
        return BindingValue{Insets{numbers[3], numbers[0], numbers[1], numbers[2]}};
    }
    // The wrong count: the last token written is the one too many, or the
    // one before the missing ones.
    reading.offending = value.indices[value.count - 1U];
    return std::nullopt;
}

std::string_view ModelBuilder::token_text(std::uint32_t index) const noexcept {
    const Token& token = tokens_[index];
    const std::string_view text = text_.substr(token.offset.value, token.length.value);
    return text;
}

std::string_view ModelBuilder::range_text(std::uint32_t first, std::uint32_t end) const noexcept {
    if (first >= end) return {};
    const Token& last = tokens_[end - 1U];
    const std::size_t begin = tokens_[first].offset.value;
    const std::size_t stop = static_cast<std::size_t>(last.offset.value) + last.length.value;
    return text_.substr(begin, stop - begin);
}

// Reports at `token`'s offset, or at the document's end for the token
// count.
void ModelBuilder::report(DiagnosticCode code, std::uint32_t token, std::string_view rule, std::string_view property,
                          std::string detail) {
    const std::size_t offset = token < tokens_.size() ? tokens_[token].offset.value : text_.size();
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    diagnostic.rule = std::string(rule);
    diagnostic.property = std::string(property);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    defective_ = true;
}

} // namespace

FormSchemaType form_root_type() {
    FormSchemaType type{};
    type.name = std::string(form_root_type_name);
    type.holds_children = true;
    type.properties.emplace_back("text", BindingValueKind::text, "Appearance", "The window's title.", std::nullopt,
                                 Dirty::none);
    type.properties.emplace_back("client_size", BindingValueKind::size, "Layout",
                                 "The size of the window's client area.", std::nullopt, Dirty::none);
    type.properties.emplace_back("accept_button", BindingValueKind::text, "Behavior",
                                 "The stable ID of the button Enter activates.", std::nullopt, Dirty::none);
    type.properties.emplace_back("cancel_button", BindingValueKind::text, "Behavior",
                                 "The stable ID of the button Escape activates.", std::nullopt, Dirty::none);
    return type;
}

FormSchemaType describe_form_type(std::string name, const Component& prototype) {
    FormSchemaType type{};
    type.name = std::move(name);
    const Control* control = dynamic_cast<const Control*>(&prototype);
    type.holds_children = control != nullptr;
    if (control != nullptr) {
        type.properties = (*control).property_descriptors();
        type.child_properties = (*control).child_property_descriptors();
    }
    const std::vector<InteractionDeclaration> interactions = prototype.interactions();
    type.interactions.reserve(interactions.size());
    for (const InteractionDeclaration& interaction : interactions) {
        type.interactions.emplace_back(interaction.name);
    }
    return type;
}

FormStringReading read_form_string(const std::string_view token) {
    FormStringReading reading{};
    const bool quoted = token.size() >= 2U && (token.front() == '"' || token.front() == '\'') &&
                        token.back() == token.front();
    if (!quoted) return reading;
    const std::string_view content = token.substr(1U, token.size() - 2U);
    std::string text{};
    text.reserve(content.size());
    std::size_t index = 0U;
    while (index < content.size()) {
        const char character = content[index];
        if (character != '\\') {
            text.push_back(character);
            ++index;
            continue;
        }
        const char escaped = index + 1U < content.size() ? content[index + 1U] : '\0';
        const char meaning = single_escape_meaning(escaped);
        if (meaning != '\0') {
            text.push_back(meaning);
            index += 2U;
            continue;
        }
        if (escaped == 'u') {
            const ScalarEscape scalar = read_scalar_escape(content, index + 1U);
            if (scalar.end != 0U) {
                append_scalar_utf8(scalar.scalar, text);
                index = scalar.end;
                continue;
            }
        }
        reading.escape = refused_escape_text(content, index);
        return reading;
    }
    reading.text = std::move(text);
    reading.valid = true;
    return reading;
}

std::string quote_form_string(const std::string_view text) {
    static constexpr std::string_view digits = "0123456789ABCDEF";
    std::string quoted{"\""};
    quoted.reserve(text.size() + 2U);
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            quoted.push_back('\\');
            quoted.push_back(character);
        } else if (character == '\n') {
            quoted += "\\n";
        } else if (character == '\r') {
            quoted += "\\r";
        } else if (character == '\t') {
            quoted += "\\t";
        } else if (byte < 0x20U || byte == 0x7FU) {
            // A NUL has no escape and stays out of a document's string; the
            // other control bytes are one or two hexadecimal digits.
            if (byte == 0U) continue;
            quoted += "\\u{";
            if (byte >= 0x10U) quoted.push_back(digits[byte >> 4U]);
            quoted.push_back(digits[byte & 0x0FU]);
            quoted.push_back('}');
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('"');
    return quoted;
}

const PropertyDescriptor& form_property_descriptor(const FormSchema& schema, const FormModel& model,
                                                   const FormProperty& property) {
    const FormObject& object = model.objects[property.object.value];
    if (property.provider == FormPropertyProvider::object_type) {
        return schema.types[object.schema_type].properties[property.schema_property];
    }
    // The model records a parent-provided property only on an object whose
    // parent's type is known.
    const FormObject& parent = model.objects[(*object.parent).value];
    return schema.types[parent.schema_type].child_properties[property.schema_property];
}

FormModelOutcome build_form_model(const theme_language::ThemeSource& source,
                                  const theme_language::ThemeTokenBuffer& tokens,
                                  const theme_language::LineStartTable& lines,
                                  const theme_language::ThemeSyntaxTree& tree, const FormSchema& schema,
                                  FormModel& model, theme_language::ThemeDiagnosticList& diagnostics) {
    FormModel scratch{};
    ModelBuilder builder(source, tokens, lines, tree, schema, diagnostics);
    if (!builder.run(scratch)) return FormModelOutcome::refused;
    model = std::move(scratch);
    return FormModelOutcome::model_built;
}

} // namespace gui_forms::form_language
