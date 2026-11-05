#import <AppKit/AppKit.h>

#include "macos_host.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class FrameProbe final : public gui_forms::Control {
public:
    explicit FrameProbe(gui_forms::StableId stable_id)
        : Control(std::move(stable_id)) {}

    // One tick of the probe's UI timer: counts it and damages the probe, as
    // an animated control's own timer does (ADR-016).
    void advance_frame() {
        ++ticks;
        invalidate(gui_forms::Dirty::paint);
    }
    void on_paint(gui_forms::Painter&, gui_forms::Rect) override { ++paints; }

    std::uint64_t ticks{};
    std::uint64_t paints{};
};

// The probe's UI timer target. Observes the probe.
class FrameProbeTick final {
public:
    explicit FrameProbeTick(const std::shared_ptr<FrameProbe>& probe) : probe_(probe) {}

    void operator()(gui_forms::FrameTime) const {
        const std::shared_ptr<FrameProbe> live = probe_.lock();
        if (live) (*live).advance_frame();
    }

private:
    std::weak_ptr<FrameProbe> probe_;
};

struct RearmProbeState final {
    std::shared_ptr<FrameProbe> root;
    gui_forms::Window* window{};
    gui_forms::FrameRequestToken* initial{};
    gui_forms::FrameRequestToken replacement;
    std::function<void()> wake;
    std::function<void()> request_close;
    std::array<std::uint64_t, 8> ticks_before{};
    std::array<std::uint64_t, 8> ticks_at_rearm{};
    std::array<std::uint64_t, 8> ticks_after{};
    std::size_t cycle{};
    std::uint64_t successful_cycles{};
    std::uint64_t quiescent_gap_failures{};
    gui_forms::DispatchOperation worker_dispatch;
    gui_forms::DispatchOperation nested_dispatch;
    std::string dispatch_trace;
    bool worker_required_invoke{};
    bool worker_ran_on_ui_thread{};
    bool nested_ran_on_ui_thread{};
    bool synchronous_worker_required_invoke{};
    bool synchronous_ran_on_ui_thread{};
    bool synchronous_returned{};
    std::string synchronous_fault;
    std::function<void()> finish;
};

struct NativeWindowProbeState final {
    bool found{};
    bool moved{};
    bool resized{};
    bool zoomed{};
    bool minimized{};
    bool restored{};
};

void run_rearm_cycle(const std::shared_ptr<RearmProbeState>& state);

using VoidHostCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(
    const gui_forms::HostDialogRequest&)>;
using ShowTooltipCallback = std::function<gui_forms::HostServiceStatus(
    const gui_forms::HostTooltipRequest&)>;
using ReadClipboardCallback =
    std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback =
    std::function<gui_forms::HostServiceStatus(std::string_view)>;

bool dialog_result_cancelled(const gui_forms::HostDialogResult& result) {
    if (!result.status.accepted()) return false;
    if (std::holds_alternative<gui_forms::HostMessageDialogResult>(
            result.payload)) {
        return std::get<gui_forms::HostMessageDialogResult>(result.payload).outcome ==
               gui_forms::HostDialogOutcome::cancelled;
    }
    if (std::holds_alternative<gui_forms::HostPathDialogResult>(result.payload)) {
        return std::get<gui_forms::HostPathDialogResult>(result.payload).outcome ==
               gui_forms::HostDialogOutcome::cancelled;
    }
    return std::get<gui_forms::HostColorDialogResult>(result.payload).outcome ==
           gui_forms::HostDialogOutcome::cancelled;
}

class RecordNestedDispatch final {
public:
    explicit RecordNestedDispatch(std::shared_ptr<RearmProbeState> state)
        : state_(std::move(state)) {}

    void operator()() const {
        (*state_).nested_ran_on_ui_thread =
            !(*(*state_).root).invoke_required();
        (*state_).dispatch_trace += 'B';
    }

private:
    std::shared_ptr<RearmProbeState> state_;
};

