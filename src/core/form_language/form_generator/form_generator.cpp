// A form document's C++ generator (stage FD, phase FD5). Two passes over
// the model: the first checks that every object, value and handler can be
// spelled, reporting every defect; the second writes the header into a
// scratch string, which replaces the caller's only when there were none.

#include "form_generator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms::form_language {

namespace {

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::NodeIndex;
using theme_language::SourcePositionCursor;
using theme_language::ThemeDiagnostic;

// C++20's keywords and alternative tokens, sorted for binary search; a
// stable ID or handler spelled as one of them cannot be declared.
constexpr std::array<std::string_view, 97> cpp_keywords{{
    "alignas",      "alignof",     "and",          "and_eq",       "asm",          "auto",
    "bitand",       "bitor",       "bool",         "break",        "case",         "catch",
    "char",         "char16_t",    "char32_t",     "char8_t",      "class",        "co_await",
    "co_return",    "co_yield",    "compl",        "concept",      "const",        "const_cast",
    "consteval",    "constexpr",   "constinit",    "continue",     "decltype",     "default",
    "delete",       "do",          "double",       "dynamic_cast", "else",         "enum",
    "explicit",     "export",      "extern",       "false",        "final",        "float",
    "for",          "friend",      "goto",         "if",           "import",       "inline",
    "int",          "long",        "module",       "mutable",      "namespace",    "new",
    "noexcept",     "not",         "not_eq",       "nullptr",      "operator",     "or",
    "or_eq",        "override",    "private",      "protected",    "public",       "register",
    "reinterpret_cast", "requires", "return",      "short",        "signed",       "sizeof",
    "static",       "static_assert", "static_cast", "struct",      "switch",       "template",
    "this",         "thread_local", "throw",       "true",         "try",          "typedef",
    "typeid",       "typename",    "union",        "unsigned",     "using",        "virtual",
    "void",         "volatile",    "wchar_t",      "while",        "xor",          "xor_eq",
    "assert",
}};

bool is_keyword(std::string_view name) {
    // `assert` is a macro, not a keyword; it is listed so that no member
    // shadows it. The list is short enough for a linear scan.
    const std::array<std::string_view, 97>::const_iterator found =
        std::find(cpp_keywords.begin(), cpp_keywords.end(), name);
    return found != cpp_keywords.end();
}

bool identifier_start(char character) noexcept {
    const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    return letter || character == '_';
}

bool identifier_part(char character) noexcept {
    const bool digit = character >= '0' && character <= '9';
    return identifier_start(character) || digit;
}

// A double as C++ source: the shortest spelling that reads back the same
// value, always with a '.' or an exponent so that it stays a double.
std::string double_literal(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), written.ptr);
    const bool has_point = text.find_first_of(".e") != std::string::npos;
    if (!has_point) text += ".0";
    return text;
}

// A string as a C++ literal: printable ASCII as itself, '\\' and '"'
// escaped, every other byte (controls, and each byte of a UTF-8 sequence)
// as a three-digit octal escape, which never absorbs a following digit.
std::string string_literal(std::string_view text) {
    std::string literal{"\""};
    literal.reserve(text.size() + 2U);
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '\\' || character == '"') {
            literal += '\\';
            literal += character;
        } else if (byte >= 0x20U && byte < 0x7FU) {
            literal += character;
        } else {
            literal += '\\';
            literal += static_cast<char>('0' + ((byte >> 6U) & 7U));
            literal += static_cast<char>('0' + ((byte >> 3U) & 7U));
            literal += static_cast<char>('0' + (byte & 7U));
        }
    }
    literal += '"';
    return literal;
}

std::string signed_literal(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min()) {
        return "std::numeric_limits<std::int64_t>::min()";
    }
    std::string literal = "std::int64_t{" + std::to_string(value) + "}";
    return literal;
}

