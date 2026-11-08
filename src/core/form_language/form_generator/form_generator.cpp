// A form document's C++ generator (stage FD, phase FD5). Two passes over
// the model: the first checks that every object, value and binding can be
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
// stable ID or binding source spelled as one of them cannot be declared.
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
    // How the generated code reaches an object: through the instance being
    // built (the document class's `made`, a derived window's Objects), or
    // as the derived class's own member (the root of a derived control
    // being the class itself).
    enum class ObjectAccess : std::uint8_t { through_made, as_members };

    void check_objects();
    void check_values();
    void write(std::string& out) const;
    void write_includes(std::string& out) const;
    void write_initialize(std::string& out) const;
    void write_window(std::string& out) const;
    // The derived class (stage B): a Window subclass for a form root, a
    // subclass of the root's type otherwise.
    void write_derived_window(std::string& out) const;
    void write_derived_control(std::string& out) const;
    // The window's title and client size, as static constants.
    void write_window_constants(std::string& out) const;
    // Makes every object in document order, adding each control to its
    // parent, then defines the spans.
    void write_objects(std::string& out, ObjectAccess access) const;
    // Sets or binds each property in document order, reading the sources
    // through `sources` (`owner.`, `model.`, `model_.`).
    void write_values(std::string& out, ObjectAccess access, const std::string& sources) const;
    // One bound property's binding call, at its place among the properties.
    void write_binding(std::string& out, const FormProperty& property, const std::string& property_name,
                       ObjectAccess access, const std::string& sources) const;
    // The object at `index` as an lvalue expression (`(*made.f)`), as a call's
    // argument (`*made.f`), and as its pointer.
    [[nodiscard]] std::string object_reference(std::size_t index, ObjectAccess access) const;
    [[nodiscard]] std::string object_argument(std::size_t index, ObjectAccess access) const;
    [[nodiscard]] std::string object_pointer(std::size_t index, ObjectAccess access) const;
    // Whether any property is bound.
    [[nodiscard]] bool binds() const noexcept;
    // The accept and cancel buttons' stable IDs a form root names.
    [[nodiscard]] std::string window_button(std::string_view property_name) const;
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
    if (options_.derived && options_.model_type.empty() && binds()) {
        report(model_.objects[0].node, model_.objects[0].stable_id, {},
               "the form binds: a derived class needs its model type (gui_forms_add_form MODEL)");
    }
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
}

void FormGenerator::check_values() {
    values_.assign(model_.properties.size(), std::string{});
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const FormProperty& property = model_.properties[index];
        // A form root's properties are the window's, written apart.
        if (property.object.value == 0U && form_root_) continue;
        const FormObject& object = model_.objects[property.object.value];
        const PropertyDescriptor& descriptor = form_property_descriptor(schema_, model_, property);
        if (property.binding) {
            const std::string& bound_source = (*property.binding).source;
            if (!form_identifier(bound_source)) {
                report(property.node, object.stable_id, descriptor.name,
                       "the binding's source " + bound_source + " is a reserved spelling");
            }
            continue;
        }
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
    if (options_.derived) {
        if (form_root_) {
            write_derived_window(out);
        } else {
            write_derived_control(out);
        }
        if (!options_.namespace_name.empty()) out += "\n} // namespace " + options_.namespace_name + "\n";
        return;
    }

    const std::string& name = options_.class_name;
    out += "// The objects " + options_.source_name + " describes, for an owner of type Owner whose\n";
    out += "// members are the sources the document binds; each bound control keeps its binding,\n";
    out += "// so those sources must outlive the form's controls.\n";
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
    out += "};\n";
    if (!options_.namespace_name.empty()) out += "\n} // namespace " + options_.namespace_name + "\n";
}

