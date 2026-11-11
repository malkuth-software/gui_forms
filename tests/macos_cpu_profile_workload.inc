// Profiling-only holds. Never enables the private scalar/causal diagnostic.
#include <os/signpost.h>
#include <unistd.h>
#include <cstdlib>

namespace {
constexpr std::size_t profile_window_count = 9U;
enum class ProfileStage { ready, settle, hold, closing };

struct ProfileWindowSnapshot final {
    gui_forms::MetricsSnapshot metrics{};
    std::string host{};
    NSRect bounds{};
    double scale{0.0};
    bool visible{false};
    bool occluded{false};
    bool focused{false};
    bool deadline{false};
    bool key{false};
    bool application_active{false};
    bool captured{false};
};

struct CpuProfileWorkload final {
    // Native run owns models. Deferred contexts own this state, and may borrow
    // models only while running; run return revokes that borrow authority.
    std::array<gui_forms::Window*, profile_window_count> models{};
    std::array<std::shared_ptr<gui_forms::TextBox>, profile_window_count> editors{};
    std::array<ProfileWindowSnapshot, profile_window_count> before{};
    std::array<ProfileWindowSnapshot, profile_window_count> after{};
    FocusedClockRead process_start{};
    FocusedClockRead thread_start{};
    FocusedClockRead thread_end{};
    FocusedClockRead process_end{};
    gui_forms::FrameTime readiness_deadline{};
    gui_forms::FrameTime wall_start{};
    gui_forms::FrameTime wall_end{};
    os_log_t signpost_log{nullptr};
    os_signpost_id_t signpost_id{OS_SIGNPOST_ID_INVALID};
    ProfileStage stage{ProfileStage::ready};
    std::size_t closed{0};
    bool focused{false};
    bool running{false};
    bool span_open{false};
    bool hold_finished{false};
    bool signposts_enabled{false};
    bool expect_recorder{false};
    std::string failure{};
};

NSString* profile_title(const std::size_t index) {
    NSString* const result = [NSString stringWithFormat:@"CPU profile window %zu", index];
    return result;
}

void profile_require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

void capture_profile_windows(CpuProfileWorkload& state,
                             std::array<ProfileWindowSnapshot, profile_window_count>& output) {
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        NSWindow* const window = find_window(profile_title(index));
        if (window == nil || window.contentView == nil) continue;
        gui_forms::Window& model = *state.models[index];
        ProfileWindowSnapshot& value = output[index];
        value.metrics = model.metrics_snapshot();
        value.bounds = window.contentView.bounds;
        value.scale = window.backingScaleFactor;
        value.visible = static_cast<bool>(window.isVisible);
        value.occluded = model.occluded();
        value.focused = static_cast<bool>(model.focused_control());
        value.deadline = model.next_wake().has_value();
        value.key = static_cast<bool>(window.isKeyWindow);
        value.application_active = static_cast<bool>(NSApp.isActive);
        value.captured = true;
    }
    // All model/state observations precede JSON serialization and all output.
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        if (!output[index].captured) continue;
        NSWindow* const window = find_window(profile_title(index));
        output[index].host = [window.contentView hostJSON];
    }
}

void check_profile_state(const CpuProfileWorkload& state,
                         const std::array<ProfileWindowSnapshot, profile_window_count>& values) {
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        const ProfileWindowSnapshot& value = values[index];
        const bool primary = index == 0U;
        const bool focus = primary && state.focused;
        profile_require(value.captured, "window_missing");
        profile_require(value.visible == primary && value.occluded == !primary,
                        "visibility_changed");
        profile_require(value.focused == focus && value.deadline == focus, "focus_or_deadline_changed");
        if (primary) profile_require(value.key && value.application_active, "native_activation_changed");
        profile_require(value.metrics.renderer_name.find("bundled fonts (optional Unicode fallback)") !=
            std::string::npos, "bundled_fonts_unavailable");
        profile_require(value.bounds.size.width == 640.0 && value.bounds.size.height == 480.0 &&
            std::isfinite(value.scale) && value.scale > 0.0, "geometry_invalid");
    }
}

