// The form schema of every registry type.

#include "form_catalog.hpp"

#include "form_registry_catalog.hpp"

namespace gui_forms::form_catalog {

void add_registry_types(form_language::FormSchema& schema) {
    schema.types.reserve(schema.types.size() + form_catalog.size() + 1U);
    for (const FormCatalogEntry& entry : form_catalog) {
        // Made only from a definition: never by its id (form_catalog.hpp).
        if (entry.construction == CatalogConstruction::definition) continue;
        (*entry.add)(entry, schema);
    }
}

form_language::FormSchema registry_form_schema() {
    form_language::FormSchema schema{};
    add_registry_types(schema);
    schema.types.push_back(form_language::form_root_type());
    return schema;
}

const form_language::FormSchema& process_registry_schema() {
    static const form_language::FormSchema* const schema = new form_language::FormSchema(registry_form_schema());
    return *schema;
}

} // namespace gui_forms::form_catalog