void FormGenerator::write_includes(std::string& out) const {
    std::set<std::string> headers{};
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        if (index == 0U && form_root_) continue;
        headers.insert(type_of(index).header);
    }
    if (options_.derived && form_root_) headers.insert("gui_forms/application/application.hpp");
    if (!options_.model_header.empty()) headers.insert(options_.model_header);
    headers.insert("gui_forms/binding/value/binding_value.hpp");
    headers.insert("gui_forms/control.hpp");
    for (const FormProperty& property : model_.properties) {
        if (property.binding) {
            headers.insert("gui_forms/binding/form_binding/form_binding.hpp");
            continue;
        }
        if (property.object.value == 0U && form_root_) continue;
        if (form_property_descriptor(schema_, model_, property).assignable_member) {
            headers.insert("gui_forms/binding/property_cell_entry/property_cell_entry.hpp");
        }
    }
    if (form_root_) headers.insert("gui_forms/window.hpp");
    if (!model_.spans.empty()) headers.insert("gui_forms/layout/grid_span/grid_span.hpp");
    for (const std::string& header : headers) out += "#include \"" + header + "\"\n";
    out += "\n#include <cstdint>\n#include <limits>\n#include <memory>\n#include <optional>\n";
    if (!model_.spans.empty()) out += "#include <stdexcept>\n";
    out += "#include <string>\n#include <string_view>\n#include <utility>\n#include <vector>\n\n";
}

std::string FormGenerator::window_button(std::string_view property_name) const {
    for (const FormProperty& property : model_.properties) {
        if (property.object.value != 0U) continue;
        if (type_of(0U).properties[property.schema_property].name != property_name) continue;
        const std::string button = std::get<std::string>(property.value);
        return button;
    }
    return {};
}

