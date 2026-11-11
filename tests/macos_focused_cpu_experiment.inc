// Opt-in native deferred-paint diagnostic. No change to the static cost probe.
#include <cerrno>

// Private native-fixture wire image, independent of the existing 13-field CPU ABI.
using FocusedCausalSnapshot = std::array<std::array<std::uint64_t, 13>, 257>;
@interface NSView (FocusedCausalProbe)
- (FocusedCausalSnapshot)cpuCausalTraceSnapshot;
@end

namespace {

constexpr std::size_t focused_window_count = 9U;
constexpr std::size_t focused_interval_count = 4U;
constexpr std::chrono::seconds focused_settle(3);
constexpr std::chrono::seconds focused_interval(10);

struct FocusedClockRead final {
    timespec raw{};
    int status{-1};
    int error{0};
    bool attempted{false};
    bool valid{false};
    std::uint64_t nanoseconds{0};
};

struct FocusedWindowObservation final {
    std::array<std::uint64_t, 13> cpu{};
    FocusedCausalSnapshot causal{};
    gui_forms::MetricsSnapshot metrics{};
    std::string host{};
    std::string capture_failure{};
    NSRect bounds{};
    double scale{0.0};
    bool present{false};
    bool disabled{false};
    bool captured{false};
    bool visible{false};
    bool occluded{false};
    bool focused{false};
    bool deadline{false};
    bool key{false};
    bool application_active{false};
};

struct FocusedCpuProbe final {
    // Models belong to the synchronous native run; one deferred callback borrows
    // this stack owner at a time. Native views are borrowed only inside callbacks.
    std::array<gui_forms::Window*, focused_window_count> models{};
    std::array<std::shared_ptr<gui_forms::TextBox>, focused_window_count> editors{};
    std::array<std::string, focused_window_count> before{};
    std::array<NSRect, focused_window_count> bounds{};
    std::array<double, focused_window_count> scales{};
    std::array<double, 4> expected_extents{};
    std::uint64_t expected_source_bytes{0};
    std::uint64_t expected_focused_draws{0};
    FocusedClockRead process_start{};
    FocusedClockRead thread_start{};
    FocusedClockRead process_end{};
    FocusedClockRead thread_end{};
    std::array<FocusedWindowObservation, focused_window_count> observations{};
    std::chrono::steady_clock::time_point wall_start{};
    std::chrono::steady_clock::time_point wall_end{};
    std::size_t phase{0};
    std::size_t closed{0};
    bool measuring{false};
    bool completed{false};
    bool have_geometry{false};
    bool wall_started{false};
    bool wall_ended{false};
    bool observations_captured{false};
    bool raw_emitted{false};
    bool raw_complete{false};
    std::string failure{};
};

void focused_require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

bool focused_mode(const std::size_t phase) noexcept {
    const bool focused = phase == 0U || phase == 3U;
    return focused;
}

NSString* focused_title(const std::size_t index) {
    NSString* title = [NSString stringWithFormat:@"Focused CPU window %zu", index];
    return title;
}

NSWindow* focused_window(const std::size_t index) {
    NSString* title = focused_title(index);
    NSWindow* window = find_window(title);
    focused_require(window != nil, "focused CPU window missing");
    return window;
}

void read_focused_cpu_clock(const clockid_t clock_id, FocusedClockRead& result) noexcept {
    result.attempted = true;
    result.status = clock_gettime(clock_id, &result.raw);
    if (result.status != 0) {
        result.error = errno;
        return;
    }
    if (result.raw.tv_sec < 0 || result.raw.tv_nsec < 0 ||
        result.raw.tv_nsec >= 1000000000L) return;
    const std::uint64_t seconds = static_cast<std::uint64_t>(result.raw.tv_sec);
    const std::uint64_t fraction = static_cast<std::uint64_t>(result.raw.tv_nsec);
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    if (seconds > (limit - fraction) / 1000000000U) return;
    result.nanoseconds = seconds * 1000000000U + fraction;
    result.valid = true;
}

std::uint64_t focused_delta(const std::string& before, const std::string& after,
                            const std::string& key) {
    const std::uint64_t first = counter(before, key);
    const std::uint64_t last = counter(after, key);
    focused_require(last >= first, "host counter regressed");
    const std::uint64_t delta = last - first;
    return delta;
}

void advance_focused_cpu(void* const context);

void queue_focused_cpu(FocusedCpuProbe& probe, const std::chrono::seconds delay) {
    const std::chrono::nanoseconds duration = delay;
    const std::int64_t nanoseconds = duration.count();
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, nanoseconds);
    dispatch_after_f(deadline, dispatch_get_main_queue(), &probe, advance_focused_cpu);
}