// The value's C++ spelling as a BindingValue's alternative, or none for a
// kind the generator cannot spell (the model refuses those first).
std::optional<std::string> value_literal(const BindingValue& value) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        std::string literal = *flag ? "true" : "false";
        return literal;
    }
    if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) return signed_literal(*number);
    if (const std::uint64_t* number = std::get_if<std::uint64_t>(&value)) {
        std::string literal = "std::uint64_t{" + std::to_string(*number) + "U}";
        return literal;
    }
    if (const double* number = std::get_if<double>(&value)) return double_literal(*number);
    if (const std::string* text = std::get_if<std::string>(&value)) {
        std::string literal = "std::string(" + string_literal(*text) + ")";
        return literal;
    }
    if (const Point* point = std::get_if<Point>(&value)) {
        std::string literal = "gui_forms::Point{.x = " + double_literal((*point).x) +
                              ", .y = " + double_literal((*point).y) + "}";
        return literal;
    }
    if (const Size* size = std::get_if<Size>(&value)) {
        std::string literal = "gui_forms::Size{.width = " + double_literal((*size).width) +
                              ", .height = " + double_literal((*size).height) + "}";
        return literal;
    }
    if (const Rect* rect = std::get_if<Rect>(&value)) {
        std::string literal = "gui_forms::Rect{.x = " + double_literal((*rect).x) +
                              ", .y = " + double_literal((*rect).y) +
                              ", .width = " + double_literal((*rect).width) +
                              ", .height = " + double_literal((*rect).height) + "}";
        return literal;
    }
    if (const Insets* insets = std::get_if<Insets>(&value)) {
        std::string literal = "gui_forms::Insets{.left = " + double_literal((*insets).left) +
                              ", .top = " + double_literal((*insets).top) +
                              ", .right = " + double_literal((*insets).right) +
                              ", .bottom = " + double_literal((*insets).bottom) + "}";
        return literal;
    }
    if (const Color* color = std::get_if<Color>(&value)) {
        std::string literal = "gui_forms::Color{.red = " + std::to_string((*color).red) +
                              ", .green = " + std::to_string((*color).green) +
                              ", .blue = " + std::to_string((*color).blue) +
                              ", .alpha = " + std::to_string((*color).alpha) + "}";
        return literal;
    }
    if (const FontSpec* font = std::get_if<FontSpec>(&value)) {
        std::string role = "control";
        if ((*font).role == FontRole::content) role = "content";
        if ((*font).role == FontRole::monospace) role = "monospace";
        std::string literal = "gui_forms::FontSpec{.role = gui_forms::FontRole::" + role +
                              ", .size = " + double_literal((*font).size) +
                              ", .weight = std::uint16_t{" + std::to_string((*font).weight) + "U}" +
                              ", .italic = " + ((*font).italic ? "true" : "false") +
                              ", .letter_spacing = " + double_literal((*font).letter_spacing) + "}";
        return literal;
    }
    if (const PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value)) {
        // Text collections only: the model writes no other.
        if (!*collection || (*collection).item_kind() != BindingValueKind::text) return std::nullopt;
        std::string literal = "gui_forms::make_property_collection(std::string(" +
                              string_literal((*collection).item_type_name()) +
                              "), gui_forms::BindingValueKind::text, std::vector<gui_forms::BindingValue>{";
        const std::span<const BindingValue> items = property_collection_items(*collection);
        for (std::size_t index = 0U; index < items.size(); ++index) {
            if (index != 0U) literal += ", ";
            literal += "gui_forms::BindingValue{std::string(" + string_literal(std::get<std::string>(items[index])) +
                       ")}";
        }
        literal += "})";
        return literal;
    }
    if (const PropertyEnumValue* choice = std::get_if<PropertyEnumValue>(&value)) {
        std::string literal = "gui_forms::PropertyEnumValue{.type_name = std::string(" +
                              string_literal((*choice).type_name) + "), .name = std::string(" +
                              string_literal((*choice).name) + "), .value = " + signed_literal((*choice).value) + "}";
        return literal;
    }
    return std::nullopt;
}

