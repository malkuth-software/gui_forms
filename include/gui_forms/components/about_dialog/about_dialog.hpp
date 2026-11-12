#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"

#include <memory>
#include <string>
#include <vector>

namespace gui_forms {

class TaskDialog;
struct TaskDialogResult;

// What an AboutDialog shows. `product` and `version` make the heading
// ("Product 1.2"); `description` and `copyright` are the body's paragraphs;
// `credits` (licences, acknowledgements) sit behind a "Show credits"
// expander, left out when empty. An empty caption gives "About <product>".
struct GUI_FORMS_API AboutInfo final {
    std::string caption{};
    std::string product{};
    std::string version{};
    std::string description{};
    std::string copyright{};
    std::string credits{};
};

// An application's About box (the superset's P1 rest; Visual Studio's
// AboutBox form template, which Windows Forms provides as a template rather
// than a control): the information icon, the product and version as the
// heading, the description and copyright, the credits behind an expander,
// and OK. It is a preset over TaskDialog, an owned in-window dialog that
// looks and behaves the same on every host. show() returns at once; closed
// reports DialogResult::ok for OK, Enter and Escape, and for a dialog its
// window revoked.
//
// Text must be valid UTF-8; anything else throws std::invalid_argument
// from the constructor. The dialog is used on the UI thread of the window
// it is shown over. Disposing or destroying a shown dialog closes it
// without raising closed.
class GUI_FORMS_API AboutDialog final : public Component {
public:
    AboutDialog(StableId stable_id, AboutInfo info);
    ~AboutDialog() override;

    [[nodiscard]] const AboutInfo& info() const noexcept { return info_; }

    // Shows the dialog over `owner`'s window. Returns false when it is
    // already shown or `owner` is not attached to a window. Throws
    // std::logic_error for a disposed dialog.
    bool show(const Control::Ptr& owner);
    [[nodiscard]] bool shown() const noexcept;
    // Closes the dialog as OK would. Does nothing when not shown.
    void close();

    [[nodiscard]] Event<DialogResult>& closed() noexcept { return closed_; }

    // The task dialog this dialog shows, for automation and tests; null once
    // disposed.
    [[nodiscard]] std::shared_ptr<TaskDialog> task_dialog() const noexcept { return task_dialog_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    void on_task_dialog_closed(const TaskDialogResult& outcome);
    void release_task_dialog() noexcept;

    AboutInfo info_{};
    std::shared_ptr<TaskDialog> task_dialog_{};
    Event<DialogResult> closed_{};
};

} // namespace gui_forms
