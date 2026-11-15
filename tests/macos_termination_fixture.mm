#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/window.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
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
// handoff 2026-11-09, item 2e; cp29 row 14; cp33). Its files live in the
// user's private temporary directory, TMP below (getconf
// DARWIN_USER_TEMP_DIR). The primary window, which ends the run, refuses
// while TMP/gf_term_refuse exists, and a
// secondary window (a floating document owned by it) while
// TMP/gf_term_secondary_refuse exists. It is started with `open -n -a` and
// ended by Cmd-Q, the quit Apple Event, a logout or a restart; it never ends
// itself. Each event appends one line with a timestamp to TMP/gf_term.log:
// start, each window's ready, closing asked (with its reason), refused or
// accepted and closed, the run's return, the saved state it found, the exit
// code and the process exit (an atexit handler, which a session end reaches
// from inside -[NSApplication terminate:]).
//
// Cmd-Q without an Automation grant (cp33): while the fixture runs it polls
// for TMP/gf_term_cmd_q every 100 ms; on finding it, it removes it and
// posts a Command-Q key-down and key-up to its own NSApp, which goes through
// the application menu's Quit item and -[NSApplication terminate:] as a
// typed Cmd-Q does. An event posted to the process's own queue needs no
// privacy grant.
//
// No lost state: an accepting closing handler saves its window's state, a
// line "saved reason=<reason>", to TMP/gf_term_<window>.state, and after
// the run returns the fixture logs what each state file holds. A refusing
// handler saves nothing.
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
// closes, the run returns (a plain quit cancels AppKit's termination, so
// main's code after the run runs) and the process exits with status 0.
// tests/run_macos_termination_fixture.sh drives the Cmd-Q case; logout and
// restart are checked by hand.

namespace {
using namespace gui_forms;

// The fixture's files, in the user's private temporary directory
// (confstr(_CS_DARWIN_USER_TEMP_DIR): one per user, mode 0700), which the
// driver script finds with `getconf DARWIN_USER_TEMP_DIR`. Fixed names in
// the shared /tmp let another user plant a link or a directory there
// (B-gf-tests-07-F4). make_fixture_paths fills them at the start of main;
// they are plain arrays, so the atexit handler still reads them.
struct FixturePaths final {
    char log[PATH_MAX]{};
    char refuse_flag[PATH_MAX]{};
    char secondary_refuse_flag[PATH_MAX]{};
    char cmd_q_trigger[PATH_MAX]{};
    char main_state[PATH_MAX]{};
    char secondary_state[PATH_MAX]{};
};
FixturePaths fixture_paths{};

// Writes `directory`/`name` into `out`; false when it does not fit.
bool join_path(char (&out)[PATH_MAX], const char* const directory, const char* const name) {
    const int written = std::snprintf(out, sizeof out, "%s/%s", directory, name);
    const bool fits = written > 0 && static_cast<std::size_t>(written) < sizeof out;
    return fits;
}

// Fills fixture_paths from the user's temporary directory; false when it
// cannot be read or a path does not fit.
bool make_fixture_paths() {
    char directory[PATH_MAX]{};
    const std::size_t needed = confstr(_CS_DARWIN_USER_TEMP_DIR, directory, sizeof directory);
    if (needed < 2U || needed > sizeof directory) return false;
    // needed counts the terminating zero; the directory ends with a slash.
    if (directory[needed - 2U] == '/') directory[needed - 2U] = '\0';
    const bool made = join_path(fixture_paths.log, directory, "gf_term.log") &&
                      join_path(fixture_paths.refuse_flag, directory, "gf_term_refuse") &&
                      join_path(fixture_paths.secondary_refuse_flag, directory, "gf_term_secondary_refuse") &&
                      join_path(fixture_paths.cmd_q_trigger, directory, "gf_term_cmd_q") &&
                      join_path(fixture_paths.main_state, directory, "gf_term_main.state") &&
                      join_path(fixture_paths.secondary_state, directory, "gf_term_secondary.state");
    return made;
}

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
    std::FILE* const file = std::fopen(fixture_paths.log, "a");
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

// Replaces the state file at `path` with one line "saved reason=<reason>".
// Returns whether the line reached the file.
bool save_state(const char* const path, const HostCloseReason reason) {
    std::FILE* const file = std::fopen(path, "w");
    if (file == nullptr) return false;
    const int written = std::fprintf(file, "saved reason=%s\n", reason_name(reason));
    const int closed = std::fclose(file);
    return written > 0 && closed == 0;
}

// Logs "state <window>=<first line of path>", or "=missing" when the file
// cannot be read.
void log_state(const char* const window, const char* const path) {
    char line[128]{};
    std::FILE* const file = std::fopen(path, "r");
    bool read{false};
    if (file != nullptr) {
        read = std::fgets(line, sizeof line, file) != nullptr;
        std::fclose(file);
    }
    std::string text = std::string("state ") + window + "=";
    if (read) {
        std::string content(line);
        if (!content.empty() && content.back() == '\n') content.pop_back();
        text += content;
    } else {
        text += "missing";
    }
    append_line(text.c_str());
}

// Posts Command-Q, down then up, to the process's own event queue, for the
// key window when there is one. The application menu's Quit item takes the
// key-down as its key equivalent.
void post_command_q() {
    NSWindow* const key = [NSApp keyWindow];
    const NSInteger window_number = key == nil ? 0 : [key windowNumber];
    const NSTimeInterval now = [[NSProcessInfo processInfo] systemUptime];
    // kVK_ANSI_Q: the Q key's virtual key code on an ANSI layout.
    const unsigned short q_key_code = 12U;
    NSEvent* const down = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                           location:NSZeroPoint
                                      modifierFlags:NSEventModifierFlagCommand
                                          timestamp:now
                                       windowNumber:window_number
                                            context:nil
                                         characters:@"q"
                        charactersIgnoringModifiers:@"q"
                                          isARepeat:NO
                                            keyCode:q_key_code];
    NSEvent* const up = [NSEvent keyEventWithType:NSEventTypeKeyUp
                                         location:NSZeroPoint
                                    modifierFlags:NSEventModifierFlagCommand
                                        timestamp:now
                                     windowNumber:window_number
                                          context:nil
                                       characters:@"q"
                      charactersIgnoringModifiers:@"q"
                                        isARepeat:NO
                                          keyCode:q_key_code];
    if (down == nil || up == nil) {
        append_line("cmd-q not posted");
        return;
    }
    [NSApp postEvent:down atStart:NO];
    [NSApp postEvent:up atStart:NO];
    append_line(key == nil ? "cmd-q posted (no key window)" : "cmd-q posted");
}

