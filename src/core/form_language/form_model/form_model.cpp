// A form document's model (stage FD, phase FD2; XML since cp34). One pass
// over the read XML document in document order builds a scratch model and
// reports every defect; the caller's model is replaced only when there were
// none.

#include "form_model.hpp"

#include "gui_forms/control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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
using theme_language::SourcePositionCursor;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeNumber;
using theme_language::ThemeSource;
using theme_language::ThemeTokenBuffer;
using theme_language::Token;
using theme_language::TokenKind;

// The most value tokens any kind reads: a rectangle's or insets' four, a
// font's four, a flags enumeration's choices, a text collection's items.
constexpr std::size_t maximum_value_tokens = 256U;

// The prefix of a handler reference, which a form no longer admits (E816).
constexpr std::string_view handler_prefix = "on-";

// A registered name a form may not write because the language spells it
// another way (E820): the name, and the detail the refusal gives, naming
// the spelling to write and how its values map.
struct RenamedProperty final {
    std::string_view registered;
    std::string_view detail;
};

// layout_direction is right_to_left's storage (owner ruling of
// 2026-11-11): forms write Windows Forms' RightToLeft.
constexpr std::array<RenamedProperty, 1> renamed_properties{{
    {"layout_direction",
     "write right-to-left instead: left-to-right is no, right-to-left is yes, inherit is inherit"},
}};

// The function a binding is written with, its '(' included (a function
// token's text).
constexpr std::string_view binding_function = "bind(";

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
};

// What reading one `bind(...)` found: the binding, or the token it stopped
// at and the diagnostic that names the defect.
struct BindingReading final {
    std::optional<FormBinding> binding{};
    std::uint32_t offending{};
    DiagnosticCode code{DiagnosticCode::form_binding_invalid};
    std::string detail{};
};

// Whether `byte` is XML whitespace, which separates a collection's items.
[[nodiscard]] bool xml_space(char byte) noexcept {
    const bool space = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
    return space;
}

// Whether `text` is XML whitespace only (the indentation an object element
// may hold).
[[nodiscard]] bool xml_space_only(std::string_view text) noexcept {
    for (const char byte : text) {
        if (!xml_space(byte)) return false;
    }
    return true;
}

// The number of elements `element` and everything below it hold: how far a
// skipped subtree moves the walk's position in FormSyntax::locations.
// Walked in an explicit work list, not recursively.
[[nodiscard]] std::uint32_t subtree_elements(const XmlElement& element) {
    std::uint32_t count = 0U;
    std::vector<const XmlElement*> pending{};
    pending.push_back(&element);
    while (!pending.empty()) {
        const XmlElement* current = pending.back();
        pending.pop_back();
        ++count;
        for (const XmlElement& child : (*current).children) pending.push_back(&child);
    }
    return count;
}

// The text a text-kind attribute value spells: the value itself, less one
// leading backslash when backslashes then `bind(` begin it (form_model.hpp,
// text).
[[nodiscard]] std::string_view literal_text(std::string_view value) noexcept {
    const std::size_t backslashes = value.find_first_not_of('\\');
    if (backslashes == 0U || backslashes == std::string_view::npos) return value;
    if (!value.substr(backslashes).starts_with(binding_function)) return value;
    const std::string_view text = value.substr(1U);
    return text;
}

