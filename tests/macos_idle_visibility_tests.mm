#import <AppKit/AppKit.h>

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/platform/macos_host.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <time.h>

// Private diagnostics already used by native exposure fixtures; no public ABI.
@interface NSView (IdleVisibilityProbe)
- (std::string)hostJSON;
- (void)collectDamage;
- (BOOL)setCpuAttributionEnabled:(const BOOL)enabled;
- (std::array<std::uint64_t, 13>)cpuAttributionSnapshot;
@end

namespace {

struct Probe final {
    gui_forms::Window* child_model{nullptr}; // Owned by run_macos_application.
    std::shared_ptr<gui_forms::TextBox> text{};
    gui_forms::FrameRequestToken timer{};
    std::function<void()> show{};
    std::function<void()> hide{};
    std::uint64_t timer_calls{0};
    std::uint64_t closed_count{0};
    std::uint64_t baseline_wakes{0};
    std::uint64_t baseline_draws{0};
    std::uint32_t stage{0};
    bool attached_hidden{false};
    bool completed{false};
    std::string failure{};
    std::string final_snapshot{};
    std::string phase_baseline{};
};

NSWindow* find_window(NSString* title) {
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:title]) return candidate;
    }
    return nil;
}

std::uint64_t counter(const std::string& snapshot, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const std::size_t position = snapshot.find(marker);
    if (position == std::string::npos) throw std::runtime_error("missing host counter");
    const std::string digits = snapshot.substr(position + marker.size());
    const std::uint64_t value = static_cast<std::uint64_t>(std::stoull(digits));
    return value;
}

void require(Probe& probe, bool condition, const char* message) {
    if (!condition && probe.failure.empty()) probe.failure = message;
}

double extent(const std::string& snapshot, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const std::size_t position = snapshot.find(marker);
    if (position == std::string::npos) throw std::runtime_error("missing host extent");
    const std::string digits = snapshot.substr(position + marker.size());
    const double value = std::stod(digits);
    return value;
}

void verify_phases(Probe& probe, const std::string& snapshot, const bool active) {
    const std::array<std::string_view, 6> names{
        "raster_prepare", "retained", "raster_finish", "cg_setup", "cg_draw", "cg_release"};
    for (const std::string_view name : names) {
        const std::string prefix = "paint_" + std::string(name);
        const std::uint64_t calls = counter(snapshot, prefix + "_calls");
        const std::uint64_t before_calls = counter(probe.phase_baseline, prefix + "_calls");
        const std::uint64_t duration = counter(snapshot, prefix + "_nanoseconds");
        const std::uint64_t before_duration = counter(probe.phase_baseline, prefix + "_nanoseconds");
        const std::uint64_t maximum = counter(snapshot, prefix + "_maximum_nanoseconds");
        require(probe, maximum <= duration, "phase maximum exceeds its total");
        const std::string unsaturated = "\"" + prefix + "_saturated\":false";
        require(probe, snapshot.find(unsaturated) != std::string::npos,
                "native phase counters saturated during bounded fixture");
        if (active) {
            require(probe, calls > before_calls && duration >= before_duration,
                    "visible caret did not record completed native paint phases");
        } else {
            require(probe, calls == before_calls && duration == before_duration,
                    "hidden interval performed native paint phase work");
        }
    }
    const std::uint64_t setups = counter(snapshot, "paint_cg_setup_calls");
    const std::uint64_t draws = counter(snapshot, "paint_cg_draw_calls");
    const std::uint64_t releases = counter(snapshot, "paint_cg_release_calls");
    require(probe, setups == draws && setups == releases,
            "healthy fixture must pair image setup, draw and release");
}

class CountTimer final {
public:
    explicit CountTimer(Probe& probe) noexcept : probe_(probe) {}
    void operator()(gui_forms::FrameTime) const { ++probe_.timer_calls; }
private:
    Probe& probe_;
};