class RecordWorkerDispatch final {
public:
    explicit RecordWorkerDispatch(std::shared_ptr<RearmProbeState> state)
        : state_(std::move(state)) {}

    void operator()() const {
        (*state_).worker_ran_on_ui_thread =
            !(*(*state_).root).invoke_required();
        (*state_).dispatch_trace += 'A';
        (*state_).nested_dispatch =
            (*(*state_).root).begin_invoke(RecordNestedDispatch(state_));
    }

private:
    std::shared_ptr<RearmProbeState> state_;
};

class BeginWorkerDispatch final {
public:
    explicit BeginWorkerDispatch(std::shared_ptr<RearmProbeState> state)
        : state_(std::move(state)) {}

    void operator()() const {
        (*state_).worker_required_invoke =
            (*(*state_).root).invoke_required();
        (*state_).worker_dispatch =
            (*(*state_).root).begin_invoke(RecordWorkerDispatch(state_));
    }

private:
    std::shared_ptr<RearmProbeState> state_;
};

class RecordSynchronousDispatch final {
public:
    explicit RecordSynchronousDispatch(std::shared_ptr<RearmProbeState> state)
        : state_(std::move(state)) {}

    void operator()() const {
        (*state_).synchronous_ran_on_ui_thread =
            !(*(*state_).root).invoke_required();
    }

private:
    std::shared_ptr<RearmProbeState> state_;
};

class ThrowNativeSynchronousFault final {
public:
    void operator()() const {
        throw std::runtime_error("native synchronous fault");
    }
};

class RunSynchronousDispatch final {
public:
    explicit RunSynchronousDispatch(std::shared_ptr<RearmProbeState> state)
        : state_(std::move(state)) {}

    void operator()() const {
        (*state_).synchronous_worker_required_invoke =
            (*(*state_).root).invoke_required();
        (*(*state_).root).invoke(RecordSynchronousDispatch(state_));
        (*state_).synchronous_returned = true;
        try {
            (*(*state_).window).invoke(ThrowNativeSynchronousFault());
        } catch (const std::runtime_error& error) {
            (*state_).synchronous_fault = error.what();
        }
    }

private:
    std::shared_ptr<RearmProbeState> state_;
};

class CountCloseRequests final {
public:
    explicit CountCloseRequests(std::uint64_t& requests) noexcept
        : requests_(requests) {}

    void operator()(gui_forms::HostCloseRequest& request) const {
        ++requests_;
        request.cancel = requests_ == 1U;
    }

private:
    std::uint64_t& requests_;
};

class StoreFinalHostSnapshot final {
public:
    explicit StoreFinalHostSnapshot(std::string& snapshot) noexcept
        : snapshot_(snapshot) {}

    void operator()(std::string_view, std::string_view host) const {
        snapshot_.assign(host);
    }

private:
    std::string& snapshot_;
};

// Main-queue work after a delay without Objective-C blocks (owner decision
// V2-1): `function` adopts the heap `context` and deletes it.
void post_after(std::int64_t delay_nanoseconds, void* context, dispatch_function_t function) {
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, delay_nanoseconds);
    dispatch_after_f(when, dispatch_get_main_queue(), context, function);
}

// The window probe's two delayed steps, with what the replaced blocks
// captured: the probe state, the window (kept alive) and the close request.
struct NativeWindowProbeWork final {
    std::shared_ptr<NativeWindowProbeState> probe;
    NSWindow* window;
    std::function<void()> request_close;
};

void probe_restored(void* context) {
    const std::unique_ptr<NativeWindowProbeWork> work(static_cast<NativeWindowProbeWork*>(context));
    (*(*work).probe).restored = !(*work).window.isMiniaturized;
    (*work).request_close();
    (*work).request_close();
}

void probe_minimized(void* context) {
    std::unique_ptr<NativeWindowProbeWork> work(static_cast<NativeWindowProbeWork*>(context));
    (*(*work).probe).minimized = (*work).window.isMiniaturized;
    [(*work).window deminiaturize:nil];
    post_after(80 * NSEC_PER_MSEC, work.release(), &probe_restored);
}