class FormGenerator final {
public:
    FormGenerator(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                  const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                  const FormSchema& schema, const FormModel& model, const FormGeneratorOptions& options,
                  theme_language::ThemeDiagnosticList& diagnostics) noexcept;

    // Checks the model, then writes the header into `scratch`; returns
    // whether nothing was refused (`scratch` is then complete).
    [[nodiscard]] bool run(std::string& scratch);

private:
    void check_objects();
    void check_values();
    void write(std::string& out) const;
    void write_includes(std::string& out) const;
    void write_initialize(std::string& out) const;
    void write_window(std::string& out) const;
    // The member that holds the object at `index`.
    [[nodiscard]] const std::string& member(std::size_t index) const { return members_[index]; }
    [[nodiscard]] const FormSchemaType& type_of(std::size_t index) const {
        return schema_.types[model_.objects[index].schema_type];
    }
    void report(NodeIndex node, const std::string& rule, const std::string& property, std::string detail);

    const theme_language::ThemeSource& source_;
    const theme_language::ThemeTokenBuffer& tokens_;
    const theme_language::ThemeSyntaxTree& tree_;
    const FormSchema& schema_;
    const FormModel& model_;
    const FormGeneratorOptions& options_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    // Whether the root is the reserved form type.
    bool form_root_{};
    // The member name of each object, at its FormModel::objects index
    // (empty for a form root).
    std::vector<std::string> members_{};
    // Each property's value as C++, at its FormModel::properties index.
    std::vector<std::string> values_{};
    // The index of the root control's object.
    std::size_t root_object_{};
    bool refused_{};
};

FormGenerator::FormGenerator(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                             const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                             const FormSchema& schema, const FormModel& model, const FormGeneratorOptions& options,
                             theme_language::ThemeDiagnosticList& diagnostics) noexcept
    : source_(source), tokens_(tokens), tree_(tree), schema_(schema), model_(model), options_(options),
      diagnostics_(diagnostics), cursor_(lines, source.text) {}

bool FormGenerator::run(std::string& scratch) {
    form_root_ = type_of(0U).name == form_root_type_name;
    check_objects();
    check_values();
    if (refused_) return false;
    write(scratch);
    return true;
}

void FormGenerator::check_objects() {
    members_.assign(model_.objects.size(), std::string{});
    // The member spelling each object took, to find two IDs that spell one
    // name (settings.ok and settings-ok).
    std::unordered_map<std::string, std::size_t> taken{};
    taken.reserve(model_.objects.size());
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        const FormObject& object = model_.objects[index];
        if (index == 0U && form_root_) continue;
        const FormSchemaType& type = type_of(index);
        if (!form_qualified_name(type.cpp_type) || type.header.empty()) {
            report(object.node, object.stable_id, {}, type.name + " has no C++ spelling for the generator");
        }
        std::string name = form_member_name(object.stable_id);
        if (name.empty()) {
            report(object.node, object.stable_id, {},
                   "the stable ID does not spell a C++ member (it starts with a digit or is a keyword)");
            continue;
        }
        const std::pair<std::unordered_map<std::string, std::size_t>::iterator, bool> inserted =
            taken.emplace(name, index);
        if (!inserted.second) {
            const std::string& other = model_.objects[(*inserted.first).second].stable_id;
            report(object.node, object.stable_id, {}, "the member " + name + " is also " + other + "'s");
            continue;
        }
        members_[index] = std::move(name);
    }
    if (form_root_) {
        // The model admits exactly one control among the form's children.
        for (const ObjectIndex child : model_.objects[0].children) {
            if (type_of(child.value).holds_children) root_object_ = child.value;
        }
    } else {
        root_object_ = 0U;
        if (!type_of(0U).holds_children) {
            report(model_.objects[0].node, model_.objects[0].stable_id, {}, "the root is not a control");
        }
    }
    for (const FormHandler& handler : model_.handlers) {
        if (!form_identifier(handler.handler)) {
            report(handler.node, model_.objects[handler.object.value].stable_id, handler.interaction,
                   "the handler " + handler.handler + " is a reserved spelling");
        }
    }
}

