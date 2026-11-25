// The form schema of every registry type.

#include "form_catalog.hpp"

#include "form_registry_catalog.hpp"

#include "gui_forms/controls/tab_control.hpp"
#include "gui_forms/controls/tab_page.hpp"

#include <memory>

namespace gui_forms::form_catalog {

bool adopt_tab_page(Control& owner, const Control::Ptr& child) {
    TabControl* const tabs = dynamic_cast<TabControl*>(&owner);
    const std::shared_ptr<TabPage> page = std::dynamic_pointer_cast<TabPage>(child);
    if (tabs == nullptr || !page) return false;
    (*tabs).add_page(page);
    return true;
}

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