class CountClosed final {
public:
    explicit CountClosed(Probe& probe) noexcept : probe_(probe) {}
    void operator()() const { ++probe_.closed_count; }
private:
    Probe& probe_;
};

void advance(void* context);

void enqueue(Probe& probe, std::int64_t milliseconds) {
    const std::int64_t nanoseconds = milliseconds * 1000000;
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, nanoseconds);
    // Exactly one outstanding callback; stack owner survives the native run loop.
    dispatch_after_f(deadline, dispatch_get_main_queue(), &probe, advance);
}

void exercise(Probe& probe) {
    NSWindow* child = find_window(@"Idle probe child");
    NSWindow* primary = find_window(@"Idle probe primary");
    if (child == nil || primary == nil) throw std::runtime_error("missing owned window");
    const std::string snapshot = [child.contentView hostJSON];
    std::printf("idle-stage=%u|host=%s\n", probe.stage, snapshot.c_str());
    if (probe.stage == 0) {
        require(probe, probe.attached_hidden, "initial hidden model was not occluded");
        require(probe, (*probe.child_model).occluded(), "hidden model became visible");
        require(probe, probe.timer_calls != 0, "hidden explicit UI timer did not run");
        probe.timer.disconnect();
        const bool hidden_deadline = (*probe.child_model).next_wake().has_value();
        require(probe, !hidden_deadline, "initial hidden caret retained a wake deadline");
        probe.show();
    } else if (probe.stage == 1) {
        require(probe, child.isVisible && !(*probe.child_model).occluded(),
                "show did not synchronize visibility");
        const bool focused = (*probe.child_model).request_focus(probe.text);
        require(probe, focused, "visible textbox could not focus");
        probe.baseline_wakes = counter(snapshot, "scheduled_wake_count");
        probe.baseline_draws = counter(snapshot, "native_draw_count");
        probe.phase_baseline = snapshot;
    } else if (probe.stage == 2) {
        const std::uint64_t wakes = counter(snapshot, "scheduled_wake_count");
        const std::uint64_t draws = counter(snapshot, "native_draw_count");
        require(probe, wakes > probe.baseline_wakes, "focused caret deadline did not wake");
        require(probe, draws > probe.baseline_draws, "focused caret did not paint");
        verify_phases(probe, snapshot, true);
        const double destination = extent(snapshot, "last_cg_destination_area");
        const NSRect bounds = child.contentView.bounds;
        const double expected_destination = bounds.size.width * bounds.size.height;
        require(probe, std::abs(destination - expected_destination) < 1.0e-6,
                "reported CG destination must match submitted logical window bounds");
        const double native_dirty = extent(snapshot, "last_native_dirty_area");
        const double frame_damage = extent(snapshot, "last_frame_damage_bounds_area");
        const double clip = extent(snapshot, "last_cg_clip_bounds_area");
        const std::uint64_t source_bytes = counter(snapshot, "last_cg_source_bytes");
        require(probe, native_dirty > 0.0 && frame_damage > 0.0 && clip > 0.0 && source_bytes > 0U,
                "visible presentation must report nonempty submitted extents");
        probe.hide();
    } else if (probe.stage == 3) {
        require(probe, !child.isVisible && (*probe.child_model).occluded(),
                "hide did not synchronize visibility");
        // Let ordering callbacks settle before measuring the hidden interval.
        probe.baseline_wakes = counter(snapshot, "scheduled_wake_count");
        probe.baseline_draws = counter(snapshot, "native_draw_count");
        probe.phase_baseline = snapshot;
    } else if (probe.stage == 4) {
        const std::uint64_t wakes = counter(snapshot, "scheduled_wake_count");
        const std::uint64_t draws = counter(snapshot, "native_draw_count");
        require(probe, wakes == probe.baseline_wakes, "hidden caret kept waking");
        require(probe, draws == probe.baseline_draws, "hidden window kept drawing");
        verify_phases(probe, snapshot, false);
        probe.show();
    } else {
        require(probe, child.isVisible && !(*probe.child_model).occluded(),
                "second show did not restore visibility");
        probe.completed = true;
        [primary performClose:nil];
        return;
    }
    ++probe.stage;
    enqueue(probe, 750);
}