void prepare_focused_phase(FocusedCpuProbe& probe) {
    gui_forms::Window& model = *probe.models[0];
    bool changed{false};
    if (focused_mode(probe.phase)) changed = model.request_focus(probe.editors[0]);
    else changed = model.request_focus({});
    focused_require(changed, "could not set requested model focus");
    // Focus invalidation and native transactions settle outside the CPU interval.
    queue_focused_cpu(probe, focused_settle);
}

void check_focused_state(FocusedCpuProbe& probe, const std::size_t index) {
    NSWindow* window = focused_window(index);
    gui_forms::Window& model = *probe.models[index];
    const bool primary = index == 0U;
    focused_require(static_cast<bool>(window.isVisible) == primary &&
        model.occluded() == !primary, "visibility or occlusion changed");
    const bool expect_focus = primary && focused_mode(probe.phase);
    focused_require(static_cast<bool>(model.focused_control()) == expect_focus,
                    "model focus changed");
    focused_require(model.next_wake().has_value() == expect_focus,
                    "unexpected autonomous deadline state");
    if (primary) {
        focused_require(window.isKeyWindow && NSApp.isActive,
                        "native key window or application activation changed");
    }
    const NSRect bounds = window.contentView.bounds;
    const double scale = window.backingScaleFactor;
    if (!probe.have_geometry) {
        probe.bounds[index] = bounds;
        probe.scales[index] = scale;
    } else {
        focused_require(NSEqualRects(bounds, probe.bounds[index]) &&
            scale == probe.scales[index], "view extent or backing scale changed");
    }
}

void begin_focused_interval(FocusedCpuProbe& probe) {
    probe.process_start = {};
    probe.thread_start = {};
    probe.process_end = {};
    probe.thread_end = {};
    probe.observations = {};
    probe.wall_start = {};
    probe.wall_end = {};
    probe.wall_started = false;
    probe.wall_ended = false;
    probe.observations_captured = false;
    probe.raw_emitted = false;
    probe.raw_complete = false;
    for (std::string& before : probe.before) before.clear();
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        check_focused_state(probe, index);
        NSWindow* window = focused_window(index);
        const gui_forms::MetricsSnapshot metrics = (*probe.models[index]).metrics_snapshot();
        focused_require(metrics.renderer_name.find("bundled fonts (optional Unicode fallback)") !=
            std::string::npos, "bundled fonts not ready; CPU interval rejected");
        probe.before[index] = [window.contentView hostJSON];
        (*probe.models[index]).reset_activity_metrics();
        const BOOL enabled = [window.contentView setCpuAttributionEnabled:YES];
        focused_require(enabled == YES, "could not enable private CPU diagnostic");
    }
    probe.have_geometry = true;
    // Sequential CPU readings use the same clocks at each boundary. Their small
    // skew is not interpreted as other-thread CPU. Logging/snapshots stay outside.
    read_focused_cpu_clock(CLOCK_PROCESS_CPUTIME_ID, probe.process_start);
    read_focused_cpu_clock(CLOCK_THREAD_CPUTIME_ID, probe.thread_start);
    probe.wall_start = std::chrono::steady_clock::now();
    probe.wall_started = true;
    focused_require(probe.process_start.valid && probe.thread_start.valid,
                    "CPU start clock read or conversion failed");
    probe.measuring = true;
    queue_focused_cpu(probe, focused_interval);
}

