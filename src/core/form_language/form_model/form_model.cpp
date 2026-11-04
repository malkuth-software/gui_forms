// A form document's model (stage FD, phase FD2). One pass over the syntax
// tree in document order builds a scratch model and reports every defect;
// the caller's model is replaced only when there were none.

#include "form_model.hpp"

#include "gui_forms/control.hpp"

#include <array>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace gui_forms::form_language {

namespace {

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

private:
    void read_object(NodeIndex node, std::optional<ObjectIndex> parent, FormModel& scratch);
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
    return !defective_;
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
            if (scratch.objects[self.value].style) {
                report(DiagnosticCode::form_style_repeated, item.tokens.first.value, stable_id, {}, {});
            } else {
                scratch.objects[self.value].style = NodeIndex{child};
            }
        } else {
            // An object: the only other node an object's block holds.
            read_object(NodeIndex{child}, self, scratch);
        }
        child = next;
    }
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
    const std::optional<std::string> registered = registered_from_kebab(written);
    std::optional<std::size_t> property{};
    if (registered) {
        for (std::size_t candidate = 0U; candidate < type.properties.size(); ++candidate) {
            if (type.properties[candidate].name == *registered) {
                property = candidate;
                break;
            }
        }
    }
    if (!property) {
        report(DiagnosticCode::form_property_unknown, name_token, stable_id, written, type.name);
        return;
    }
    const PropertyDescriptor& descriptor = type.properties[*property];
    if (!descriptor.writable) {
        report(DiagnosticCode::form_property_read_only, name_token, stable_id, written, {});
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
    if (!reading.value) {
        report(DiagnosticCode::form_value_invalid, reading.offending, stable_id, written, {});
        return;
    }
    FormProperty record{};
    record.object = owner;
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
        // The text between the quotes: the language admits no escapes.
        const std::string_view quoted = token_text(first);
        return BindingValue{std::string(quoted.substr(1U, quoted.size() - 2U))};
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
        const std::string_view quoted = token_text(index);
        items.emplace_back(std::string(quoted.substr(1U, quoted.size() - 2U)));
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
    if (control != nullptr) type.properties = (*control).property_descriptors();
    const std::vector<InteractionDeclaration> interactions = prototype.interactions();
    type.interactions.reserve(interactions.size());
    for (const InteractionDeclaration& interaction : interactions) {
        type.interactions.emplace_back(interaction.name);
    }
    return type;
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