void FormGenerator::write_window_constants(std::string& out) const {
    std::string title{};
    std::optional<Size> client_size{};
    for (const FormProperty& property : model_.properties) {
        if (property.object.value != 0U) continue;
        const std::string& property_name = type_of(0U).properties[property.schema_property].name;
        if (property_name == "text") {
            title = std::get<std::string>(property.value);
        } else if (property_name == "client_size") {
            client_size = std::get<Size>(property.value);
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
}

void FormGenerator::write_window(std::string& out) const {
    const std::string accept = window_button("accept_button");
    const std::string cancel = window_button("cancel_button");
    write_window_constants(out);
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
    out += "    // it is made, then sets or binds each property in document order, a bound one\n";
    out += "    // taking its source's value and subscribing in one step. Throws what the\n";
    out += "    // objects and bindings throw, leaving this form as it was.\n";
    out += "    void initialize_component(Owner& owner) {\n";
    out += "        " + name + " made{};\n";
    write_objects(out, ObjectAccess::through_made);
    write_values(out, ObjectAccess::through_made, "owner.");
    if (!binds()) out += "        static_cast<void>(owner);\n";
    out += "        *this = std::move(made);\n";
    out += "    }\n\n";
}

std::string FormGenerator::object_reference(std::size_t index, ObjectAccess access) const {
    // A derived control is its document's root.
    if (index == 0U && options_.derived && !form_root_) return "(*this)";
    const std::string reference = "(*" + object_pointer(index, access) + ")";
    return reference;
}

std::string FormGenerator::object_argument(std::size_t index, ObjectAccess access) const {
    if (index == 0U && options_.derived && !form_root_) return "*this";
    const std::string argument = "*" + object_pointer(index, access);
    return argument;
}

std::string FormGenerator::object_pointer(std::size_t index, ObjectAccess access) const {
    if (access == ObjectAccess::through_made) {
        const std::string pointer = "made." + member(index);
        return pointer;
    }
    return member(index);
}

bool FormGenerator::binds() const noexcept {
    for (const FormProperty& property : model_.properties) {
        if (property.binding) return true;
    }
    return false;
}

void FormGenerator::write_objects(std::string& out, ObjectAccess access) const {
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        // A form root is the window; a derived control's root is the class.
        if (index == 0U && (form_root_ || options_.derived)) continue;
        const FormObject& object = model_.objects[index];
        const FormSchemaType& type = type_of(index);
        const std::string pointer = object_pointer(index, access);
        if (type.holds_children) {
            out += "        " + pointer + " = gui_forms::make_control<" + type.cpp_type +
                   ">(gui_forms::StableId(" + string_literal(object.stable_id) + "));\n";
        } else {
            out += "        " + pointer + " = std::make_shared<" + type.cpp_type + ">();\n";
        }
        if (!object.parent) continue;
        const std::size_t parent = (*object.parent).value;
        // A component is the form's, not its parent's; a form root's
        // control is the window's.
        if (!type.holds_children || (parent == 0U && form_root_)) continue;
        out += "        " + object_reference(parent, access) + ".add_child(" + pointer + ");\n";
    }
    // Spans before any placement (study 10.9).
    for (const FormSpan& span : model_.spans) {
        const GridSpan& value = span.span;
        out += "        if (" + object_reference(span.owner.value, access) + ".define_span(gui_forms::GridSpan{" +
               string_literal(value.name) + ", gui_forms::GridCell{" + std::to_string(value.anchor.row) + "U, " +
               std::to_string(value.anchor.column) + "U}, " + std::to_string(value.rows) + "U, " +
               std::to_string(value.columns) + "U}) != gui_forms::GridSpanOutcome::applied) {\n";
        out += "            throw std::invalid_argument(\"the form's span is refused\");\n";
        out += "        }\n";
    }
}

void FormGenerator::write_values(std::string& out, ObjectAccess access, const std::string& sources) const {
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const FormProperty& property = model_.properties[index];
        if (property.object.value == 0U && form_root_) continue;
        const PropertyDescriptor& descriptor = form_property_descriptor(schema_, model_, property);
        const std::string& property_name = descriptor.name;
        const std::string object = object_reference(property.object.value, access);
        const std::string value = "gui_forms::BindingValue{" + values_[index] + "}";
        if (property.provider == FormPropertyProvider::parent_type) {
            // The parent's child view, subscripted by the child, where the
            // property is one (CLOSURE P-6); by name otherwise.
            const std::string argument = object_argument(property.object.value, access);
            const std::size_t parent = (*model_.objects[property.object.value].parent).value;
            const std::string parent_object = object_reference(parent, access);
            if (descriptor.assignable_member) {
                const std::string view = parent_object + "." + property_name;
                out += "        " + view + "[" + argument + "] = gui_forms::assigned_property_value<decltype(" +
                       view + ")>(" + value + ");\n";
                continue;
            }
            out += "        " + parent_object + ".set_child_property_value(" + argument + ", \"" +
                   property_name + "\", " + value + ");\n";
            continue;
        }
        if (property.binding) {
            write_binding(out, property, property_name, access, sources);
            continue;
        }
        // A property on a capability is assigned through its cell or view,
        // so its hooks run as for any write (CLOSURE P-6); one not yet
        // migrated is written by name.
        if (descriptor.assignable_member) {
            const std::string cell = object + "." + property_name;
            out += "        " + cell + " = gui_forms::assigned_property_value<decltype(" + cell + ")>(" + value +
                   ");\n";
            continue;
        }
        out += "        " + object + ".set_property_value(\"" + property_name + "\", " + value + ");\n";
    }
}

void FormGenerator::write_binding(std::string& out, const FormProperty& property, const std::string& property_name,
                                  ObjectAccess access, const std::string& sources) const {
    const FormBinding& binding = *property.binding;
    const std::string object = object_reference(property.object.value, access);
    const std::string argument = object_argument(property.object.value, access);
    const std::string source = sources + binding.source;
    const FormSchemaType& type = type_of(property.object.value);
    const PropertyDescriptor& descriptor = form_property_descriptor(schema_, model_, property);
    const std::string direction = binding.direction == PropertyBindingDirection::two_way
        ? "gui_forms::PropertyBindingDirection::two_way"
        : "gui_forms::PropertyBindingDirection::one_way";
    if (type.bind_command != nullptr && property_name == form_command_slot_name) {
        out += "        " + object + ".bind(" + source + ");\n";
        return;
    }
    if (!binding.field.empty()) {
        const std::string trigger = binding.trigger == FormUpdateTrigger::on_change
            ? "gui_forms::FormUpdateTrigger::on_change"
            : "gui_forms::FormUpdateTrigger::on_validate";
        out += "        gui_forms::bind_form_field(" + argument + ", \"" + property_name + "\", " + source +
               ", std::string(" + string_literal(binding.field) + "), " + direction + ", " + trigger + ");\n";
        return;
    }
    // The model admits a Value source for these three kinds only.
    std::string kind = "gui_forms::BindingValueKind::number";
    if (descriptor.kind == BindingValueKind::boolean) kind = "gui_forms::BindingValueKind::boolean";
    if (descriptor.kind == BindingValueKind::signed_integer) kind = "gui_forms::BindingValueKind::signed_integer";
    out += "        gui_forms::bind_form_value<" + kind + ">(" + argument + ", \"" + property_name + "\", " + source +
           ", " + direction + ");\n";
}