void capture_focused_windows(FocusedCpuProbe& probe) {
    // Freeze attribution on ALL views before serializing any host/metrics data.
    // This main-queue callback neither pumps events nor requests painting.
    std::array<NSWindow*, focused_window_count> windows{};
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        NSWindow* const window = find_window(focused_title(index));
        windows[index] = window;
        FocusedWindowObservation& observation = probe.observations[index];
        observation.present = window != nil && window.contentView != nil;
        if (observation.present) {
            const BOOL disabled = [window.contentView setCpuAttributionEnabled:NO];
            observation.disabled = disabled == YES;
        }
    }
    // No printing, validation or metrics-to-JSON work between window captures.
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        FocusedWindowObservation& observation = probe.observations[index];
        if (!observation.present) continue;
        NSWindow* const window = windows[index];
        gui_forms::Window& model = *probe.models[index];
        try {
            observation.cpu = [window.contentView cpuAttributionSnapshot];
            observation.causal = [window.contentView cpuCausalTraceSnapshot];
            observation.metrics = model.metrics_snapshot();
            observation.bounds = window.contentView.bounds;
            observation.scale = window.backingScaleFactor;
            observation.visible = static_cast<bool>(window.isVisible);
            observation.occluded = model.occluded();
            observation.focused = static_cast<bool>(model.focused_control());
            observation.deadline = model.next_wake().has_value();
            observation.key = static_cast<bool>(window.isKeyWindow);
            observation.application_active = static_cast<bool>(NSApp.isActive);
            observation.captured = true;
        } catch (const std::exception& error) {
            observation.capture_failure = error.what();
        }
    }
    // hostJSON serializes native counters. Defer it until every attribution,
    // model metric and state snapshot above has been taken.
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        FocusedWindowObservation& observation = probe.observations[index];
        if (!observation.present) continue;
        NSWindow* const window = windows[index];
        try {
            observation.host = [window.contentView hostJSON];
        } catch (const std::exception& error) {
            observation.captured = false;
            observation.capture_failure = error.what();
        }
    }
    probe.observations_captured = true;
}

void emit_focused_clock(const std::size_t phase, const char* const name,
                         const FocusedClockRead& value) {
    std::printf("focused-cpu-raw-clock|interval=%zu|comparison_accepted=false|name=%s|attempted=%d|status=%d|errno=%d|raw_sec=%lld|raw_nsec=%lld|conversion_valid=%d|ns=%llu\n",
        phase, name, value.attempted, value.status, value.error,
        static_cast<long long>(value.raw.tv_sec), static_cast<long long>(value.raw.tv_nsec),
        value.valid, static_cast<unsigned long long>(value.nanoseconds));
}