void FormGenerator::check_values() {
    values_.assign(model_.properties.size(), std::string{});
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const FormProperty& property = model_.properties[index];
        // A form root's properties are the window's, written apart.
        if (property.object.value == 0U && form_root_) continue;
        const FormObject& object = model_.objects[property.object.value];
        const PropertyDescriptor& descriptor = form_property_descriptor(schema_, model_, property);
        std::optional<std::string> literal = value_literal(property.value);
        if (!literal) {
            report(property.node, object.stable_id, descriptor.name, "the value's kind has no C++ spelling");
            continue;
        }
        values_[index] = std::move(*literal);
    }
}

void FormGenerator::write(std::string& out) const {
    out += "// Generated by the GUI.Forms form compiler from " + options_.source_name + ".\n";
    out += "// Do not edit: change the form document and generate it again.\n\n";
    out += "#pragma once\n\n";
    write_includes(out);
    if (!options_.namespace_name.empty()) out += "namespace " + options_.namespace_name + " {\n\n";

    const std::string& name = options_.class_name;
    out += "// The objects " + options_.source_name + " describes, for an owner of type Owner whose\n";
    out += "// member functions are the document's handlers. The form holds the handlers'\n";
    out += "// connections, so Owner must outlive them.\n";
    out += "template <typename Owner>\n";
    out += "class " + name + " final {\n";
    out += "public:\n";
    out += "    // The document's objects, in document order.\n";
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        if (index == 0U && form_root_) continue;
        out += "    std::shared_ptr<" + type_of(index).cpp_type + "> " + member(index) + "{};\n";
    }
    out += '\n';
    if (form_root_) write_window(out);
    write_initialize(out);
    out += "    // The root control: the one a window shows.\n";
    out += "    [[nodiscard]] gui_forms::Control::Ptr root_control() const {\n";
    out += "        const gui_forms::Control::Ptr root = " + member(root_object_) + ";\n";
    out += "        return root;\n";
    out += "    }\n\n";
    out += "private:\n";
    out += "    // The delegate type of an event accessor's result, Event<Arguments...>&.\n";
    out += "    template <typename EventReference>\n";
    out += "    struct HandlerDelegate;\n\n";
    out += "    template <typename... Arguments>\n";
    out += "    struct HandlerDelegate<gui_forms::Event<Arguments...>&> final {\n";
    out += "        using Type = gui_forms::Delegate<Arguments...>;\n";
    out += "    };\n\n";
    out += "    // The handlers' connections, in document order.\n";
    out += "    std::vector<gui_forms::SubscriptionToken> connections_{};\n";
    out += "};\n";
    if (!options_.namespace_name.empty()) out += "\n} // namespace " + options_.namespace_name + "\n";
}

void FormGenerator::write_includes(std::string& out) const {
    std::set<std::string> headers{};
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        if (index == 0U && form_root_) continue;
        headers.insert(type_of(index).header);
    }
    headers.insert("gui_forms/binding/value/binding_value.hpp");
    headers.insert("gui_forms/control.hpp");
    headers.insert("gui_forms/delegate.hpp");
    headers.insert("gui_forms/event.hpp");
    if (form_root_) headers.insert("gui_forms/window.hpp");
    if (!model_.spans.empty()) headers.insert("gui_forms/layout/grid_span/grid_span.hpp");
    for (const std::string& header : headers) out += "#include \"" + header + "\"\n";
    out += "\n#include <cstdint>\n#include <limits>\n#include <memory>\n#include <optional>\n";
    if (!model_.spans.empty()) out += "#include <stdexcept>\n";
    out += "#include <string>\n#include <string_view>\n#include <utility>\n#include <vector>\n\n";
}

