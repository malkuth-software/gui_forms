#pragma once

// A form document's editor (stage FD, phase FD4): one property set or
// reset, one object added, removed or moved, changing only the bytes of
// what it touches. Every edit is made on a copy of the text, which is then
// parsed and checked against the schema like any document; the document
// changes only when that copy is a valid document (validation before
// mutation). The text is never written again from the model, so attribute
// order, comments and whitespace survive every edit. Internal until stage
// D.
//
// Layout: an inserted attribute or element follows the line style around
// it. A new attribute goes after the element's last one: on the same line
// when that one is on the start tag's line, otherwise on a line of its own,
// indented like the last attribute's line. A new element goes on a line of
// its own, indented like the sibling it precedes or follows, or two spaces
// deeper than its parent when it is the first (an empty-element parent is
// opened first). The document's own line ending (CRLF when it has one) is
// used. A removed attribute takes the whitespace before it; a removed
// element takes its line with it when it is alone on that line. A moved
// element keeps its bytes as written, inner indentation included.

#include "../form_model/form_model.hpp"
#include "../form_xml/form_xml.hpp"

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::form_language {

// One open document: its text and everything read from it, kept in step
// (the syntax's locations are byte offsets into `text`). Caller-owned; the
// editor replaces it whole or leaves it as it was.
struct FormDocument final {
    std::string file{};
    std::string text{};
    FormSyntax syntax{};
    FormModel model{};
};

enum class FormEditOutcome : std::uint8_t {
    // The document holds the edit.
    edited,
    // The edit would change nothing (a reset of a property not written).
    unchanged,
    // No object of the document has the stable ID.
    object_missing,
    // The request itself is not an edit the editor makes: a name, value,
    // type or stable ID that is not one item's text, a position past the
    // children, the root removed or moved, or an object moved into itself.
    not_admitted,
    // The edited text is not a valid document: its diagnostics (against
    // the edited text) are appended.
    refused,
};

// Reads `text` as a form document named `file`. edited when it is valid;
// refused, with its diagnostics, leaves `document` unchanged.
[[nodiscard]] FormEditOutcome open_form_document(FormDocument& document, std::string file, std::string text,
                                                 const FormSchema& schema,
                                                 theme_language::ThemeDiagnosticList& diagnostics);

// Writes `property` (kebab-case, as a document spells it; a binding is a
// value, `bind(...)`) of the object `object_id` as `value`, the attribute's
// value as XML decodes it; the editor escapes it. A written attribute's
// value bytes are replaced; an unwritten one is added after the element's
// last attribute. `id` and `xmlns` are no properties (not_admitted).
[[nodiscard]] FormEditOutcome set_form_property(FormDocument& document, std::string_view object_id,
                                                std::string_view property, std::string_view value,
                                                const FormSchema& schema,
                                                theme_language::ThemeDiagnosticList& diagnostics);

// Removes the attribute `property` from the object's element, so that the
// property keeps the value its type gives it.
[[nodiscard]] FormEditOutcome reset_form_property(FormDocument& document, std::string_view object_id,
                                                  std::string_view property, const FormSchema& schema,
                                                  theme_language::ThemeDiagnosticList& diagnostics);

// Adds an empty object `<type id="stable_id"/>` to `parent_id` as its
// child object at `position` (0 first; the child count appends).
[[nodiscard]] FormEditOutcome add_form_object(FormDocument& document, std::string_view parent_id,
                                              std::size_t position, std::string_view type,
                                              std::string_view stable_id, const FormSchema& schema,
                                              theme_language::ThemeDiagnosticList& diagnostics);

// Removes the object and everything in it.
[[nodiscard]] FormEditOutcome remove_form_object(FormDocument& document, std::string_view object_id,
                                                 const FormSchema& schema,
                                                 theme_language::ThemeDiagnosticList& diagnostics);

// Moves the object to `parent_id` as its child object at `position`,
// counted among the parent's child objects once the object has left its
// place.
[[nodiscard]] FormEditOutcome move_form_object(FormDocument& document, std::string_view object_id,
                                               std::string_view parent_id, std::size_t position,
                                               const FormSchema& schema,
                                               theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
