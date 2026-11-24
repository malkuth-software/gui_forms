#pragma once

// What a program tells the automation about itself (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, sections 4.3, 7.2, 9 and 10): which dialogs a
// model may drive, which acts are destructive, which commands a model does
// not see, which steps need the user, whether the clipboard may be read,
// and the program's MCP identity.
//
// Every mark is settable whether or not the build has MCP (GUI_FORMS_MCP):
// without it they are kept and read by nothing, so a program states its
// policy once. Code is the default place for a mark; form documents carry
// the same marks through the registry later (study 17.7).
//
// Defaults (rulings 13 to 18): every act is non-destructive, every dialog
// refuses a model, every command is offered, no step needs the user beyond
// the ones marked, and the clipboard is not readable.
//
// Text is NUL-terminated UTF-8, read during the call only. UI thread: the
// marks are read by the automation core on the UI thread of the windows.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"

namespace gui_forms {

class Window;

namespace automation {

// Whether this build has the MCP capability (the GUI_FORMS_MCP build
// option, on by default).
[[nodiscard]] GUI_FORMS_API bool mcp_capability_built() noexcept;

// A dialog (or any window) a model may drive while it is open (study 9.2):
// the installer's setup dialog is the example. Without the mark a model is
// refused control while a dialog is open (dialog_open). The mark lasts as
// long as the window.
GUI_FORMS_API void set_model_drivable(Window& window, bool drivable);
[[nodiscard]] GUI_FORMS_API bool model_drivable(const Window& window) noexcept;

// Every act in `window` is destructive: the first one of a launch asks the
// user once (study 10).
GUI_FORMS_API void set_window_destructive(Window& window, bool destructive);
[[nodiscard]] GUI_FORMS_API bool window_destructive(const Window& window) noexcept;

// `action` on the node with stable identity `stable_id` (a control's or a
// virtual child's) is destructive. A Command marks itself with
// Command::set_destructive. False for an identity that is null, empty or
// not UTF-8, marking nothing.
GUI_FORMS_API bool set_action_destructive(const char* stable_id, SemanticAction action, bool destructive);
[[nodiscard]] GUI_FORMS_API bool action_destructive(const char* stable_id, SemanticAction action) noexcept;

// The command with stable identity `stable_id` is not offered to a model as
// a tool, and invoke_command refuses it as not_found (study 7.2: "A program
// can hide any of them").
GUI_FORMS_API bool set_command_hidden_from_model(const char* stable_id, bool hidden);
[[nodiscard]] GUI_FORMS_API bool command_hidden_from_model(const char* stable_id) noexcept;

// A step inside an instrumented dialog that needs the user (study 9.3): a
// security approval, an administrator password, a licence. While it is
// pending, a model acting on the node `stable_id` is answered needs_user
// with `what_user_must_do`, and the interface shows the step to the user.
// The program completes it once the user has done it; a model's wait on it
// then reports it done. False for invalid text, changing nothing.
GUI_FORMS_API bool require_user_step(const char* stable_id, const char* what_user_must_do);
GUI_FORMS_API void complete_user_step(const char* stable_id);
[[nodiscard]] GUI_FORMS_API bool user_step_pending(const char* stable_id) noexcept;

// Whether a model may read the clipboard (ruling 24); off by default.
GUI_FORMS_API void set_clipboard_readable_by_model(bool readable) noexcept;
[[nodiscard]] GUI_FORMS_API bool clipboard_readable_by_model() noexcept;

// The program's MCP identity (study 4.3): app.<vendor>.<appname> by
// default, made from the program identity's vendor and product
// (set_program_identity). A program may set its own: 3 to 128 bytes of
// lowercase ASCII letters, digits, '.', '-' and '_', starting with a letter,
// with at least one '.'. False, keeping the identity, for anything else.
GUI_FORMS_API bool set_mcp_identity(const char* identity);
// The identity set, or empty when none was. Static storage, valid until the
// next set_mcp_identity.
[[nodiscard]] GUI_FORMS_API const char* mcp_identity_override() noexcept;

// Removes the automation's launch arguments from a program's argv before
// the program reads it (study 4.2): --mcp, --model-directed,
// --mcp-appearance-review and --mcp-script <file>. argv's pointers move down and argc shrinks; argv
// [argc] stays null. The library reads the process's own arguments, so a
// program that never calls this still gets MCP; one that parses argv calls
// it first so the arguments do not reach its parser.
GUI_FORMS_API void take_mcp_arguments(int& argc, char** argv) noexcept;

} // namespace automation
} // namespace gui_forms
