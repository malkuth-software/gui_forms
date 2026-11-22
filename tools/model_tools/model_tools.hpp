#pragma once

// The model tools of stage MCP G6, registered with the automation's tool
// registry (track M2's seam, src/controls/automation/tools/
// automation_tools.hpp): `forms.export`, `forms.validate`, `forms.apply`
// and `forms.revert` over the process's form documents
// (register_form_document, the build toggle GUI_FORMS_FORM_EXPORT), on the
// UI thread, on the window the call names.
//
// Linked as gui_forms_model_tools, which holds the form compiler's catalog
// (the registry schema the tools read against).

#include "../../src/controls/automation/tools/automation_tools.hpp"

#include <cstddef>

namespace gui_forms::automation {

// Builds the registry schema and registers the four tools. A name already
// registered is left as it is. Returns how many were added. UI thread;
// call once per registry.
std::size_t register_model_tools(ToolRegistry& tools);

} // namespace gui_forms::automation
