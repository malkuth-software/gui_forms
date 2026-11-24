#pragma once

// The automation core's reading of the program's marks
// (gui_forms/automation/marks/automation_marks.hpp) beyond the public
// queries: the text a pending user step shows. UI thread.

#include "gui_forms/semantics/types/semantic_types.hpp"

#include <string>
#include <string_view>

namespace gui_forms::automation::detail {

// The text of the pending user step on `stable_id`, into `what`; false,
// leaving it, when none is pending.
[[nodiscard]] bool pending_user_step(std::string_view stable_id, std::string& what);
[[nodiscard]] bool action_destructive_mark(std::string_view stable_id, SemanticAction action);
[[nodiscard]] bool command_hidden_mark(std::string_view stable_id);
// Whether `stable_id` is marked as opening a dialog; its dialog-free tool,
// or empty, into `tool`.
[[nodiscard]] bool opens_dialog_mark(std::string_view stable_id, std::string& tool);

} // namespace gui_forms::automation::detail