class ProbeNativeWindow final {
public:
    ProbeNativeWindow(
        std::shared_ptr<NativeWindowProbeState> native_window_probe,
        std::function<void()> request_close)
        : native_window_probe_(std::move(native_window_probe)),
          request_close_(std::move(request_close)) {}

    void operator()() const {
        // The scheduled work copies these locals, so it does not depend on
        // this callable outliving the dispatch.
        const std::shared_ptr<NativeWindowProbeState> nativeWindowProbe =
            native_window_probe_;
        const std::function<void()> requestClose = request_close_;
        NSWindow* nativeWindow = nil;
        for (NSWindow* candidate in NSApplication.sharedApplication.windows) {
            if ([candidate.title isEqualToString:@"GUI.Forms Close Test"]) {
                nativeWindow = candidate;
                break;
            }
        }
        if (nativeWindow == nil) return;
        (*nativeWindowProbe).found = true;
        [nativeWindow setAnimationBehavior:NSWindowAnimationBehaviorNone];
        const NSRect original = nativeWindow.frame;
        NSRect changed = original;
        changed.origin.x += 6.0;
        changed.origin.y -= 6.0;
        changed.size.width += 12.0;
        changed.size.height += 12.0;
        [nativeWindow setFrame:changed display:NO];
        const NSRect committed = nativeWindow.frame;
        (*nativeWindowProbe).moved =
            committed.origin.x != original.origin.x ||
            committed.origin.y != original.origin.y;
        (*nativeWindowProbe).resized =
            committed.size.width != original.size.width ||
            committed.size.height != original.size.height;
        [nativeWindow zoom:nil];
        (*nativeWindowProbe).zoomed = nativeWindow.isZoomed ||
            !NSEqualRects(nativeWindow.frame, committed);
        [nativeWindow zoom:nil];
        [nativeWindow miniaturize:nil];
        NativeWindowProbeWork* const work = new NativeWindowProbeWork{nativeWindowProbe, nativeWindow, requestClose};
        post_after(80 * NSEC_PER_MSEC, work, &probe_minimized);
    }

private:
    std::shared_ptr<NativeWindowProbeState> native_window_probe_;
    std::function<void()> request_close_;
};

// The re-arm probe's delayed steps, with what the replaced blocks captured.
struct RearmStartWork final {
    std::shared_ptr<RearmProbeState> state;
    std::shared_ptr<FrameProbe> surface;
};

struct RearmCycleWork final {
    std::shared_ptr<RearmProbeState> state;
    std::size_t cycle;
};

void rearm_first_cycle(void* context) {
    const std::unique_ptr<RearmStartWork> work(static_cast<RearmStartWork*>(context));
    run_rearm_cycle((*work).state);
}

void rearm_show_and_wake(void* context) {
    std::unique_ptr<RearmStartWork> work(static_cast<RearmStartWork*>(context));
    (*(*work).surface).set_visible(true);
    (*(*work).state).wake();
    post_after(80 * NSEC_PER_MSEC, work.release(), &rearm_first_cycle);
}

void rearm_next_cycle(void* context) {
    const std::unique_ptr<RearmCycleWork> work(static_cast<RearmCycleWork*>(context));
    run_rearm_cycle((*work).state);
}

void rearm_measure(void* context) {
    std::unique_ptr<RearmCycleWork> work(static_cast<RearmCycleWork*>(context));
    RearmProbeState& state = *(*work).state;
    const std::size_t current = (*work).cycle;
    state.ticks_after[current] = (*state.root).ticks;
    if (state.ticks_after[current] > state.ticks_at_rearm[current]) {
        ++state.successful_cycles;
    }
    ++state.cycle;
    if (state.cycle == state.ticks_before.size()) {
        if (state.finish) {
            state.finish();
        } else {
            state.request_close();
            state.request_close();
        }
        return;
    }
    post_after(10 * NSEC_PER_MSEC, work.release(), &rearm_next_cycle);
}