class ModelBuilder final {
public:
    ModelBuilder(const ThemeSource& source, const FormSyntax& syntax, const FormSchema& schema,
                 ThemeDiagnosticList& diagnostics) noexcept;

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
    // Reads the object `element` and, in document order, its attributes and
    // child elements. `next_element` is the element's position in
    // FormSyntax::locations, and is left past its subtree. Objects nest at
    // most XmlLimits::maximum_depth deep (the reader's bound), so this
    // recursion's depth is bounded by it.
    void read_object(const XmlElement& element, std::uint32_t& next_element, std::optional<ObjectIndex> parent,
                     FormModel& scratch);
    // A `span` element (position `index`) of `owner`, whose type holds spans.
    void read_span(const XmlElement& element, std::uint32_t index, ObjectIndex owner, FormModel& scratch);
    // One whole number of `value`'s token `position`, within [minimum,
    // maximum_grid_span_extent - 1] (a cell coordinate) or [1,
    // maximum_grid_span_extent] (an extent).
    [[nodiscard]] std::optional<std::uint32_t> span_number(const ValueTokens& value, std::size_t position,
                                                           std::uint32_t minimum) const;
    // The property attribute `attribute` of the element `index`, the
    // object `owner`'s; `written_names` holds the registered names already
    // written on it.
    void read_attribute(const XmlElement& element, std::uint32_t index, std::uint32_t attribute, ObjectIndex owner,
                        WrittenNames& written_names, FormModel& scratch);
    // Makes the attribute's value the one the value readers read: its
    // tokens, its text and where it was written. False, with an E807
    // reported, when the theme tokenizer finds a lexical defect in it.
    [[nodiscard]] bool load_value(std::uint32_t index, std::uint32_t attribute, std::string_view text,
                                  std::string_view rule, std::string_view property);
    // Reads the binding the loaded value writes for a property of
    // `descriptor`; `command_slot` says whether the property is a type's
    // Command slot.
    [[nodiscard]] BindingReading read_binding(const ValueTokens& value, const PropertyDescriptor& descriptor,
                                              bool command_slot) const;
    // The loaded value, read for the descriptor's kind (a kind other than
    // text and collection), then by the descriptor's own rules.
    [[nodiscard]] ValueReading read_value(const PropertyDescriptor& descriptor) const;
    [[nodiscard]] std::optional<BindingValue> candidate_value(const ValueTokens& value,
                                                              const PropertyDescriptor& descriptor,
                                                              ValueReading& reading) const;
    [[nodiscard]] std::optional<BindingValue> boolean_value(const ValueTokens& value) const;
    [[nodiscard]] std::optional<BindingValue> integer_value(const ValueTokens& value, BindingValueKind kind) const;
    [[nodiscard]] std::optional<BindingValue> font_value(const ValueTokens& value, ValueReading& reading) const;
    [[nodiscard]] std::optional<BindingValue> geometry_value(const ValueTokens& value, BindingValueKind kind,
                                                             ValueReading& reading) const;
    // A text collection's items from the attribute value `text` (written at
    // `index`, `attribute`): whitespace-separated, a quoted item read as a
    // form string.
    [[nodiscard]] ValueReading text_collection_value(std::string_view text, const PropertyDescriptor& descriptor,
                                                     std::uint32_t index, std::uint32_t attribute) const;
    // The loaded value's tokens without trivia, at most
    // maximum_value_tokens of them.
    [[nodiscard]] ValueTokens value_tokens() const noexcept;
    [[nodiscard]] std::optional<double> number_at(std::uint32_t index) const noexcept;
    [[nodiscard]] std::optional<Color> color_at(std::uint32_t index) const;
    [[nodiscard]] std::optional<BindingValue> enumeration_from(const ValueTokens& value,
                                                              const PropertyDescriptor& descriptor,
                                                              std::uint32_t& offending) const;
    [[nodiscard]] std::string_view token_text(std::uint32_t index) const noexcept;
    // Where the attribute's value begins in the source; and the source
    // offset of the loaded value's token `token` (the value's end for the
    // token count). A value whose entities or line ends XML rewrote
    // reports at its start, since its token offsets no longer match.
    [[nodiscard]] std::size_t value_start(std::uint32_t index, std::uint32_t attribute) const noexcept;
    [[nodiscard]] std::size_t token_offset(std::uint32_t token) const noexcept;
    [[nodiscard]] std::size_t element_offset(std::uint32_t index) const noexcept;
    [[nodiscard]] std::size_t attribute_offset(std::uint32_t index, std::uint32_t attribute) const noexcept;
    void report(DiagnosticCode code, std::size_t offset, std::string_view rule, std::string_view property,
                std::string detail);
    void report_at(DiagnosticCode code, theme_language::SourcePosition position, std::string_view rule,
                   std::string_view property, std::string detail);
    void report_token(DiagnosticCode code, std::uint32_t token, std::string_view rule, std::string_view property,
                      std::string detail);

    const ThemeSource& source_;
    const FormSyntax& syntax_;
    const FormSchema& schema_;
    ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    std::unordered_set<std::string> identities_{};
    bool defective_{};
    // The value being read (load_value): the theme tokenizer's tokens of
    // it, reused from value to value, and its text and source start.
    ThemeTokenBuffer value_buffer_{};
    LineStartTable value_lines_{};
    ThemeDiagnosticList value_diagnostics_{};
    const std::vector<Token>& tokens_;
    const std::vector<ThemeNumber>& numbers_;
    std::string_view text_{};
    std::size_t value_offset_{};
    // Whether the value's bytes are as written, so its token offsets map
    // onto the source.
    bool value_as_written_{};
};

ModelBuilder::ModelBuilder(const ThemeSource& source, const FormSyntax& syntax, const FormSchema& schema,
                           ThemeDiagnosticList& diagnostics) noexcept
    : source_(source), syntax_(syntax), schema_(schema), diagnostics_(diagnostics),
      cursor_(syntax.lines, source.text), tokens_(value_buffer_.tokens), numbers_(value_buffer_.numbers) {}

