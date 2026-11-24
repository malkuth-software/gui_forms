#pragma once
// The input box (Visual Basic's InputBox): a prompt, one text field and OK
// and Cancel, as a TaskDialog page. GUI.Forms runs no nested event loop, so
// the answer arrives when the dialog closes: show_input_box shows it and
// returns the dialog, whose `closed` event carries the TaskDialogResult that
// input_box_result reads.
//
//     const std::shared_ptr<TaskDialog> box = show_input_box(owner, {.prompt = "New folder name"});
//     on((*box).closed(), *this, &Page::on_named);   // input_box_result(result).text
//
// Enter in the field clicks OK, Escape cancels, and the field takes focus
// first, its text selected.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/task_dialog.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace gui_forms {

// What to ask (Visual Basic's InputBox arguments): the prompt above the
// field, the title in the caption band (none without one), the field's
// starting text (DefaultResponse), the placeholder its empty field shows,
// and its maximum length in bytes (0 for no limit).
struct GUI_FORMS_API InputBoxRequest final {
    std::string prompt{};
    std::string title{};
    std::string default_response{};
    std::string placeholder{};
    std::size_t maximum_length{};
};

// The answer: accepted when OK closed the box, with the field's text; not
// accepted, with empty text, for Cancel, Escape or a box its window revoked
// (InputBox returns an empty string then).
struct GUI_FORMS_API InputBoxResult final {
    bool accepted{};
    std::string text{};
};

// The TaskDialog page of an input box: the caption, the prompt as its text,
// a TaskDialogInput, OK (the default) and Cancel, and Escape allowed. Throws
// std::invalid_argument as TaskDialog::set_page does, when it is set.
[[nodiscard]] GUI_FORMS_API TaskDialogPage input_box_page(const InputBoxRequest& request);

// The answer a closed input box gave.
[[nodiscard]] GUI_FORMS_API InputBoxResult input_box_result(const TaskDialogResult& result);

// Makes a TaskDialog named `stable_id` with the request's page and shows it
// over `owner`'s window. Returns the shown dialog, which its layer keeps
// until it closes, or null when `owner` is not attached to a window. Throws
// std::invalid_argument for text that is not valid UTF-8 or a default
// response longer than the maximum length.
[[nodiscard]] GUI_FORMS_API std::shared_ptr<TaskDialog> show_input_box(const Control::Ptr& owner,
                                                                      const InputBoxRequest& request,
                                                                      StableId stable_id = StableId("input-box"));

} // namespace gui_forms
