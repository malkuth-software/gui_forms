// OpenFileDialog (stage W): the Windows Forms filter syntax, the name part
// of a path, the settings and their refusals, and showing the dialog through
// the headless host: the request it sends, accepted, cancelled and refused
// answers, file_ok with cancel, disposal and destruction from file_ok,
// reentrancy, the wrong thread, reset and the interactions it declares.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstddef>
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

HostPathDialogResult accepted(std::vector<std::string> paths) {
    HostPathDialogResult result{HostDialogOutcome::accepted, std::move(paths)};
    return result;
}

const HostOpenFileDialogRequest* open_request(const HostDialogRequest& request) {
    const HostOpenFileDialogRequest* const result = std::get_if<HostOpenFileDialogRequest>(&request.payload);
    return result;
}

void filter_syntax() {
    const FileDialogFilterParse none = parse_file_dialog_filter("");
    require(none.error == FileDialogFilterError::none && none.filters.empty(), "an empty filter is no filter");

    const FileDialogFilterParse two = parse_file_dialog_filter("Text files|*.txt; *.log|All files|*.*");
    require(two.error == FileDialogFilterError::none && two.filters.size() == 2U, "two pairs give two filters");
    require(two.filters[0].label == "Text files" && two.filters[0].extensions.size() == 2U &&
                two.filters[0].extensions[0] == "txt" && two.filters[0].extensions[1] == "log",
            "patterns become extensions in order, spaces around them ignored");
    require(two.filters[1].label == "All files" && two.filters[1].extensions.size() == 1U &&
                two.filters[1].extensions[0] == "*",
            "*.* is any file");

    const FileDialogFilterParse star = parse_file_dialog_filter("|*");
    require(star.error == FileDialogFilterError::none && star.filters.size() == 1U && star.filters[0].label.empty() &&
                star.filters[0].extensions[0] == "*",
            "an empty label is allowed and * is any file");
    const FileDialogFilterParse dotted = parse_file_dialog_filter("Archives|*.tar.gz");
    require(dotted.error == FileDialogFilterError::none && dotted.filters[0].extensions[0] == "tar.gz",
            "a multi-dotted extension is kept whole");

    require(parse_file_dialog_filter("Text files").error == FileDialogFilterError::unpaired_label,
            "a label alone is refused");
    require(parse_file_dialog_filter("Text|*.txt|").error == FileDialogFilterError::unpaired_label,
            "a trailing separator leaves a label without patterns");
    require(parse_file_dialog_filter("Text|").error == FileDialogFilterError::empty_pattern,
            "an empty pattern list is refused");
    require(parse_file_dialog_filter("Text|*.txt;;*.log").error == FileDialogFilterError::empty_pattern,
            "an empty pattern is refused");
    require(parse_file_dialog_filter("Data|data*.csv").error == FileDialogFilterError::unsupported_pattern,
            "a pattern other than *.extension is refused");
    require(parse_file_dialog_filter("Data|*.c?v").error == FileDialogFilterError::unsupported_pattern,
            "a wildcard inside an extension is refused");
    require(parse_file_dialog_filter("Data|*.").error == FileDialogFilterError::unsupported_pattern,
            "a pattern without an extension is refused");
    require(parse_file_dialog_filter(std::string("Bad \xFF|*.txt")).error == FileDialogFilterError::invalid_text,
            "invalid UTF-8 is refused");

    std::string many{};
    for (std::size_t index = 0; index <= HostServices::maximum_dialog_filters; ++index) {
        if (!many.empty()) many += "|";
        many += "F|*.f";
    }
    require(parse_file_dialog_filter(many).error == FileDialogFilterError::too_many,
            "more filters than the host takes are refused");
    std::string patterns{"P|*.a"};
    for (std::size_t index = 0; index < HostServices::maximum_dialog_extensions; ++index) patterns += ";*.a";
    require(parse_file_dialog_filter(patterns).error == FileDialogFilterError::too_many,
            "more patterns in one filter than the host takes are refused");
}