void FormGenerator::write_derived_window(std::string& out) const {
    const std::string& name = options_.class_name;
    const bool bound = binds();
    const std::string& model = options_.model_type;
    // The model parameter, with a leading ", " where it follows another.
    const std::string model_parameter = bound ? model + "& model" : std::string{};
    const std::string model_after = bound ? ", " + model_parameter : std::string{};
    const std::string model_argument = bound ? ", model" : std::string{};
    const std::string& root = member(root_object_);
    std::string client_size = "gui_forms::Size{}";
    for (const FormProperty& property : model_.properties) {
        if (property.object.value != 0U) continue;
        if (type_of(0U).properties[property.schema_property].name != "client_size") continue;
        client_size = *value_literal(property.value);
    }
    out += "// The window " + options_.source_name + " describes (stage B): its objects are members,\n";
    out += "// made in document order before the window, then each property is set or bound in\n";
    out += "// document order";
    if (bound) {
        out += ", reading the model's members as sources. Each bound control keeps its\n";
        out += "// binding, so the model must outlive the window: declare it first";
    }
    out += ". A throw leaves no window.\n";
    out += "class " + name + " final : public gui_forms::Window {\n";
    out += "public:\n";
    out += "    // The document's objects, in document order.\n";
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        out += "    const std::shared_ptr<" + type_of(index).cpp_type + "> " + member(index) + ";\n";
    }
    out += '\n';
    write_window_constants(out);
    out += "    // A window of the current application (a test window without one).\n";
    out += "    " + std::string(bound ? "explicit " : "") + name + "(" + model_parameter + ") : " + name +
           "(make_objects()" + model_argument + ") {}\n";
    out += "    // A window of `application`.\n";
    out += "    " + std::string(bound ? "" : "explicit ") + name + "(gui_forms::Application& application" +
           model_after + ") : " + name + "(application, make_objects()" + model_argument + ") {}\n\n";
    out += "private:\n";
    out += "    // The objects, made before the window that shows their root.\n";
    out += "    struct Objects final {\n";
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        out += "        std::shared_ptr<" + type_of(index).cpp_type + "> " + member(index) + "{};\n";
    }
    out += "    };\n\n";
    out += "    [[nodiscard]] static Objects make_objects() {\n";
    out += "        Objects made{};\n";
    write_objects(out, ObjectAccess::through_made);
    out += "        return made;\n";
    out += "    }\n\n";
    std::string members{};
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        members += ", " + member(index) + "(std::move(objects." + member(index) + "))";
    }
    out += "    " + name + "(Objects objects" + model_after + ")\n";
    out += "        : gui_forms::Window(objects." + root + ", " + client_size + ")" + members + " {\n";
    out += "        initialize(" + std::string(bound ? "model" : "") + ");\n";
    out += "    }\n\n";
    out += "    " + name + "(gui_forms::Application& application, Objects objects" + model_after + ")\n";
    out += "        : gui_forms::Window(application, objects." + root + ", " + client_size + ")" + members + " {\n";
    out += "        initialize(" + std::string(bound ? "model" : "") + ");\n";
    out += "    }\n\n";
    out += "    // Sets or binds each property in document order, then gives the window its\n";
    out += "    // accept and cancel buttons.\n";
    out += "    void initialize(" + model_parameter + ") {\n";
    write_values(out, ObjectAccess::as_members, "model.");
    const std::string accept = window_button("accept_button");
    const std::string cancel = window_button("cancel_button");
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        const std::string& stable_id = model_.objects[index].stable_id;
        if (stable_id == accept) out += "        set_accept_button(" + member(index) + ");\n";
        if (stable_id == cancel) out += "        set_cancel_button(" + member(index) + ");\n";
    }
    out += "    }\n";
    out += "};\n";
}

