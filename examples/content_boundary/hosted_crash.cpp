// A hosted program that really crashes (docs/user/content-surfaces.md,
// "Hosted content"), driven headless. The application starts
// crashing_program in a process of its own on a ContentSurface; the program
// shows frames and then crashes (SIGABRT on macOS and Linux, an unhandled
// exception on Windows). Only its process ends: the surface reports `failed`
// with the signal or the NTSTATUS, keeps the last frame under a veil, and
// the application goes on. A counter button keeps counting throughout, and
// Restart runs a new copy of the program on the same surface.
//
// The program's path is the first argument, or the one the build compiled
// in. macOS, Linux and Windows; on a platform without hosted content the
// example says so and exits 0.
//
// This is integrity, not security: the program runs with the application's
// rights; the boundary keeps its crash, hang or nonsense away from the
// application, and does not confine it.

#include <gui_forms/component.hpp>
#include <gui_forms/content.hpp>
#include <gui_forms/control/stable_id/stable_id.hpp>
#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/window/window.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace gf = gui_forms;

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

// How to run the program: a new process each time it is started.
gf::HostedContentOptions program_options(const std::filesystem::path& executable) {
    gf::HostedContentOptions options{};
    options.executable = executable;
    options.slot_bytes = 4U * 1024U * 1024U;
    // Below the surface's termination grace (the default 250 ms).
    options.stop_grace = std::chrono::milliseconds(100);
    return options;
}

// The window's behaviour: a status line, a counter that shows the
// application still answers, and Restart, offered once the program failed.
class Shell final : public gf::Component {
public:
    Shell(gf::ContentSurface& surface, gf::Label& status, gf::Button& restart, gf::Button& count,
          std::filesystem::path executable)
        : surface_(surface), status_(status), restart_(restart), executable_(std::move(executable)) {
        gf::on(surface.status_changed(), *this, &Shell::status_changed);
        gf::on(restart.clicked(), *this, &Shell::restart_clicked);
        gf::on(count.clicked(), *this, &Shell::count_clicked);
        restart_.enabled = false;
    }

    void start() {
        // The surface forwards its pointer, keys and focus to the program
        // (this program reads none of them).
        static_cast<void>(surface_.start_hosted_content(program_options(executable_), true));
        ++starts;
    }

    std::uint32_t counted{};
    std::uint32_t starts{};
    // Every transition the surface reported, counted as it arrives, so a
    // program that goes live and fails between two turns of the loop is
    // still seen to have gone live.
    std::uint32_t went_live{};
    std::uint32_t went_failed{};

private:
    void status_changed(gf::ContentSurfaceStatus status) {
        std::string line = std::string("Program: ") + std::string(gf::content_surface_status_name(status));
        if (status == gf::ContentSurfaceStatus::live) ++went_live;
        if (status == gf::ContentSurfaceStatus::failed) ++went_failed;
        if (status == gf::ContentSurfaceStatus::failed) line += " (" + surface_.failure() + ")";
        status_.text = line;
        restart_.enabled = status == gf::ContentSurfaceStatus::failed;
    }
    void restart_clicked() { start(); }
    void count_clicked() { ++counted; }

    gf::ContentSurface& surface_;
    gf::Label& status_;
    gf::Button& restart_;
    std::filesystem::path executable_;
};

// Whether the surface has gone live, and failed, at least the given numbers
// of times since the program first started.
struct TransitionsReach final {
    std::uint32_t live{};
    std::uint32_t failed{};
    bool operator()(const Shell& shell) const { return shell.went_live >= live && shell.went_failed >= failed; }
};

// What the surface holds when a wait gives up, for the failure message.
void report_surface(const gf::ContentSurface& surface, const Shell& shell) {
    const gf::ContentSurfaceStatistics statistics = surface.statistics();
    std::cerr << "surface: " << gf::content_surface_status_name(surface.status()) << " (" << surface.failure()
              << "); starts " << shell.starts << ", live " << shell.went_live << ", failed " << shell.went_failed
              << "; frames published " << statistics.frames_published << ", refused " << statistics.frames_refused
              << ", dropped " << statistics.frames_dropped << ", after revocation " << statistics.after_revocation
              << '\n';
}

