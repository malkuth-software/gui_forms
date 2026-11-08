// Stage DK phase W0 measurement probe (decision 12): how often a native host
// moves a window when the framework sets its frame once per pointer move.
// Not a ctest: it puts two windows on the desktop, so it runs only when the
// owner asks for a measurement (notes:results/2026-10-18-dk-w0-capture-drag/).
//
//   gui_forms_frame_drag_probe            scripted: 120 frame requests at the
//                                         timer's 16 ms cadence, then exits
//   gui_forms_frame_drag_probe --manual   drag the small window's body with
//                                         the mouse; each drag prints a line;
//                                         close the main window to exit
//
// Each result is one JSON line on standard output.
#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {
using namespace gui_forms;

std::uint64_t now_nanoseconds() {
    const std::chrono::steady_clock::duration since = std::chrono::steady_clock::now().time_since_epoch();
    const std::uint64_t value = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
    return value;
}

void print_statistics(const char* mode, const char* platform, const WindowFrameDragStatistics& statistics) {
    std::printf("{\"probe\":\"frame_drag\",\"mode\":\"%s\",\"platform\":\"%s\",\"pointer_moves\":%llu,"
                "\"frame_requests\":%llu,\"frame_reports\":%llu,\"refused\":%llu,"
                "\"duration_s\":%.4f,\"pointer_hz\":%.2f,\"frames_hz\":%.2f,\"keeps_up\":%s}\n",
                mode, platform, static_cast<unsigned long long>(statistics.pointer_moves),
                static_cast<unsigned long long>(statistics.frame_requests),
                static_cast<unsigned long long>(statistics.frame_reports),
                static_cast<unsigned long long>(statistics.refused_requests), statistics.duration_seconds(),
                statistics.pointer_rate_hz(), statistics.achieved_rate_hz(),
                statistics.keeps_up() ? "true" : "false");
    std::fflush(stdout);
}

// The floating window's body: pressing it starts the framework's drag.
class DragBody final : public Control {
public:
    explicit DragBody(StableId id) : Control(std::move(id)) {}
    ApplicationWindowHandle window{};
    std::string platform{};
    WindowFrameDrag drag{};

    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down && event.button == PointerButton::primary) {
            set_pointer_capture(true);
            static_cast<void>(drag.begin(window, now_nanoseconds()));
            event.handled = true;
        } else if (event.action == PointerAction::move && drag.active()) {
            static_cast<void>(drag.move(now_nanoseconds()));
            event.handled = true;
        } else if (event.action == PointerAction::up && drag.active()) {
            drag.end(now_nanoseconds());
            set_pointer_capture(false);
            print_statistics("manual", platform.c_str(), drag.statistics());
            event.handled = true;
        }
    }
};

// Scripted: one frame request per timer tick along a diagonal, counting the
// host's frame reports, as WindowFrameDrag counts them.
struct Script final {
    ApplicationWindowHandle main{};
    ApplicationWindowHandle floating{};
    std::string platform{};
    Rect start{};
    std::uint64_t ticks{};
    WindowFrameDragStatistics statistics{};
    SubscriptionToken tick_subscription{};
    // Owned by the probe; released when the floating window closes, while
    // the window it was made for still exists.
    std::unique_ptr<Timer> timer{};
};

struct Tick final {
    Script* script{};
    void operator()() const {
        Script& state = *script;
        const std::uint64_t now = now_nanoseconds();
        if (state.ticks == 0U) state.statistics.first_timestamp_nanoseconds = now;
        state.statistics.last_timestamp_nanoseconds = now;
        if (state.ticks >= 120U) {
            (*state.timer).stop();
            print_statistics("scripted", state.platform.c_str(), state.statistics);
            static_cast<void>(state.main.request_close());
            return;
        }
        ++state.ticks;
        ++state.statistics.pointer_moves;
        ++state.statistics.frame_requests;
        const double offset = static_cast<double>(state.ticks) * 2.0;
        const Rect target{state.start.x + offset, state.start.y + offset / 2.0, state.start.width,
                          state.start.height};
        if (!state.floating.request_frame(target).accepted()) ++state.statistics.refused_requests;
    }
};

struct FloatingFrames final {
    Script* script{};
    void operator()(const HostWindowFrameEvent& event) const {
        if ((*script).timer && event.window == (*script).floating.id()) ++(*script).statistics.frame_reports;
    }
};

struct ReleaseTimer final {
    Script* script{};
    void operator()() const {
        (*script).tick_subscription.disconnect();
        if ((*script).timer) (*(*script).timer).dispose();
        (*script).timer.reset();
    }
};

struct FloatingReady final {
    Script* script{};
    std::shared_ptr<DragBody> body{};
    bool manual{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        Script& state = *script;
        state.floating = handle;
        (*body).window = handle;
        (*body).platform = state.platform;
        if (manual) return;
        state.start = handle.frame().frame.content;
        state.timer = std::make_unique<Timer>(window, std::chrono::milliseconds(16));
        state.tick_subscription = (*state.timer).tick().subscribe(Tick{script});
        (*state.timer).start();
    }
};

struct MainReady final {
    Script* script{};
    bool manual{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        Script& state = *script;
        state.main = handle;
        const ApplicationWindows windows = handle.windows();
        state.platform = windows.capabilities().platform;
        if (WindowFrameDrag::mode_for(windows.capabilities()) != WindowFrameDragMode::framework_capture) {
            std::printf("{\"probe\":\"frame_drag\",\"platform\":\"%s\",\"mode\":\"native_move_loop\"}\n",
                        state.platform.c_str());
            std::fflush(stdout);
            static_cast<void>(handle.request_close());
            return;
        }
        const std::shared_ptr<DragBody> body = make_control<DragBody>(StableId("probe.body"));
        ApplicationWindow floating{};
        floating.stable_id = "probe.floating";
        floating.owner_id = "main";
        floating.floating = HostWindowFloating::document;
        floating.model = std::make_unique<Window>(body, Size{240.0, 160.0}, window.resources());
        floating.options.title = "Drag me";
        floating.options.initial_size = {240.0, 160.0};
        floating.options.minimum_size = {120.0, 80.0};
        floating.options.ready = FloatingReady{script, body, manual};
        floating.options.frame_changed = FloatingFrames{script};
        floating.options.closed = ReleaseTimer{script};
        static_cast<void>(windows.open_window(std::move(floating)));
    }
};
} // namespace

int main(int argc, char** argv) {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    const bool manual = argc > 1 && std::strcmp(argv[1], "--manual") == 0;
    Script script{};
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms frame drag probe";
    options.initial_size = {480.0, 320.0};
    options.ready = MainReady{&script, manual};
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    const ApplicationResult result =
        application.run(std::make_unique<Window>(root, Size{480.0, 320.0}), std::move(options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}