bool ModelBuilder::run(FormModel& scratch) {
    const XmlElement& root = syntax_.root;
    // The root declares the vocabulary; read_object refuses an `xmlns`
    // anywhere else.
    const XmlAttribute* declared = root.find_attribute(form_namespace_attribute);
    if (declared == nullptr || (*declared).value != form_namespace) {
        report(DiagnosticCode::form_xml_invalid, element_offset(0U), {}, form_namespace_attribute,
               "the root declares xmlns=\"" + std::string(form_namespace) + "\"");
    }
    std::uint32_t next_element = 0U;
    read_object(root, next_element, std::nullopt, scratch);
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
    if (controls != 1U) {
        report_at(DiagnosticCode::form_root_invalid, root.position, root.stable_id, {}, "a form holds one control");
    }
    const FormSchemaType& form = schema_.types[root.schema_type];
    for (const FormProperty& property : scratch.properties) {
        if (property.object.value != 0U) continue;
        const std::string& name = form.properties[property.schema_property].name;
        if (name != "accept_button" && name != "cancel_button") continue;
        const std::string& named = std::get<std::string>(property.value);
        if (identities_.count(named) != 0U) continue;
        report_at(DiagnosticCode::form_value_invalid, property.position, root.stable_id, name,
                  "names no object of the document");
    }
}

void ModelBuilder::read_object(const XmlElement& element, std::uint32_t& next_element,
                               std::optional<ObjectIndex> parent, FormModel& scratch) {
    const std::uint32_t index = next_element;
    ++next_element;
    const std::size_t start = element_offset(index);
    const std::string_view written_type = element.name;
    // The stable ID: the `id` attribute, required.
    const XmlAttribute* identity = element.find_attribute(form_identity_attribute);
    const std::string stable_id = identity == nullptr ? std::string{} : (*identity).value;

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
    if (!schema_type) report(DiagnosticCode::form_type_unknown, start, stable_id, {}, std::string(written_type));
    if (stable_id.empty()) {
        report(DiagnosticCode::form_root_invalid, start, {}, {}, "an object needs an id");
    } else if (!identities_.insert(stable_id).second) {
        report(DiagnosticCode::form_identity_repeated, start, stable_id, {}, stable_id);
    }
    if (parent && schema_type && schema_.types[*schema_type].name == form_root_type_name) {
        report(DiagnosticCode::form_root_invalid, start, stable_id, {}, "a form is only the root");
    }
    if (parent && scratch.objects[(*parent).value].schema_type < schema_.types.size()) {
        const FormSchemaType& parent_type = schema_.types[scratch.objects[(*parent).value].schema_type];
        if (!parent_type.holds_children) {
            report(DiagnosticCode::form_children_not_admitted, start, stable_id, {}, parent_type.name);
        }
    }
    if (!xml_space_only(element.text)) {
        report(DiagnosticCode::form_xml_invalid, start, stable_id, {}, "an object holds no text");
    }

    const ObjectIndex self{static_cast<std::uint32_t>(scratch.objects.size())};
    FormObject object{};
    // An unknown type is recorded past the schema's types, so that its
    // members are reported as nothing else.
    object.schema_type = schema_type ? *schema_type : schema_.types.size();
    object.stable_id = stable_id;
    object.parent = parent;
    object.element = index;
    object.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(start)});
    scratch.objects.push_back(std::move(object));
    if (parent) scratch.objects[(*parent).value].children.push_back(self);

    WrittenNames written{};
    for (std::uint32_t attribute = 0U; attribute < element.attributes.size(); ++attribute) {
        const std::string& name = element.attributes[attribute].name;
        if (name == form_identity_attribute) continue;
        if (name == form_namespace_attribute && !parent) continue;
        if (name == form_namespace_attribute || name.starts_with("xmlns:")) {
            report(DiagnosticCode::form_xml_invalid, attribute_offset(index, attribute), stable_id, name,
                   "only the root declares the namespace, and no prefix is declared");
            continue;
        }
        read_attribute(element, index, attribute, self, written, scratch);
    }
    for (const XmlElement& child : element.children) {
        if (child.name == form_span_type_name) {
            const std::uint32_t span_index = next_element;
            next_element += subtree_elements(child);
            read_span(child, span_index, self, scratch);
        } else if (child.name == "style") {
            report(DiagnosticCode::form_style_block, element_offset(next_element), stable_id, {},
                   "move its rules to the project theme, on a class the object names in `classes`");
            next_element += subtree_elements(child);
        } else {
            read_object(child, next_element, self, scratch);
        }
    }
}

