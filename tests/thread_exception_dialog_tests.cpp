// ThreadExceptionDialog (stage W): the report a caught exception gives, the
// task dialog page a report shows, Continue, Quit, Escape and close, a warning
// report, report validation, disposal and destruction while shown, and a
// dialog destroyed by its own closed handler.

#include "gui_forms/component/component/component.hpp"
#include "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/task_dialog.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/rules/rule_set/rule_set.hpp"
#include "gui_forms/schema/types/schema_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Records each close; with `owner` set, releases the dialog that closed.
struct Recorder final : public Component {
    std::vector<DialogResult> results;
    std::shared_ptr<ThreadExceptionDialog>* owner{};

    void on_closed(DialogResult result) {
        results.push_back(result);
        if (owner != nullptr) (*owner).reset();
    }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> owner;
    std::unique_ptr<Window> window;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("error.root"));
    fixture.owner = make_control<Button>(StableId("error.owner"), "Run");
    (*fixture.root).add_child(fixture.owner);
    (*fixture.owner).set_requested_bounds({20.0, 20.0, 120.0, 32.0});
    fixture.window = std::make_unique<Window>(fixture.root, Size{720.0, 560.0});
    (*fixture.window).perform_layout();
    return fixture;
}

ThreadExceptionReport error_report() {
    ThreadExceptionReport report;
    report.caption = "Station scanner";
    report.message = "The tuner stopped answering.";
    report.details = "scan_band failed at 98.1 MHz after 3 retries.";
    return report;
}

template <typename Action>
bool throws_invalid(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

struct MakeDialog final {
    ThreadExceptionReport report{};
    void operator()() const { ThreadExceptionDialog dialog(StableId("error.bad"), report); }
};

struct CloseAs final {
    ThreadExceptionDialog* dialog{};
    DialogResult result{};
    void operator()() const { (*dialog).close(result); }
};

void described_exceptions() {
    const ThreadExceptionReport standard =
        describe_exception(std::make_exception_ptr(std::runtime_error("index out of date")));
    require(standard.message == "index out of date" && standard.details.empty() && !standard.warning,
            "a std::exception gives its message");
    const ThreadExceptionReport unknown = describe_exception(std::make_exception_ptr(42));
    require(unknown.message == "An exception of unknown type was thrown.", "another exception is of unknown type");
    const ThreadExceptionReport none = describe_exception(std::exception_ptr{});
    require(none.message.empty(), "a null exception gives an empty message");
}

void report_pages() {
    const ThreadExceptionDialog error(StableId("error.page"), error_report());
    const std::shared_ptr<TaskDialog> task = error.task_dialog();
    require(task != nullptr, "the dialog has a task dialog");
    const TaskDialogPage& page = (*task).page();
    require(page.caption == "Station scanner" && page.text == "The tuner stopped answering." &&
                page.icon == TaskDialogIcon::error && !page.heading.empty() && !page.footnote.empty(),
            "an error report shows its caption, message and the error icon");
    require(page.buttons.size() == 2U && page.buttons[0].stable_id == "continue" &&
                page.buttons[0].text == "Continue" && page.buttons[1].stable_id == "quit" &&
                page.buttons[1].text == "Quit" && page.default_button == "continue" && page.allow_cancel,
            "an error report offers Continue, the default, and Quit");
    require(page.expander.has_value() && (*page.expander).text == error_report().details,
            "the details sit behind the expander");
    require(error.result() == DialogResult::none && !error.shown(), "a new dialog has no result");

    ThreadExceptionReport warning;
    warning.message = "The cache is nearly full.";
    warning.warning = true;
    const ThreadExceptionDialog warned(StableId("error.warning"), warning);
    const TaskDialogPage& warning_page = (*warned.task_dialog()).page();
    require(warning_page.caption == "Unhandled exception" && warning_page.icon == TaskDialogIcon::warning &&
                warning_page.buttons.size() == 1U && warning_page.buttons[0].stable_id == "continue" &&
                !warning_page.expander.has_value(),
            "a warning offers Continue only, with the default caption and no expander");

    ThreadExceptionReport invalid;
    invalid.message = std::string("bad \xC3", 5U);
    require(throws_invalid(MakeDialog{invalid}), "a report that is not UTF-8 is refused");

    const std::vector<InteractionDeclaration> interactions = error.interactions();
    require(!interactions.empty() && interactions.back().name == "closed" &&
                interactions.back().payload == "DialogResult",
            "the dialog declares its closed event");
}

void closing() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ThreadExceptionDialog dialog(StableId("error.dialog"), error_report());
    Recorder recorder;
    on(dialog.closed(), recorder, &Recorder::on_closed);
    const std::shared_ptr<TaskDialog> task = dialog.task_dialog();

    require(!dialog.show(nullptr), "a dialog without an owner does not show");
    require(dialog.show(fixture.owner) && dialog.shown(), "the dialog shows over its owner's window");
    require(!dialog.show(fixture.owner), "a shown dialog does not show twice");
    window.perform_layout();
    const std::shared_ptr<Control> continue_button = (*task).button("continue");
    require(window.focused_control() == continue_button, "Continue takes focus");
    require((*task).click("quit") && !dialog.shown() && recorder.results.size() == 1U &&
                recorder.results.back() == DialogResult::abort && dialog.result() == DialogResult::abort,
            "Quit closes the dialog as abort");

    require(dialog.show(fixture.owner), "the dialog shows again");
    window.perform_layout();
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::enter}));
    static_cast<void>(window.dispatch_key({KeyAction::up, PhysicalKey::enter}));
    require(!dialog.shown() &&
                recorder.results.back() == DialogResult::cancel,
            "Enter on the focused Continue closes as cancel");

    require(dialog.show(fixture.owner), "the dialog shows for Escape");
    window.perform_layout();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !dialog.shown() &&
                recorder.results.back() == DialogResult::cancel,
            "Escape continues");

    require(throws_invalid(CloseAs{&dialog, DialogResult::ok}), "close refuses a result the dialog lacks");
    const std::size_t before = recorder.results.size();
    dialog.close(DialogResult::abort);
    require(recorder.results.size() == before, "closing a dialog that is not shown does nothing");
    require(dialog.show(fixture.owner), "the dialog shows for close");
    dialog.close(DialogResult::abort);
    require(!dialog.shown() && recorder.results.back() == DialogResult::abort, "close names its result");

    require(dialog.show(fixture.owner), "the dialog shows before its owner leaves");
    static_cast<void>((*fixture.root).remove_child((*fixture.owner).runtime_id()));
    require(!dialog.shown() && recorder.results.back() == DialogResult::cancel,
            "a dialog whose owner leaves the window continues");

    ThreadExceptionReport warning;
    warning.warning = true;
    ThreadExceptionDialog warned(StableId("error.warned"), warning);
    require(throws_invalid(CloseAs{&warned, DialogResult::abort}), "a warning cannot close as Quit");
}

