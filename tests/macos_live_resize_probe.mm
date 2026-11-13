#import <AppKit/AppKit.h>

#include "macos_host.hpp"
#include "gallery.hpp"
#include "gui_forms/window/window.hpp"
#include "../src/host/macos/application/display_link_trace.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dispatch/dispatch.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Live resize on the Gallery (Mac handoff 2026-11-09, item 2; cp33): a
// scripted resize that records every draw's timing and damage, checks that
// no draw leaves an exposed strip undamaged (stale damage) and that the
// window keeps drawing while it resizes (no stall), and, for the owner's
// perceptual verdict, can save the presented frames as PNGs.
//
// Workload (stated with every result): the Gallery on its opening page, at
// its default size (1280x820 logical units) on the main display at its own
// backing scale. Each phase widens the window by 320 and heightens it by 200
// logical units in 60 steps, one every 16 ms (about 1 s), holds for 250 ms,
// then shrinks it back in 60 steps; that is 120 size changes per phase.
//   - drag: the steps are mouse drags of the window's bottom-right corner,
//     posted to the process's own event queue, so AppKit runs its own live
//     resize (inLiveResize) as for a user's drag. Posting to the own queue
//     needs no Accessibility grant. If AppKit does not take the posted
//     drag as a resize on this system (the window size does not change),
//     the phase reports drag=unavailable and is not judged.
//   - stepped: the steps are -[NSWindow setFrame:display:YES] calls, the
//     programmatic resize every system honours (no live-resize mode).
//
// Output: one line per phase on stdout,
//   live-resize|phase=<p>|workload=...|draws=..|size_changes=..|live_resize_draws=..
//   |interval_median_ms=..|interval_p95_ms=..|interval_max_ms=..
//   |draw_median_ms=..|draw_p95_ms=..|draw_max_ms=..|stale=..|stalls=..|final_size_drawn=..
// and <dir>/<phase>.csv holding every draw record, where <dir> is --out's
// or, by default, live-resize/ beside the bundle; the last line is
// "failures=N" (cmake/run_macos_activated_test.cmake). With
// --capture <dir>, a second pass of each phase saves every 4th presented
// frame (at most 120) as <dir>/<phase>/frame_NNNN.png; that pass's timings
// include the PNG encoding and are not reported.
//
// Judged (exit status 1 on failure), for each phase that ran:
//   - stale damage: the first draw after the size grows has a dirty
//     rectangle containing the strips the growth exposed (to 1 unit);
//   - stall: no gap between consecutive draws while the size is changing
//     exceeds 100 ms (six frames at 60 Hz);
//   - final frame: after the phase, a presented draw at the window's final
//     size covers the whole view.
// Frame timing is recorded, not judged: the verdict on smoothness and
// tearing is the owner's, from the numbers and the captured frames.

@interface NSView (LiveResizeProbe)
- (void)setDrawTraceEnabled:(BOOL)enabled
           captureDirectory:(std::string)directory
                     stride:(std::size_t)stride
                      limit:(std::size_t)limit;
- (BOOL)readDrawTrace:(std::vector<MacDrawRecord>&)records;
@end