void emit_focused_causal_trace(const std::size_t phase, const std::size_t window,
                              const FocusedCausalSnapshot& snapshot) {
    const std::array<std::uint64_t, 13>& header = snapshot[0];
    // Header fields: version/count, overflow/ambiguity/clock/serial failures,
    // unpaired/unfinished draw, pending deadlines, enabled, then reserved x3.
    const bool complete = header[0] == 1U && header[1] <= 256U &&
        header[2] == 0U && header[3] == 0U && header[4] == 0U && header[5] == 0U &&
        header[6] == 0U && header[7] == 0U && header[8] == 0U && header[9] == 0U;
    std::printf("focused-cpu-raw-trace|interval=%zu|window=%zu|comparison_accepted=false|version=%llu|rows=%llu|overflow=%llu|ambiguous=%llu|clock_failure=%llu|serial_exhausted=%llu|unpaired=%llu|unfinished_draw=%llu|pending_deadlines=%llu|enabled=%llu|header_status_clear=%d\n",
        phase, window, static_cast<unsigned long long>(header[0]),
        static_cast<unsigned long long>(header[1]), static_cast<unsigned long long>(header[2]),
        static_cast<unsigned long long>(header[3]), static_cast<unsigned long long>(header[4]),
        static_cast<unsigned long long>(header[5]), static_cast<unsigned long long>(header[6]),
        static_cast<unsigned long long>(header[7]), static_cast<unsigned long long>(header[8]),
        static_cast<unsigned long long>(header[9]), complete);
    const std::size_t count = header[1] <= 256U ? static_cast<std::size_t>(header[1]) : 256U;
    for (std::size_t index = 0; index < count; ++index) {
        const std::array<std::uint64_t, 13>& row = snapshot[index + 1U];
        std::printf("focused-cpu-raw-trace-row|interval=%zu|window=%zu|row=%zu|comparison_accepted=false|kind=%llu|time_ns=%llu|arm=%llu|desired_ns=%llu|delay_ns=%llu|wake=%llu|poll=%llu|fired=%llu|next_ns=%llu|linked_poll=%llu|draw=%llu|pending=%llu|flags=%llu\n",
            phase, window, index, static_cast<unsigned long long>(row[0]),
            static_cast<unsigned long long>(row[1]), static_cast<unsigned long long>(row[2]),
            static_cast<unsigned long long>(row[3]), static_cast<unsigned long long>(row[4]),
            static_cast<unsigned long long>(row[5]), static_cast<unsigned long long>(row[6]),
            static_cast<unsigned long long>(row[7]), static_cast<unsigned long long>(row[8]),
            static_cast<unsigned long long>(row[9]), static_cast<unsigned long long>(row[10]),
            static_cast<unsigned long long>(row[11]), static_cast<unsigned long long>(row[12]));
    }
}

void emit_focused_observations(FocusedCpuProbe& probe) {
    bool complete{true};
    const std::chrono::duration<double> wall = probe.wall_end - probe.wall_start;
    const std::chrono::steady_clock::duration start = probe.wall_start.time_since_epoch();
    const std::chrono::steady_clock::duration end = probe.wall_end.time_since_epoch();
    std::printf("focused-cpu-raw-interval|interval=%zu|comparison_accepted=false|mode=%s|wall_started=%d|wall_ended=%d|wall_start_ticks=%lld|wall_end_ticks=%lld|wall_tick_ratio=%lld/%lld|wall_seconds=%.9f|expected_focused_draws=%llu|expected_extents=%.17g,%.17g,%.17g,%.17g|expected_source_bytes=%llu\n",
        probe.phase, focused_mode(probe.phase) ? "focused" : "cleared",
        probe.wall_started, probe.wall_ended, static_cast<long long>(start.count()),
        static_cast<long long>(end.count()),
        static_cast<long long>(std::chrono::steady_clock::period::num),
        static_cast<long long>(std::chrono::steady_clock::period::den), wall.count(),
        static_cast<unsigned long long>(probe.expected_focused_draws),
        probe.expected_extents[0], probe.expected_extents[1], probe.expected_extents[2],
        probe.expected_extents[3], static_cast<unsigned long long>(probe.expected_source_bytes));
    emit_focused_clock(probe.phase, "process_start", probe.process_start);
    emit_focused_clock(probe.phase, "thread_start", probe.thread_start);
    emit_focused_clock(probe.phase, "thread_end", probe.thread_end);
    emit_focused_clock(probe.phase, "process_end", probe.process_end);
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        const FocusedWindowObservation& observation = probe.observations[index];
        emit_focused_causal_trace(probe.phase, index, observation.causal);
        if (!observation.present || !observation.captured) complete = false;
        const std::array<std::uint64_t, 13>& cpu = observation.cpu;
        const NSRect& expected = probe.bounds[index];
        std::printf("focused-cpu-raw-window|interval=%zu|window=%zu|comparison_accepted=false|present=%d|disabled=%d|captured=%d|capture_failure=%s|visible=%d|occluded=%d|focused=%d|deadline=%d|key=%d|application_active=%d|bounds=%.17g,%.17g,%.17g,%.17g|scale=%.17g|expected_bounds=%.17g,%.17g,%.17g,%.17g|expected_scale=%.17g|draw_calls=%llu|draw_cpu_ns=%llu|collect_calls=%llu|collect_cpu_ns=%llu|clock_failures=%llu|overlaps=%llu|saturated=%llu|extent_mismatches=%llu|first_draw_wall_ns=%llu|last_draw_wall_ns=%llu|min_draw_gap_ns=%llu|max_draw_gap_ns=%llu|draw_wall_count=%llu|host_before=%s|host=%s\n",
            probe.phase, index, observation.present, observation.disabled,
            observation.captured, observation.capture_failure.c_str(),
            observation.visible, observation.occluded, observation.focused,
            observation.deadline, observation.key, observation.application_active,
            observation.bounds.origin.x, observation.bounds.origin.y,
            observation.bounds.size.width, observation.bounds.size.height, observation.scale,
            expected.origin.x, expected.origin.y, expected.size.width, expected.size.height,
            probe.scales[index], static_cast<unsigned long long>(cpu[0]),
            static_cast<unsigned long long>(cpu[1]), static_cast<unsigned long long>(cpu[2]),
            static_cast<unsigned long long>(cpu[3]), static_cast<unsigned long long>(cpu[4]),
            static_cast<unsigned long long>(cpu[5]), static_cast<unsigned long long>(cpu[6]),
            static_cast<unsigned long long>(cpu[7]), static_cast<unsigned long long>(cpu[8]),
            static_cast<unsigned long long>(cpu[9]), static_cast<unsigned long long>(cpu[10]),
            static_cast<unsigned long long>(cpu[11]), static_cast<unsigned long long>(cpu[12]),
            probe.before[index].c_str(), observation.host.c_str());
        try {
            const std::string metrics_json = observation.metrics.to_json();
            std::printf("focused-cpu-raw-metrics|interval=%zu|window=%zu|comparison_accepted=false|metrics=%s\n",
                probe.phase, index, metrics_json.c_str());
        } catch (const std::exception& error) {
            complete = false;
            std::printf("focused-cpu-raw-metrics|interval=%zu|window=%zu|comparison_accepted=false|serialization_failed=%s\n",
                probe.phase, index, error.what());
        }
    }
    std::fflush(stdout);
    probe.raw_emitted = true;
    probe.raw_complete = complete && std::ferror(stdout) == 0;
}

