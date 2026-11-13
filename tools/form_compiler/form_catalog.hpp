#pragma once

// The form schema of every registry type (stage FD, phase FD5): what the
// form compiler checks and generates documents against. The registry's
// types come from the generated catalog (form_compiler_catalog.hpp,
// `python3 tools/control_registry.py emit-forms`), each described from a
// fresh prototype with its C++ class and header.
//
// A type joins the schema when the loader and the generated code can make
// it alone: a control from its stable ID, or a component from nothing.
// Types that need another object (a dock site's workspace, a command's
// owner) are left out until a document can name that object.

#include "../../src/core/form_language/form_model/form_model.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <string>
#include <string_view>
#include <type_traits>

namespace gui_forms::form_compiler {

struct FormCatalogEntry;

// Adds one catalog entry's type to a schema, or nothing when the type
// cannot be made alone.
using FormCatalogAdd = void (*)(const FormCatalogEntry& entry, form_language::FormSchema& schema);

struct FormCatalogEntry final {
    // The registered name, the C++ class in namespace gui_forms and its
    // public header: static storage, the generated catalog's literals.
    std::string_view id{};
    std::string_view cpp_type{};
    std::string_view header{};
    FormCatalogAdd add{};
};

template <typename Type>
inline constexpr bool form_control_alone = std::is_base_of_v<Control, Type> && std::is_constructible_v<Type, StableId>;

template <typename Type>
inline constexpr bool form_component_alone = std::is_base_of_v<Component, Type> &&
                                             !std::is_base_of_v<Control, Type> &&
                                             std::is_default_constructible_v<Type>;

template <typename Type>
void add_catalog_type(const FormCatalogEntry& entry, form_language::FormSchema& schema) {
    if constexpr (form_control_alone<Type>) {
        form_language::FormSchemaType type = form_language::form_control_type<Type>(std::string(entry.id));
        type.cpp_type = "gui_forms::" + std::string(entry.cpp_type);
        type.header = std::string(entry.header);
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

// The schema of every registry type that can be made alone, in registry
// order, then the extension types (add_extension_types), then the reserved
// form root. Makes and releases one prototype of each type. UI thread.
[[nodiscard]] form_language::FormSchema registry_form_schema();

// Appends the types a compiler build knows beyond the registry, each from
// a fresh prototype like a registry type. The installed compiler links
// form_core_catalog.cpp, which adds none; the Gallery's compiler and the
// tests that read its documents link form_lab_catalog.cpp, which adds the
// lab library's controls (lab/) and the demonstration controls
// (demo/lab_controls/) that left the installed library (audit F9). UI
// thread.
void add_extension_types(form_language::FormSchema& schema);

// Adds an application type to `schema`, described by
// "<name>,<C++ class>,<base type>,<header>": a document names it as
// <name> (snake case, written kebab), the generated code makes it as the
// qualified <C++ class> declared in <header>, and it has the properties and
// interactions of the schema's <base type>, which it must derive from. The
// loader cannot make it (no creator). Throws std::invalid_argument for a
// malformed description, a name the schema already has, or an unknown base;
// `schema` is then unchanged.
void add_application_type(form_language::FormSchema& schema, std::string_view description);

} // namespace gui_forms::form_compiler
