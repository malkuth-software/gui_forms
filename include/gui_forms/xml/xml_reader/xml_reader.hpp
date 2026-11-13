#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <string_view>

namespace gui_forms {

// Reads one document of the subset described in xml_types.hpp. The whole
// input is checked before a result is returned; on any error the result
// holds the error, where it was found and an empty root, and nothing else
// is produced. Expected faults in the input never throw. Allocation failure
// propagates as std::bad_alloc; the limits bound the storage a read can
// retain to a small multiple of maximum_input_bytes.
[[nodiscard]] GUI_FORMS_API XmlReadResult read_xml(std::string_view utf8, const XmlLimits& limits = {});

} // namespace gui_forms
