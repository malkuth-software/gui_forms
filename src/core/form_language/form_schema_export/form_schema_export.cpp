// A form schema as a designer reads it (cp34). See form_schema_export.hpp.

#include "form_schema_export.hpp"

#include "../../control_language/composite_definition/composite_definition.hpp"
#include "../../control_language/control_model/control_model.hpp"

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::form_language {

namespace {

void append_string(std::string& out, const std::string_view text) {
    out += '"';
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            out += '\\';
            out += character;
        } else if (byte < 0x20U) {
            char escaped[8] = {};
            static_cast<void>(std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(byte)));
            out += escaped;
        } else {
            out += character;
        }
    }
    out += '"';
}

void append_bool(std::string& out, const bool value) {
    out += value ? "true" : "false";
}

std::string_view value_kind_name(const BindingValueKind kind) {
    switch (kind) {
    case BindingValueKind::null: return "null";
    case BindingValueKind::boolean: return "boolean";
    case BindingValueKind::signed_integer: return "signed_integer";
    case BindingValueKind::unsigned_integer: return "unsigned_integer";
    case BindingValueKind::number: return "number";
    case BindingValueKind::text: return "text";
    case BindingValueKind::point: return "point";
    case BindingValueKind::size: return "size";
    case BindingValueKind::rectangle: return "rectangle";
    case BindingValueKind::insets: return "insets";
    case BindingValueKind::color: return "color";
    case BindingValueKind::font: return "font";
    case BindingValueKind::image: return "image";
    case BindingValueKind::enumeration: return "enumeration";
    case BindingValueKind::object: return "object";
    case BindingValueKind::collection: return "collection";
    default: return "unknown";
    }
}

std::string_view type_kind(const FormSchemaType& type) {
    if (type.name == form_root_type_name) return "form";
    if (type.composite != nullptr) return "composite";
    if (type.holds_children) return "control";
    return "component";
}

std::string_view children_word(const FormSchemaType& type) {
    if (!type.holds_children || type.name == form_root_type_name) return "none";
    switch (type.child_placement) {
    case FormChildPlacement::self: return "self";
    case FormChildPlacement::slot: return "slot";
    case FormChildPlacement::refused: return "refused";
    default: return "none";
    }
}

// `kebab` from a registered (snake_case) name.
std::string element_name(const std::string_view registered) {
    std::string made(registered);
    for (char& character : made) {
        if (character == '_') character = '-';
    }
    return made;
}

void append_properties(std::string& out, const std::span<const PropertyDescriptor> properties) {
    out += '[';
    for (std::size_t index = 0U; index < properties.size(); ++index) {
        const PropertyDescriptor& property = properties[index];
        out += index == 0U ? "\n        " : ",\n        ";
        out += "{\"name\": ";
        append_string(out, property.name);
        out += ", \"kind\": ";
        append_string(out, value_kind_name(property.kind));
        out += ", \"category\": ";
        append_string(out, property.category);
        out += ", \"writable\": ";
        append_bool(out, property.writable);
        out += ", \"bindable\": ";
        append_bool(out, property.bindable);
        out += ", \"choices\": [";
        if (property.enumeration != nullptr) {
            const std::vector<PropertyEnumChoice>& choices = (*property.enumeration).choices;
            for (std::size_t choice = 0U; choice < choices.size(); ++choice) {
                if (choice > 0U) out += ", ";
                append_string(out, choices[choice].name);
            }
        }
        out += "]}";
    }
    out += properties.empty() ? "]" : "\n      ]";
}

void append_composite(std::string& out, const CompositeDefinition& definition) {
    out += "{\"name\": ";
    append_string(out, definition.name);
    out += ", \"version\": ";
    append_string(out, std::to_string(definition.version_major) + "." + std::to_string(definition.version_minor));
    out += ", \"slot\": ";
    if (definition.slot.empty()) {
        out += "null";
    } else {
        append_string(out, definition.slot);
    }
    out += ", \"events\": [";
    for (std::size_t index = 0U; index < definition.events.size(); ++index) {
        const control_language::CompositeEventDefinition& event = definition.events[index];
        if (index > 0U) out += ", ";
        out += "{\"name\": ";
        append_string(out, event.name);
        out += ", \"payload\": ";
        if (event.payload) {
            append_string(out, control_language::control_property_kind_name(*event.payload));
        } else {
            out += "null";
        }
        out += '}';
    }
    out += "], \"commands\": [";
    for (std::size_t index = 0U; index < definition.commands.size(); ++index) {
        if (index > 0U) out += ", ";
        append_string(out, definition.commands[index].name);
    }
    out += "]}";
}

void append_type(std::string& out, const FormSchemaType& type) {
    out += "    {\n      \"name\": ";
    append_string(out, type.name);
    out += ",\n      \"element\": ";
    append_string(out, element_name(type.name));
    out += ",\n      \"kind\": ";
    append_string(out, type_kind(type));
    out += ",\n      \"cpp_type\": ";
    append_string(out, type.cpp_type);
    out += ",\n      \"header\": ";
    append_string(out, type.header);
    out += ",\n      \"children\": ";
    append_string(out, children_word(type));
    out += ",\n      \"properties\": ";
    append_properties(out, type.properties);
    out += ",\n      \"child_properties\": ";
    append_properties(out, type.child_properties);
    out += ",\n      \"interactions\": [";
    for (std::size_t index = 0U; index < type.interactions.size(); ++index) {
        if (index > 0U) out += ", ";
        append_string(out, type.interactions[index]);
    }
    out += ']';
    if (type.composite != nullptr) {
        out += ",\n      \"composite\": ";
        append_composite(out, *type.composite);
    }
    out += "\n    }";
}

} // namespace

std::string form_schema_json(const FormSchema& schema) {
    std::string out = "{\n  \"schema\": 1,\n  \"types\": [";
    for (std::size_t index = 0U; index < schema.types.size(); ++index) {
        out += index == 0U ? "\n" : ",\n";
        append_type(out, schema.types[index]);
    }
    out += schema.types.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

} // namespace gui_forms::form_language
