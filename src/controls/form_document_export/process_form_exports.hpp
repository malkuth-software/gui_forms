#pragma once

// The process's FormExportRegistry (register_form_document's records), for
// the automation's forms tools. Internal. UI thread.

#include "../../core/form_language/form_export/form_export.hpp"

namespace gui_forms::form_language {

// Never destroyed (exit releases nothing: tools/check_exit_statics.py).
[[nodiscard]] FormExportRegistry& process_form_exports();

} // namespace gui_forms::form_language