void emit_profile_snapshot(const char* const boundary, const std::size_t index,
                            const ProfileWindowSnapshot& value) {
    const std::string metrics = value.metrics.to_json();
    std::printf("cpu-profile-window|boundary=%s|window=%zu|captured=%d|visible=%d|occluded=%d|focused=%d|deadline=%d|key=%d|active=%d|bounds=%.17g,%.17g,%.17g,%.17g|scale=%.17g|metrics=%s|host=%s\n",
        boundary, index, value.captured, value.visible, value.occluded, value.focused,
        value.deadline, value.key, value.application_active, value.bounds.origin.x,
        value.bounds.origin.y, value.bounds.size.width, value.bounds.size.height,
        value.scale, metrics.c_str(), value.host.c_str());
}

void emit_profile_clock(const char* const name, const FocusedClockRead& value) {
    std::printf("cpu-profile-clock|name=%s|attempted=%d|status=%d|errno=%d|raw_sec=%lld|raw_nsec=%lld|valid=%d|ns=%llu\n",
        name, value.attempted, value.status, value.error,
        static_cast<long long>(value.raw.tv_sec), static_cast<long long>(value.raw.tv_nsec),
        value.valid, static_cast<unsigned long long>(value.nanoseconds));
}

void emit_profile_evidence(const CpuProfileWorkload& state) {
    const std::chrono::duration<double> wall = state.wall_end - state.wall_start;
    std::printf("cpu-profile-span|mode=%s|pid=%ld|hold_finished=%d|wall_seconds=%.9f|wall_start_ticks=%lld|wall_end_ticks=%lld|tick_ratio=%lld/%lld|signpost_name=CpuProfileHold|signpost_id=%llu|signposts_enabled_at_begin=%d|diagnostic_scalar_and_trace=off|comparison_accepted=false\n",
        state.focused ? "focused" : "cleared", static_cast<long>(getpid()),
        state.hold_finished, wall.count(), static_cast<long long>(state.wall_start.time_since_epoch().count()),
        static_cast<long long>(state.wall_end.time_since_epoch().count()),
        static_cast<long long>(gui_forms::FrameClock::period::num),
        static_cast<long long>(gui_forms::FrameClock::period::den),
        static_cast<unsigned long long>(state.signpost_id), state.signposts_enabled);
    emit_profile_clock("process_start", state.process_start);
    emit_profile_clock("thread_start", state.thread_start);
    emit_profile_clock("thread_end", state.thread_end);
    emit_profile_clock("process_end", state.process_end);
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        emit_profile_snapshot("before", index, state.before[index]);
        emit_profile_snapshot("after", index, state.after[index]);
    }
    std::fflush(stdout);
}

void validate_profile_hold(const CpuProfileWorkload& state) {
    const std::chrono::duration<double> wall = state.wall_end - state.wall_start;
    profile_require(wall.count() >= 120.0 && wall.count() <= 130.0, "hold_duration_invalid");
    profile_require(state.process_start.valid && state.thread_start.valid &&
        state.thread_end.valid && state.process_end.valid, "cpu_clock_invalid");
    profile_require(state.process_end.nanoseconds >= state.process_start.nanoseconds &&
        state.thread_end.nanoseconds >= state.thread_start.nanoseconds, "cpu_clock_regressed");
    check_profile_state(state, state.before);
    check_profile_state(state, state.after);
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        const ProfileWindowSnapshot& first = state.before[index];
        const ProfileWindowSnapshot& last = state.after[index];
        profile_require(NSEqualRects(first.bounds, last.bounds) && first.scale == last.scale,
                        "geometry_changed");
        profile_require(first.metrics.input_events == last.metrics.input_events &&
            first.metrics.focus_transitions == last.metrics.focus_transitions &&
            first.metrics.measure_passes == last.metrics.measure_passes &&
            first.metrics.arrange_passes == last.metrics.arrange_passes &&
            first.metrics.occlusion_suspensions == last.metrics.occlusion_suspensions &&
            first.metrics.occlusion_resumes == last.metrics.occlusion_resumes,
            "hold_activity_changed");
        const bool active = index == 0U && state.focused;
        if (active) {
            profile_require(last.metrics.paint_passes > first.metrics.paint_passes &&
                last.metrics.frame_deadlines_fired > first.metrics.frame_deadlines_fired,
                "focused_caret_inactive");
        } else {
            profile_require(last.metrics.paint_passes == first.metrics.paint_passes &&
                last.metrics.frame_deadlines_fired == first.metrics.frame_deadlines_fired &&
                counter(first.host, "native_draw_count") == counter(last.host, "native_draw_count") &&
                counter(first.host, "scheduled_wake_count") == counter(last.host, "scheduled_wake_count") &&
                counter(first.host, "damage_collection_count") == counter(last.host, "damage_collection_count"),
                "cleared_or_hidden_activity");
        }
    }
}