void filter_matching() {
    const HostFileDialogFilter images{"Images", {"png", "JPG"}};
    require(file_dialog_filter_matches(images, "photo.png"), "a lower-case extension matches");
    require(file_dialog_filter_matches(images, "photo.PNG"), "a pattern matches an upper-case extension");
    require(file_dialog_filter_matches(images, "photo.Png"), "a pattern matches a mixed-case extension");
    require(file_dialog_filter_matches(images, "photo.jpg"), "an upper-case pattern matches a lower-case name");
    require(!file_dialog_filter_matches(images, "photo.gif"), "another extension does not match");
    require(!file_dialog_filter_matches(images, "photopng"), "an extension needs its dot");
    require(!file_dialog_filter_matches(images, ".png"), "a name that is only the extension does not match");
    require(!file_dialog_filter_matches(images, "photo"), "a name without an extension does not match");
    const HostFileDialogFilter archives{"Archives", {"tar.gz"}};
    require(file_dialog_filter_matches(archives, "backup.TAR.GZ"), "a multi-dotted extension matches whole");
    require(!file_dialog_filter_matches(archives, "backup.gz"), "a part of a multi-dotted extension does not");
    const HostFileDialogFilter any{"All files", {"*"}};
    require(file_dialog_filter_matches(any, "README"), "* matches any file");
    const HostFileDialogFilter empty{"Nothing", {}};
    require(file_dialog_filter_matches(empty, "README"), "a filter without extensions matches any file");
    const HostFileDialogFilter written{"Written", {"*.txt", ".log"}};
    require(file_dialog_filter_matches(written, "a.TXT") && file_dialog_filter_matches(written, "b.log"),
            "a hand-built extension keeps working with its pattern prefix");
    const FileDialogFilterParse parsed = parse_file_dialog_filter("PNG images|*.PNG");
    require(parsed.error == FileDialogFilterError::none && file_dialog_filter_matches(parsed.filters[0], "a.png"),
            "a parsed upper-case pattern matches a lower-case name");
}

void name_part() {
    require(file_dialog_name_part("/tmp/alpha.txt") == "alpha.txt", "the name follows the last separator");
    require(file_dialog_name_part("alpha.txt") == "alpha.txt", "a bare name is its own name");
    require(file_dialog_name_part("/tmp/").empty(), "a path ending in a separator has no name");
    require(file_dialog_name_part("/tmp/\xC3\xA9t\xC3\xA9.txt") == "\xC3\xA9t\xC3\xA9.txt",
            "a UTF-8 name is kept byte for byte");
#if defined(_WIN32)
    const std::string windows_path = std::string("C:") + '\\' + "data" + '\\' + "beta.log";
    require(file_dialog_name_part(windows_path) == "beta.log", "Windows paths split at a backslash");
#endif
}

void settings() {
    OpenFileDialog dialog;
    require(dialog.title.read().empty() && dialog.filter.read().empty() && dialog.filters().empty() && !dialog.multiselect.read() &&
                dialog.file_name.read().empty() && dialog.file_names().empty() && dialog.status().accepted(),
            "a new dialog has the defaults");
    dialog.title = "Open a recording";
    dialog.initial_directory = "/srv/recordings";
    dialog.filter = "Recordings|*.wav;*.flac";
    dialog.multiselect = true;
    dialog.file_name = "/srv/recordings/first.wav";
    require(dialog.filters().size() == 1U && dialog.filters()[0].extensions.size() == 2U,
            "the filter is parsed when set");
    require(dialog.file_name.read() == "/srv/recordings/first.wav" && dialog.file_names().size() == 1U &&
                dialog.safe_file_name() == "first.wav",
            "a set name is the one chosen name");

    bool refused = false;
    try {
        dialog.filter = "Recordings|rec*.wav";
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.filter.read() == "Recordings|*.wav;*.flac" && dialog.filters().size() == 1U,
            "a bad filter throws and keeps the old one");
    refused = false;
    try {
        dialog.title = std::string("Bad \xFF");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.title.read() == "Open a recording", "an invalid title throws and keeps the old one");
    refused = false;
    try {
        dialog.file_name = std::string("a\0b", 3U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.file_name.read() == "/srv/recordings/first.wav", "a name holding NUL is refused");

    dialog.file_name = "";
    require(dialog.file_names().empty() && dialog.safe_file_names().empty(), "an empty name chooses nothing");
    dialog.reset();
    require(dialog.title.read().empty() && dialog.initial_directory.read().empty() && dialog.filter.read().empty() &&
                dialog.filters().empty() && !dialog.multiselect.read() && dialog.file_names().empty(),
            "reset restores the defaults");

    const std::vector<InteractionDeclaration> interactions = dialog.interactions();
    require(!interactions.empty() && interactions.back().name == "file_ok" &&
                interactions.back().payload == "FileDialogOk" && interactions.back().mutable_payload,
            "the dialog declares file_ok with a mutable payload");
}

void without_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("open.unhosted"));
    Window window(root, {320.0, 200.0});
    OpenFileDialog dialog;
    dialog.file_name = "/tmp/kept.txt";
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::unsupported && dialog.file_name.read() == "/tmp/kept.txt",
            "without a host the dialog is not shown and the names stay");
}

// A file_ok handler that records what it saw and cancels the first `refusals`
// acceptances.
struct OkLog final {
    std::vector<std::vector<std::string>> seen{};
    int refusals{};

