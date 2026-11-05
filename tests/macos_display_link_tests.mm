#import <AppKit/AppKit.h>
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/macos_host.hpp"
#include "../src/host/macos/application/display_link_trace.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <time.h>

@interface NSView (DisplayLinkProbe)
- (void)setDisplayTraceEnabled:(BOOL)enabled;
- (MacDisplayLinkTrace)displayTraceSnapshot;
- (std::string)hostJSON;
@end

namespace {
struct Probe final {
    std::shared_ptr<gui_forms::LiveSurface> surface{};
    gui_forms::Window* model{nullptr};
    std::function<void()> show{};
    std::function<void()> hide{};
    std::chrono::steady_clock::time_point start{};
    timespec cpu{};
    unsigned int phase{0};
    // Presentation reports counted at the last active phase.
    std::uint64_t presents_seen{0};
    bool baseline{false};
    bool producing{false};
    bool finished{false};
    std::string failure{};
};

NSWindow* window() {
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:@"Display link probe"]) return candidate;
    }
    throw std::runtime_error("native window missing");
}

void publish(void* address);
void advance(void* address);

void queue(Probe& probe, const int milliseconds, dispatch_function_t function) {
    const dispatch_time_t time = dispatch_time(DISPATCH_TIME_NOW,
        static_cast<std::int64_t>(milliseconds) * 1000000);
    dispatch_after_f(time, dispatch_get_main_queue(), &probe, function);
}

void publish(void* const address) {
    Probe& probe = *static_cast<Probe*>(address);
    if (!probe.producing) return;
    try {
        gui_forms::LiveSurfaceWriteLease write = (*probe.surface).try_acquire_write(false);
        if (write) {
            const std::span<std::byte> pixels = write.pixels();
            std::fill(pixels.begin(), pixels.end(), std::byte{255});
            static_cast<void>(write.publish());
        }
        queue(probe, 4, publish);
    } catch (const std::exception& error) {
        probe.failure = error.what();
        probe.producing = false;
    }
}

void begin_measurement(Probe& probe) {
    NSView* view = window().contentView;
    [view setDisplayTraceEnabled:YES];
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &probe.cpu) != 0)
        throw std::runtime_error("process clock unavailable");
    probe.start = std::chrono::steady_clock::now();
}

// The host reports each presented frame to the surface with the display
// link's timing (threaded sub-surfaces design note sections 7 and 8), so a
// LiveSurfacePacer on the producer's thread is phased to the display.
void require_presentation_reports(Probe& probe, const char* const name) {
    const gui_forms::LivePresentationRecord record = (*probe.surface).presentation_record();
    std::printf("display-link-presented|phase=%s|presents=%llu|generation=%llu|period_us=%lld|source=%d\n",
        name, static_cast<unsigned long long>(record.presents),
        static_cast<unsigned long long>(record.presented_generation),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
            record.timing.refresh_period).count()),
        static_cast<int>(record.timing.source));
    if (record.presents <= probe.presents_seen)
        throw std::runtime_error("presented frames were not reported to the surface");
    probe.presents_seen = record.presents;
    if (record.presented_generation == 0U)
        throw std::runtime_error("no presented generation was reported");
    if (record.timing.source != gui_forms::LiveTimingSource::display_phased)
        throw std::runtime_error("the report did not carry the display link's timing");
    if (record.timing.refresh_period <= gui_forms::FrameInterval::zero() ||
        record.timing.last_vblank > gui_forms::FrameClock::now())
        throw std::runtime_error("the reported display timing is not plausible");
}

void report(Probe& probe, const char* const name, const bool active) {
    timespec cpu{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu) != 0)
        throw std::runtime_error("process clock unavailable");
    const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - probe.start;
    const double cpu_seconds = static_cast<double>(cpu.tv_sec - probe.cpu.tv_sec) +
        static_cast<double>(cpu.tv_nsec - probe.cpu.tv_nsec) / 1.0e9;
    NSView* view = window().contentView;
    const MacDisplayLinkTrace trace = [view displayTraceSnapshot];
    [view setDisplayTraceEnabled:NO];
    std::vector<double> intervals{};
    intervals.reserve(trace.count);
    for (std::size_t index = 1; index < trace.count; ++index) {
        intervals.push_back(static_cast<double>(trace.timestamps[index] -
            trace.timestamps[index - 1]) / 1.0e6);
    }
    std::sort(intervals.begin(), intervals.end());
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
    if (!intervals.empty()) {
        median = intervals[intervals.size() / 2];
        p95 = intervals[(intervals.size() - 1) * 95 / 100];
        maximum = intervals.back();
    }
    std::printf("display-link|phase=%s|wall_s=%.6f|process_cpu_ms=%.6f|cpu_percent=%.6f|ticks=%zu|median_ms=%.6f|p95_ms=%.6f|max_ms=%.6f|overflow=%d\n",
        name, wall.count(), cpu_seconds * 1000.0, cpu_seconds / wall.count() * 100.0,
        trace.count, median, p95, maximum, static_cast<int>(trace.overflow));
    const std::string host = [view hostJSON];
    std::printf("display-link-host|phase=%s|%s\n", name, host.c_str());
    if (trace.overflow || (active && trace.count < 20))
        throw std::runtime_error("missing active pacing or trace overflow");
    if (!probe.baseline && !active && trace.count != 0)
        throw std::runtime_error("idle or hidden display link kept ticking");
    if (active) require_presentation_reports(probe, name);
}