void rearm_after_disconnect(void* context) {
    std::unique_ptr<RearmCycleWork> work(static_cast<RearmCycleWork*>(context));
    RearmProbeState& state = *(*work).state;
    const std::size_t current = (*work).cycle;
    state.ticks_at_rearm[current] = (*state.root).ticks;
    if (state.ticks_at_rearm[current] != state.ticks_before[current]) {
        ++state.quiescent_gap_failures;
    }
    state.replacement = (*state.window).schedule_ui_timer(
        *state.root, std::chrono::milliseconds(10),
        gui_forms::FrameClock::now() + std::chrono::milliseconds(10),
        FrameProbeTick{state.root});
    state.wake();
    post_after(70 * NSEC_PER_MSEC, work.release(), &rearm_measure);
}

class PrepareCloseTestHost final {
public:
    PrepareCloseTestHost(
        std::shared_ptr<RearmProbeState>& rearm_state,
        const std::shared_ptr<FrameProbe>& surface,
        gui_forms::Window* live_window,
        gui_forms::FrameRequestToken& active,
        std::thread& synchronous_worker,
        const std::shared_ptr<NativeWindowProbeState>& native_window_probe) noexcept
        : rearm_state_(rearm_state),
          surface_(surface),
          live_window_(live_window),
          active_(active),
          synchronous_worker_(synchronous_worker),
          native_window_probe_(native_window_probe) {}

    void operator()(
        VoidHostCallback wake,
        VoidHostCallback request_close,
        ShowDialogCallback,
        ShowTooltipCallback,
        VoidHostCallback,
        ReadClipboardCallback,
        WriteClipboardCallback) const {
        // Exercise the exact transport used by pause/resume: disconnect the
        // last timer, force the host to disarm, then publish a new
        // timer and wake it again. Repeating the transition catches a host
        // source that reports a logically live model but remains disarmed.
        rearm_state_ = std::make_shared<RearmProbeState>();
        (*rearm_state_).root = surface_;
        (*rearm_state_).window = live_window_;
        (*rearm_state_).initial = &active_;
        (*rearm_state_).wake = std::move(wake);
        (*rearm_state_).request_close = std::move(request_close);
        std::thread worker{BeginWorkerDispatch(rearm_state_)};
        worker.join();
        synchronous_worker_ =
            std::thread{RunSynchronousDispatch(rearm_state_)};
        const std::shared_ptr<NativeWindowProbeState> nativeWindowProbe =
            native_window_probe_;
        const std::function<void()> requestClose =
            (*rearm_state_).request_close;
        (*rearm_state_).finish =
            ProbeNativeWindow(nativeWindowProbe, requestClose);
        RearmStartWork* const work = new RearmStartWork{rearm_state_, surface_};
        post_after(80 * NSEC_PER_MSEC, work, &rearm_show_and_wake);
    }

private:
    std::shared_ptr<RearmProbeState>& rearm_state_;
    std::shared_ptr<FrameProbe> surface_;
    gui_forms::Window* live_window_;
    gui_forms::FrameRequestToken& active_;
    std::thread& synchronous_worker_;
    std::shared_ptr<NativeWindowProbeState> native_window_probe_;
};

void run_rearm_cycle(const std::shared_ptr<RearmProbeState>& state) {
    const std::size_t current = (*state).cycle;
    (*state).ticks_before[current] = (*(*state).root).ticks;
    if (current == 0U) {
        (*(*state).initial).disconnect();
    } else {
        (*state).replacement.disconnect();
    }
    (*state).wake();
    RearmCycleWork* const work = new RearmCycleWork{state, current};
    post_after(12 * NSEC_PER_MSEC, work, &rearm_after_disconnect);
}

