#import <Foundation/Foundation.h>

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>
#include <vector>

// Two windows whose closing handlers refuse while a flag file exists, as
// Cancel on an unsaved-changes prompt does, and accept otherwise (Mac
// handoff 2026-11-09, item 2e; cp29 row 14): the primary window, which ends
// the run, refuses while /tmp/gf_term_refuse exists, and a secondary window
// (a floating document owned by it) while /tmp/gf_term_secondary_refuse
// exists. It is started with `open -n -a` and ended by Cmd-Q (the quit
// Apple Event), a logout or a restart; it never ends itself. Each event
// appends one line with a timestamp to /tmp/gf_term.log: start, each
// window's ready, closing asked (with its reason), refused or accepted and
// closed, and the process exit (an atexit handler, since a quit ends the
// process inside -[NSApplication terminate:]).
//
// A quit asks every window that does not end the run first, then the
// primary one (audit P2), and the first refusal cancels it. With only the
// secondary flag present:
//   - Cmd-Q logs "secondary closing asked reason=quit" and "secondary
//     refused"; the primary window is not asked and the process keeps
//     running;
//   - a logout or restart logs "secondary closing asked
//     reason=session_end" and "secondary refused", and the system reports
//     that the application cancelled it.
// With no flag, both windows accept (the secondary first), the primary
// closes and the process exits. tests/run_macos_termination_fixture.sh
// drives the Cmd-Q case with the quit Apple Event; logout and restart are
// checked by hand.

namespace {
using namespace gui_forms;

constexpr const char* log_path = "/tmp/gf_term.log";
constexpr const char* refuse_flag = "/tmp/gf_term_refuse";
constexpr const char* secondary_refuse_flag = "/tmp/gf_term_secondary_refuse";

// Appends "<UTC time>.<ms> pid=<pid> <event>" to the log, opening and
// closing it each time so every line is on disk at once. UTC, not local
// time: lsan.supp suppresses leaks under the local-time call, so no first-party frame
// may call it (test_ci_build).
void append_line(const char* const event) {
    timeval now{};
    gettimeofday(&now, nullptr);
    const std::time_t seconds = now.tv_sec;
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%SZ", &utc);
    std::FILE* const file = std::fopen(log_path, "a");
    if (file == nullptr) return;
    std::fprintf(file, "%s.%03d pid=%d %s\n", stamp, static_cast<int>(now.tv_usec / 1000), static_cast<int>(getpid()),
                 event);
    std::fclose(file);
}

const char* reason_name(const HostCloseReason reason) {
    switch (reason) {
    case HostCloseReason::user:
        return "user";
    case HostCloseReason::application:
        return "application";
    case HostCloseReason::session_end:
        return "session_end";
    case HostCloseReason::test:
        return "test";
    case HostCloseReason::quit:
        return "quit";
    }
    return "unknown";
}

void log_process_exit() { append_line("process exit"); }

// One window's closing handler: `window` names it in the log, and it
// refuses while the file at `flag` exists. Both are string literals.
struct RefuseWhileFlagged final {
    const char* window{};
    const char* flag{};

    void operator()(HostCloseRequest& request) const {
        const std::string asked = std::string(window) + " closing asked reason=" + reason_name(request.reason);
        append_line(asked.c_str());
        struct stat present{};
        const bool refuse = stat(flag, &present) == 0;
        request.cancel = refuse;
        const std::string answer = std::string(window) + (refuse ? " refused (flag file present)" : " accepted");
        append_line(answer.c_str());
    }
};

struct LogReady final {
    const char* window{};

    void operator()(Window& model, ApplicationWindowHandle handle) const {
        static_cast<void>(model);
        static_cast<void>(handle);
        const std::string ready = std::string(window) + " ready";
        append_line(ready.c_str());
    }
};

struct LogClosed final {
    const char* window{};

    void operator()() const {
        const std::string closed = std::string(window) + " closed";
        append_line(closed.c_str());
    }
};

ApplicationWindow make_window(const char* const stable_id, const char* const owner_id, const char* const name,
                              const char* const flag, const char* const title) {
    ApplicationWindow entry{};
    entry.stable_id = stable_id;
    entry.owner_id = owner_id;
    const std::shared_ptr<Control> root = make_control<Control>(StableId(std::string(stable_id) + ".root"));
    entry.model = std::make_unique<Window>(root, gui_forms::Size{360.0, 200.0});
    entry.options.title = title;
    entry.options.initial_size = {360.0, 200.0};
    entry.options.ready = LogReady{name};
    entry.options.closing = RefuseWhileFlagged{name, flag};
    entry.options.closed = LogClosed{name};
    return entry;
}

} // namespace

int main() {
    std::atexit(&log_process_exit);
    append_line("start");
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) {
        append_line("application refused");
        return 1;
    }
    std::vector<ApplicationWindow> windows{};
    windows.reserve(2U);
    windows.push_back(make_window("termination.fixture.main", "", "main", refuse_flag,
                                  "GUI.Forms termination fixture"));
    ApplicationWindow secondary = make_window("termination.fixture.secondary", "termination.fixture.main",
                                              "secondary", secondary_refuse_flag,
                                              "GUI.Forms termination fixture: secondary");
    secondary.floating = HostWindowFloating::document;
    windows.push_back(std::move(secondary));
    const ApplicationResult result = (*created.application).run(std::move(windows));
    append_line(result.accepted() ? "run returned accepted" : "run returned refused");
    return result.accepted() ? 0 : 1;
}