void advance_cpu_profile(void* const context);

void queue_cpu_profile(const std::shared_ptr<CpuProfileWorkload>& state,
                       const std::chrono::milliseconds delay) {
    std::unique_ptr<std::shared_ptr<CpuProfileWorkload>> retained =
        std::make_unique<std::shared_ptr<CpuProfileWorkload>>(state);
    const std::chrono::nanoseconds duration = delay;
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, duration.count());
    void* const context = retained.release();
    dispatch_after_f(deadline, dispatch_get_main_queue(), context, advance_cpu_profile);
}

void close_cpu_profile(const std::shared_ptr<CpuProfileWorkload>& state) {
    (*state).stage = ProfileStage::closing;
    queue_cpu_profile(state, std::chrono::seconds(5));
    for (std::size_t remaining = profile_window_count; remaining > 0U; --remaining) {
        NSWindow* const window = find_window(profile_title(remaining - 1U));
        if (window != nil) [window performClose:nil];
    }
}

void begin_cpu_profile(CpuProfileWorkload& state) {
    capture_profile_windows(state, state.before);
    check_profile_state(state, state.before);
    state.signposts_enabled = os_signpost_enabled(state.signpost_log);
    state.signpost_id = os_signpost_id_generate(state.signpost_log);
    os_signpost_interval_begin(state.signpost_log, state.signpost_id, "CpuProfileHold",
        "mode=%{public}s", state.focused ? "focused" : "cleared");
    state.span_open = true;
    read_focused_cpu_clock(CLOCK_PROCESS_CPUTIME_ID, state.process_start);
    read_focused_cpu_clock(CLOCK_THREAD_CPUTIME_ID, state.thread_start);
    state.wall_start = gui_forms::FrameClock::now();
    profile_require(state.process_start.valid && state.thread_start.valid, "cpu_start_clock_invalid");
    state.stage = ProfileStage::hold;
}

void end_cpu_profile(CpuProfileWorkload& state) {
    state.wall_end = gui_forms::FrameClock::now();
    read_focused_cpu_clock(CLOCK_THREAD_CPUTIME_ID, state.thread_end);
    read_focused_cpu_clock(CLOCK_PROCESS_CPUTIME_ID, state.process_end);
    os_signpost_interval_end(state.signpost_log, state.signpost_id, "CpuProfileHold");
    state.span_open = false;
    state.hold_finished = true;
    capture_profile_windows(state, state.after);
    emit_profile_evidence(state);
    validate_profile_hold(state);
}

void advance_cpu_profile(void* const context) {
    const std::unique_ptr<std::shared_ptr<CpuProfileWorkload>> retained(
        static_cast<std::shared_ptr<CpuProfileWorkload>*>(context));
    const std::shared_ptr<CpuProfileWorkload>& owner = *retained;
    CpuProfileWorkload& state = *owner;
    if (!state.running) return;
    try {
        if (state.stage == ProfileStage::closing) {
            state.failure = "close_timeout";
            [NSApp stop:nil];
            return;
        }
        if (state.stage == ProfileStage::ready) {
            NSWindow* const primary = find_window(profile_title(0U));
            const gui_forms::MetricsSnapshot metrics = (*state.models[0]).metrics_snapshot();
            const bool ready = primary != nil && primary.isVisible && primary.isKeyWindow &&
                NSApp.isActive && metrics.paint_passes > 0U &&
                (!state.expect_recorder || os_signpost_enabled(state.signpost_log));
            if (!ready) {
                profile_require(gui_forms::FrameClock::now() < state.readiness_deadline,
                    state.expect_recorder ? "readiness_or_recorder_unmapped" : "readiness_timeout");
                queue_cpu_profile(owner, std::chrono::milliseconds(50));
                return;
            }
            bool changed{false};
            if (state.focused) changed = (*state.models[0]).request_focus(state.editors[0]);
            else changed = (*state.models[0]).request_focus({});
            profile_require(changed, "focus_request_failed");
            state.stage = ProfileStage::settle;
            queue_cpu_profile(owner, std::chrono::seconds(3));
        } else if (state.stage == ProfileStage::settle) {
            begin_cpu_profile(state);
            queue_cpu_profile(owner, std::chrono::seconds(120));
        } else {
            end_cpu_profile(state);
            close_cpu_profile(owner);
        }
    } catch (const std::exception& error) {
        state.failure = error.what();
        if (state.span_open) {
            os_signpost_interval_end(state.signpost_log, state.signpost_id, "CpuProfileHold",
                                     "incomplete=true");
            state.span_open = false;
        }
        std::fprintf(stderr, "cpu-profile-failure|reason=%s\n", state.failure.c_str());
        try {
            close_cpu_profile(owner);
        } catch (const std::exception&) {
            // Allocation failure while scheduling the close watchdog must not
            // escape the dispatch C callback. The runner still owns its timeout.
            [NSApp stop:nil];
        }
    }
}