void FormGenerator::write_window(std::string& out) const {
    std::string title{};
    std::optional<Size> client_size{};
    std::string accept{};
    std::string cancel{};
    for (const FormProperty& property : model_.properties) {
        if (property.object.value != 0U) continue;
        const std::string& property_name = type_of(0U).properties[property.schema_property].name;
        if (property_name == "text") {
            title = std::get<std::string>(property.value);
        } else if (property_name == "client_size") {
            client_size = std::get<Size>(property.value);
        } else if (property_name == "accept_button") {
            accept = std::get<std::string>(property.value);
        } else {
            // The reserved type's last property: cancel_button.
            cancel = std::get<std::string>(property.value);
        }
    }
    out += "    // The window the form describes.\n";
    out += "    static constexpr std::string_view window_title = " + string_literal(title) + ";\n";
    out += "    static constexpr std::optional<gui_forms::Size> window_client_size";
    if (client_size) {
        const BindingValue size_value{*client_size};
        out += " = " + *value_literal(size_value) + ";\n\n";
    } else {
        out += "{};\n\n";
    }
    out += "    // Gives `window` the form's accept and cancel buttons, those the document names.\n";
    out += "    void attach_buttons(gui_forms::Window& window) const {\n";
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        const std::string& stable_id = model_.objects[index].stable_id;
        if (stable_id == accept) out += "        window.set_accept_button(" + member(index) + ");\n";
        if (stable_id == cancel) out += "        window.set_cancel_button(" + member(index) + ");\n";
    }
    if (accept.empty() && cancel.empty()) out += "        static_cast<void>(window);\n";
    out += "    }\n\n";
}

void FormGenerator::write_initialize(std::string& out) const {
    const std::string& name = options_.class_name;
    out += "    // Makes every object in document order, adding each control to its parent as\n";
    out += "    // it is made, then sets each property and connects each handler in document\n";
    out += "    // order. Throws what the objects throw, leaving this form as it was.\n";
    out += "    void initialize_component(Owner& owner) {\n";
    out += "        " + name + " made{};\n";
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        if (index == 0U && form_root_) continue;
        const FormObject& object = model_.objects[index];
        const FormSchemaType& type = type_of(index);
        const std::string& field = member(index);
        if (type.holds_children) {
            out += "        made." + field + " = gui_forms::make_control<" + type.cpp_type +
                   ">(gui_forms::StableId(" + string_literal(object.stable_id) + "));\n";
        } else {
            out += "        made." + field + " = std::make_shared<" + type.cpp_type + ">();\n";
        }
        if (!object.parent) continue;
        const std::size_t parent = (*object.parent).value;
        // A component is the form's, not its parent's; a form root's
        // control is the window's.
        if (!type.holds_children || (parent == 0U && form_root_)) continue;
        out += "        (*made." + member(parent) + ").add_child(made." + field + ");\n";
    }
    // Spans before any placement (study 10.9).
    for (const FormSpan& span : model_.spans) {
        const GridSpan& value = span.span;
        out += "        if ((*made." + member(span.owner.value) + ").define_span(gui_forms::GridSpan{" +
               string_literal(value.name) + ", gui_forms::GridCell{" + std::to_string(value.anchor.row) + "U, " +
               std::to_string(value.anchor.column) + "U}, " + std::to_string(value.rows) + "U, " +
               std::to_string(value.columns) + "U}) != gui_forms::GridSpanOutcome::applied) {\n";
        out += "            throw std::invalid_argument(\"the form's span is refused\");\n";
        out += "        }\n";
    }
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const FormProperty& property = model_.properties[index];
        if (property.object.value == 0U && form_root_) continue;
        const std::string& property_name = form_property_descriptor(schema_, model_, property).name;
        const std::string& field = member(property.object.value);
        if (property.provider == FormPropertyProvider::parent_type) {
            // Name-keyed until the child-property capabilities exist (study
            // section 10.8); they replace this call in the M-G step.
            const std::size_t parent = (*model_.objects[property.object.value].parent).value;
            out += "        (*made." + member(parent) + ").set_child_property_value(*made." + field + ", \"" +
                   property_name + "\", gui_forms::BindingValue{" + values_[index] + "});\n";
            continue;
        }
        out += "        (*made." + field + ").set_property_value(\"" + property_name +
               "\", gui_forms::BindingValue{" + values_[index] + "});\n";
    }
    for (const FormHandler& handler : model_.handlers) {
        const std::string& field = member(handler.object.value);
        out += "        {\n";
        out += "            using Handler = typename HandlerDelegate<decltype((*made." + field + ")." +
               handler.interaction + "())>::Type;\n";
        out += "            made.connections_.push_back((*made." + field + ")." + handler.interaction +
               "().subscribe(Handler::template bind<Owner, &Owner::" + handler.handler + ">(owner)));\n";
        out += "        }\n";
    }
    if (model_.handlers.empty()) out += "        static_cast<void>(owner);\n";
    out += "        *this = std::move(made);\n";
    out += "    }\n\n";
}