bool test_isolated_macos_services() {
    constexpr const char* pasteboard_name = "local.gui_forms.m3b.test";
    gui_forms::host::MacHostServiceOptions service_options;
    service_options.clipboard_name = pasteboard_name;
    service_options.cancel_dialogs_for_testing = true;
    std::unique_ptr<gui_forms::HostServices> services =
        gui_forms::host::make_macos_host_services(service_options);
    if (!services) {
        return false;
    }
    const gui_forms::CursorImage pixels{32, 32, 1.0,
        std::vector<gui_forms::Color>(1024, {30, 60, 90, 255})};
    const gui_forms::CursorImage retina{64, 64, 2.0,
        std::vector<gui_forms::Color>(4096, {30, 60, 90, 255})};
    const gui_forms::CursorImagesPtr images = gui_forms::CursorImages::create({pixels, retina}, .25, .75);
    const gui_forms::HostServiceStatus custom_cursor =
        (*services).set_custom_cursor(images, 2.0, gui_forms::CursorKind::crosshair);
    if (!custom_cursor.accepted()) return false;
    NSCursor* native = [NSCursor currentCursor];
    if (native == [NSCursor crosshairCursor] || native.hotSpot.x != 8 || native.hotSpot.y != 24 ||
        native.image.size.width != 32 || native.image.representations.count != 2) return false;
    for (int i = 0; i < 200; ++i) {
        const gui_forms::HostServiceStatus repeated_cursor =
            (*services).set_custom_cursor(images, 2.0, gui_forms::CursorKind::crosshair);
        if (!repeated_cursor.accepted() ||
            [NSCursor currentCursor] != native) return false;
    }
    const gui_forms::HostServiceStatus standard_cursor = (*services).set_cursor(gui_forms::CursorKind::crosshair);
    if (!standard_cursor.accepted() ||
        [NSCursor currentCursor] != [NSCursor crosshairCursor]) return false;
    const gui_forms::HostMonitorResult monitors = (*services).query_monitors();
    if (!monitors.status.accepted() || monitors.monitors.empty()) {
        return false;
    }
    if (!(*services).set_cursor(gui_forms::CursorKind::hand).accepted() ||
        !(*services).set_cursor(gui_forms::CursorKind::arrow).accepted() ||
        !(*services).set_pointer_capture(true, 9).accepted() ||
        !(*services).set_pointer_capture(false).accepted() ||
        !(*services).write_clipboard_text("M3c AppKit Ω").accepted()) {
        return false;
    }
    const gui_forms::HostClipboardTextResult clipboard =
        (*services).read_clipboard_text();
    if (!clipboard.status.accepted() || !clipboard.has_text ||
        clipboard.text_utf8 != "M3c AppKit Ω") {
        return false;
    }
    gui_forms::HostMessageDialogRequest message_request;
    message_request.title = "M3d message";
    message_request.message = "Native cancellation smoke";
    message_request.buttons = gui_forms::HostMessageButtons::ok_cancel;
    const gui_forms::HostDialogResult message = (*services).show_dialog(
        {101, "test.window", std::move(message_request)});
    gui_forms::HostOpenFileDialogRequest open_request;
    open_request.title = "M3d open";
    // Two filters put the filter overlay in the panel, starting on the second.
    open_request.filters = {{"Text", {"txt"}}, {"Logs", {"log"}}};
    open_request.initial_filter = 1U;
    const gui_forms::HostDialogResult open = (*services).show_dialog(
        {102, "test.window", std::move(open_request)});
    gui_forms::HostSaveFileDialogRequest save_request;
    save_request.title = "M3d save";
    save_request.suggested_name = "evidence";
    save_request.default_extension = "txt";
    save_request.filters = {{"Text", {"txt"}}, {"Any file", {"*"}}};
    save_request.initial_filter = 1U;
    const gui_forms::HostDialogResult save = (*services).show_dialog(
        {103, "test.window", std::move(save_request)});
    const gui_forms::HostDialogResult folder = (*services).show_dialog(
        {104, "test.window",
         gui_forms::HostFolderDialogRequest{"M3d folder", {}}});
    const gui_forms::HostDialogResult color = (*services).show_dialog(
        {105, "test.window",
         gui_forms::HostColorDialogRequest{"M3d color", 0x3366CCFFU, false}});
    if (!dialog_result_cancelled(message) || !dialog_result_cancelled(open) ||
        !dialog_result_cancelled(save) || !dialog_result_cancelled(folder) ||
        !dialog_result_cancelled(color) ||
        (*services).snapshot().dialog_requests != 5 ||
        (*services).snapshot().dialog_completions != 5 ||
        (*services).snapshot().dialog_cancellations != 5 ||
        (*services).snapshot().maximum_modal_depth != 1) {
        return false;
    }
    (*services).shutdown();
    if ((*services).query_monitors().status.error !=
        gui_forms::HostServiceError::after_shutdown) {
        return false;
    }
    std::cout << "appkit_services monitor_count=" << monitors.monitors.size()
              << " primary=" << monitors.monitors.front().id
              << " clipboard_generation=" << clipboard.generation
              << " snapshot=" << (*services).snapshot().to_json() << '\n';
    NSString* name = [NSString stringWithUTF8String:pasteboard_name];
    [[NSPasteboard pasteboardWithName:name] releaseGlobally];
    return true;
}

} // namespace

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        if (!test_isolated_macos_services()) {
            return 6;
        }
    }
    const gui_forms::HostCapabilities capabilities =
        gui_forms::host::macos_capabilities();
    if (!capabilities.supports(
            gui_forms::HostCapability::lifecycle |
            gui_forms::HostCapability::scale_notifications |
            gui_forms::HostCapability::occlusion |
            gui_forms::HostCapability::pointer_input |
            gui_forms::HostCapability::keyboard_input |
            gui_forms::HostCapability::text_composition |
            gui_forms::HostCapability::monitor_geometry |
            gui_forms::HostCapability::pointer_capture |
            gui_forms::HostCapability::cursor |
            gui_forms::HostCapability::clipboard |
            gui_forms::HostCapability::typed_drag_destination |
            gui_forms::HostCapability::dialogs)) {
        return 4;
    }
    std::shared_ptr<FrameProbe> root = gui_forms::make_control<FrameProbe>(
        gui_forms::StableId("host.close.root"));
    std::shared_ptr<FrameProbe> surface = gui_forms::make_control<FrameProbe>(
        gui_forms::StableId("host.close.surface"));
    (*surface).set_visible(false);
    (*surface).set_requested_bounds({0.0, 0.0, 320.0, 180.0});
    (*root).add_child(surface);
    std::unique_ptr<gui_forms::Window> window = std::make_unique<gui_forms::Window>(root,
                                                      gui_forms::Size{320.0, 180.0});
    gui_forms::Window* const live_window = window.get();
    gui_forms::FrameRequestToken active = (*window).schedule_ui_timer(
        *surface, std::chrono::milliseconds(10),
        gui_forms::FrameClock::now() + std::chrono::milliseconds(10),
        FrameProbeTick{surface});
    std::shared_ptr<RearmProbeState> rearm_state;
    const std::shared_ptr<NativeWindowProbeState> native_window_probe =
        std::make_shared<NativeWindowProbeState>();
    std::thread synchronous_worker;
    gui_forms::host::MacHostOptions options;
    options.title = "GUI.Forms Close Test";
    options.initial_size = {320.0, 180.0};
    options.minimum_size = {320.0, 180.0};
    options.titlebar_presentation =
        gui_forms::host::MacTitlebarPresentation::
            transparent_full_size_content;
    options.window_drag_region_id = "host.close.root";
    options.print_metrics_on_close = false;
    std::uint64_t close_requests = 0;
    std::string final_host_snapshot;
    options.close_request = CountCloseRequests(close_requests);
    options.final_snapshot = StoreFinalHostSnapshot(final_host_snapshot);
    options.host_ready = PrepareCloseTestHost(
        rearm_state, surface, live_window, active, synchronous_worker,
        native_window_probe);
    const int result = gui_forms::host::run_macos(std::move(window), std::move(options));
    if (synchronous_worker.joinable()) synchronous_worker.join();
    if (result != 0) {
        return result;
    }
    if (!rearm_state || active.connected() || (*rearm_state).replacement.connected()) {
        return 3;
    }
    if (!(*native_window_probe).found || !(*native_window_probe).moved ||
        !(*native_window_probe).resized || !(*native_window_probe).zoomed ||
        !(*native_window_probe).minimized || !(*native_window_probe).restored) {
        std::cerr << "native_window_probe found=" << (*native_window_probe).found
                  << " moved=" << (*native_window_probe).moved
                  << " resized=" << (*native_window_probe).resized
                  << " zoomed=" << (*native_window_probe).zoomed
                  << " minimized=" << (*native_window_probe).minimized
                  << " restored=" << (*native_window_probe).restored << '\n';
        return 9;
    }
    if (close_requests != 2) {
        return 5;
    }
    if (final_host_snapshot.find("\"display_changes\":") == std::string::npos ||
        final_host_snapshot.find("\"display_changes\":0") != std::string::npos ||
        final_host_snapshot.find("\"monitor_count\":1") == std::string::npos ||
        final_host_snapshot.find("\"pointer_capture\"") == std::string::npos) {
        return 7;
    }
    if (final_host_snapshot.find(
            "\"titlebar_presentation\":\"transparent_full_size_content\"") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_full_size_content\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_title_hidden\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_titlebar_transparent\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_close_button_present\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_minimize_button_present\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_zoom_button_present\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"native_system_title_present\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"window_drag_region_configured\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"window_drag_region_resolved\":true") ==
            std::string::npos ||
        final_host_snapshot.find("\"window_drag_region_count\":1") ==
            std::string::npos ||
        final_host_snapshot.find("\"window_drag_region_resolved_count\":1") ==
            std::string::npos) {
        return 8;
    }
    if ((*rearm_state).ticks_before.front() == 0 ||
        (*rearm_state).ticks_after.back() <= (*rearm_state).ticks_before.front() + 2U ||
        (*(*rearm_state).root).paints < 4U ||
        (*rearm_state).successful_cycles != (*rearm_state).ticks_before.size() ||
        (*rearm_state).quiescent_gap_failures != 0U ||
        !(*rearm_state).worker_required_invoke ||
        !(*rearm_state).worker_ran_on_ui_thread ||
        !(*rearm_state).nested_ran_on_ui_thread ||
        !(*rearm_state).synchronous_worker_required_invoke ||
        !(*rearm_state).synchronous_ran_on_ui_thread ||
        !(*rearm_state).synchronous_returned ||
        (*rearm_state).synchronous_fault != "native synchronous fault" ||
        (*rearm_state).dispatch_trace != "AB" ||
        (*rearm_state).worker_dispatch.state() !=
            gui_forms::DispatchOperationState::completed ||
        (*rearm_state).nested_dispatch.state() !=
            gui_forms::DispatchOperationState::completed) {
        std::cerr << "AppKit scheduled wake did not autonomously advance after "
                     "repeated UI timer replacement: before="
                  << (*rearm_state).ticks_before.front()
                  << " after=" << (*rearm_state).ticks_after.back()
                  << " paints=" << (*(*rearm_state).root).paints
                  << " successful_cycles=" << (*rearm_state).successful_cycles
                  << " quiescent_gap_failures="
                  << (*rearm_state).quiescent_gap_failures << '\n';
        for (std::size_t cycle = 0; cycle < (*rearm_state).ticks_before.size();
             ++cycle) {
            std::cerr << "  cycle=" << cycle
                      << " before=" << (*rearm_state).ticks_before[cycle]
                      << " rearm=" << (*rearm_state).ticks_at_rearm[cycle]
                      << " after=" << (*rearm_state).ticks_after[cycle] << '\n';
        }
        return 8;
    }
    std::cout << "macos_capabilities=" << capabilities.to_json()
              << " close_requests=" << close_requests << '\n';
    return 0;
}
