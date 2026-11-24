#pragma once

// The form compiler's schema (stage FD, phase FD5): the registry's types
// (the controls library's catalog, src/controls/form_catalog), then the
// compiler build's extension types, then the reserved form root; and the
// application and composite types the compiler's options add.

#include "../../src/controls/form_catalog/form_catalog.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"

#include <string_view>

namespace gui_forms::form_compiler {

// The catalog's entry and recipe, which the lab catalog names too.
using form_catalog::add_catalog_type;
using form_catalog::CatalogConstruction;
using form_catalog::FormCatalogEntry;

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

// Adds a composite control's type to `schema` (cp34), described by
// "<document>,<C++ class>,<header>": the `.control` document is read and
// compiled against `schema` as gui_forms_add_control compiles it, a form
// names the type by the document's name (`card-with-action`), and the
// generated code makes it as the qualified <C++ class> that
// gui_forms_add_control generated into <header>. Its properties and
// interactions are the definition's; its children go into its slot, or
// are refused without one (E809). The type keeps the definition (for
// --schema) but no creator. The document path may hold commas; the
// last two separate the class and the header. Throws
// std::invalid_argument for a malformed description, an unreadable
// document or a name the schema already has, `schema` then unchanged;
// returns false with the document's diagnostics in `diagnostics` when the
// document is refused.
[[nodiscard]] bool add_composite_type(form_language::FormSchema& schema, std::string_view description,
                                      theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_compiler
