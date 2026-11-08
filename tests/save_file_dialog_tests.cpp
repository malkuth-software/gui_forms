// SaveFileDialog (stage W): the settings and their refusals, the default
// extension and when it is added, and showing the dialog through the
// headless host: the request it sends, accepted, cancelled and refused
// answers, file_ok with cancel, destruction from file_ok, a save dialog shown
// from an open dialog's file_ok, reset and the interactions it declares.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
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

// What the scripted host saw and what it answers next. An answer queue that
// runs out answers with a cancelled dialog.
struct DialogScript final {
    std::vector<HostDialogRequest> requests{};
    std::deque<HostPathDialogResult> answers{};
    HostServiceError error{HostServiceError::none};
};

// The headless host's dialog handler: records each request and answers from
// the script.
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

const HostSaveFileDialogRequest* save_request(const HostDialogRequest& request) {
    const HostSaveFileDialogRequest* const result = std::get_if<HostSaveFileDialogRequest>(&request.payload);
    return result;
}

bool refuses_default_ext(SaveFileDialog& dialog, std::string extension) {
    bool refused = false;
    try {
        dialog.default_ext = std::move(extension);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    return refused;
}

void settings() {
    SaveFileDialog dialog;
    require(dialog.title.read().empty() && dialog.filter.read().empty() && dialog.default_ext.read().empty() &&
                dialog.add_extension.read() && dialog.overwrite_prompt.read() && dialog.file_name.read().empty() &&
                dialog.file_names().empty() && dialog.status().accepted(),
            "a new dialog has the defaults");
    dialog.default_ext = ".txt";
    require(dialog.default_ext.read() == "txt", "a leading dot is removed from the default extension");
    dialog.default_ext = "tar.gz";
    require(dialog.default_ext.read() == "tar.gz", "a multi-dotted default extension is kept whole");
    require(refuses_default_ext(dialog, "*.txt") && refuses_default_ext(dialog, "a/b") &&
                refuses_default_ext(dialog, "a?") &&
                refuses_default_ext(dialog, std::string("t\0t", 3U)) && dialog.default_ext.read() == "tar.gz",
            "wildcards, separators and NUL are refused and the old extension kept");
    dialog.default_ext = "";
    require(dialog.default_ext.read().empty(), "an empty default extension adds none");

    dialog.title = "Export";
    dialog.filter = "Reports|*.txt|All files|*.*";
    dialog.add_extension = false;
    dialog.overwrite_prompt = false;
    dialog.file_name = "/srv/report.txt";
    bool refused = false;
    try {
        dialog.filter = "Reports|report*.txt";
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.filters().size() == 2U, "a bad filter throws and keeps the old one");
    require(dialog.file_name.read() == "/srv/report.txt" && dialog.file_names().size() == 1U,
            "a set name is the chosen path");
    dialog.reset();
    require(dialog.title.read().empty() && dialog.filter.read().empty() && dialog.filters().empty() &&
                dialog.default_ext.read().empty() && dialog.add_extension.read() && dialog.overwrite_prompt.read() &&
                dialog.file_names().empty(),
            "reset restores the defaults");

    const std::vector<InteractionDeclaration> interactions = dialog.interactions();
    require(!interactions.empty() && interactions.back().name == "file_ok" &&
                interactions.back().payload == "FileDialogOk" && interactions.back().mutable_payload,
            "the dialog declares file_ok with a mutable payload");
}

void without_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("save.unhosted"));
    Window window(root, {320.0, 200.0});
    SaveFileDialog dialog;
    dialog.file_name = "/tmp/kept.txt";
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::unsupported && dialog.file_name.read() == "/tmp/kept.txt",
            "without a host the dialog is not shown and the name stays");
}

// A file_ok handler that records what it saw and cancels the first `refusals`
// acceptances.
struct OkLog final {
    std::vector<std::string> seen{};
    int refusals{};

    void on_ok(FileDialogOk& ok) {
        seen.push_back(ok.file_names.empty() ? std::string{} : ok.file_names.front());
        if (refusals > 0) {
            --refusals;
            ok.cancel = true;
        }
    }
};

