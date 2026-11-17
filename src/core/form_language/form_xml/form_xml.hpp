#pragma once

// A form document's XML front end (cp34: forms move to XML before 1.0).
// A form document is XML in GUI.Forms' bounded subset (xml_types.hpp, DK
// decision 43), read with where each element and attribute was written, so
// the model can locate its diagnostics and the editor can splice its edits.
// Internal until stage D.
//
// The vocabulary (docs/user/forms.md; form_model.hpp checks it):
//
//   <?xml version="1.0" encoding="UTF-8"?>
//   <form xmlns="urn:gui-forms:form:1" id="settings.window" text="Settings"
//       accept-button="settings.ok">
//     <panel id="settings.root">
//       <check-box id="settings.autosave" checked="bind(autosave)"/>
//     </panel>
//   </form>
//
// - An element is an object, named by its type in kebab case; `form` is the
//   window root. Its child elements are its children, in document order.
// - `id` holds the stable ID; `xmlns` (the root only) the language and its
//   version, form_namespace. Every other attribute is a property.
// - `<span name? anchor rows? columns?/>` directly inside a type that holds
//   spans declares one region of its grid.
// - Whitespace between elements is indentation; an object holds no other
//   text.

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../xml/xml_reader/xml_located_reader.hpp"

#include "gui_forms/xml/types/xml_types.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms::form_language {

// The default namespace a form document's root declares: the vocabulary
// and its version. An incompatible later vocabulary is `:2`.
inline constexpr std::string_view form_namespace = "urn:gui-forms:form:1";

// The attribute that holds an object's stable ID, and the root's namespace
// declaration; neither is a property.
inline constexpr std::string_view form_identity_attribute = "id";
inline constexpr std::string_view form_namespace_attribute = "xmlns";

// The limits a form document is read under: XmlLimits with the input bound
// to a theme sheet's (theme_language::maximum_sheet_bytes), which the line
// table the diagnostics use admits.
[[nodiscard]] XmlLimits form_xml_limits() noexcept;

// One read document: the elements, where each was written, and the line
// table that turns those offsets into lines and columns. Caller-owned and
// reused; replaced whole by read_form_syntax, left empty by one that
// refuses. The model and the editor borrow it with the text it was read
// from.
struct FormSyntax final {
    XmlElement root{};
    detail::XmlLocations locations{};
    theme_language::LineStartTable lines{};
};

enum class FormSyntaxOutcome : std::uint8_t {
    read,
    // Not a document of the subset: one E821 (form_xml_invalid) whose
    // detail is the reader's XmlError name, at the reader's position.
    refused,
};

// Reads `source` (its text and, for the diagnostics, its file name) into
// `syntax`. Both are borrowed for the call.
[[nodiscard]] FormSyntaxOutcome read_form_syntax(const theme_language::ThemeSource& source, FormSyntax& syntax,
                                                 theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