    void on_ok(FileDialogOk& ok) {
        seen.emplace_back(ok.file_names.begin(), ok.file_names.end());
        if (refusals > 0) {
            --refusals;
            ok.cancel = true;
        }
    }
};

void shown_through_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("open.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    OpenFileDialog dialog;
    dialog.title = "Open a log";
    dialog.initial_directory = "/var/log";
    dialog.filter = "Logs|*.log|All files|*.*";
    dialog.file_name = "system.log";
    OkLog log;
    SubscriptionToken ok_token =
        dialog.file_ok().subscribe(Delegate<FileDialogOk&>::bind<OkLog, &OkLog::on_ok>(log));

    script.answers.push_back(accepted({"/var/log/kernel.log"}));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.status().accepted(),
            "an accepted dialog returns ok");
    require(script.requests.size() == 1U && script.requests[0].request_id != 0U &&
                script.requests[0].owner_id == "open.root",
            "the request is owned by the window's root");
    const HostOpenFileDialogRequest* const sent = open_request(script.requests[0]);
    require(sent != nullptr && (*sent).title == "Open a log" && (*sent).initial_directory == "/var/log" &&
                (*sent).suggested_name == "system.log" && (*sent).filters.size() == 2U &&
                (*sent).filters[1].extensions[0] == "*" && !(*sent).allow_multiple,
            "the request carries the settings");
    require(dialog.file_name.read() == "/var/log/kernel.log" && dialog.safe_file_name() == "kernel.log" &&
                log.seen.size() == 1U && log.seen[0].size() == 1U && log.seen[0][0] == "/var/log/kernel.log",
            "file_ok sees the chosen path and the dialog takes it");

    dialog.multiselect = true;
    script.answers.push_back(accepted({"/var/log/a.log", "/var/log/b.log"}));
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_names().size() == 2U &&
                dialog.safe_file_names()[1] == "b.log",
            "several chosen paths are kept in order");
    const HostOpenFileDialogRequest* const multiple = open_request(script.requests[1]);
    require(multiple != nullptr && (*multiple).allow_multiple && (*multiple).suggested_name == "/var/log/kernel.log" &&
                script.requests[1].request_id != script.requests[0].request_id,
            "each request has its own identity and suggests the first chosen path");

    require(dialog.show_dialog(window) == DialogResult::cancel && dialog.file_names().size() == 2U &&
                log.seen.size() == 2U,
            "a cancelled dialog keeps the names and raises no file_ok");

    log.refusals = 1;
    script.answers.push_back(accepted({"/var/log/wrong.log"}));
    script.answers.push_back(accepted({"/var/log/right.log"}));
    const std::size_t before = script.requests.size();
    require(dialog.show_dialog(window) == DialogResult::ok && dialog.file_names().size() == 1U &&
                dialog.file_name.read() == "/var/log/right.log",
            "a cancelled file_ok shows the dialog again and the second choice is taken");
    require(script.requests.size() == before + 2U && log.seen.size() == 4U && log.seen[2][0] == "/var/log/wrong.log",
            "the dialog was shown twice and file_ok raised for each acceptance");

    log.refusals = 1;
    script.answers.push_back(accepted({"/var/log/refused.log"}));
    require(dialog.show_dialog(window) == DialogResult::cancel && dialog.file_name.read() == "/var/log/right.log",
            "cancelling file_ok and then the dialog keeps the old names");

    script.error = HostServiceError::backend_failure;
    require(dialog.show_dialog(window) == DialogResult::none &&
                dialog.status().error == HostServiceError::backend_failure && dialog.file_name.read() == "/var/log/right.log",
            "a host failure returns none with the host's answer");
    script.error = HostServiceError::none;
    require(services.snapshot().modal_depth == 0U, "no dialog is left on the modal stack");
}

// file_ok handlers that end the dialog: dispose it, destroy it, or show it
// again while it is showing.
struct DisposeOnOk final {
    OpenFileDialog* dialog{};
    void on_ok(FileDialogOk&) { (*dialog).dispose(); }
};

struct DestroyOnOk final {
    std::shared_ptr<OpenFileDialog>* owner{};
    void on_ok(FileDialogOk&) { (*owner).reset(); }
};

struct ReenterOnOk final {
    OpenFileDialog* dialog{};
    Window* window{};
    bool refused{};
    void on_ok(FileDialogOk&) {
        try {
            static_cast<void>((*dialog).show_dialog(*window));
        } catch (const std::logic_error&) {
            refused = true;
        }
    }
};

