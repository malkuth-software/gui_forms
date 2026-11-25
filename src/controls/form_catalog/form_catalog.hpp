#pragma once

// The form schema of every registry type: what the form compiler checks and
// generates documents against, and what GUI.Forms' public form documents
// (gui_forms/forms/form_documents) read. The registry's types come from the
// generated catalog (form_registry_catalog.hpp, `python3
// tools/control_registry.py emit-forms`), each described from a fresh
// prototype with its C++ class and header. Internal; part of the controls
// library, which holds every registry type.
//
// A type joins the schema when the loader and the generated code can make
// it alone: a control from its stable ID, or a component from nothing.
// Types that need another object (a dock site's workspace, a command's
// owner) are left out until a document can name that object. A type made
// only from a definition (CompositeControl) stays in the catalog but never
// joins the schema by its id: each composite joins as its own type, from
// its .control definition (the form compiler's add_composite_type).

#include "../../core/form_language/form_model/form_model.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace gui_forms::form_catalog {

struct FormCatalogEntry;

// Adds one catalog entry's type to a schema, or nothing when the type
// cannot be made alone.
using FormCatalogAdd = void (*)(const FormCatalogEntry& entry, form_language::FormSchema& schema);

// How a registry type is made (manifests/controls.json, `construction`):
// from a stable id alone (a component: from nothing), or only from a
// definition.
enum class CatalogConstruction : std::uint8_t {
    stable_id,
    definition,
};

struct FormCatalogEntry final {
    // The registered name, the C++ class in namespace gui_forms and its
    // public header: static storage, the generated catalog's literals.
    std::string_view id{};
    std::string_view cpp_type{};
    std::string_view header{};
    FormCatalogAdd add{};
    CatalogConstruction construction{CatalogConstruction::stable_id};
};

template <typename Type>
inline constexpr bool form_control_alone = std::is_base_of_v<Control, Type> && std::is_constructible_v<Type, StableId>;

template <typename Type>
inline constexpr bool form_component_alone = std::is_base_of_v<Component, Type> &&
                                             !std::is_base_of_v<Control, Type> &&
                                             std::is_default_constructible_v<Type>;

// A TabControl's document children: a TabPage becomes a page (add_page),
// anything else is left to add_child. The loader's FormChildAdopter.
[[nodiscard]] bool adopt_tab_page(Control& owner, const Control::Ptr& child);

template <typename Type>
void add_catalog_type(const FormCatalogEntry& entry, form_language::FormSchema& schema) {
    if constexpr (form_control_alone<Type>) {
        form_language::FormSchemaType type = form_language::form_control_type<Type>(std::string(entry.id));
        type.cpp_type = "gui_forms::" + std::string(entry.cpp_type);
        type.header = std::string(entry.header);
        if (entry.id == "tab_control") {
            type.adopt_child = &adopt_tab_page;
            type.adopted_type = "gui_forms::TabPage";
            type.adopt_method = "add_page";
        }
        schema.types.push_back(std::move(type));
    } else if constexpr (form_component_alone<Type>) {
        form_language::FormSchemaType type = form_language::form_component_type<Type>(std::string(entry.id));
        type.cpp_type = "gui_forms::" + std::string(entry.cpp_type);
        type.header = std::string(entry.header);
        schema.types.push_back(std::move(type));
    } else {
        static_cast<void>(entry);
        static_cast<void>(schema);
    }
}

// Appends every registry type that can be made alone, in registry order.
// Makes and releases one prototype of each type. UI thread.
void add_registry_types(form_language::FormSchema& schema);

// The schema of every registry type that can be made alone, in registry
// order, then the reserved form root. UI thread.
[[nodiscard]] form_language::FormSchema registry_form_schema();

// The process's registry schema: registry_form_schema, made on the first
// call (UI thread) and never destroyed. Read by the public form documents
// and the forms.* model tools.
[[nodiscard]] const form_language::FormSchema& process_registry_schema();

} // namespace gui_forms::form_catalog
