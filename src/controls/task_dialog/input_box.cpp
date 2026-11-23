#include "gui_forms/controls/input_box.hpp"

#include "gui_forms/controls/text_box.hpp"

#include <memory>
#include <string>
#include <utility>

namespace gui_forms {

TaskDialogPage input_box_page(const InputBoxRequest& request) {
    TaskDialogPage page{};
    page.caption = request.title;
    page.text = request.prompt;
    TaskDialogInput input{};
    input.text = request.default_response;
    input.placeholder = request.placeholder;
    input.maximum_length = request.maximum_length;
    page.input.set(std::move(input));
    page.buttons.push_back({.standard = TaskDialogStandardButton::ok});
    page.buttons.push_back({.standard = TaskDialogStandardButton::cancel});
    page.default_button = "ok";
    page.allow_cancel = true;
    return page;
}

InputBoxResult input_box_result(const TaskDialogResult& result) {
    InputBoxResult answer{};
    answer.accepted = result.button_id == "ok";
    if (answer.accepted) answer.text = result.input_text;
    return answer;
}

std::shared_ptr<TaskDialog> show_input_box(const Control::Ptr& owner, const InputBoxRequest& request,
                                           StableId stable_id) {
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(std::move(stable_id));
    (*dialog).set_page(input_box_page(request));
    if (!(*dialog).show(owner)) return nullptr;
    // The default response is selected, as InputBox's is, so typing
    // replaces it.
    const std::shared_ptr<TextBox> field = (*dialog).input_box();
    if (field) (*field).select_all();
    return dialog;
}

} // namespace gui_forms
