#pragma once

// A compiled program's form documents, for a model (stage MCP G6; study
// section 18.2): with the build toggle GUI_FORMS_FORM_EXPORT on,
// gui_forms_add_form runs the form compiler with --export-document, and the
// generated code registers each form it makes here: its document's text and
// the program's members it binds. The automation tools `forms_export`,
// `forms_validate`, `forms_apply` and `forms_revert` then read the document
// back and may replace the form's structure with a candidate bound to those
// same members, and to nothing else. Off (the default), nothing registers
// and the program carries no document.
//
//     const gui_forms::FormDocumentSource sources[] = {
//         gui_forms::form_document_source("level_", 6U, owner.level_)};
//     gui_forms::register_form_document(root_control(), "settings.form", 13U,
//                                       document, sizeof(document), sources, 1U);
//
// Generated code writes these calls; a program has no need to. The texts
// are UTF-8, given with their byte counts (a file name, a document, source
// names), and copied. UI thread.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/value.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// What a registered source is: the kinds a form document binds.
enum class FormDocumentSourceKind : std::uint8_t {
    boolean_value,
    integer_value,
    number_value,
    command,
    binding_source,
};

// One member the form binds, by the name its document's `to` gives. The
// source is borrowed: it must outlive the form's controls, as the bindings
// generated code makes already require. Made by form_document_source.
struct GUI_FORMS_API FormDocumentSource final {
    const char* name{};
    std::size_t name_bytes{};
    FormDocumentSourceKind kind{FormDocumentSourceKind::boolean_value};
    void* source{};
};

[[nodiscard]] GUI_FORMS_API FormDocumentSource form_document_source(const char* name, std::size_t name_bytes,
                                                                    Value<bool>& source) noexcept;
[[nodiscard]] GUI_FORMS_API FormDocumentSource form_document_source(const char* name, std::size_t name_bytes,
                                                                    Value<int>& source) noexcept;
[[nodiscard]] GUI_FORMS_API FormDocumentSource form_document_source(const char* name, std::size_t name_bytes,
                                                                    Value<double>& source) noexcept;
[[nodiscard]] GUI_FORMS_API FormDocumentSource form_document_source(const char* name, std::size_t name_bytes,
                                                                    Command& source) noexcept;
// A BindingSource the program holds shared (as generated bindings take it).
[[nodiscard]] GUI_FORMS_API FormDocumentSource form_document_source(const char* name, std::size_t name_bytes,
                                                                    BindingSource& source) noexcept;

// What register_form_document did.
enum class FormDocumentRegistration : std::uint8_t {
    registered,
    // A null root, a text that is not UTF-8, a source without a name or
    // object, or a name given twice: nothing registered.
    refused,
    // The process's limit of registered forms is reached
    // (form_language::maximum_exported_windows).
    full,
};

// Registers the form whose root control is `root`: its document named
// `file`, and its `sources`. A second registration of the same root
// replaces the first. A form whose root ends is forgotten.
GUI_FORMS_API FormDocumentRegistration register_form_document(const Control::Ptr& root, const char* file,
                                                              std::size_t file_bytes, const char* document,
                                                              std::size_t document_bytes,
                                                              const FormDocumentSource* sources,
                                                              std::size_t source_count);

} // namespace gui_forms