void advance(void* context) {
    Probe& probe = *static_cast<Probe*>(context);
    try {
        exercise(probe);
    } catch (const std::exception& error) {
        probe.failure = error.what();
        NSWindow* primary = find_window(@"Idle probe primary");
        [primary performClose:nil];
    }
}

class CaptureVisibility final {
public:
    explicit CaptureVisibility(Probe& probe) noexcept : probe_(probe) {}
    void operator()(std::function<void()> show, std::function<void()> hide) const {
        probe_.show = std::move(show);
        probe_.hide = std::move(hide);
        probe_.attached_hidden = (*probe_.child_model).occluded();
        const bool focused = (*probe_.child_model).request_focus(probe_.text);
        require(probe_, focused, "hidden textbox could not establish model focus");
        const std::chrono::milliseconds interval(100);
        const gui_forms::FrameTime deadline = gui_forms::FrameClock::now() + interval;
        probe_.timer = (*probe_.child_model).schedule_ui_timer(
            *probe_.text, interval, deadline, CountTimer(probe_));
        enqueue(probe_, 750);
    }
private:
    Probe& probe_;
};

class CaptureFinal final {
public:
    explicit CaptureFinal(Probe& probe) noexcept : probe_(probe) {}
    void operator()(std::string_view, std::string_view host) const {
        probe_.final_snapshot = host;
    }
private:
    Probe& probe_;
};

} // namespace

#include "macos_paint_cost_experiment.inc"
#include "macos_focused_cpu_experiment.inc"
#include "macos_cpu_profile_workload.inc"

int main(const int argc, char** const argv) {
    if (argc == 3 && std::strcmp(argv[1], "--cpu-profile-hold") == 0) {
        const int result = run_cpu_profile_workload(argv[2]);
        return result;
    }
    if (argc == 2 && std::strcmp(argv[1], "--focused-cpu-attribution") == 0) {
        const int result = run_focused_cpu_experiment();
        return result;
    }
    if (argc == 2 && std::strcmp(argv[1], "--paint-cost-experiment") == 0) {
        const int result = run_paint_cost_experiment();
        return result;
    }
    if (argc != 1) return 2;
    Probe probe{};
    gui_forms::host::MacApplicationWindow primary{};
    primary.stable_id = "idle.primary";
    std::shared_ptr<gui_forms::Panel> root = std::make_shared<gui_forms::Panel>(
        gui_forms::StableId("idle.primary.root"));
    primary.model = std::make_unique<gui_forms::Window>(root, gui_forms::Size{320, 200});
    primary.options.title = "Idle probe primary";
    primary.options.initial_size = {320, 200};
    primary.options.closed = CountClosed(probe);

    gui_forms::host::MacApplicationWindow child{};
    child.stable_id = "idle.child";
    child.owner_id = primary.stable_id;
    probe.text = std::make_shared<gui_forms::TextBox>(gui_forms::StableId("idle.text"));
    child.model = std::make_unique<gui_forms::Window>(probe.text, gui_forms::Size{320, 200});
    probe.child_model = child.model.get();
    child.options.title = "Idle probe child";
    child.options.initial_size = {320, 200};
    child.options.initially_visible = false;
    child.options.visibility_ready = CaptureVisibility(probe);
    child.options.final_snapshot = CaptureFinal(probe);
    child.options.closed = CountClosed(probe);

    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.push_back(std::move(primary));
    windows.push_back(std::move(child));
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    probe.timer.disconnect();
    if (result != 0 || !probe.completed || probe.closed_count != 2 || !probe.failure.empty()) {
        std::fprintf(stderr, "idle visibility probe failed: %s\n", probe.failure.c_str());
        return 1;
    }
    std::printf("Initial hidden, explicit UI timer, caret, hide/show and shutdown passed\n");
    return 0;
}
