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

// A window whose closing handler refuses while /tmp/gf_term_refuse exists,
// as Cancel on an unsaved-changes prompt does, and accepts otherwise (Mac
// handoff 2026-11-09, item 2e). It is started with `open -n -a` and ended by
// Cmd-Q (the quit Apple Event), a logout or a restart, which a person or the
// coordinator triggers; it never ends itself. Each event appends one line
// with a timestamp to /tmp/gf_term.log: start, closing asked (with its
// reason), refused or accepted, closed, and the process exit (an atexit
// handler, since a quit ends the process inside -[NSApplication terminate:]).

namespace {
using namespace gui_forms;

constexpr const char* log_path = "/tmp/gf_term.log";
constexpr const char* refuse_flag = "/tmp/gf_term_refuse";

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
    }
    return "unknown";
}

void log_process_exit() { append_line("process exit"); }

struct RefuseWhileFlagged final {
    void operator()(HostCloseRequest& request) const {
        const std::string asked = std::string("closing asked reason=") + reason_name(request.reason);
        append_line(asked.c_str());
        struct stat flag{};
        const bool refuse = stat(refuse_flag, &flag) == 0;
        request.cancel = refuse;
        append_line(refuse ? "refused (flag file present)" : "accepted");
    }
};

struct LogClosed final {
    void operator()() const { append_line("closed"); }
};

} // namespace

int main() {
    std::atexit(&log_process_exit);
    append_line("start");
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) {
        append_line("application refused");
        return 1;
    }
    const std::shared_ptr<Control> root = make_control<Control>(StableId("termination.fixture.root"));
    std::unique_ptr<Window> window = std::make_unique<Window>(root, gui_forms::Size{360.0, 200.0});
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms termination fixture";
    options.initial_size = {360.0, 200.0};
    options.closing = RefuseWhileFlagged{};
    options.closed = LogClosed{};
    const ApplicationResult result = (*created.application).run(std::move(window), std::move(options));
    append_line(result.accepted() ? "run returned accepted" : "run returned refused");
    return result.accepted() ? 0 : 1;
}
