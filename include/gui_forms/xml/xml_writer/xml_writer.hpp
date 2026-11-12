#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

namespace gui_forms {

// Writes root as a document that read_xml accepts under the same limits.
// The output is deterministic: attributes in the order given, LF line ends,
// options.indent_spaces per level, `<name/>` for an element with neither
// children nor text, character data on the element's own line, and the same
// escaping every time, so writing what read_xml returned for a written
// document reproduces it byte for byte. Names, values, counts and depth are
// validated against limits; an element with both children and text is
// refused with XmlError::mixed_content. On failure the text is empty.
[[nodiscard]] GUI_FORMS_API XmlWriteResult write_xml(const XmlElement& root,
                                       const XmlWriteOptions& options = {},
                                       const XmlLimits& limits = {});

} // namespace gui_forms
