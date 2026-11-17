// The form schema of every registry type (stage FD, phase FD5).

#include "form_catalog.hpp"

#include "form_compiler_catalog.hpp"

#include "../../src/controls/composite_control/composite_forms.hpp"
#include "../../src/core/control_language/composite_definition/composite_definition.hpp"
#include "../../src/core/control_language/control_model/control_model.hpp"
#include "../../src/core/form_language/form_generator/form_generator.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms::form_compiler {

form_language::FormSchema registry_form_schema() {
    form_language::FormSchema schema{};
    schema.types.reserve(form_catalog.size() + 1U);
    for (const FormCatalogEntry& entry : form_catalog) {
        // Made only from a definition: never by its id (form_catalog.hpp).
        if (entry.construction == CatalogConstruction::definition) continue;
        (*entry.add)(entry, schema);
    }
    add_extension_types(schema);
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

bool add_composite_type(form_language::FormSchema& schema, std::string_view description,
                        theme_language::ThemeDiagnosticList& diagnostics) {
    // The last two commas separate the class and the header.
    const std::size_t header_comma = description.rfind(',');
    const std::size_t class_comma =
        header_comma == std::string_view::npos || header_comma == 0U ? std::string_view::npos
                                                                      : description.rfind(',', header_comma - 1U);
    if (class_comma == std::string_view::npos) {
        throw std::invalid_argument("composite type: expected <document>,<C++ class>,<header>");
    }
    const std::string path(description.substr(0U, class_comma));
    const std::string_view cpp_type = description.substr(class_comma + 1U, header_comma - class_comma - 1U);
    const std::string_view header = description.substr(header_comma + 1U);
    if (path.empty()) throw std::invalid_argument("composite type: the document is empty");
    if (!form_language::form_qualified_name(cpp_type)) {
        throw std::invalid_argument("composite type: the C++ class must be ::-separated identifiers");
    }
    if (header.empty()) throw std::invalid_argument("composite type: the header is empty");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::invalid_argument("composite type: the document cannot be read: " + path);
    std::ostringstream contents{};
    contents << stream.rdbuf();
    if (stream.bad()) throw std::invalid_argument("composite type: the document cannot be read: " + path);
    const std::string text = contents.str();

    // Compiled against the schema so far, as the control mode compiles it.
    theme_language::ThemeSource source{};
    source.file = path;
    source.text = text;
    control_language::ControlSyntax syntax{};
    control_language::ControlModel model{};
    const control_language::ControlModelOutcome read =
        control_language::read_control_model(source, syntax, schema, model, diagnostics);
    if (read != control_language::ControlModelOutcome::model_built) return false;
    form_language::FormSchema compiled_against = schema;
    const std::shared_ptr<const CompositeDefinition> definition = control_language::compile_composite_definition(
        source, syntax, model, std::move(compiled_against), diagnostics);
    if (definition == nullptr) return false;
    for (const form_language::FormSchemaType& type : schema.types) {
        if (type.name == (*definition).registered) {
            throw std::invalid_argument("composite type: the schema already has the name " + type.name);
        }
    }
    form_language::FormSchemaType type = composite_form_type(definition);
    type.cpp_type = std::string(cpp_type);
    type.header = std::string(header);
    // The generated code makes it by its class, which embeds the document;
    // nothing here loads it. The definition stays for --schema, which
    // lists its slot, events and commands.
    type.create_composite = nullptr;
    schema.types.push_back(std::move(type));
    return true;
}

} // namespace gui_forms::form_compiler