void verify_focused_window(FocusedCpuProbe& probe, const std::size_t index,
                           std::uint64_t& draw_cpu, std::uint64_t& collect_cpu) {
    const FocusedWindowObservation& observation = probe.observations[index];
    focused_require(observation.present && observation.captured, "window snapshot unavailable");
    focused_require(observation.disabled, "could not disable private CPU diagnostic");
    const std::array<std::uint64_t, 13>& trace = observation.causal[0];
    focused_require(trace[0] == 1U && trace[1] <= 256U && trace[2] == 0U &&
        trace[3] == 0U && trace[4] == 0U && trace[5] == 0U && trace[6] == 0U &&
        trace[7] == 0U && trace[8] == 0U && trace[9] == 0U,
        "causal trace incomplete, ambiguous or invalid");
    const bool primary = index == 0U;
    focused_require(observation.visible == primary && observation.occluded == !primary,
                    "visibility or occlusion changed");
    const bool expect_focus = primary && focused_mode(probe.phase);
    focused_require(observation.focused == expect_focus, "model focus changed");
    focused_require(observation.deadline == expect_focus, "unexpected autonomous deadline state");
    if (primary) {
        focused_require(observation.key && observation.application_active,
                        "native key window or application activation changed");
    }
    focused_require(NSEqualRects(observation.bounds, probe.bounds[index]) &&
        observation.scale == probe.scales[index], "view extent or backing scale changed");
    const std::array<std::uint64_t, 13>& cpu = observation.cpu;
    const std::string& after = observation.host;
    const gui_forms::MetricsSnapshot& metrics = observation.metrics;
    const bool active = index == 0U && focused_mode(probe.phase);
    const std::uint64_t draws = focused_delta(probe.before[index], after, "native_draw_count");
    const std::uint64_t wakes = focused_delta(probe.before[index], after, "scheduled_wake_count");
    focused_require(cpu[4] == 0U && cpu[5] == 0U && cpu[6] == 0U && cpu[7] == 0U,
                    "CPU read, overlap, saturation or extent diagnostic failure");
    focused_require(cpu[0] == draws && cpu[12] == draws,
                    "draw attribution count mismatch");
    const std::uint64_t collections = focused_delta(probe.before[index], after, "damage_collection_count");
    focused_require(cpu[2] == collections, "damage collection attribution count mismatch");
    std::array<std::uint64_t, 5> trace_counts{};
    const std::size_t trace_count = static_cast<std::size_t>(trace[1]);
    for (std::size_t row_index = 0; row_index < trace_count; ++row_index) {
        const std::uint64_t kind = observation.causal[row_index + 1U][0];
        focused_require(kind >= 1U && kind <= 5U, "unknown causal trace event kind");
        const std::size_t kind_index = static_cast<std::size_t>(kind - 1U);
        ++trace_counts[kind_index];
    }
    focused_require(trace_counts[1] == wakes && trace_counts[2] == collections &&
        trace_counts[3] == draws && trace_counts[4] == draws,
        "causal trace event counts differ from scalar diagnostics");
    focused_require(focused_delta(probe.before[index], after, "display_tick_count") == 0U,
        "unexpected display-link work");
    focused_require(metrics.measure_passes == 0U && metrics.arrange_passes == 0U &&
        metrics.focus_transitions == 0U && metrics.input_events == 0U &&
        metrics.occlusion_suspensions == 0U && metrics.occlusion_resumes == 0U,
        "layout, focus, input or occlusion changed in measured interval");
    if (active) {
        // armWakeTimer floors to milliseconds, so an early wake may legitimately
        // rearm once before the same caret deadline. Do not equate wakes to paints.
        focused_require(draws >= 18U && draws <= 19U && wakes >= draws &&
            wakes <= 2U * draws + 1U &&
            metrics.frame_deadlines_fired == draws && metrics.paint_passes == draws,
            "normal ten-second caret counts diverged");
        focused_require(cpu[10] >= 430000000U && cpu[11] <= 630000000U,
                        "caret draw cadence outside predeclared bounds");
        if (probe.phase == 0U) probe.expected_focused_draws = draws;
        else focused_require(draws == probe.expected_focused_draws,
                             "focused intervals have different draw counts");
        const std::array<double, 4> current{
            extent(after, "last_native_dirty_area"), extent(after, "last_frame_damage_bounds_area"),
            extent(after, "last_cg_destination_area"), extent(after, "last_cg_clip_bounds_area")};
        const std::uint64_t bytes = counter(after, "last_cg_source_bytes");
        for (const double area : current) focused_require(area > 0.0, "empty caret presentation extent");
        focused_require(bytes > 0U, "empty presentation source");
        if (probe.phase == 0U) {
            probe.expected_extents = current;
            probe.expected_source_bytes = bytes;
        } else {
            focused_require(current == probe.expected_extents && bytes == probe.expected_source_bytes,
                            "focused interval presentation extents differ");
        }
        const double expected_area = current[1] * static_cast<double>(draws);
        focused_require(std::abs(metrics.painted_damage_area - expected_area) < 1.0e-6,
                        "aggregate caret damage differs from observed extents");
    } else {
        focused_require(draws == 0U && wakes == 0U && collections == 0U &&
            metrics.paint_passes == 0U && metrics.frame_deadlines_fired == 0U &&
            cpu[1] == 0U && cpu[3] == 0U, "cleared or hidden window was not quiet");
    }
    const std::array<std::string_view, 6> phases{
        "raster_prepare", "retained", "raster_finish", "cg_setup", "cg_draw", "cg_release"};
    for (const std::string_view phase : phases) {
        const std::string prefix = "paint_" + std::string(phase);
        const std::string flag = "\"" + prefix + "_saturated\":false";
        focused_require(after.find(flag) != std::string::npos, "wall phase saturated");
        const std::uint64_t calls = focused_delta(probe.before[index], after, prefix + "_calls");
        focused_require(calls == draws, "synchronous wall phase count differs");
    }
    // Only the primary is permitted measured work; hidden values were proven zero.
    if (index == 0U) {
        draw_cpu = cpu[1];
        collect_cpu = cpu[3];
    }
}

