#pragma once

// Stage MCP G6's model tools: `forms_export`, `forms_validate`,
// `forms_apply` and `forms_revert` over the process's form documents, on
// the UI thread, on the window the call names, against the process's
// registry schema (src/controls/form_catalog). The first
// register_form_document registers them (the build toggle
// GUI_FORMS_FORM_EXPORT on).

#include "../tools/automation_tools.hpp"

#include <cstddef>

namespace gui_forms::automation {

// Registers the four tools. A name already registered is left as it is.
// Returns how many were added. The schema is made by the first call of a
// tool (UI thread).
std::size_t register_model_form_tools(ToolRegistry& tools);

} // namespace gui_forms::automation