class StartCpuProfile final {
public:
    explicit StartCpuProfile(const std::shared_ptr<CpuProfileWorkload>& state) : state_(state) {}
    void operator()(const std::function<void()>, const std::function<void()>) const {
        (*state_).readiness_deadline = gui_forms::FrameClock::now() + std::chrono::seconds(15);
        queue_cpu_profile(state_, std::chrono::milliseconds(50));
    }
private:
    std::shared_ptr<CpuProfileWorkload> state_;
};

class CloseCpuProfile final {
public:
    explicit CloseCpuProfile(const std::shared_ptr<CpuProfileWorkload>& state) : state_(state) {}
    void operator()() const { ++(*state_).closed; }
private:
    std::shared_ptr<CpuProfileWorkload> state_;
};

int run_cpu_profile_workload(const std::string_view mode) {
    if (mode != "focused" && mode != "cleared") return 2;
    const std::shared_ptr<CpuProfileWorkload> state = std::make_shared<CpuProfileWorkload>();
    (*state).focused = mode == "focused";
    const char* const recorder = std::getenv("GUI_FORMS_CPU_PROFILE_EXPECT_RECORDER");
    (*state).expect_recorder = recorder != nullptr && std::strcmp(recorder, "1") == 0;
    (*state).signpost_log = os_log_create("local.guiforms.cpu-profile", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.reserve(profile_window_count);
    for (std::size_t index = 0; index < profile_window_count; ++index) {
        gui_forms::host::MacApplicationWindow entry{};
        entry.stable_id = "cpu.profile." + std::to_string(index);
        if (index != 0U) entry.owner_id = "cpu.profile.0";
        (*state).editors[index] = std::make_shared<gui_forms::TextBox>(
            gui_forms::StableId("cpu.profile.editor." + std::to_string(index)));
        (*(*state).editors[index]).multiline = true;
        entry.model = std::make_unique<gui_forms::Window>((*state).editors[index], gui_forms::Size{640.0, 480.0});
        (*state).models[index] = entry.model.get();
        profile_require((*entry.model).request_focus({}), "initial_focus_clear_failed");
        entry.options.title = "CPU profile window " + std::to_string(index);
        entry.options.initial_size = {640.0, 480.0};
        entry.options.initially_visible = index == 0U;
        entry.options.print_metrics_on_close = false;
        entry.options.closed = CloseCpuProfile(state);
        if (index == 0U) entry.options.visibility_ready = StartCpuProfile(state);
        windows.push_back(std::move(entry));
    }
    std::printf("cpu-profile-protocol|mode=%s|hold_seconds=120|settle_seconds=3|readiness_max_seconds=15|close_max_seconds=5|diagnostic_scalar_and_trace=off|comparison_accepted=false\n",
                (*state).focused ? "focused" : "cleared");
    std::fflush(stdout);
    (*state).running = true;
    const int status = gui_forms::host::run_macos_application(std::move(windows));
    (*state).running = false;
    if ((*state).span_open) {
        os_signpost_interval_end((*state).signpost_log, (*state).signpost_id,
                                 "CpuProfileHold", "incomplete=true");
        (*state).span_open = false;
    }
    const bool completed = status == 0 && (*state).hold_finished &&
        (*state).closed == profile_window_count && (*state).failure.empty();
    std::printf("cpu-profile-result|mode=%s|status=%s|closed=%zu|reason=%s|comparison_accepted=false\n",
        (*state).focused ? "focused" : "cleared", completed ? "completed" : "invalid",
        (*state).closed, (*state).failure.c_str());
    const int result = completed ? 0 : 1;
    return result;
}
} // namespace