// The host's loop, run by hand: deliver posted work, run the surface's
// timer check, and press the counter, until `done` holds or ten seconds
// pass.
bool run_until(gf::Window& window, gf::ContentSurface& surface, gf::Button& count, const Shell& shell,
               TransitionsReach done) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(window.drain_posted_work());
        surface.check_watchdog(gf::FrameClock::now());
        static_cast<void>(count.perform_click());
        if (done(shell)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    report_surface(surface, shell);
    return false;
}

std::filesystem::path program_path(int argument_count, char** arguments) {
    if (argument_count > 1) return std::filesystem::path(arguments[1]);
#if defined(GUI_FORMS_CRASHING_PROGRAM)
    return std::filesystem::path(GUI_FORMS_CRASHING_PROGRAM);
#else
    return std::filesystem::path{};
#endif
}

} // namespace

int main(int argument_count, char** arguments) {
    if (gf::hosted_content_support() != gf::HostedContentSupport::available) {
        std::cout << "Hosted content is not reported on this platform\n";
        return 0;
    }
    const std::filesystem::path executable = program_path(argument_count, arguments);
    require(executable.is_absolute(), "pass the crashing program's absolute path");

    std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("hosted.root"));
    std::shared_ptr<gf::ContentSurface> surface = gf::make_control<gf::ContentSurface>(gf::StableId("hosted.surface"));
    std::shared_ptr<gf::Label> status = gf::make_control<gf::Label>(gf::StableId("hosted.status"));
    std::shared_ptr<gf::Button> restart = gf::make_control<gf::Button>(gf::StableId("hosted.restart"), "Restart");
    std::shared_ptr<gf::Button> count = gf::make_control<gf::Button>(gf::StableId("hosted.count"), "Count");
    (*surface).set_requested_bounds({8.0, 8.0, 304.0, 160.0});
    (*status).set_requested_bounds({8.0, 176.0, 304.0, 24.0});
    (*restart).set_requested_bounds({8.0, 208.0, 96.0, 28.0});
    (*count).set_requested_bounds({112.0, 208.0, 96.0, 28.0});
    (*root).add_child(surface);
    (*root).add_child(status);
    (*root).add_child(restart);
    (*root).add_child(count);
    gf::Window window(root, gf::Size{320.0, 244.0});
    Shell shell(*surface, *status, *restart, *count, executable);
    window.perform_layout();

    shell.start();
    require(run_until(window, *surface, *count, shell, TransitionsReach{1U, 0U}), "the hosted program's frames arrive");
    require(run_until(window, *surface, *count, shell, TransitionsReach{1U, 1U}),
            "the program's crash fails the surface");
    require((*surface).failure().find("crashed") != std::string::npos, "the failure names the crash");
    require((*surface).live_surface() != nullptr, "the last good frame stays, veiled");
    const std::uint32_t counted_at_crash = shell.counted;

    // The application is unaffected: it lays out, answers, and starts the
    // program again on the same surface.
    window.perform_layout();
    require((*restart).perform_click(), "Restart is pressed");
    require((*surface).status() == gf::ContentSurfaceStatus::waiting && (*surface).failure().empty(),
            "Restart clears the failure and waits for the new program");
    require(run_until(window, *surface, *count, shell, TransitionsReach{2U, 1U}),
            "the restarted program shows frames");
    require(run_until(window, *surface, *count, shell, TransitionsReach{2U, 2U}), "and crashes again, alone");
    require(shell.counted > counted_at_crash && shell.starts == 2U, "the application answered throughout");

    std::cout << "Hosted program crashed twice (" << (*surface).failure() << "); the application counted "
              << shell.counted << " clicks and is still running\n";
    return 0;
}