void end_focused_interval(FocusedCpuProbe& probe) {
    probe.wall_end = std::chrono::steady_clock::now();
    probe.wall_ended = true;
    read_focused_cpu_clock(CLOCK_THREAD_CPUTIME_ID, probe.thread_end);
    read_focused_cpu_clock(CLOCK_PROCESS_CPUTIME_ID, probe.process_end);
    probe.measuring = false;
    capture_focused_windows(probe);
    emit_focused_observations(probe);
    focused_require(probe.raw_complete, "raw interval evidence incomplete");
    const std::chrono::duration<double> wall = probe.wall_end - probe.wall_start;
    focused_require(wall.count() >= 10.0 && wall.count() <= 10.1,
                    "ten-second interval exceeded declared duration bound");
    focused_require(probe.thread_end.valid && probe.process_end.valid,
                    "CPU end clock read or conversion failed");
    focused_require(probe.thread_end.nanoseconds >= probe.thread_start.nanoseconds &&
        probe.process_end.nanoseconds >= probe.process_start.nanoseconds,
                    "interval CPU clock regressed");
    const std::uint64_t thread_cpu = probe.thread_end.nanoseconds - probe.thread_start.nanoseconds;
    const std::uint64_t process_cpu = probe.process_end.nanoseconds - probe.process_start.nanoseconds;
    std::uint64_t draw_cpu{0};
    std::uint64_t collect_cpu{0};
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        verify_focused_window(probe, index, draw_cpu, collect_cpu);
    }
    focused_require(draw_cpu <= thread_cpu && collect_cpu <= thread_cpu - draw_cpu,
                    "attributed CPU exceeds same-clock main-thread interval");
    const std::uint64_t rest_cpu = thread_cpu - draw_cpu - collect_cpu;
    const double process_percent = static_cast<double>(process_cpu) / 1.0e7 / wall.count();
    const char* const mode = focused_mode(probe.phase) ? "focused" : "cleared";
    std::printf("focused-cpu-interval|interval=%zu|mode=%s|interval_gates_passed=true|comparison_accepted=false|wall_seconds=%.9f|process_cpu_ns=%llu|main_thread_cpu_ns=%llu|draw_cpu_ns=%llu|collect_cpu_ns=%llu|remaining_main_cpu_ns=%llu|one_core_percent=%.9f|remaining_is_not_CA_attribution=true\n",
        probe.phase, mode, wall.count(), static_cast<unsigned long long>(process_cpu),
        static_cast<unsigned long long>(thread_cpu), static_cast<unsigned long long>(draw_cpu),
        static_cast<unsigned long long>(collect_cpu), static_cast<unsigned long long>(rest_cpu),
        process_percent);
    std::fflush(stdout);
    ++probe.phase;
    if (probe.phase == focused_interval_count) {
        probe.completed = true;
        NSWindow* primary = focused_window(0U);
        [primary performClose:nil];
    } else {
        prepare_focused_phase(probe);
    }
}

