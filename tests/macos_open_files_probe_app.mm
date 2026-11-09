#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// The application tests/run_macos_open_files_probe.sh drives with `open -a`
// (Mac handoff 2026-11-09, item 2d): LaunchServices sends it the
// open-documents Apple Event at launch and again while it runs, and AppKit
// calls application:openURLs:, which the host hands to
// Application::request_open_files (branch claude/host-file-gaps).
//
// It subscribes late, 1.5 s after its window is ready, and first takes what
// was queued before that ("queued: ..."), so the launch request must have
// been kept for it. Later requests arrive through open_files_requested
// ("event: ..."). A request naming quit.txt closes the window. Every line
// goes to handoff_open_files/log.txt beside the bundle, which the script
// reads.

namespace {
using namespace gui_forms;

struct OpenFilesProbe final {
    handoff_probe::Watchdog* watchdog{};
    Application* application{};
    ApplicationWindowHandle handle{};
    std::FILE* log{};
    SubscriptionToken subscription{};
};

void write_line(OpenFilesProbe& probe, const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    if (probe.log == nullptr) return;
    std::fprintf(probe.log, "%s\n", line.c_str());
    std::fflush(probe.log);
}

// Logs `paths` under `label` and closes the window when one is quit.txt.
void record_paths(OpenFilesProbe& probe, const char* const label, const std::vector<std::string>& paths) {
    std::string line(label);
    bool quit = false;
    for (const std::string& path : paths) {
        line += " " + path;
        const std::string::size_type slash = path.rfind('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1U);
        if (name == "quit.txt") quit = true;
    }
    write_line(probe, line);
    if (quit) {
        const HostServiceStatus closed = probe.handle.request_close();
        write_line(probe, closed.accepted() ? "close: accepted" : "close: refused");
    }
}

struct TakeRequestedFiles final {
    OpenFilesProbe* probe{};
    void operator()() const {
        const std::vector<std::string> taken = (*(*probe).application).take_open_files();
        record_paths(*probe, "event:", taken);
    }
};

void subscribe_late(void* const context) {
    OpenFilesProbe& probe = *static_cast<OpenFilesProbe*>(context);
    (*probe.watchdog).reached(2);
    const std::vector<std::string> queued = (*probe.application).take_open_files();
    record_paths(probe, "queued:", queued);
    probe.subscription = (*probe.application).open_files_requested().subscribe(TakeRequestedFiles{&probe});
    write_line(probe, "subscribed");
}

struct OpenFilesReady final {
    OpenFilesProbe* probe{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        (*probe).handle = handle;
        (*(*probe).watchdog).reached(1);
        write_line(*probe, "ready");
        handoff_probe::after(1.5, probe, &subscribe_late);
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(40);
    OpenFilesProbe probe{};
    probe.watchdog = &watchdog;
    const std::string directory = handoff_probe::beside_bundle("handoff_open_files");
    const std::string log_path = directory + "/log.txt";
    probe.log = std::fopen(log_path.c_str(), "w");
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) {
        write_line(probe, "application: refused");
        if (probe.log != nullptr) std::fclose(probe.log);
        return 1;
    }
    probe.application = created.application.get();
    const std::shared_ptr<Control> root = make_control<Control>(StableId("open.files.probe.root"));
    std::unique_ptr<Window> window = std::make_unique<Window>(root, gui_forms::Size{320.0, 160.0});
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms open files probe";
    options.initial_size = {320.0, 160.0};
    options.ready = OpenFilesReady{&probe};
    const ApplicationResult result = (*created.application).run(std::move(window), std::move(options));
    write_line(probe, result.accepted() ? "run: accepted" : "run: refused");
    probe.subscription.disconnect();
    watchdog.finish();
    write_line(probe, "end");
    if (probe.log != nullptr) std::fclose(probe.log);
    return result.accepted() ? 0 : 1;
}
