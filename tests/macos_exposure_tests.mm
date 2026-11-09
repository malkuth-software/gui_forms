#import <AppKit/AppKit.h>
#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/gui_forms.hpp"
#include <iostream>
#include <functional>
#include <string>
#include <chrono>
#include <memory>
#include <cstdint>

// Exercise the host's native callback ordering without adding testing APIs to
// the public C++ surface. AppKit can request a snapshot before unocclusion.
@interface NSView (ExposureProbe)
- (void)notifyOcclusion:(BOOL)occluded;
@end

namespace {
class ExposureRoot final : public gui_forms::Control {
public:
    explicit ExposureRoot(gui_forms::StableId id) : Control(std::move(id)) {}
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect) override {
        ++paints;
        painter.fill_rect(client_rectangle(), gui_forms::Color::rgba(30, 95, 180));
        painter.fill_rect({10, 10, 30, 30}, gui_forms::Color::rgba(220, 50, 60));
    }
    int paints{};
};
struct State {
    std::shared_ptr<ExposureRoot> root;
    std::function<void()> close;
    std::string failure;
    bool completed{};
    std::chrono::steady_clock::time_point initial_deadline{};
    unsigned initial_checks{0};
};
enum class ExposureStage {
    initial,
    restore,
    check_restore,
    show,
    check_show,
};
struct ExposureStep {
    std::shared_ptr<State> state{};
    ExposureStage stage{ExposureStage::initial};
};
void exercise(const std::shared_ptr<State>& state);
void run_exposure_step(void* const context);
void queue_exposure_step(const std::shared_ptr<State>& state,
                         const ExposureStage stage, const std::int64_t delay_ns) {
    std::unique_ptr<ExposureStep> retained = std::make_unique<ExposureStep>();
    (*retained).state = state;
    (*retained).stage = stage;
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, delay_ns);
    void* const context = retained.release();
    dispatch_after_f(deadline, dispatch_get_main_queue(), context, run_exposure_step);
}
bool has_retained_pixels(NSView* view) {
    static NSColor* reference = nil;
    NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    if (bitmap == nil) return false;
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
    NSColor* sample = [[bitmap colorAtX:bitmap.pixelsWide / 2 y:bitmap.pixelsHigh / 2]
        colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (sample == nil || sample.alphaComponent < 0.98) return false;
    if (reference == nil) {
        if (sample.blueComponent < 0.6 || sample.redComponent > 0.3) return false;
        reference = sample;
    }
    return std::abs(sample.redComponent - reference.redComponent) < 0.01 &&
        std::abs(sample.greenComponent - reference.greenComponent) < 0.01 &&
        std::abs(sample.blueComponent - reference.blueComponent) < 0.01;
}
[[nodiscard]] NSWindow* find_exposure_window() {
    NSWindow* window = nil;
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:@"GUI.Forms exposure regression"]) {
            window = candidate;
            break;
        }
    }
    return window;
}
void exercise(const std::shared_ptr<State>& state) {
    NSWindow* const window = find_exposure_window();
    ++(*state).initial_checks;
    // host_ready publishes services before showing the window. A fixed 100 ms
    // delay does not establish first-paint readiness on a shared native runner.
    // Wait for actual application painting; do not force it via the snapshot.
    const bool ready_for_snapshot = window != nil && window.isVisible &&
        (*(*state).root).paints > 0;
    if (!ready_for_snapshot && std::chrono::steady_clock::now() < (*state).initial_deadline) {
        queue_exposure_step(state, ExposureStage::initial, 50 * NSEC_PER_MSEC);
        return;
    }
    if (!ready_for_snapshot || !has_retained_pixels(window.contentView)) {
        (*state).failure = "initial native frame was not painted";
        std::cerr << "Initial readiness checks=" << (*state).initial_checks
                  << " application_paints=" << (*(*state).root).paints << '\n';
        (*state).close();
        return;
    }
    NSView* const view = window.contentView;
    [view notifyOcclusion:YES];
    const int paints_before = (*(*state).root).paints;
    if (!has_retained_pixels(view)) {
        (*state).failure = "native exposure while occlusion is pending lost the retained pixels";
    }
    if ((*(*state).root).paints != paints_before) {
        (*state).failure = "occluded exposure reran application painting instead of presenting the cache";
    }
    [view notifyOcclusion:NO];
    [window miniaturize:nil];
    queue_exposure_step(state, ExposureStage::restore, 150 * NSEC_PER_MSEC);
}
void continue_exposure(const std::shared_ptr<State>& state, const ExposureStage stage) {
    if (stage == ExposureStage::initial) {
        exercise(state);
        return;
    }
    // Native objects are borrowed only for this main-queue invocation.
    NSWindow* const window = find_exposure_window();
    NSView* const view = window.contentView;
    if (window == nil || view == nil) {
        (*state).failure = "native exposure window disappeared during deferred checks";
        (*state).close();
        return;
    }
    switch (stage) {
    case ExposureStage::restore:
        [window deminiaturize:nil];
        queue_exposure_step(state, ExposureStage::check_restore, 250 * NSEC_PER_MSEC);
        break;
    case ExposureStage::check_restore:
        if (!has_retained_pixels(view)) {
            (*state).failure = "minimize/restore lost the frame without mouse input";
        }
        [window orderOut:nil];
        queue_exposure_step(state, ExposureStage::show, 50 * NSEC_PER_MSEC);
        break;
    case ExposureStage::show:
        [window makeKeyAndOrderFront:nil];
        queue_exposure_step(state, ExposureStage::check_show, 100 * NSEC_PER_MSEC);
        break;
    case ExposureStage::check_show:
        if (!has_retained_pixels(view)) {
            (*state).failure = "hide/show lost the frame without mouse input";
        }
        (*state).completed = true;
        (*state).close();
        break;
    case ExposureStage::initial:
    default:
        // Ignored: initial is exercised before the native window is looked up
        // (above), and no other stage exists.
        break;
    }
}
void run_exposure_step(void* const context) {
    // Each continuation owns a shared reference. No later invocation borrows
    // this holder: queue_exposure_step copies it before this owner is destroyed.
    const std::unique_ptr<ExposureStep> retained(static_cast<ExposureStep*>(context));
    const std::shared_ptr<State>& state = (*retained).state;
    try {
        continue_exposure(state, (*retained).stage);
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
    (*state).initial_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    queue_exposure_step(state, ExposureStage::initial, 50 * NSEC_PER_MSEC);
}
}
int main() {
    std::shared_ptr<State> state = std::make_shared<State>();
    (*state).root = gui_forms::make_control<ExposureRoot>(gui_forms::StableId("exposure-root"));
    gui_forms::host::MacHostOptions options;
    options.title = "GUI.Forms exposure regression";
    options.initial_size = {320, 200};
    options.print_metrics_on_close = false;
    options.host_ready = std::bind(ready, state, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
    int result = gui_forms::host::run_macos(std::make_unique<gui_forms::Window>((*state).root,
        options.initial_size), std::move(options));
    if (result || !(*state).completed || !(*state).failure.empty()) {
        std::cerr << (*state).failure << '\n';
        return 1;
    }
    std::cout << "Cached native exposure, minimize/restore and hide/show preserve pixels without input\n";
}