void shown_through_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("save.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    SaveFileDialog dialog;
    dialog.title = "Export a report";
    dialog.initial_directory = "/srv/reports";
    dialog.filter = "Reports|*.txt|All files|*.*";
    dialog.default_ext = "txt";
    dialog.file_name = "summary";
    OkLog log;
    SubscriptionToken ok_token =
        dialog.file_ok().subscribe(Delegate<FileDialogOk&>::bind<OkLog, &OkLog::on_ok>(log));

    script.answers.push_back(accepted("/srv/reports/summary"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.status().accepted(),
            "an accepted dialog returns ok");
    require(script.requests.size() == 1U && script.requests[0].request_id != 0U &&
                script.requests[0].owner_id == "save.root",
            "the request is owned by the window's root");
    const HostSaveFileDialogRequest* const sent = save_request(script.requests[0]);
    require(sent != nullptr && (*sent).title == "Export a report" && (*sent).initial_directory == "/srv/reports" &&
                (*sent).suggested_name == "summary" && (*sent).default_extension == "txt" &&
                (*sent).confirm_overwrite && (*sent).filters.size() == 2U,
            "the request carries the settings");
    require(dialog.file_name.read() == "/srv/reports/summary.txt" && dialog.file_names().size() == 1U &&
                log.seen.size() == 1U && log.seen[0] == "/srv/reports/summary.txt",
            "a name without an extension is given the default one before file_ok");

    script.answers.push_back(accepted("/srv/reports/summary.md"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/srv/reports/summary.md",
            "a name with an extension keeps it");
    const HostSaveFileDialogRequest* const second = save_request(script.requests[1]);
    require(second != nullptr && (*second).suggested_name == "/srv/reports/summary.txt" &&
                script.requests[1].request_id != script.requests[0].request_id,
            "each request has its own identity and suggests the chosen path");
    script.answers.push_back(accepted("/srv/reports/.profile"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/srv/reports/.profile",
            "a name starting with a dot has an extension, as in .NET");

    dialog.add_extension = false;
    dialog.overwrite_prompt = false;
    script.answers.push_back(accepted("/srv/reports/plain"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/srv/reports/plain",
            "with add_extension off the name is kept as chosen");
    const HostSaveFileDialogRequest* const plain = save_request(script.requests[3]);
    require(plain != nullptr && (*plain).default_extension.empty() && !(*plain).confirm_overwrite,
            "with add_extension off the host is given no extension to add, and no overwrite prompt");
    dialog.add_extension = true;

    require(dialog.show_dialog(window) == DialogResult::cancel && dialog.file_name.read() == "/srv/reports/plain" &&
                log.seen.size() == 4U,
            "a cancelled dialog keeps the path and raises no file_ok");

    log.refusals = 1;
    script.answers.push_back(accepted("/srv/reports/taken.txt"));
    script.answers.push_back(accepted("/srv/reports/free.txt"));
    const std::size_t before = script.requests.size();
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/srv/reports/free.txt",
            "a cancelled file_ok shows the dialog again and the second choice is taken");
    require(script.requests.size() == before + 2U && log.seen.size() == 6U && log.seen[4] == "/srv/reports/taken.txt",
            "the dialog was shown twice and file_ok raised for each acceptance");

    script.error = HostServiceError::backend_failure;
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::backend_failure &&
                dialog.file_name.read() == "/srv/reports/free.txt",
            "a host failure returns none with the host's answer");
    script.error = HostServiceError::none;
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

struct DestroyOnOk final {
    std::shared_ptr<SaveFileDialog>* owner{};
    void on_ok(FileDialogOk&) { (*owner).reset(); }
};

// An open dialog's file_ok that asks where to save a copy, inside the open
// dialog's show_dialog call.
struct SaveFromOpen final {
    SaveFileDialog* save{};
    Window* window{};
    DialogResult result{DialogResult::none};
    void on_ok(FileDialogOk&) { result = (*save).show_dialog(*window); }
};

// Answers the open request and then the save request, recording each
// request's identity and the deepest modal stack it saw.
struct NestedDialogs final {
    std::size_t* deepest{};
    std::vector<std::uint64_t>* identities{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices& services) const {
        (*identities).push_back(request.request_id);
        const std::size_t depth = services.snapshot().modal_depth;
        if (depth > *deepest) *deepest = depth;
        HostDialogResult result;
        result.request_id = request.request_id;
        if (std::holds_alternative<HostOpenFileDialogRequest>(request.payload)) {
            result.payload = HostPathDialogResult{HostDialogOutcome::accepted, {"/tmp/source.txt"}};
        } else {
            result.payload = HostPathDialogResult{HostDialogOutcome::accepted, {"/tmp/copy"}};
        }
        return result;
    }
};

void ended_and_nested() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("save.ended"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    std::shared_ptr<SaveFileDialog> destroyed = std::make_shared<SaveFileDialog>();
    SaveFileDialog* const destroyed_address = destroyed.get();
    DestroyOnOk destroyer{&destroyed};
    SubscriptionToken destroy_token = (*destroyed).file_ok().subscribe(
        Delegate<FileDialogOk&>::bind<DestroyOnOk, &DestroyOnOk::on_ok>(destroyer));
    script.answers.push_back(accepted("/tmp/b.txt"));
    require((*destroyed_address).show_dialog(window) == DialogResult::cancel && !destroyed,
            "a dialog destroyed by file_ok returns cancel without touching itself");

    std::size_t deepest = 0;
    std::vector<std::uint64_t> identities{};
    services.set_dialog_handler(NestedDialogs{&deepest, &identities});
    OpenFileDialog open;
    SaveFileDialog save;
    save.default_ext = "txt";
    SaveFromOpen nested{&save, &window, DialogResult::none};
    SubscriptionToken nested_token =
        open.file_ok().subscribe(Delegate<FileDialogOk&>::bind<SaveFromOpen, &SaveFromOpen::on_ok>(nested));
    require(open.show_dialog(window) == DialogResult::ok && nested.result == DialogResult::ok &&
                save.file_name.read() == "/tmp/copy.txt" && open.file_name.read() == "/tmp/source.txt",
            "a save dialog shown from an open dialog's file_ok is answered and given the extension");
    require(identities.size() == 2U && identities[0] != identities[1] && deepest == 1U &&
                services.snapshot().modal_depth == 0U,
            "the two dialogs' requests have their own identities, one on the modal stack at a time");
}

// The filter the dialog starts on, the one the user ended on, and the
// extension that filter gives a name without one.
void filter_index_and_extension() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("save.filter"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    SaveFileDialog dialog;
    dialog.filter = "Text|*.txt|Markdown|*.md;*.markdown|All files|*.*";
    dialog.default_ext = "log";
    dialog.filter_index = 2U;
    HostPathDialogResult answer = accepted("/tmp/notes");
    answer.chosen_filter = 1U;
    script.answers.push_back(answer);
    require(dialog.show_dialog(window) == DialogResult::ok, "the dialog is accepted");
    const HostSaveFileDialogRequest* const sent = save_request(script.requests[0]);
    require(sent != nullptr && (*sent).initial_filter == 2U && dialog.filter_index.read() == 1U &&
                dialog.file_name.read() == "/tmp/notes.md",
            "the chosen filter's first extension is added and its index taken");

    answer.chosen_filter = 2U;
    script.answers.push_back(answer);
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/tmp/notes.log" &&
                dialog.filter_index.read() == 2U,
            "a filter of any file adds the default extension");

    script.answers.push_back(accepted("/tmp/notes"));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/tmp/notes.log" &&
                dialog.filter_index.read() == 2U,
            "a host that does not tell the filter adds the default extension and keeps the index");

    answer.chosen_filter = 0U;
    script.answers.push_back(answer);
    dialog.add_extension = false;
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_name.read() == "/tmp/notes" &&
                dialog.filter_index.read() == 0U,
            "without add_extension the name is kept as chosen");
    dialog.reset();
    require(dialog.filter_index.read() == 0U, "reset returns to the first filter");
}

} // namespace

int main() {
    settings();
    without_host();
    shown_through_host();
    filter_index_and_extension();
    ended_and_nested();
    std::cout << "SaveFileDialog: settings, refusals, default extension, host requests and answers, the filter index, file_ok, "
                 "destruction and nesting under an open dialog passed\n";
    return 0;
}