// The main-queue timer's handler (dispatch_source_set_event_handler_f): a
// Cmd-Q trigger file found is removed, then Command-Q is posted. Context is
// unused.
void poll_cmd_q_trigger(void* const context) {
    static_cast<void>(context);
    if (unlink(fixture_paths.cmd_q_trigger) != 0) return;
    post_command_q();
}

// One window's closing handler: `window` names it in the log, it refuses
// while the file at `flag` exists, and on accepting it saves its state to
// `state`. All three outlive the run (literals and fixture_paths).
struct RefuseWhileFlagged final {
    const char* window{};
    const char* flag{};
    const char* state{};

    void operator()(HostCloseRequest& request) const {
        const std::string asked = std::string(window) + " closing asked reason=" + reason_name(request.reason);
        append_line(asked.c_str());
        struct stat present{};
        const bool refuse = stat(flag, &present) == 0;
        request.cancel = refuse;
        const std::string answer = std::string(window) + (refuse ? " refused (flag file present)" : " accepted");
        append_line(answer.c_str());
        if (refuse) return;
        if (!save_state(state, request.reason)) {
            const std::string failed = std::string(window) + " state not saved";
            append_line(failed.c_str());
        }
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
                              const char* const flag, const char* const state, const char* const title) {
    ApplicationWindow entry{};
    entry.stable_id = stable_id;
    entry.owner_id = owner_id;
    const std::shared_ptr<Control> root = make_control<Control>(StableId(std::string(stable_id) + ".root"));
    entry.model = std::make_unique<Window>(root, gui_forms::Size{360.0, 200.0});
    entry.options.title = title;
    entry.options.initial_size = {360.0, 200.0};
    entry.options.ready = LogReady{name};
    entry.options.closing = RefuseWhileFlagged{name, flag, state};
    entry.options.closed = LogClosed{name};
    return entry;
}

} // namespace

int main() {
    if (!make_fixture_paths()) return 1;
    std::atexit(&log_process_exit);
    unlink(fixture_paths.main_state);
    unlink(fixture_paths.secondary_state);
    unlink(fixture_paths.cmd_q_trigger);
    append_line("start");
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) {
        append_line("application refused");
        return 1;
    }
    // The Cmd-Q trigger's poll: a main-queue timer every 100 ms, with 10 ms
    // leeway, cancelled after the run. The queue is drained in AppKit's
    // run loop.
    dispatch_source_t const poll = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, 0U, dispatch_get_main_queue());
    if (poll == nullptr) {
        append_line("cmd-q poll not created");
        return 1;
    }
    const std::uint64_t poll_interval_ns = 100U * NSEC_PER_MSEC;
    dispatch_source_set_timer(poll, dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(poll_interval_ns)),
                              poll_interval_ns, 10U * NSEC_PER_MSEC);
    dispatch_source_set_event_handler_f(poll, &poll_cmd_q_trigger);
    dispatch_resume(poll);
    std::vector<ApplicationWindow> windows{};
    windows.reserve(2U);
    windows.push_back(make_window("termination.fixture.main", "", "main", fixture_paths.refuse_flag, fixture_paths.main_state,
                                  "GUI.Forms termination fixture"));
    ApplicationWindow secondary = make_window("termination.fixture.secondary", "termination.fixture.main",
                                              "secondary", fixture_paths.secondary_refuse_flag,
                                              fixture_paths.secondary_state,
                                              "GUI.Forms termination fixture: secondary");
    secondary.floating = HostWindowFloating::document;
    windows.push_back(std::move(secondary));
    const ApplicationResult result = (*created.application).run(std::move(windows));
    dispatch_source_cancel(poll);
    append_line(result.accepted() ? "run returned accepted" : "run returned refused");
    log_state("main", fixture_paths.main_state);
    log_state("secondary", fixture_paths.secondary_state);
    const int exit_code = result.accepted() ? 0 : 1;
    const std::string exit_line = "exit code " + std::to_string(exit_code);
    append_line(exit_line.c_str());
    return exit_code;
}