void advance_focused_cpu(void* const context) {
    FocusedCpuProbe& probe = *static_cast<FocusedCpuProbe*>(context);
    try {
        if (probe.measuring) end_focused_interval(probe);
        else begin_focused_interval(probe);
    } catch (const std::exception& error) {
        probe.failure = error.what();
        // Setup/clock failures also retain all available evidence. End validation
        // already emitted its immutable snapshot; never recapture it after logging.
        if (!probe.observations_captured) capture_focused_windows(probe);
        if (!probe.raw_emitted) emit_focused_observations(probe);
        std::fprintf(stderr, "focused-cpu-rejected|interval=%zu|reason=%s\n",
                     probe.phase, probe.failure.c_str());
        std::fflush(stdout);
        // Disable diagnostics before teardown even when validation failed midway.
        for (std::size_t index = 0; index < focused_window_count; ++index) {
            NSWindow* window = find_window(focused_title(index));
            if (window != nil) [window.contentView setCpuAttributionEnabled:NO];
        }
        // Close owned children before the primary stops the native run loop.
        for (std::size_t remaining = focused_window_count; remaining > 1U; --remaining) {
            const std::size_t index = remaining - 1U;
            NSWindow* child = find_window(focused_title(index));
            if (child != nil) [child performClose:nil];
        }
        NSWindow* primary = find_window(focused_title(0U));
        if (primary != nil) [primary performClose:nil];
    }
}

