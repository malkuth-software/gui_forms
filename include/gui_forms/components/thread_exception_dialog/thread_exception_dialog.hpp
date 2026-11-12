#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"

#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace gui_forms {

class TaskDialog;
struct TaskDialogResult;

// What a ThreadExceptionDialog reports. `message` is the exception's own
// text and `details` the diagnostic text behind the details expander (a
// stack trace, the operation that failed); C++ exceptions carry neither a
// portable type name nor a stack trace, so the application supplies what it
// has. An empty caption gives "Unhandled exception"; empty details leave the
// expander out. A `warning` report (Windows Forms' WarningException) shows the
// warning icon and offers Continue only.
struct GUI_FORMS_API ThreadExceptionReport final {
    std::string caption{};
    std::string message{};
    std::string details{};
    bool warning{};
};

// The report for a caught exception: the message of a std::exception, or
// "An exception of unknown type was thrown." for anything else, with no
// details. A null `error` gives an empty message.
[[nodiscard]] GUI_FORMS_API ThreadExceptionReport describe_exception(const std::exception_ptr& error);

// The dialog an application shows for an exception it did not handle
// (Windows Forms' ThreadExceptionDialog): an error icon, the message, the
// details behind an expander, and Continue and Quit buttons. It is a preset
// over TaskDialog, an owned in-window dialog that looks and behaves the same
// on every host. show() returns at once; `closed` reports DialogResult::cancel
// for Continue (also Escape, and a dialog revoked by its window) and
// DialogResult::abort for Quit, as Windows Forms' dialog returns them. The
// dialog never ends the application: on abort the application closes itself.
//
// Report text must be valid UTF-8; anything else throws
// std::invalid_argument from the constructor. The dialog is used on the UI
// thread of the window it is shown over. Disposing or destroying a shown
// dialog closes it without raising closed.
class GUI_FORMS_API ThreadExceptionDialog final : public Component {
public:
    ThreadExceptionDialog(StableId stable_id, ThreadExceptionReport report);
    ~ThreadExceptionDialog() override;

    [[nodiscard]] const ThreadExceptionReport& report() const noexcept { return report_; }

    // Shows the dialog over `owner`'s window. Returns false when it is
    // already shown or `owner` is not attached to a window. Throws
    // std::logic_error for a disposed dialog.
    bool show(const Control::Ptr& owner);
    [[nodiscard]] bool shown() const noexcept;
    // Closes the dialog as Continue (cancel) or Quit (abort) would, raising
    // closed. Throws std::invalid_argument for any other result, and for
    // abort on a warning, which has no Quit. Does nothing when not shown.
    void close(DialogResult result);
    // The result of the last close, or none before the first.
    [[nodiscard]] DialogResult result() const noexcept { return result_; }

    [[nodiscard]] Event<DialogResult>& closed() noexcept { return closed_; }

    // The task dialog this dialog shows, for automation and tests; null once
    // disposed.
    [[nodiscard]] std::shared_ptr<TaskDialog> task_dialog() const noexcept { return task_dialog_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    void on_task_dialog_closed(const TaskDialogResult& outcome);
    // Disposes the task dialog, which closes it without raising closed.
    void release_task_dialog() noexcept;

    ThreadExceptionReport report_{};
    std::shared_ptr<TaskDialog> task_dialog_{};
    DialogResult result_{DialogResult::none};
    Event<DialogResult> closed_{};
};

} // namespace gui_forms
