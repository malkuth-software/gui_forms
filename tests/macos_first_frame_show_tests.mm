#import <AppKit/AppKit.h>
#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

// The first-frame show gate on the AppKit host (first_frame_show.hpp): the
// window's first frame is painted and presented while the window is off
// screen, and the window is first seen on screen with that frame already in
// its storage, released by the frame (not by the 300 ms ceiling).

namespace {

constexpr const char* window_title = "GUI.Forms first-frame show";

[[nodiscard]] NSWindow* find_first_show_window() {
    NSWindow* window = nil;
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:@"GUI.Forms first-frame show"]) {
            window = candidate;
            break;
        }
    }
    return window;
}

class FirstShowRoot final : public gui_forms::Control {
public:
    explicit FirstShowRoot(gui_forms::StableId id) : Control(std::move(id)) {}
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect) override {
        if (paints == 0) {
            NSWindow* const window = find_first_show_window();
            first_paint_found_window = window != nil;
            first_paint_while_visible = window != nil && window.isVisible;
        }
        ++paints;
        painter.fill_rect(client_rectangle(), gui_forms::Color::rgba(30, 95, 180));
    }
    int paints{};
    bool first_paint_found_window{};
    bool first_paint_while_visible{};
};

struct State {
    std::shared_ptr<FirstShowRoot> root;
    std::function<void()> close;
    std::string failure;
    std::string host_json;
    bool checked{};
    std::chrono::steady_clock::time_point deadline{};
};

struct CheckStep {
    std::shared_ptr<State> state{};
};

void run_check(void* context);

void queue_check(const std::shared_ptr<State>& state, std::int64_t delay_ns) {
    std::unique_ptr<CheckStep> retained = std::make_unique<CheckStep>();
    (*retained).state = state;
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, delay_ns);
    void* const context = retained.release();
    dispatch_after_f(when, dispatch_get_main_queue(), context, run_check);
}

// The view's own pixels at its centre: the root's blue, read from the
// window's storage without drawing again.
bool shows_root_pixels(NSView* view) {
    NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    if (bitmap == nil) return false;
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
    NSColor* sample = [[bitmap colorAtX:bitmap.pixelsWide / 2 y:bitmap.pixelsHigh / 2]
        colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    const bool blue = sample != nil && sample.alphaComponent > 0.98 &&
        sample.blueComponent > 0.6 && sample.redComponent < 0.3;
    return blue;
}

// Checks once the window is first seen on screen.
void check(const std::shared_ptr<State>& state) {
    NSWindow* const window = find_first_show_window();
    const bool visible = window != nil && window.isVisible;
    if (!visible && std::chrono::steady_clock::now() < (*state).deadline) {
        queue_check(state, 5 * NSEC_PER_MSEC);
        return;
    }
    if (!visible) {
        (*state).failure = "the window was never shown";
    } else if ((*(*state).root).paints == 0) {
        (*state).failure = "the window was shown before its first frame was painted";
    } else if (!(*(*state).root).first_paint_found_window) {
        (*state).failure = "the first paint ran before the native window existed";
    } else if ((*(*state).root).first_paint_while_visible) {
        (*state).failure = "the first frame was painted after the window was on screen";
    } else if (!shows_root_pixels(window.contentView)) {
        (*state).failure = "the window was shown without its first frame's pixels";
    }
    (*state).checked = true;
    (*state).close();
}

void run_check(void* context) {
    const std::unique_ptr<CheckStep> retained(static_cast<CheckStep*>(context));
    const std::shared_ptr<State>& state = (*retained).state;
    try {
        check(state);
    } catch (const std::exception& error) {
        (*state).failure = error.what();
        (*state).close();
    }
}

void ready(const std::shared_ptr<State>& state, std::function<void()>, std::function<void()> close,
           std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>,
           std::function<gui_forms::HostClipboardTextResult()>,
           std::function<gui_forms::HostServiceStatus(std::string_view)>) {
    (*state).close = std::move(close);
    (*state).deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    // host_ready runs before the show is requested: the first check finds
    // the window off screen or, the frame presented, already shown.
    queue_check(state, 0);
}

void snapshot(const std::shared_ptr<State>& state, std::string_view, std::string_view host_json) {
    (*state).host_json = std::string(host_json);
}

} // namespace

int main() {
    std::shared_ptr<State> state = std::make_shared<State>();
    (*state).root = gui_forms::make_control<FirstShowRoot>(gui_forms::StableId("first-show-root"));
    gui_forms::host::MacHostOptions options;
    options.title = window_title;
    options.initial_size = {320, 200};
    options.print_metrics_on_close = false;
    options.host_ready = std::bind(ready, state, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
    options.final_snapshot = std::bind(snapshot, state, std::placeholders::_1, std::placeholders::_2);
    const int result = gui_forms::host::run_macos(std::make_unique<gui_forms::Window>((*state).root,
        options.initial_size), std::move(options));
    if (result == 0 && (*state).failure.empty() && (*state).checked) {
        // Released by the presented frame, never by the ceiling.
        if ((*state).host_json.find("\"first_show_cause\":\"frame_presented\"") == std::string::npos) {
            (*state).failure = "the first show was not released by the presented frame: " + (*state).host_json;
        }
    }
    if (result != 0 || !(*state).checked || !(*state).failure.empty()) {
        std::cerr << "first-frame show: " << (*state).failure << '\n';
        return 1;
    }
    std::cout << "The window is shown with its first frame already presented\n";
    return 0;
}