namespace {

constexpr std::size_t steps_each_way = 60U;
constexpr double grow_width = 320.0;
constexpr double grow_height = 200.0;
constexpr std::uint64_t step_interval_ms = 16U;
constexpr std::uint64_t hold_ms = 250U;
constexpr std::uint64_t settle_ms = 400U;
constexpr double stall_limit_ms = 100.0;
constexpr std::size_t capture_stride = 4U;
constexpr std::size_t capture_limit = 120U;
constexpr const char* workload_text =
    "gallery-opening-page_1280x820_+320x200_60-steps-each-way_16ms";

enum class PhaseKind : std::uint8_t { drag, stepped };

const char* phase_name(const PhaseKind kind) {
    switch (kind) {
    case PhaseKind::drag:
        return "drag";
    case PhaseKind::stepped:
        return "stepped";
    }
    return "unknown";
}

// One pass of one phase: what it is, and whether it captures frames.
struct Pass final {
    PhaseKind kind{PhaseKind::stepped};
    bool capture{false};
};

// The run's state, owned by main for the whole run; read and written on the
// main queue only.
struct ProbeState final {
    std::function<void()> request_close{};
    std::string out_directory{};
    std::string capture_directory{};
    std::vector<Pass> passes{};
    std::size_t pass_index{0};
    // The step within the current pass: 0 starts it, 1..2*steps_each_way
    // move, then the hold and the release.
    std::size_t step{0};
    NSRect start_frame{};
    // The drag's current pointer, in screen coordinates.
    NSPoint drag_point{};
    std::vector<MacDrawRecord> records{};
    bool failed{false};
    bool ran{false};
};

NSWindow* gallery_window() {
    NSString* const title = [[NSString alloc] initWithBytes:gui_forms::gallery::gallery_program_name.data()
                                                     length:gui_forms::gallery::gallery_program_name.size()
                                                   encoding:NSUTF8StringEncoding];
    for (NSWindow* candidate in [NSApp windows]) {
        if ([[candidate title] isEqualToString:title]) return candidate;
    }
    return nil;
}

// The GUIFormsView in `view`'s subtree (the content view, or inside a
// backdrop container), or nil.
NSView* find_host_view(NSView* const view) {
    if (view == nil) return nil;
    if ([view respondsToSelector:@selector(readDrawTrace:)]) return view;
    for (NSView* child in [view subviews]) {
        NSView* const found = find_host_view(child);
        if (found != nil) return found;
    }
    return nil;
}

NSView* host_view() {
    NSWindow* const window = gallery_window();
    return window == nil ? nil : find_host_view([window contentView]);
}

// The size offset at `step` (1..2*steps_each_way): rising to the full
// growth at steps_each_way, back to zero at the end.
NSSize step_offset(const std::size_t step) {
    const std::size_t rising = step <= steps_each_way ? step : 2U * steps_each_way - step;
    const double fraction = static_cast<double>(rising) / static_cast<double>(steps_each_way);
    return NSMakeSize(grow_width * fraction, grow_height * fraction);
}

void post_mouse(NSWindow* const window, const NSEventType type, const NSPoint screen_point) {
    const NSRect in_window = [window convertRectFromScreen:NSMakeRect(screen_point.x, screen_point.y, 0.0, 0.0)];
    NSEvent* const event = [NSEvent mouseEventWithType:type
                                              location:in_window.origin
                                         modifierFlags:0
                                             timestamp:[[NSProcessInfo processInfo] systemUptime]
                                          windowNumber:[window windowNumber]
                                               context:nil
                                           eventNumber:0
                                            clickCount:1
                                              pressure:type == NSEventTypeLeftMouseUp ? 0.0F : 1.0F];
    if (event != nil) [NSApp postEvent:event atStart:NO];
}

double percentile(const std::vector<double>& sorted, const std::size_t percent) {
    if (sorted.empty()) return 0.0;
    const std::size_t index = (sorted.size() - 1U) * percent / 100U;
    return sorted[index];
}

// Whether `outer` contains `inner`, with a tolerance of one unit.
bool contains_with_tolerance(const MacDrawRecord& outer, const double x, const double y, const double width,
                             const double height) {
    const double tolerance = 1.0;
    const bool left = outer.dirty_x <= x + tolerance;
    const bool top = outer.dirty_y <= y + tolerance;
    const bool right = outer.dirty_x + outer.dirty_width + tolerance >= x + width;
    const bool bottom = outer.dirty_y + outer.dirty_height + tolerance >= y + height;
    return left && top && right && bottom;
}

// Judges and reports one pass's records; returns whether it passed.
bool report_pass(ProbeState& state, const Pass& pass, NSView* const view, const bool overflow) {
    const std::vector<MacDrawRecord>& records = state.records;
    const char* const name = phase_name(pass.kind);
    std::size_t size_changes{};
    std::size_t live_resize_draws{};
    std::size_t stale{};
    std::size_t stalls{};
    std::vector<double> intervals{};
    std::vector<double> durations{};
    intervals.reserve(records.size());
    durations.reserve(records.size());
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const MacDrawRecord& record = records[index];
        durations.push_back(static_cast<double>(record.duration_ns) / 1.0e6);
        if (record.live_resize) ++live_resize_draws;
        if (index == 0U) continue;
        const MacDrawRecord& previous = records[index - 1U];
        const double interval = static_cast<double>(record.started_ns - previous.started_ns) / 1.0e6;
        intervals.push_back(interval);
        const bool resized = record.width != previous.width || record.height != previous.height;
        if (!resized) continue;
        ++size_changes;
        if (interval > stall_limit_ms) ++stalls;
        // The view is flipped: growth exposes a strip on the right and one
        // at the bottom.
        if (record.width > previous.width &&
            !contains_with_tolerance(record, previous.width, 0.0, record.width - previous.width, record.height)) {
            ++stale;
        }
        if (record.height > previous.height &&
            !contains_with_tolerance(record, 0.0, previous.height, record.width, record.height - previous.height)) {
            ++stale;
        }
    }
    const NSSize final_size = [view bounds].size;
    bool final_drawn{false};
    for (const MacDrawRecord& record : records) {
        if (!record.presented || record.width != final_size.width || record.height != final_size.height) continue;
        if (contains_with_tolerance(record, 0.0, 0.0, record.width, record.height)) final_drawn = true;
    }
    std::sort(intervals.begin(), intervals.end());
    std::sort(durations.begin(), durations.end());
    const bool ran = size_changes > 0U;
    if (pass.capture) {
        std::printf("live-resize-capture|phase=%s|draws=%zu|directory=%s/%s\n", name, records.size(),
                    state.capture_directory.c_str(), name);
        return true;
    }
    if (pass.kind == PhaseKind::drag && !ran) {
        std::printf("live-resize|phase=drag|workload=%s|drag=unavailable|draws=%zu\n", workload_text,
                    records.size());
        return true;
    }
    std::printf("live-resize|phase=%s|workload=%s|scale=%.2f|draws=%zu|size_changes=%zu|live_resize_draws=%zu"
                "|interval_median_ms=%.3f|interval_p95_ms=%.3f|interval_max_ms=%.3f"
                "|draw_median_ms=%.3f|draw_p95_ms=%.3f|draw_max_ms=%.3f|stale=%zu|stalls=%zu"
                "|final_size_drawn=%d|overflow=%d\n",
                name, workload_text, [[view window] backingScaleFactor], records.size(), size_changes,
                live_resize_draws, percentile(intervals, 50U), percentile(intervals, 95U),
                intervals.empty() ? 0.0 : intervals.back(), percentile(durations, 50U), percentile(durations, 95U),
                durations.empty() ? 0.0 : durations.back(), stale, stalls, final_drawn ? 1 : 0, overflow ? 1 : 0);
    if (!state.out_directory.empty()) {
        const std::string path = state.out_directory + "/" + name + ".csv";
        std::FILE* const file = std::fopen(path.c_str(), "w");
        if (file != nullptr) {
            std::fprintf(file, "started_ns,duration_ns,width,height,dirty_x,dirty_y,dirty_width,dirty_height,"
                               "presented,live_resize\n");
            for (const MacDrawRecord& record : records) {
                std::fprintf(file, "%llu,%llu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
                             static_cast<unsigned long long>(record.started_ns),
                             static_cast<unsigned long long>(record.duration_ns), record.width, record.height,
                             record.dirty_x, record.dirty_y, record.dirty_width, record.dirty_height,
                             record.presented ? 1 : 0, record.live_resize ? 1 : 0);
            }
            std::fclose(file);
        }
    }
    const bool passed = stale == 0U && stalls == 0U && final_drawn && !overflow && ran;
    if (!passed) std::printf("live-resize|phase=%s|FAIL\n", name);
    return passed;
}

