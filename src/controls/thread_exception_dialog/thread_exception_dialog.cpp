#include "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp"

#include "gui_forms/controls/task_dialog.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The task dialog's button IDs. Continue is the standard button, so it takes
// the standard text; Quit has no standard button and names its own.
constexpr std::string_view continue_id = "continue";
constexpr std::string_view quit_id = "quit";

// The page a report shows. The heading says what happened, the text is the
// exception's message, the footnote says what each button does, and the
// details sit behind the expander.
TaskDialogPage report_page(const ThreadExceptionReport& report) {
    TaskDialogPage page;
    page.caption = report.caption.empty() ? std::string("Unhandled exception") : report.caption;
    page.text = report.message;
    page.buttons.push_back({.stable_id = std::string(continue_id), .standard = TaskDialogStandardButton::continue_});
    if (report.warning) {
        page.heading = "This application raised a warning.";
        page.icon = TaskDialogIcon::warning;
        page.footnote = "Continue ignores the warning.";
    } else {
        page.heading = "An unhandled exception occurred in this application.";
        page.icon = TaskDialogIcon::error;
        page.footnote = "Continue ignores the error and tries to keep running. Quit closes the application.";
        page.buttons.push_back({.stable_id = std::string(quit_id), .text = "Quit"});
    }
    page.default_button = std::string(continue_id);
    if (!report.details.empty()) page.expander = TaskDialogExpander{.text = report.details};
    // Escape continues, as the Windows Forms dialog's cancel button does.
    page.allow_cancel = true;
    return page;
}

} // namespace

ThreadExceptionReport describe_exception(const std::exception_ptr& error) {
    ThreadExceptionReport report;
    if (!error) return report;
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        report.message = exception.what();
    } catch (...) {
        report.message = "An exception of unknown type was thrown.";
    }
    return report;
}

ThreadExceptionDialog::ThreadExceptionDialog(StableId stable_id, ThreadExceptionReport report)
    : report_(std::move(report)) {
    // set_page validates the report's text before the dialog holds anything.
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(std::move(stable_id));
    (*dialog).set_page(report_page(report_));
    task_dialog_ = std::move(dialog);
    on((*task_dialog_).closed(), *this, &ThreadExceptionDialog::on_task_dialog_closed);
}

ThreadExceptionDialog::~ThreadExceptionDialog() {
    release_task_dialog();
}

bool ThreadExceptionDialog::show(const Control::Ptr& owner) {
    if (!is_alive() || !task_dialog_) {
        throw std::logic_error("GUI.Forms cannot show a disposed ThreadExceptionDialog");
    }
    const bool opened = (*task_dialog_).show(owner);
    return opened;
}

bool ThreadExceptionDialog::shown() const noexcept {
    const bool showing = task_dialog_ && (*task_dialog_).shown();
    return showing;
}

void ThreadExceptionDialog::close(DialogResult result) {
    if (result != DialogResult::cancel && result != DialogResult::abort) {
        throw std::invalid_argument("A ThreadExceptionDialog closes as cancel (Continue) or abort (Quit)");
    }
    if (result == DialogResult::abort && report_.warning) {
        throw std::invalid_argument("A warning ThreadExceptionDialog has no Quit button");
    }
    if (!shown()) return;
    const std::string_view button_id = result == DialogResult::abort ? quit_id : continue_id;
    (*task_dialog_).close(button_id);
}

void ThreadExceptionDialog::on_task_dialog_closed(const TaskDialogResult& outcome) {
    // Quit is the only abort; Continue, Escape and a revoked dialog continue.
    const DialogResult result = outcome.button_id == quit_id ? DialogResult::abort : DialogResult::cancel;
    result_ = result;
    closed_.emit(result);
}

void ThreadExceptionDialog::release_task_dialog() noexcept {
    const std::shared_ptr<TaskDialog> dialog = std::move(task_dialog_);
    task_dialog_.reset();
    if (!dialog) return;
    try {
        (*dialog).dispose();
    } catch (...) {
        // Disposal off the UI thread throws; the window closes the dialog's
        // overlay when it shuts down.
    }
}

void ThreadExceptionDialog::on_dispose() noexcept {
    release_task_dialog();
}

namespace {

// The events ThreadExceptionDialog adds to those of Component.
constexpr std::array<InteractionDeclaration, 1> thread_exception_dialog_interactions{{
    {.name = "closed", .payload = "DialogResult", .mutable_payload = false},
}};

} // namespace

void ThreadExceptionDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), thread_exception_dialog_interactions.begin(),
                        thread_exception_dialog_interactions.end());
}

} // namespace gui_forms
