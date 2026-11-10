// The form schema of every registry type (stage FD, phase FD5).

#include "form_catalog.hpp"

#include "form_compiler_catalog.hpp"

#include "../../src/core/form_language/form_generator/form_generator.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace gui_forms::form_compiler {

form_language::FormSchema registry_form_schema() {
    form_language::FormSchema schema{};
    schema.types.reserve(form_catalog.size() + 1U);
    for (const FormCatalogEntry& entry : form_catalog) {
        (*entry.add)(entry, schema);
    }
    schema.types.push_back(form_language::form_root_type());
    return schema;
}

namespace {

// Whether `name` is a registered type name: lower-case words joined by '_'.
bool registered_spelling(std::string_view name) {
    if (name.empty() || name.back() == '_') return false;
    const bool leading_letter = name.front() >= 'a' && name.front() <= 'z';
    if (!leading_letter) return false;
    for (const char character : name) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !digit && character != '_') return false;
    }
    return true;
}

} // namespace

void add_application_type(form_language::FormSchema& schema, std::string_view description) {
    // The four comma-separated fields.
    std::array<std::string_view, 4> fields{};
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < fields.size(); ++index) {
        const std::size_t comma = description.find(',', start);
        const bool last = index + 1U == fields.size();
        if (last != (comma == std::string_view::npos)) {
            throw std::invalid_argument("application type: expected <name>,<C++ class>,<base type>,<header>");
        }
        fields[index] = description.substr(start, comma - start);
        start = comma + 1U;
    }
    const std::string_view name = fields[0];
    const std::string_view cpp_type = fields[1];
    const std::string_view base = fields[2];
    const std::string_view header = fields[3];
    if (!registered_spelling(name)) {
        throw std::invalid_argument("application type: the name must be lower-case words joined by '_'");
    }
    if (!form_language::form_qualified_name(cpp_type)) {
        throw std::invalid_argument("application type: the C++ class must be ::-separated identifiers");
    }
    if (header.empty()) throw std::invalid_argument("application type: the header is empty");
    const form_language::FormSchemaType* base_type = nullptr;
    for (const form_language::FormSchemaType& type : schema.types) {
        if (type.name == name) throw std::invalid_argument("application type: the schema already has the name");
        if (type.name == base) base_type = &type;
    }
    if (base_type == nullptr || base == form_language::form_root_type_name) {
        throw std::invalid_argument("application type: the base type is not in the schema");
    }
    form_language::FormSchemaType type = *base_type;
    type.name = std::string(name);
    type.cpp_type = std::string(cpp_type);
    type.header = std::string(header);
    type.create = nullptr;
    // The application's class derives from the base, so it is not the
    // base's final class; whether it is final itself the schema cannot
    // know, and the generator derives from it.
    type.final_type = false;
    schema.types.push_back(std::move(type));
}

} // namespace gui_forms::form_compiler