void tick(void* context);

void schedule(ProbeState& state, const std::uint64_t milliseconds) {
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(milliseconds * NSEC_PER_MSEC));
    dispatch_after_f(when, dispatch_get_main_queue(), &state, &tick);
}

// One step of the current pass, on the main queue (which AppKit's resize
// tracking loop keeps draining, as it runs in the common modes).
void tick(void* const context) {
    ProbeState& state = *static_cast<ProbeState*>(context);
    NSWindow* const window = gallery_window();
    NSView* const view = host_view();
    if (window == nil || view == nil) {
        std::printf("live-resize|FAIL the Gallery window or its view is missing\n");
        state.failed = true;
        state.request_close();
        return;
    }
    if (state.pass_index >= state.passes.size()) {
        state.ran = true;
        state.request_close();
        return;
    }
    const Pass pass = state.passes[state.pass_index];
    const std::size_t moving_steps = 2U * steps_each_way;
    if (state.step == 0U) {
        state.start_frame = [window frame];
        std::string directory{};
        if (pass.capture) {
            directory = state.capture_directory + "/" + phase_name(pass.kind);
            [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:directory.c_str()]
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
        }
        [view setDrawTraceEnabled:YES
                 captureDirectory:std::move(directory)
                           stride:capture_stride
                            limit:pass.capture ? capture_limit : 0U];
        if (pass.kind == PhaseKind::drag) {
            // Two units inside the bottom-right corner, where AppKit's
            // resize region is; screen coordinates rise upwards.
            state.drag_point = NSMakePoint(NSMaxX(state.start_frame) - 2.0, NSMinY(state.start_frame) + 2.0);
            post_mouse(window, NSEventTypeLeftMouseDown, state.drag_point);
        }
        state.step = 1U;
        schedule(state, step_interval_ms);
        return;
    }
    if (state.step <= moving_steps) {
        const NSSize offset = step_offset(state.step);
        if (pass.kind == PhaseKind::drag) {
            const NSPoint point = NSMakePoint(NSMaxX(state.start_frame) - 2.0 + offset.width,
                                              NSMinY(state.start_frame) + 2.0 - offset.height);
            state.drag_point = point;
            post_mouse(window, NSEventTypeLeftMouseDragged, point);
        } else {
            // The top-left corner stays put, as in a corner drag.
            const NSRect frame = NSMakeRect(NSMinX(state.start_frame), NSMinY(state.start_frame) - offset.height,
                                            NSWidth(state.start_frame) + offset.width,
                                            NSHeight(state.start_frame) + offset.height);
            [window setFrame:frame display:YES];
        }
        const bool turning = state.step == steps_each_way;
        ++state.step;
        schedule(state, turning ? hold_ms : step_interval_ms);
        return;
    }
    if (state.step == moving_steps + 1U) {
        if (pass.kind == PhaseKind::drag) post_mouse(window, NSEventTypeLeftMouseUp, state.drag_point);
        ++state.step;
        schedule(state, settle_ms);
        return;
    }
    const BOOL overflow = [view readDrawTrace:state.records];
    [view setDrawTraceEnabled:NO captureDirectory:std::string{} stride:1U limit:0U];
    if (!report_pass(state, pass, view, overflow == YES)) state.failed = true;
    // Back to the starting frame before the next pass.
    [window setFrame:state.start_frame display:YES];
    ++state.pass_index;
    state.step = 0U;
    schedule(state, settle_ms);
}

using VoidCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback = std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback = std::function<gui_forms::HostServiceStatus(std::string_view)>;

class StartProbe final {
public:
    explicit StartProbe(ProbeState& state) noexcept : state_(state) {}

    void operator()(VoidCallback, VoidCallback request_close, ShowDialogCallback, ReadClipboardCallback,
                    WriteClipboardCallback) const {
        state_.request_close = std::move(request_close);
        schedule(state_, 800U);
    }

private:
    ProbeState& state_;
};

} // namespace

int main(int argc, char** argv) {
    ProbeState state{};
    for (int index = 1; index + 1 < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--out") state.out_directory = argv[index + 1];
        if (argument == "--capture") state.capture_directory = argv[index + 1];
    }
    // LaunchServices passes no arguments (cmake/run_macos_activated_test.cmake):
    // the CSVs then go to live-resize/ beside the bundle.
    if (state.out_directory.empty()) {
        NSString* const beside = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
        NSString* const directory = [beside stringByAppendingPathComponent:@"live-resize"];
        [[NSFileManager defaultManager] createDirectoryAtPath:directory
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        state.out_directory = std::string([directory UTF8String]);
    }
    state.passes.push_back(Pass{PhaseKind::drag, false});
    state.passes.push_back(Pass{PhaseKind::stepped, false});
    if (!state.capture_directory.empty()) {
        state.passes.push_back(Pass{PhaseKind::drag, true});
        state.passes.push_back(Pass{PhaseKind::stepped, true});
    }
    state.records.reserve(4096U);
    // The Gallery's application before its window, as its own main makes it,
    // with the default theme.
    const std::unique_ptr<gui_forms::Application> application = gui_forms::gallery::create_gallery_application();
    gui_forms::host::MacHostOptions options;
    options.title = std::string(gui_forms::gallery::gallery_program_name);
    options.initial_size = gui_forms::gallery::gallery_default_size;
    options.minimum_size = gui_forms::gallery::gallery_minimum_size;
    options.print_metrics_on_close = false;
    options.host_ready = StartProbe(state);
    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    const int result = gui_forms::host::run_macos(std::move(window), std::move(options));
    if (!state.ran) {
        std::printf("live-resize|FAIL the probe did not finish (host result %d)\nfailures=1\n", result);
        return 2;
    }
    const bool failed = state.failed || result != 0;
    std::printf("failures=%d\n", failed ? 1 : 0);
    return failed ? 1 : 0;
}