void FormGenerator::report(NodeIndex node, const std::string& rule, const std::string& property,
                           std::string detail) {
    const std::uint32_t token = tree_.nodes[node.value].tokens.first.value;
    const std::uint32_t offset = tokens_.tokens[token].offset.value;
    ThemeDiagnostic diagnostic{};
    diagnostic.code = DiagnosticCode::form_generation_refused;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(ByteOffset{offset});
    diagnostic.rule = rule;
    diagnostic.property = property;
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    refused_ = true;
}

} // namespace

bool form_qualified_name(std::string_view name) {
    std::size_t start = 0U;
    while (true) {
        const std::size_t separator = name.find("::", start);
        const std::string_view part = name.substr(start, separator - start);
        if (!form_identifier(part)) return false;
        if (separator == std::string_view::npos) return true;
        start = separator + 2U;
    }
}

bool form_identifier(std::string_view name) noexcept {
    if (name.empty() || !identifier_start(name.front()) || name.front() == '_') return false;
    for (const char character : name) {
        if (!identifier_part(character)) return false;
    }
    const bool keyword = is_keyword(name);
    return !keyword;
}

std::string form_member_name(std::string_view stable_id) {
    std::string name(stable_id);
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), '-', '_');
    if (!form_identifier(name)) return {};
    return name;
}

FormGenerateOutcome generate_form_cpp(const theme_language::ThemeSource& source,
                                      const theme_language::ThemeTokenBuffer& tokens,
                                      const theme_language::LineStartTable& lines,
                                      const theme_language::ThemeSyntaxTree& tree, const FormSchema& schema,
                                      const FormModel& model, const FormGeneratorOptions& options,
                                      std::string& generated, theme_language::ThemeDiagnosticList& diagnostics) {
    if (!form_identifier(options.class_name)) {
        throw std::invalid_argument("generate_form_cpp: the class name is not a C++ identifier");
    }
    if (!options.namespace_name.empty() && !form_qualified_name(options.namespace_name)) {
        throw std::invalid_argument("generate_form_cpp: the namespace is not ::-separated C++ identifiers");
    }
    if (model.objects.empty()) {
        throw std::invalid_argument("generate_form_cpp: the model has no objects");
    }
    std::string scratch{};
    FormGenerator generator(source, tokens, lines, tree, schema, model, options, diagnostics);
    if (!generator.run(scratch)) return FormGenerateOutcome::refused;
    generated = std::move(scratch);
    return FormGenerateOutcome::generated;
}

} // namespace gui_forms::form_language