void ModelBuilder::read_span(const XmlElement& element, std::uint32_t index, ObjectIndex owner, FormModel& scratch) {
    const std::string owner_id = scratch.objects[owner.value].stable_id;
    const std::size_t owner_type = scratch.objects[owner.value].schema_type;
    const std::size_t start = element_offset(index);
    // An unknown owner type has been reported once.
    if (owner_type >= schema_.types.size()) return;
    if (!schema_.types[owner_type].holds_spans) {
        report(DiagnosticCode::form_children_not_admitted, start, owner_id, form_span_type_name,
               schema_.types[owner_type].name + " holds no spans");
        return;
    }
    bool valid = true;
    if (!element.children.empty() || !xml_space_only(element.text)) {
        report(DiagnosticCode::form_children_not_admitted, start, owner_id, form_span_type_name,
               "a span holds only anchor, rows and columns");
        valid = false;
    }
    GridSpan span{};
    bool anchored = false;
    for (std::uint32_t attribute = 0U; attribute < element.attributes.size(); ++attribute) {
        const XmlAttribute& written = element.attributes[attribute];
        const std::string& member = written.name;
        const std::size_t name_offset = attribute_offset(index, attribute);
        if (member == "name") {
            // Lowercase letters, digits and single '-' (as a type's name);
            // empty is an anonymous span.
            if (!written.value.empty() && !registered_from_kebab(written.value)) {
                report(DiagnosticCode::form_value_invalid, value_start(index, attribute), owner_id,
                       form_span_type_name, "a span's name is lowercase letters, digits and single '-'");
                valid = false;
            }
            span.name = written.value;
            continue;
        }
        if (member != "anchor" && member != "rows" && member != "columns") {
            report(DiagnosticCode::form_property_unknown, name_offset, owner_id, member, "span");
            valid = false;
            continue;
        }
        if (!load_value(index, attribute, written.value, owner_id, member)) {
            valid = false;
            continue;
        }
        const ValueTokens value = value_tokens();
        const std::size_t offending = value.count == 0U ? value_start(index, attribute) : token_offset(value.indices[0]);
        if (member == "anchor") {
            const std::optional<std::uint32_t> row = value.count == 2U ? span_number(value, 0U, 0U) : std::nullopt;
            const std::optional<std::uint32_t> column = value.count == 2U ? span_number(value, 1U, 0U) : std::nullopt;
            if (!row || !column || value.overflowed) {
                report(DiagnosticCode::form_value_invalid, offending, owner_id, member, "two cell numbers, row column");
                valid = false;
                continue;
            }
            span.anchor = GridCell{*row, *column};
            anchored = true;
            continue;
        }
        const std::optional<std::uint32_t> extent = value.count == 1U ? span_number(value, 0U, 1U) : std::nullopt;
        if (!extent) {
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
        report(DiagnosticCode::form_value_invalid, start, owner_id, "anchor", "a span needs an anchor");
        valid = false;
    }
    if (!valid) return;
    FormSpan record{};
    record.owner = owner;
    record.span = std::move(span);
    record.element = index;
    record.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(start)});
    scratch.spans.push_back(std::move(record));
}

