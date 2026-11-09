// FolderBrowserDialog (stage W): the settings and their refusals, and showing
// the dialog through the headless host: the request it sends, where it
// starts, accepted, cancelled and refused answers, destruction while showing,
// reentrancy, the wrong thread and reset.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// What the scripted host saw and what it answers next. An answer queue that
// runs out answers with a cancelled dialog.
struct DialogScript final {
    std::vector<HostDialogRequest> requests{};
    std::deque<HostPathDialogResult> answers{};
    HostServiceError error{HostServiceError::none};
};

struct ScriptedDialogs final {
    DialogScript* script{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        (*script).requests.push_back(request);
        HostDialogResult result;
        result.request_id = request.request_id;
        if ((*script).error != HostServiceError::none) {
            result.status.error = (*script).error;
            result.payload = HostPathDialogResult{};
            return result;
        }
        if ((*script).answers.empty()) {
            result.payload = HostPathDialogResult{HostDialogOutcome::cancelled, {}};
            return result;
        }
        result.payload = (*script).answers.front();
        (*script).answers.pop_front();
        return result;
    }
};

HostPathDialogResult accepted(std::string path) {
    HostPathDialogResult result{HostDialogOutcome::accepted, {std::move(path)}};
    return result;
}

const HostFolderDialogRequest* folder_request(const HostDialogRequest& request) {
    const HostFolderDialogRequest* const result = std::get_if<HostFolderDialogRequest>(&request.payload);
    return result;
}

void settings() {
    FolderBrowserDialog dialog;
    require(dialog.description.read().empty() && dialog.initial_directory.read().empty() && dialog.selected_path.read().empty() &&
                dialog.status().accepted(),
            "a new dialog has the defaults");
    dialog.description = "Choose where the exports go";
    dialog.initial_directory = "/srv";
    dialog.selected_path = "/srv/exports";
    bool refused = false;
    try {
        dialog.selected_path = std::string("a\0b", 3U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.selected_path.read() == "/srv/exports", "a path holding NUL is refused");
    refused = false;
    try {
        dialog.description = std::string("Bad \xFF");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.description.read() == "Choose where the exports go",
            "an invalid description throws and keeps the old one");
    dialog.reset();
    require(dialog.description.read().empty() && dialog.initial_directory.read().empty() && dialog.selected_path.read().empty(),
            "reset restores the defaults");
}

void without_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("folder.unhosted"));
    Window window(root, {320.0, 200.0});
    FolderBrowserDialog dialog;
    dialog.selected_path = "/tmp/kept";
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::unsupported && dialog.selected_path.read() == "/tmp/kept",
            "without a host the dialog is not shown and the folder stays");
}

void shown_through_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("folder.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    FolderBrowserDialog dialog;
    dialog.description = "Choose where the exports go";
    dialog.initial_directory = "/srv";
    script.answers.push_back(accepted("/srv/exports"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.status().accepted() &&
                dialog.selected_path.read() == "/srv/exports",
            "an accepted dialog returns ok and takes the folder");
    const HostFolderDialogRequest* const first = folder_request(script.requests[0]);
    require(first != nullptr && script.requests[0].request_id != 0U && script.requests[0].owner_id == "folder.root" &&
                (*first).title == "Choose where the exports go" && (*first).initial_directory == "/srv",
            "the request carries the description and starts in the initial directory");

    require(dialog.show_dialog(window) == DialogResult::cancel && dialog.selected_path.read() == "/srv/exports",
            "a cancelled dialog keeps the folder");
    const HostFolderDialogRequest* const second = folder_request(script.requests[1]);
    require(second != nullptr && (*second).initial_directory == "/srv/exports" &&
                script.requests[1].request_id != script.requests[0].request_id,
            "a selected folder is where the dialog starts, with a new identity");

    script.error = HostServiceError::backend_failure;
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::backend_failure && dialog.selected_path.read() == "/srv/exports",
            "a host failure returns none with the host's answer");
    script.error = HostServiceError::none;
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

// A host whose modal loop destroys, disposes or reshows the dialog it is
// showing, as a timer or another callback inside a real host's loop could.
struct EndingDialogs final {
    std::shared_ptr<FolderBrowserDialog>* owner{};
    Window* window{};
    bool* reentry_refused{};
    int mode{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        if (mode == 0) {
            (*owner).reset();
        } else if (mode == 1) {
            (*(*owner)).dispose();
        } else {
            try {
                static_cast<void>((*(*owner)).show_dialog(*window));
            } catch (const std::logic_error&) {
                *reentry_refused = true;
            }
        }
        HostDialogResult result;
        result.request_id = request.request_id;
        result.payload = HostPathDialogResult{HostDialogOutcome::accepted, {"/tmp/late"}};
        return result;
    }
};

struct ShowFromOtherThread final {
    FolderBrowserDialog* dialog{};
    Window* window{};
    bool* refused{};
    void operator()() const {
        try {
            static_cast<void>((*dialog).show_dialog(*window));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void ended_while_showing() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("folder.ended"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    std::shared_ptr<FolderBrowserDialog> dialog = std::make_shared<FolderBrowserDialog>();
    bool reentry_refused = false;

    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 2});
    require((*dialog).show_dialog(window) == DialogResult::ok && reentry_refused &&
                (*dialog).selected_path.read() == "/tmp/late",
            "showing a dialog that is already showing is refused");

    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 1});
    require((*dialog).show_dialog(window) == DialogResult::cancel && (*dialog).selected_path.read() == "/tmp/late",
            "a dialog disposed while showing returns cancel and takes nothing");

    dialog = std::make_shared<FolderBrowserDialog>();
    FolderBrowserDialog* const address = dialog.get();
    services.set_dialog_handler(EndingDialogs{&dialog, &window, &reentry_refused, 0});
    require((*address).show_dialog(window) == DialogResult::cancel && !dialog,
            "a dialog destroyed while showing returns cancel without touching itself");

    FolderBrowserDialog threaded;
    bool wrong_thread = false;
    std::thread other(ShowFromOtherThread{&threaded, &window, &wrong_thread});
    other.join();
    require(wrong_thread, "showing off the UI thread is refused");
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

} // namespace

int main() {
    settings();
    without_host();
    shown_through_host();
    ended_while_showing();
    std::cout << "FolderBrowserDialog: settings, refusals, host requests and answers, destruction, reentrancy "
                 "and threads passed\n";
    return 0;
}
