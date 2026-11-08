#pragma once

// --smoke-test for the Explore demoboard, shaped like PlaySuite's packaged
// smoke and the GUI.Forms demo programs': the product window runs normally,
// a UI timer on its root requests the ordinary close once a frame has been
// presented (or at the deadline), the host prints its close receipt and main
// prints "GUI.Forms window smoke passed" when a frame had been presented and
// the host returned 0, and a failure line to stderr otherwise.
//
// Only public GUI.Forms headers are used, as the demoboard boundary requires.

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

namespace file_manager_demoboard {

struct SmokeTest final {
    gui_forms::Window* window{};
    gui_forms::FrameTime started{};
    bool completed{};
    // Observed when the close was requested. The core declines to paint an
    // occluded window, so a window the host never reported visible closes at
    // the deadline with nothing presented.
    bool presented{};
    bool occluded{};
    std::function<void()> request_close;
    gui_forms::FrameRequestToken timer;
};

inline constexpr std::string_view smoke_test_pass_line = "GUI.Forms window smoke passed";

struct SmokeTestTick final {
    std::weak_ptr<SmokeTest> state;

    void operator()(gui_forms::FrameTime now) const {
        const std::shared_ptr<SmokeTest> live = state.lock();
        if (!live || (*live).completed || !(*live).request_close || (*live).window == nullptr) {
            return;
        }
        const double seconds = std::chrono::duration<double>(now - (*live).started).count();
        const bool presented = (*(*live).window).metrics_snapshot().frames_presented >= 1U;
        if ((presented && seconds >= 1.0) || seconds >= 10.0) {
            (*live).completed = true;
            (*live).presented = presented;
            (*live).occluded = (*(*live).window).occluded();
            (*live).request_close();
        }
    }
};

using HostReadyCallback = std::function<void(
    std::function<void()>, std::function<void()>,
    std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>,
    std::function<gui_forms::HostClipboardTextResult()>,
    std::function<gui_forms::HostServiceStatus(std::string_view)>)>;

struct SmokeTestHostReady final {
    std::shared_ptr<SmokeTest> state;
    HostReadyCallback previous;

    void operator()(std::function<void()> wake, std::function<void()> request_close,
                    std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)> show_dialog,
                    std::function<gui_forms::HostClipboardTextResult()> read_clipboard_text,
                    std::function<gui_forms::HostServiceStatus(std::string_view)> write_clipboard_text) const {
        (*state).request_close = request_close;
        (*state).started = gui_forms::FrameClock::now();
        const gui_forms::Control::Ptr root = (*(*state).window).root();
        const gui_forms::FrameInterval interval = std::chrono::milliseconds(50);
        (*state).timer = (*(*state).window).schedule_ui_timer(
            *root, interval, (*state).started + interval, SmokeTestTick{state});
        if (previous) {
            previous(std::move(wake), std::move(request_close), std::move(show_dialog),
                     std::move(read_clipboard_text), std::move(write_clipboard_text));
        }
    }
};

template <class HostOptions>
[[nodiscard]] inline std::shared_ptr<SmokeTest> arm_smoke_test(bool smoke_test, HostOptions& host,
                                                               gui_forms::Window& window) {
    if (!smoke_test) return nullptr;
    std::shared_ptr<SmokeTest> state = std::make_shared<SmokeTest>();
    (*state).window = &window;
    host.print_metrics_on_close = true;
    host.host_ready = SmokeTestHostReady{state, std::move(host.host_ready)};
    return state;
}

inline int smoke_test_result(const std::shared_ptr<SmokeTest>& state, int exit_code) {
    if (!state) return exit_code;
    if (!(*state).completed) {
        std::fprintf(stderr, "GUI.Forms window smoke did not reach its scheduled close\n");
        return exit_code == 0 ? 1 : exit_code;
    }
    if (!(*state).presented) {
        std::fprintf(stderr, "GUI.Forms window smoke failed: no frame was presented%s\n",
                     (*state).occluded ? " (the window was occluded at the deadline)" : "");
        return exit_code == 0 ? 1 : exit_code;
    }
    if (exit_code == 0) {
        std::fprintf(stdout, "%.*s\n", static_cast<int>(smoke_test_pass_line.size()),
                     smoke_test_pass_line.data());
        std::fflush(stdout);
    }
    return exit_code;
}

}  // namespace file_manager_demoboard