void disposal_and_destruction() {
    Fixture fixture = make_fixture();
    Recorder recorder;

    ThreadExceptionDialog disposed(StableId("error.disposed"), error_report());
    on(disposed.closed(), recorder, &Recorder::on_closed);
    const std::shared_ptr<TaskDialog> disposed_task = disposed.task_dialog();
    require(disposed.show(fixture.owner), "the dialog shows before disposal");
    disposed.dispose();
    require(!(*disposed_task).shown() && recorder.results.empty() && disposed.task_dialog() == nullptr &&
                !disposed.shown(),
            "a disposed dialog closes without raising closed");
    bool refused = false;
    try {
        static_cast<void>(disposed.show(fixture.owner));
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a disposed dialog cannot show");

    std::shared_ptr<ThreadExceptionDialog> destroyed =
        std::make_shared<ThreadExceptionDialog>(StableId("error.destroyed"), error_report());
    const std::shared_ptr<TaskDialog> destroyed_task = (*destroyed).task_dialog();
    require((*destroyed).show(fixture.owner), "the dialog shows before destruction");
    destroyed.reset();
    require(!(*destroyed_task).shown(), "a destroyed dialog closes");

    // A handler that destroys the dialog while it reports the close.
    std::shared_ptr<ThreadExceptionDialog> released =
        std::make_shared<ThreadExceptionDialog>(StableId("error.released"), error_report());
    Recorder releaser;
    releaser.owner = &released;
    on((*released).closed(), releaser, &Recorder::on_closed);
    const std::shared_ptr<TaskDialog> released_task = (*released).task_dialog();
    require((*released).show(fixture.owner), "the dialog shows before its handler releases it");
    require((*released_task).click("continue") && released == nullptr && releaser.results.size() == 1U &&
                releaser.results.back() == DialogResult::cancel && !(*released_task).shown(),
            "a closed handler may destroy the dialog");
}

} // namespace

int main() {
    described_exceptions();
    report_pages();
    closing();
    disposal_and_destruction();
    std::cout << "ThreadExceptionDialog: described exceptions, report pages, Continue, Quit, Enter, Escape, close, "
                 "a revoked owner, warnings, validation, disposal, destruction and release in closed passed\n";
    return 0;
}