void FormGenerator::write_derived_control(std::string& out) const {
    const std::string& name = options_.class_name;
    const bool bound = binds();
    const std::string& model = options_.model_type;
    const std::string& base = type_of(0U).cpp_type;
    out += "// The control " + options_.source_name + " describes (stage B): a " + base + " that\n";
    out += "// makes the document's other objects, its members, once it has an owner (make_control),\n";
    out += "// then sets or binds each property in document order";
    if (bound) {
        out += ", reading the model's members\n";
        out += "// as sources. Each bound control keeps its binding, so the model must outlive the\n";
        out += "// control: declare it first";
    }
    out += ". A throw leaves no control.\n";
    out += "class " + name + " final : public " + base + " {\n";
    out += "public:\n";
    out += "    static constexpr bool initialize_tree_after_construction = true;\n\n";
    out += "    // The document's objects other than the root, in document order.\n";
    for (std::size_t index = 1U; index < model_.objects.size(); ++index) {
        out += "    std::shared_ptr<" + type_of(index).cpp_type + "> " + member(index) + "{};\n";
    }
    out += '\n';
    if (bound) {
        out += "    " + name + "(gui_forms::StableId stable_id, " + model + "& model)\n";
        out += "        : " + base + "(std::move(stable_id)), model_(model) {}\n\n";
    } else {
        out += "    explicit " + name + "(gui_forms::StableId stable_id) : " + base + "(std::move(stable_id)) {}\n\n";
    }
    out += "    // The control, with the document's stable ID.\n";
    out += "    [[nodiscard]] static std::shared_ptr<" + name + "> make(" + (bound ? model + "& model" : "") + ") {\n";
    out += "        std::shared_ptr<" + name + "> made = gui_forms::make_control<" + name +
           ">(gui_forms::StableId(" + string_literal(model_.objects[0].stable_id) + ")" + (bound ? ", model" : "") +
           ");\n";
    out += "        return made;\n";
    out += "    }\n\n";
    out += "    // make_control's step once the control has an owner: the base's own step\n";
    out += "    // first, when its type has one.\n";
    out += "    void initialize_control_tree() {\n";
    out += "        initialize_base_tree<" + base + ">(*this);\n";
    write_objects(out, ObjectAccess::as_members);
    write_values(out, ObjectAccess::as_members, "model_.");
    out += "    }\n\n";
    out += "private:\n";
    out += "    // A template so that a base without the step names no member it lacks:\n";
    out += "    // the discarded branch is not instantiated.\n";
    out += "    template <typename Base>\n";
    out += "    static void initialize_base_tree(Base& control) {\n";
    out += "        if constexpr (Base::initialize_tree_after_construction) {\n";
    out += "            control.Base::initialize_control_tree();\n";
    out += "        }\n";
    out += "    }\n";
    if (bound) {
        out += "\n";
        out += "    // The bindings' sources; it outlives the control.\n";
        out += "    " + model + "& model_;\n";
    }
    out += "};\n";
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
    if (!options.model_type.empty() && !form_qualified_name(options.model_type)) {
        throw std::invalid_argument("generate_form_cpp: the model type is not ::-separated C++ identifiers");
    }
    if (!options.derived && (!options.model_type.empty() || !options.model_header.empty())) {
        throw std::invalid_argument("generate_form_cpp: a model type or header names a derived class's model");
    }
    if (options.model_header.find_first_of("\"\n") != std::string::npos) {
        throw std::invalid_argument("generate_form_cpp: the model header cannot be spelled in an #include");
    }
    std::string scratch{};
    FormGenerator generator(source, tokens, lines, tree, schema, model, options, diagnostics);
    if (!generator.run(scratch)) return FormGenerateOutcome::refused;
    generated = std::move(scratch);
    return FormGenerateOutcome::generated;
}

} // namespace gui_forms::form_language