struct ShowFromOtherThread final {
    OpenFileDialog* dialog{};
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

void ended_from_file_ok() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("open.ended"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    OpenFileDialog disposed;
    DisposeOnOk disposer{&disposed};
    SubscriptionToken dispose_token =
        disposed.file_ok().subscribe(Delegate<FileDialogOk&>::bind<DisposeOnOk, &DisposeOnOk::on_ok>(disposer));
    script.answers.push_back(accepted({"/tmp/a.txt"}));
    require(disposed.show_dialog(window) == DialogResult::cancel && disposed.file_names().empty(),
            "a dialog disposed by file_ok returns cancel and takes nothing");
    bool refused = false;
    try {
        static_cast<void>(disposed.show_dialog(window));
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a disposed dialog cannot be shown");

    std::shared_ptr<OpenFileDialog> destroyed = std::make_shared<OpenFileDialog>();
    OpenFileDialog* const destroyed_address = destroyed.get();
    DestroyOnOk destroyer{&destroyed};
    SubscriptionToken destroy_token = (*destroyed).file_ok().subscribe(
        Delegate<FileDialogOk&>::bind<DestroyOnOk, &DestroyOnOk::on_ok>(destroyer));
    script.answers.push_back(accepted({"/tmp/b.txt"}));
    require((*destroyed_address).show_dialog(window) == DialogResult::cancel && !destroyed,
            "a dialog destroyed by file_ok returns cancel without touching itself");

    OpenFileDialog reentered;
    ReenterOnOk reenter{&reentered, &window, false};
    SubscriptionToken reenter_token =
        reentered.file_ok().subscribe(Delegate<FileDialogOk&>::bind<ReenterOnOk, &ReenterOnOk::on_ok>(reenter));
    script.answers.push_back(accepted({"/tmp/c.txt"}));
    require(reentered.show_dialog(window) == DialogResult::ok && reenter.refused &&
                reentered.file_name.read() == "/tmp/c.txt",
            "showing a dialog that is already showing is refused");
    reenter_token.disconnect();
    script.answers.push_back(accepted({"/tmp/d.txt"}));
    require(reentered.show_dialog(window) == DialogResult::ok && reentered.file_name.read() == "/tmp/d.txt",
            "the dialog shows again once the first call has returned");

    bool wrong_thread = false;
    std::thread other(ShowFromOtherThread{&reentered, &window, &wrong_thread});
    other.join();
    require(wrong_thread && reentered.file_name.read() == "/tmp/d.txt", "showing off the UI thread is refused");
}

// The filter the dialog starts on, and the one the user ended on.
void filter_index() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("open.filter"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    DialogScript script;
    services.set_dialog_handler(ScriptedDialogs{&script});

    OpenFileDialog dialog;
    require(dialog.filter_index.read() == 0U, "the first filter is the default");
    dialog.filter = "Logs|*.log|Text|*.txt|All files|*.*";
    dialog.filter_index = 1U;
    HostPathDialogResult answer = accepted({"/tmp/a.txt"});
    answer.chosen_filter = 2U;
    script.answers.push_back(answer);
    require(dialog.show_dialog(window) == DialogResult::ok, "the dialog is accepted");
    const HostOpenFileDialogRequest* const first = open_request(script.requests[0]);
    require(first != nullptr && (*first).initial_filter == 1U && dialog.filter_index.read() == 2U,
            "the request starts on the set filter and the dialog takes the one the user ended on");

    dialog.filter_index = 7U;
    script.answers.push_back(accepted({"/tmp/b.log"}));
    require(dialog.show_dialog(window) == DialogResult::ok, "the dialog is accepted again");
    const HostOpenFileDialogRequest* const second = open_request(script.requests[1]);
    require(second != nullptr && (*second).initial_filter == 0U && dialog.filter_index.read() == 7U,
            "an index past the filters starts on the first, and a host that does not tell keeps the index");

    answer.chosen_filter = 3U;
    script.answers.push_back(answer);
    require(dialog.show_dialog(window) == DialogResult::none && !dialog.status().accepted() &&
                dialog.filter_index.read() == 7U && dialog.file_name.read() == "/tmp/b.log",
            "a host naming a filter the request does not have is refused and changes nothing");

    require(dialog.show_dialog(window) == DialogResult::cancel && dialog.filter_index.read() == 7U,
            "a cancelled dialog keeps the index");
    dialog.reset();
    require(dialog.filter_index.read() == 0U, "reset returns to the first filter");
}

} // namespace

int main() {
    filter_syntax();
    filter_matching();
    name_part();
    settings();
    without_host();
    shown_through_host();
    filter_index();
    ended_from_file_ok();
    std::cout << "OpenFileDialog: filter syntax, filter matching, name part, settings, refusals, host requests and answers, "
                 "the filter index, file_ok, disposal, destruction, reentrancy and threads passed\n";
    return 0;
}