class StartFocusedCpu final {
public:
    explicit StartFocusedCpu(FocusedCpuProbe& probe) noexcept : probe_(probe) {}
    void operator()(const std::function<void()>, const std::function<void()>) const {
        prepare_focused_phase(probe_);
    }
private:
    FocusedCpuProbe& probe_;
};

class CloseFocusedCpu final {
public:
    explicit CloseFocusedCpu(FocusedCpuProbe& probe) noexcept : probe_(probe) {}
    void operator()() const { ++probe_.closed; }
private:
    FocusedCpuProbe& probe_;
};

int run_focused_cpu_experiment() {
    FocusedCpuProbe probe{};
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.reserve(focused_window_count);
    for (std::size_t index = 0; index < focused_window_count; ++index) {
        gui_forms::host::MacApplicationWindow entry{};
        entry.stable_id = "focused.cpu." + std::to_string(index);
        if (index != 0U) entry.owner_id = "focused.cpu.0";
        const gui_forms::StableId editor_id("focused.editor." + std::to_string(index));
        probe.editors[index] = std::make_shared<gui_forms::TextBox>(editor_id);
        (*probe.editors[index]).multiline = true;
        entry.model = std::make_unique<gui_forms::Window>(probe.editors[index], gui_forms::Size{640.0, 480.0});
        probe.models[index] = entry.model.get();
        const bool cleared = (*entry.model).request_focus({});
        focused_require(cleared, "could not initialize cleared focus");
        entry.options.title = "Focused CPU window " + std::to_string(index);
        entry.options.initial_size = {640.0, 480.0};
        entry.options.initially_visible = index == 0U;
        entry.options.print_metrics_on_close = false;
        entry.options.closed = CloseFocusedCpu(probe);
        if (index == 0U) entry.options.visibility_ready = StartFocusedCpu(probe);
        windows.push_back(std::move(entry));
    }
    std::printf("focused-cpu-protocol|order=focused,cleared,cleared,focused|interval_seconds=10|settle_seconds=3|duration_max_seconds=10.1|caret_draws=18..19_equal_between_focused_intervals|native_wakes=draws..2*draws+1|draw_gap_ms=430..630|native_deferred_paint=true|clock_overhead_included=true|performance_threshold=none\n");
    std::printf("focused-cpu-trace-protocol|version=1|fixture_row_capacity=256|kinds=1:arm,2:wake,3:poll,4:draw_entry,5:draw_exit|flags=1:deadline,2:next_deadline,4:early_wake,8:untimed_poll,16:ambiguous,32:unpaired,64:clock_invalid,128:inherited_arm|arm_zero=armed_before_capture|delay_ns=requested_relative_delay_not_kernel_due_time|timestamps=steady_clock_ns|raw_is_not_accepted_comparison=true\n");
    std::fflush(stdout);
    const int status = gui_forms::host::run_macos_application(std::move(windows));
    if (status != 0 || !probe.completed || probe.closed != focused_window_count || !probe.failure.empty()) return 1;
    std::printf("focused-cpu-attribution=accepted-comparable-intervals|CA_causation=unmeasured\n");
    return 0;
}

} // namespace