void advance(void* const address) {
    Probe& probe = *static_cast<Probe*>(address);
    try {
        if (probe.phase == 0) {
            NSWindow* native = window();
            const double scale = native.backingScaleFactor;
            [native setContentSize:NSMakeSize(1060.0 / scale, 618.0 / scale)];
            std::printf("display-link-environment|scale=%.1f|screen_max_fps=%ld|pixels=1060x618|producer=main_queue_4ms_full_fill\n",
                scale, static_cast<long>(native.screen.maximumFramesPerSecond));
            probe.producing = true;
            publish(&probe);
            queue(probe, 1000, advance);
        } else if (probe.phase == 1) {
            begin_measurement(probe);
            queue(probe, 4000, advance);
        } else if (probe.phase == 2) {
            report(probe, "active", true);
            probe.producing = false;
            queue(probe, 1000, advance);
        } else if (probe.phase == 3) {
            begin_measurement(probe);
            queue(probe, 3000, advance);
        } else if (probe.phase == 4) {
            report(probe, "attached_idle", false);
            probe.hide();
            queue(probe, 1000, advance);
        } else if (probe.phase == 5) {
            begin_measurement(probe);
            queue(probe, 3000, advance);
        } else if (probe.phase == 6) {
            report(probe, "hidden", false);
            probe.show();
            probe.producing = true;
            publish(&probe);
            queue(probe, 1000, advance);
        } else if (probe.phase == 7) {
            begin_measurement(probe);
            queue(probe, 2000, advance);
        } else {
            report(probe, "resumed", true);
            probe.producing = false;
            probe.finished = true;
            [window() close];
        }
        ++probe.phase;
    } catch (const std::exception& error) {
        probe.failure = error.what();
        probe.producing = false;
        for (NSWindow* candidate in NSApp.windows) [candidate close];
    }
}

class Ready final {
public:
    explicit Ready(Probe& probe) : probe_(probe) {}
    void operator()(std::function<void()> show, std::function<void()> hide) const {
        probe_.show = std::move(show);
        probe_.hide = std::move(hide);
        queue(probe_, 1000, advance);
    }
private:
    Probe& probe_;
};
} // namespace

int main(const int argc, const char* const* argv) {
    Probe probe{};
    probe.baseline = argc == 2 && std::strcmp(argv[1], "--baseline") == 0;
    probe.surface = gui_forms::LiveSurface::create({.width = 1060, .height = 618,
        .pixel_format = gui_forms::native_live_surface_pixel_format(), .opaque = true});
    const std::shared_ptr<gui_forms::Control> root =
        gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("live"));
    if (argc == 2 && std::strcmp(argv[1], "--overlay") == 0) {
        const std::shared_ptr<gui_forms::Control> overlay =
            gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("overlay"));
        (*overlay).set_requested_bounds({20, 20, 100, 100});
        (*overlay).set_paint_plane(gui_forms::PaintPlane::overlay);
        (*root).add_child(overlay);
    }
    gui_forms::host::MacApplicationWindow entry{};
    entry.stable_id = "display-link";
    entry.model = std::make_unique<gui_forms::Window>(root, gui_forms::Size{1060, 618});
    probe.model = entry.model.get();
    if (!(*entry.model).queue_live_surface_presentation(root, probe.surface)) return 2;
    entry.options.title = "Display link probe";
    entry.options.initial_size = {1060, 618};
    entry.options.visibility_ready = Ready(probe);
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.push_back(std::move(entry));
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    if (result != 0 || !probe.finished || !probe.failure.empty()) {
        std::fprintf(stderr, "display-link probe failed: %s\n", probe.failure.c_str());
        return 1;
    }
    return 0;
}