void ModelBuilder::check_spans(const FormModel& scratch) {
    const std::vector<FormSpan>& spans = scratch.spans;
    for (std::size_t later = 0U; later < spans.size(); ++later) {
        const FormSpan& span = spans[later];
        const std::string& owner_id = scratch.objects[span.owner.value].stable_id;
        if (!grid_span_extent_valid(span.span)) {
            report_at(DiagnosticCode::form_span_conflict, span.position, owner_id, span.span.name,
                      "it ends past the grid's limit");
            continue;
        }
        for (std::size_t earlier = 0U; earlier < later; ++earlier) {
            const FormSpan& other = spans[earlier];
            if (!(other.owner == span.owner) || !grid_span_extent_valid(other.span)) continue;
            if (!span.span.name.empty() && span.span.name == other.span.name) {
                report_at(DiagnosticCode::form_member_repeated, span.position, owner_id, span.span.name,
                          "a span's name");
            } else if (grid_spans_overlap(span.span, other.span)) {
                report_at(DiagnosticCode::form_span_conflict, span.position, owner_id, span.span.name,
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
        if (name == "area") {
            const std::string& area = std::get<std::string>(property.value);
            if (area.empty()) continue;
            bool named = false;
            for (const FormSpan& span : spans) {
                if (span.owner == parent && span.span.name == area) named = true;
            }
            if (!named) {
                report_at(DiagnosticCode::form_span_conflict, property.position, object.stable_id, name,
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
            report_at(DiagnosticCode::form_value_invalid, property.position, object.stable_id, name,
                      "two cell numbers, row column");
            continue;
        }
        const GridCell cell{static_cast<std::uint32_t>(point.x), static_cast<std::uint32_t>(point.y)};
        for (const FormSpan& span : spans) {
            if (!(span.owner == parent) || !grid_span_extent_valid(span.span)) continue;
            if (grid_span_interior(span.span, cell)) {
                report_at(DiagnosticCode::form_span_conflict, property.position, object.stable_id, name,
                          "it lies inside a span; place the child at the span's anchor");
            }
        }
    }
    // A span over a cell a child is placed at is the same conflict as
    // that cell inside the span, reported once above at the placement.
}

void ModelBuilder::read_attribute(const XmlElement& element, std::uint32_t index, std::uint32_t attribute,
                                  ObjectIndex owner, WrittenNames& written_names, FormModel& scratch) {
    const XmlAttribute& written_attribute = element.attributes[attribute];
    const std::string_view written = written_attribute.name;
    const std::string_view text = written_attribute.value;
    const std::size_t name_offset = attribute_offset(index, attribute);
    const FormObject& object = scratch.objects[owner.value];
    const std::string stable_id = object.stable_id;
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
    // A renamed property is refused with its new spelling (E820), whether
    // or not the type still stores it under the old name.
    if (registered) {
        for (const RenamedProperty& renamed : renamed_properties) {
            if (*registered != renamed.registered) continue;
            report(DiagnosticCode::form_property_renamed, name_offset, stable_id, written,
                   std::string(renamed.detail));
            return;
        }
    }
    // A handler reference is refused (E816); a property whose name begins
    // with "on" (a ToggleSwitch's on_text) is not one.
    if (!property && written.starts_with(handler_prefix)) {
        report(DiagnosticCode::form_handler_removed, name_offset, stable_id, written,
               "a form names no program code: attach the behaviour from program logic");
        return;
    }
    if (!property) {
        const std::string detail = parent_type == nullptr
            ? type.name
            : "neither " + type.name + " nor its parent's type " + (*parent_type).name + " provides it";
        report(DiagnosticCode::form_property_unknown, name_offset, stable_id, written, detail);
        return;
    }
    const PropertyDescriptor& descriptor = provider == FormPropertyProvider::object_type
        ? type.properties[*property]
        : (*parent_type).child_properties[*property];
    const bool bound = text.starts_with(binding_function);
    if (!descriptor.writable) {
        report(DiagnosticCode::form_property_read_only, name_offset, stable_id, written, {});
        return;
    }
    // A form is ontological: a font or a colour belongs to the project
    // theme, reached through `classes` (owner, 2026-11-04). A content value
    // (the font a TypographySpecimen shows) is not presentation.
    if (!descriptor.content_value && descriptor.kind == BindingValueKind::font) {
        report(DiagnosticCode::form_presentation_property, name_offset, stable_id, written,
               "set it in the project theme (font-family, font-size, font-weight, font-style, "
               "letter-spacing) on a class the object names in `classes`");
        return;
    }
    if (!descriptor.content_value && descriptor.kind == BindingValueKind::color) {
        report(DiagnosticCode::form_presentation_property, name_offset, stable_id, written,
               "set it in the project theme (color, background, border-color) on a class the object "
               "names in `classes`");
        return;
    }
    // XML refuses an attribute written twice; a registered name reached by
    // two spellings is the same member written twice.
    if (!written_names.properties.insert(*registered).second) {
        report(DiagnosticCode::form_member_repeated, name_offset, stable_id, written, {});
        return;
    }
    FormProperty record{};
    record.object = owner;
    record.provider = provider;
    record.schema_property = *property;
    record.attribute = attribute;
    record.position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(name_offset)});
    if (bound) {
        if (!load_value(index, attribute, text, stable_id, written)) return;
        const ValueTokens value = value_tokens();
        const bool window_property = !object.parent && type.name == form_root_type_name;
        const bool command_slot = provider == FormPropertyProvider::object_type && type.bind_command != nullptr &&
                                  descriptor.name == form_command_slot_name;
        BindingReading binding{};
        if (window_property || provider == FormPropertyProvider::parent_type) {
            binding.offending = value.indices[0];
            binding.detail = window_property ? "the form's window properties are not bound"
                                             : "a property the parent provides is not bound";
        } else if (!descriptor.bindable) {
            binding.offending = value.indices[0];
            binding.detail = "the property is not bindable";
        } else {
            binding = read_binding(value, descriptor, command_slot);
        }
        if (!binding.binding) {
            report_token(binding.code, binding.offending, stable_id, written, std::move(binding.detail));
            return;
        }
        record.binding = std::move(binding.binding);
        scratch.properties.push_back(std::move(record));
        return;
    }
    ValueReading reading{};
    if (descriptor.kind == BindingValueKind::text) {
        // The whole value is the text (form_model.hpp); a form's button
        // properties name an object, which check_form_root checks.
        reading.value = convert_property_value(BindingValue{std::string(literal_text(text))}, descriptor);
        if (!reading.value) {
            report(DiagnosticCode::form_value_invalid, value_start(index, attribute), stable_id, written, {});
            return;
        }
    } else if (descriptor.kind == BindingValueKind::collection) {
        reading = text_collection_value(text, descriptor, index, attribute);
    } else {
        if (!load_value(index, attribute, text, stable_id, written)) return;
        reading = read_value(descriptor);
    }
    if (reading.not_expressible) {
        report(DiagnosticCode::not_yet_built, value_start(index, attribute), stable_id, written,
               "a value of this kind");
        return;
    }
    if (!reading.value && !reading.refused_escape.empty()) {
        report(DiagnosticCode::form_escape_invalid, reading.offending, stable_id, written, reading.refused_escape);
        return;
    }
    if (!reading.value) {
        // Text and collection readings carry a source offset; the others a
        // token of the loaded value.
        const bool offset_form =
            descriptor.kind == BindingValueKind::text || descriptor.kind == BindingValueKind::collection;
        const std::size_t offending = offset_form ? reading.offending : token_offset(reading.offending);
        report(DiagnosticCode::form_value_invalid, offending, stable_id, written, {});
        return;
    }
    record.value = std::move(*reading.value);
    scratch.properties.push_back(std::move(record));
}

bool ModelBuilder::load_value(std::uint32_t index, std::uint32_t attribute, std::string_view text,
                              std::string_view rule, std::string_view property) {
    const detail::XmlAttributeLocation& where = syntax_.locations.elements[index].attributes[attribute];
    text_ = text;
    value_offset_ = where.value_offset;
    value_as_written_ = where.value_end - where.value_offset == text.size();
    value_diagnostics_.clear();
    ThemeSource value_source{};
    value_source.file = source_.file;
    value_source.text = text;
    static_cast<void>(theme_language::tokenize_theme_sheet(value_source, value_buffer_, value_lines_,
                                                           value_diagnostics_));
    if (value_diagnostics_.error_count() == 0U) return true;
    const ThemeDiagnostic& first = value_diagnostics_.records()[0];
    std::string detail = std::string(theme_language::diagnostic_code_name(first.code));
    const std::size_t offset = value_as_written_ ? value_offset_ + first.position.offset.value : value_offset_;
    report(DiagnosticCode::form_value_invalid, offset, rule, property, std::move(detail));
    return false;
}

ValueTokens ModelBuilder::value_tokens() const noexcept {
    ValueTokens value{};
    for (std::uint32_t index = 0U; index < tokens_.size(); ++index) {
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

ValueReading ModelBuilder::read_value(const PropertyDescriptor& descriptor) const {
    ValueReading reading{};
    const ValueTokens value = value_tokens();
    reading.offending = value.count == 0U ? static_cast<std::uint32_t>(tokens_.size()) : value.indices[0];
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
    // Read from the attribute's text by read_attribute, never here.
    case BindingValueKind::text:
    case BindingValueKind::collection: return std::nullopt;
    case BindingValueKind::null:
    case BindingValueKind::image:
    case BindingValueKind::object:
        reading.not_expressible = true;
        return std::nullopt;
    }
    // A kind outside the enumeration: no value of it can be written.
    return std::nullopt;
}

// A collection of text (owner decision FD6-4): items separated by
// whitespace, for a property whose default is a text collection; its item
// type is the default's. An item that begins with a quote is a form string
// up to its closing quote. `offending` is a source offset.
ValueReading ModelBuilder::text_collection_value(std::string_view text, const PropertyDescriptor& descriptor,
                                                 std::uint32_t index, std::uint32_t attribute) const {
    ValueReading reading{};
    const std::size_t start = value_start(index, attribute);
    const detail::XmlAttributeLocation& where = syntax_.locations.elements[index].attributes[attribute];
    const bool as_written = where.value_end - where.value_offset == text.size();
    reading.offending = static_cast<std::uint32_t>(start);
    const PropertyCollectionValue* model = nullptr;
    if (descriptor.default_value) model = std::get_if<PropertyCollectionValue>(&*descriptor.default_value);
    if (model == nullptr || !*model || (*model).item_kind() != BindingValueKind::text) {
        reading.not_expressible = true;
        return reading;
    }
    std::vector<BindingValue> items{};
    std::size_t position = 0U;
    while (position < text.size()) {
        if (xml_space(text[position])) {
            ++position;
            continue;
        }
        const std::size_t item_start = position;
        const char quote = text[position];
        if (quote != '"' && quote != '\'') {
            while (position < text.size() && !xml_space(text[position])) ++position;
            items.emplace_back(std::string(text.substr(item_start, position - item_start)));
            continue;
        }
        // A quoted item: to the matching quote, a backslash escaping the
        // byte after it; it must end the item.
        ++position;
        while (position < text.size() && text[position] != quote) {
            if (text[position] == '\\') ++position;
            ++position;
        }
        const bool closed = position < text.size();
        if (closed) ++position;
        const bool separated = position >= text.size() || xml_space(text[position]);
        const std::size_t offset = as_written ? start + item_start : start;
        reading.offending = static_cast<std::uint32_t>(offset);
        if (!closed || !separated) return reading;
        FormStringReading item = read_form_string(text.substr(item_start, position - item_start));
        if (!item.valid) {
            reading.refused_escape = std::move(item.escape);
            return reading;
        }
        items.emplace_back(std::move(item.text));
    }
    if (items.empty()) {
        reading.offending = static_cast<std::uint32_t>(start);
        return reading;
    }
    BindingValue collection{
        make_property_collection(std::string((*model).item_type_name()), BindingValueKind::text, std::move(items))};
    reading.value = convert_property_value(collection, descriptor);
    return reading;
}

std::size_t ModelBuilder::value_start(std::uint32_t index, std::uint32_t attribute) const noexcept {
    const std::size_t offset = syntax_.locations.elements[index].attributes[attribute].value_offset;
    return offset;
}

std::size_t ModelBuilder::token_offset(std::uint32_t token) const noexcept {
    if (!value_as_written_) return value_offset_;
    const std::size_t inside = token < tokens_.size() ? tokens_[token].offset.value : text_.size();
    const std::size_t offset = value_offset_ + inside;
    return offset;
}

std::size_t ModelBuilder::element_offset(std::uint32_t index) const noexcept {
    const std::size_t offset = syntax_.locations.elements[index].start_offset;
    return offset;
}

std::size_t ModelBuilder::attribute_offset(std::uint32_t index, std::uint32_t attribute) const noexcept {
    const std::size_t offset = syntax_.locations.elements[index].attributes[attribute].name_offset;
    return offset;
}

void ModelBuilder::report(DiagnosticCode code, std::size_t offset, std::string_view rule, std::string_view property,
                          std::string detail) {
    const theme_language::SourcePosition position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    report_at(code, position, rule, property, std::move(detail));
}

void ModelBuilder::report_at(DiagnosticCode code, theme_language::SourcePosition position, std::string_view rule,
                             std::string_view property, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = position;
    diagnostic.rule = std::string(rule);
    diagnostic.property = std::string(property);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    defective_ = true;
}

void ModelBuilder::report_token(DiagnosticCode code, std::uint32_t token, std::string_view rule,
                                std::string_view property, std::string detail) {
    report(code, token_offset(token), rule, property, std::move(detail));
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

BindingReading ModelBuilder::read_binding(const ValueTokens& value, const PropertyDescriptor& descriptor,
                                          bool command_slot) const {
    BindingReading reading{};
    // The tokens between `bind(` and its `)`: [1, close).
    std::size_t close = value.count;
    for (std::size_t position = 1U; position < value.count; ++position) {
        if (tokens_[value.indices[position]].kind == TokenKind::right_paren) {
            close = position;
            break;
        }
    }
    if (value.overflowed || close == value.count) {
        reading.offending = value.indices[value.count - 1U];
        reading.detail = "the binding has no closing parenthesis";
        return reading;
    }
    if (close + 1U != value.count) {
        reading.offending = value.indices[close + 1U];
        reading.detail = "a token after the binding";
        return reading;
    }
    FormBinding binding{};
    binding.direction = descriptor.binding_direction;
    std::size_t position = 1U;
    if (position == close || tokens_[value.indices[position]].kind != TokenKind::ident ||
        !cpp_identifier(token_text(value.indices[position]))) {
        reading.offending = value.indices[position == close ? 0U : position];
        reading.detail = "the binding's source is one C++ identifier";
        return reading;
    }
    binding.source = std::string(token_text(value.indices[position]));
    ++position;
    // The field, a string after the source's comma.
    const bool comma_next = position < close && tokens_[value.indices[position]].kind == TokenKind::comma;
    if (comma_next && position + 1U < close && tokens_[value.indices[position + 1U]].kind == TokenKind::string) {
        FormStringReading field = read_form_string(token_text(value.indices[position + 1U]));
        if (!field.valid || field.text.empty()) {
            reading.offending = value.indices[position + 1U];
            reading.code = field.valid ? DiagnosticCode::form_binding_invalid : DiagnosticCode::form_escape_invalid;
            reading.detail = field.valid ? "the binding's field is empty" : field.escape;
            return reading;
        }
        binding.field = std::move(field.text);
        position += 2U;
    }
    // The options, each after a comma.
    bool narrowed = false;
    bool trigger_written = false;
    std::uint32_t trigger_token{};
    while (position < close) {
        const std::uint32_t separator = value.indices[position];
        if (tokens_[separator].kind != TokenKind::comma || position + 1U == close) {
            reading.offending = separator;
            reading.detail = "the binding's arguments are separated by commas";
            return reading;
        }
        ++position;
        const std::uint32_t option = value.indices[position];
        const std::string_view word = tokens_[option].kind == TokenKind::ident ? token_text(option) : std::string_view{};
        reading.offending = option;
        reading.code = DiagnosticCode::form_binding_option_invalid;
        if (word == "one_way") {
            if (narrowed) {
                reading.detail = "one_way is written twice";
                return reading;
            }
            narrowed = true;
            binding.direction = PropertyBindingDirection::one_way;
            ++position;
        } else if (word == "update") {
            const std::uint32_t argument = position + 1U < close ? value.indices[position + 1U] : option;
            const std::string_view when = position + 1U < close && tokens_[argument].kind == TokenKind::ident
                ? token_text(argument)
                : std::string_view{};
            if (trigger_written) {
                reading.detail = "update is written twice";
                return reading;
            }
            if (when == "on-change") {
                binding.trigger = FormUpdateTrigger::on_change;
            } else if (when == "on-validate") {
                binding.trigger = FormUpdateTrigger::on_validate;
            } else {
                reading.offending = argument;
                reading.detail = "update is on-change or on-validate";
                return reading;
            }
            trigger_written = true;
            trigger_token = option;
            position += 2U;
        } else if (word == "convert") {
            reading.code = DiagnosticCode::not_yet_built;
            reading.detail = "a named converter";
            return reading;
        } else {
            reading.detail = "binding options are one_way, update and convert";
            return reading;
        }
    }
    // The options against the source and the property.
    reading.code = DiagnosticCode::form_binding_option_invalid;
    if (trigger_written) {
        reading.offending = trigger_token;
        if (binding.field.empty() || descriptor.kind != BindingValueKind::text) {
            reading.detail = "update applies to a text property bound to a field";
            return reading;
        }
        if (binding.direction != PropertyBindingDirection::two_way) {
            reading.detail = "update applies to a two-way binding";
            return reading;
        }
    }
    reading.code = DiagnosticCode::form_binding_invalid;
    reading.offending = value.indices[1];
    if (command_slot) {
        if (!binding.field.empty() || narrowed) {
            reading.detail = "a Command is bound by its source alone";
            return reading;
        }
    } else if (binding.field.empty()) {
        const bool scalar = descriptor.kind == BindingValueKind::boolean ||
                            descriptor.kind == BindingValueKind::signed_integer ||
                            descriptor.kind == BindingValueKind::number;
        if (!scalar) {
            reading.detail = "no Value carries this property's kind: bind a BindingSource field";
            return reading;
        }
    }
    reading.binding = std::move(binding);
    return reading;
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

void add_form_command_slot(FormSchemaType& type) {
    for (const PropertyDescriptor& existing : type.properties) {
        if (existing.name == form_command_slot_name) return;
    }
    PropertyDescriptor slot{std::string(form_command_slot_name), BindingValueKind::object, "Behavior",
                            "The Command the object runs and follows: bound only.", std::nullopt, Dirty::none};
    slot.readable = false;
    slot.serialization_visibility = PropertySerializationVisibility::hidden;
    type.properties.push_back(std::move(slot));
}

FormSchemaType describe_form_type(std::string name, const Component& prototype) {
    FormSchemaType type{};
    type.name = std::move(name);
    const Control* control = dynamic_cast<const Control*>(&prototype);
    type.holds_children = control != nullptr;
    if (control != nullptr) {
        type.properties = (*control).property_descriptors();
        type.child_properties = (*control).child_property_descriptors();
        // Once per type: the table entry behind each descriptor, unless the
        // registry replaces that name.
        const std::span<const PropertyEntry> table = (*control).property_table();
        type.property_entries.assign(type.properties.size(), nullptr);
        for (std::size_t index = 0U; index < type.properties.size(); ++index) {
            const std::string& name = type.properties[index].name;
            if ((*control).registry_holds_property(name)) continue;
            for (const PropertyEntry& entry : table) {
                if (entry.name != name) continue;
                type.property_entries[index] = &entry;
                break;
            }
        }
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

FormModelOutcome build_form_model(const theme_language::ThemeSource& source, const FormSyntax& syntax,
                                  const FormSchema& schema, FormModel& model,
                                  theme_language::ThemeDiagnosticList& diagnostics) {
    FormModel scratch{};
    ModelBuilder builder(source, syntax, schema, diagnostics);
    if (!builder.run(scratch)) return FormModelOutcome::refused;
    model = std::move(scratch);
    return FormModelOutcome::model_built;
}

FormModelOutcome read_form_model(const theme_language::ThemeSource& source, FormSyntax& syntax,
                                 const FormSchema& schema, FormModel& model,
                                 theme_language::ThemeDiagnosticList& diagnostics) {
    if (read_form_syntax(source, syntax, diagnostics) != FormSyntaxOutcome::read) return FormModelOutcome::refused;
    const FormModelOutcome outcome = build_form_model(source, syntax, schema, model, diagnostics);
    return outcome;
}

} // namespace gui_forms::form_language
