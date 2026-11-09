#include "gui_forms/animation.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/diagnostic_controls.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close_to(double left, double right, double tolerance = 0.000001) {
    return std::abs(left - right) <= tolerance;
}

void test_easing_endpoints_and_curves() {
    for (const EasingCurve curve : {
             EasingCurve::linear, EasingCurve::ease_in,
             EasingCurve::ease_out, EasingCurve::ease_in_out,
             EasingCurve::smooth_step, EasingCurve::back_out,
             EasingCurve::bounce_out, EasingCurve::elastic_out}) {
        require(close_to(apply_easing(curve, 0.0), 0.0) &&
                    close_to(apply_easing(curve, 1.0), 1.0),
                "every easing curve must preserve exact endpoints");
    }
    require(apply_easing(EasingCurve::ease_in, 0.5) < 0.5 &&
                apply_easing(EasingCurve::ease_out, 0.5) > 0.5 &&
                close_to(apply_easing(EasingCurve::smooth_step, 0.5), 0.5),
            "easing families must retain their declared timing character");
}

void test_timeline_delay_repeat_direction_and_finish() {
    AnimationSpec specification;
    specification.delay = 50ms;
    specification.duration = 100ms;
    specification.iterations = 2.0;
    specification.easing = EasingCurve::linear;
    specification.direction = AnimationDirection::alternate;
    AnimationTimeline timeline(specification);
    const FrameTime start{};
    timeline.start(start);

    const AnimationSample delayed = timeline.sample(start + 25ms);
    const AnimationSample first = timeline.sample(start + 100ms);
    const AnimationSample second = timeline.sample(start + 200ms);
    const AnimationSample finished = timeline.sample(start + 250ms);
    require(delayed.active && delayed.delayed && close_to(delayed.progress, 0.0),
            "timeline delay must preserve the directed initial value");
    require(first.active && first.iteration == 0U &&
                close_to(first.progress, 0.5),
            "first timeline iteration must advance normally");
    require(second.active && second.iteration == 1U &&
                close_to(second.progress, 0.5),
            "alternate timeline iteration must reverse deterministically");
    require(finished.finished && !finished.active &&
                close_to(finished.progress, 0.0),
            "finite alternate timeline must finish at its directed endpoint");
}

void test_timeline_pause_resume_retains_phase_without_catch_up() {
    AnimationSpec specification;
    specification.duration = 1000ms;
    specification.infinite = true;
    AnimationTimeline timeline(specification);
    const FrameTime start{};
    timeline.start(start);
    require(close_to(timeline.sample(start + 250ms).progress, 0.25),
            "timeline must advance before suspension");

    timeline.pause(start + 250ms);
    require(timeline.running() && timeline.paused() &&
                close_to(timeline.sample(start + 5s).progress, 0.25),
            "paused timeline must retain its exact phase at arbitrary sample times");
    timeline.pause(start + 10s);
    require(close_to(timeline.sample(start + 20s).progress, 0.25),
            "repeated pause must be idempotent");

    timeline.resume(start + 1250ms);
    require(timeline.running() && !timeline.paused() &&
                close_to(timeline.sample(start + 1250ms).progress, 0.25) &&
                close_to(timeline.sample(start + 1500ms).progress, 0.5),
            "resume must rebase suspended time and continue from the retained phase");
    timeline.resume(start + 2s);
    require(close_to(timeline.sample(start + 1750ms).progress, 0.75),
            "repeated resume must be idempotent");

    for (std::size_t cycle = 0U; cycle < 16U; ++cycle) {
        const FrameTime pause_at = start + 1750ms + 100ms * cycle;
        const double before = timeline.sample(pause_at).progress;
        timeline.pause(pause_at);
        timeline.resume(pause_at + 40ms);
        require(close_to(timeline.sample(pause_at + 40ms).progress, before),
                "every pause/resume cycle must preserve its boundary phase");
    }
}

void test_invalid_timeline_contract() {
    bool duration_rejected = false;
    bool iterations_rejected = false;
    try {
        AnimationSpec invalid;
        invalid.duration = FrameInterval::zero();
        AnimationTimeline timeline(invalid);
        static_cast<void>(timeline);
    } catch (const std::invalid_argument&) {
        duration_rejected = true;
    }
    try {
        AnimationSpec invalid;
        invalid.iterations = 0.0;
        AnimationTimeline timeline(invalid);
        static_cast<void>(timeline);
    } catch (const std::invalid_argument&) {
        iterations_rejected = true;
    }
    require(duration_rejected && iterations_rejected,
            "timeline must reject degenerate duration and finite iteration count");
}

void test_reduced_motion_remains_active_and_calm() {
    constexpr MotionPolicy reduced{true, false, true};
    static_assert(reduced.active());
    static_assert(!reduced.quiescent());
    static_assert(reduced.frame_interval(16ms) == 100ms);
    static_assert(reduced.speed_scale() == 0.35);
    static_assert(reduced.presentation_phase(0.0) == 0.25);
    static_assert(reduced.presentation_phase(1.0) == 0.75);

    constexpr MotionPolicy paused_reduced{true, true, true};
    constexpr MotionPolicy disabled_reduced{false, false, true};
    static_assert(paused_reduced.quiescent());
    static_assert(disabled_reduced.quiescent());
    require(reduced.active() && paused_reduced.quiescent() &&
                disabled_reduced.quiescent(),
            "reduced motion must remain active; only pause and disable may quiesce it");
}

class StripePainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect bounds) override { clip = bounds; }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point from, Point to, Color, double width) override {
        ++lines;
        if (close_to(width, 6.0)) {
            ++stripe_lines;
            line_width = width;
            if (from.y <= to.y) slopes_down = false;
        }
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    int saves{};
    int restores{};
    int lines{};
    int stripe_lines{};
    Rect clip{};
    double line_width{};
    bool slopes_down{true};
};

// ADR-016: each tick damages only what the phase moved, and a bar that is
// painted keeps its timer.
void test_progress_animation_damages_what_moves() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("animation.damage.root"));
    std::shared_ptr<gui_forms::ProgressBar> progress =
        make_control<ProgressBar>(StableId("animation.damage.progress"));
    (*progress).set_requested_bounds({0.0, 0.0, 240.0, 20.0});
    (*progress).value = 50.0;
    (*progress).visual_style = ProgressBarVisualStyle::marching_stripes;
    (*progress).animation_period = 1000ms;
    (*root).add_child(progress);
    Window window(root, {240.0, 40.0});
    window.perform_layout();
    for (std::size_t tick = 0U; tick < 3U; ++tick) {
        StripePainter painter;
        (*progress).on_paint(painter, {0.0, 0.0, 240.0, 20.0});
        static_cast<void>(window.take_damage());
        const std::uint64_t before = (*progress).effect_instance().counters().ticks;
        static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
        const DamageRegion damage = window.take_damage();
        const Rect bounds = damage.bounds();
        // The stripes are an effect program on the motion clock: its frame
        // makes ticks due, which the next paint runs.
        StripePainter after;
        (*progress).on_paint(after, {0.0, 0.0, 240.0, 20.0});
        // The fill is the left half of the 236-pixel interior, inset 2.
        require((*progress).effect_instance().counters().ticks > before && !bounds.empty() &&
                    bounds.x >= 1.0 && bounds.x + bounds.width <= 121.0 &&
                    bounds.y >= 1.0 && bounds.y + bounds.height <= 19.0 &&
                    window.next_wake().has_value(),
                "a painted marching-stripe bar must damage only its fill on each tick");
    }
}

// A control animated by an AnimationTimer: counts ticks, reports them as
// damaging or not, and can stop its own timer from a tick.
class TimerProbe final : public Control {
public:
    explicit TimerProbe(StableId id) : Control(std::move(id)) {}

    struct Tick final {
        TimerProbe* probe;
        AnimationTickResult operator()(FrameTime) const {
            ++(*probe).ticks;
            if ((*probe).stop_in_tick) (*probe).timer.stop();
            const AnimationTickResult result = (*probe).damaging
                ? AnimationTickResult::damaged : AnimationTickResult::unchanged;
            return result;
        }
    };

    void on_paint(Painter&, Rect) override { timer.note_painted(); }

    AnimationTimer timer;
    int ticks{};
    bool damaging{true};
    bool stop_in_tick{};
};

void test_animation_timer_is_paint_gated() {
    require(animation_interval_for_travel(20.0) == 50ms &&
                animation_interval_for_travel(1000.0) == display_frame_interval &&
                animation_interval_for_travel(0.0) == display_frame_interval &&
                animation_interval_for_travel(std::nan("")) == display_frame_interval &&
                animation_interval_for_travel(1.0e-300) == std::chrono::seconds(3600),
            "travel pacing must give one device pixel a tick, between the display and one hour");

    std::shared_ptr<TimerProbe> probe = make_control<TimerProbe>(StableId("animation.timer.probe"));
    (*probe).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
    Window window(probe, {40.0, 20.0});
    StripePainter painter;
    bool rejected = false;
    try {
        (*probe).timer.start(*probe, 500us, TimerProbe::Tick{probe.get()});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && !(*probe).timer.started(),
            "an interval below one millisecond must be rejected without starting");

    // An interval of a display interval or more ticks at the nearest whole
    // number of display intervals, so that timers of different speeds share
    // frame polls.
    (*probe).timer.start(*probe, 20ms, TimerProbe::Tick{probe.get()});
    const FrameInterval near_one = *window.next_wake() - FrameClock::now();
    require((*window.next_wake()).time_since_epoch() % display_frame_interval == FrameInterval{} &&
                near_one <= display_frame_interval && (*probe).timer.interval() == 20ms,
            "a 20 ms timer must tick at one display interval and report its given interval");
    (*probe).timer.start(*probe, 25ms, TimerProbe::Tick{probe.get()});
    require((*window.next_wake()).time_since_epoch() % (display_frame_interval * 2) == FrameInterval{},
            "a 25 ms timer must tick at two display intervals");
    (*probe).timer.start(*probe, 32ms, TimerProbe::Tick{probe.get()});
    require((*probe).timer.ticking() && window.next_wake().has_value() &&
                (*window.next_wake()).time_since_epoch() % 32ms == FrameInterval{},
            "a started timer's first tick must fall on a multiple of its interval");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require((*probe).ticks == 1 && (*probe).timer.ticking(),
            "the first tick counts the paint its start requested");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require((*probe).ticks == 1 && !(*probe).timer.ticking() &&
                (*probe).timer.started() && !window.next_wake().has_value(),
            "a tick after an unpainted damaging tick must stop the timer and hold no wake");
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require((*probe).timer.ticking() && window.next_wake().has_value(),
            "the owner's next paint must start a waiting timer");

    // A late poll is one tick, and the cadence keeps its phase.
    const FrameTime due = *window.next_wake();
    const FramePollResult late = window.poll_frame_schedule(due + 105ms);
    require(late.ui_timer_ticks == 1U && (*probe).ticks == 2 &&
                *window.next_wake() == due + 128ms,
            "a late tick must be one tick, the next one the first deadline of its cadence");

    // A tick that changes nothing expects no paint and stays scheduled.
    (*probe).damaging = false;
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require((*probe).ticks == 4 && (*probe).timer.ticking(),
            "unchanged ticks must keep the timer running without paints");

    // Hidden: the timer waits for a paint instead of holding a wake.
    (*probe).visible = false;
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require((*probe).ticks == 4 && !window.next_wake().has_value() &&
                (*probe).timer.started(),
            "a hidden owner's timer must stop at its next tick without ticking");
    (*probe).visible = true;
    (*probe).timer.start(*probe, 32ms, TimerProbe::Tick{probe.get()});
    (*probe).visible = false;
    (*probe).timer.start(*probe, 32ms, TimerProbe::Tick{probe.get()});
    require(!(*probe).timer.ticking() && !window.next_wake().has_value(),
            "starting for a hidden owner must wait for its paint");
    (*probe).visible = true;
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require((*probe).timer.ticking(), "a paint must start a timer started while hidden");

    // A tick may stop its own timer; a stopped timer is not restarted by paint.
    (*probe).stop_in_tick = true;
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require((*probe).ticks == 5 && !(*probe).timer.started() &&
                !window.next_wake().has_value(),
            "a timer stopped by its tick must stay stopped through paints");

    // The window's cancel_frame_requests stops a ticking timer, and the
    // next paint does not start it again.
    (*probe).stop_in_tick = false;
    (*probe).timer.start(*probe, 20ms, TimerProbe::Tick{probe.get()});
    window.cancel_frame_requests();
    require(!(*probe).timer.ticking() && !(*probe).timer.started(),
            "cancelling the window's frame requests must stop a ticking timer");
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require(!(*probe).timer.ticking() && !(*probe).timer.started() &&
                !window.next_wake().has_value(),
            "a paint must not start a timer its window cancelled");

    // A timer waiting for a paint holds no request, and is stopped as well
    // (the Gallery accessibility clients freeze a page this way before
    // reading it: a hidden page's animation started at its first paint).
    (*probe).visible = false;
    (*probe).timer.start(*probe, 20ms, TimerProbe::Tick{probe.get()});
    require((*probe).timer.started() && !(*probe).timer.ticking(),
            "a hidden owner's timer must wait for a paint");
    window.cancel_frame_requests();
    require(!(*probe).timer.started(),
            "cancelling the window's frame requests must stop a timer waiting for a paint");
    (*probe).visible = true;
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require(!(*probe).timer.ticking() && !(*probe).timer.started() &&
                !window.next_wake().has_value(),
            "a paint must not start a waiting timer its window cancelled");

    // Another window's cancellation leaves the timer waiting; start after a
    // cancellation runs again.
    std::shared_ptr<TimerProbe> other_root = make_control<TimerProbe>(StableId("animation.timer.other"));
    Window other(other_root, {40.0, 20.0});
    (*probe).visible = false;
    (*probe).timer.start(*probe, 20ms, TimerProbe::Tick{probe.get()});
    other.cancel_frame_requests();
    (*probe).visible = true;
    (*probe).on_paint(painter, {0.0, 0.0, 40.0, 20.0});
    require((*probe).timer.started() && (*probe).timer.ticking() && window.next_wake().has_value(),
            "another window's cancellation must not stop the timer, and start must run it again");
}

void test_progress_animation_and_hidden_suspension() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("animation.root"));
    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId("animation.progress"));
    (*progress).set_requested_bounds({0.0, 0.0, 240.0, 20.0});
    (*progress).visual_style = ProgressBarVisualStyle::marquee;
    (*progress).animation_period = 1000ms;
    (*root).add_child(progress);
    Window window(root, {240.0, 20.0});
    require(window.next_wake().has_value(),
            "animated progress style must register a retained frame source");

    const FrameTime origin = FrameClock::now();
    (*progress).advance_animation(origin);
    (*progress).advance_animation(origin + 250ms);
    require((*progress).animation_phase() > 0.24 &&
                (*progress).animation_phase() < 0.26,
            "progress animation phase must follow its declared period");

    // The animation runs on a UI timer (ADR-016). A tick that finds the bar
    // unpainted since the last one stops the timer; the bar's next paint
    // starts it again.
    (*progress).visible = false;
    const double hidden_phase = (*progress).animation_phase();
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require(!window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), hidden_phase),
            "a hidden progress bar must stop its timer at its next tick, without advancing");
    (*progress).visible = true;
    StripePainter shown_painter;
    (*progress).on_paint(shown_painter, {0.0, 0.0, 240.0, 20.0});
    require(window.next_wake().has_value(),
            "a reshown progress bar must start its timer again when painted");
    (*progress).visual_style = ProgressBarVisualStyle::continuous;
    require(!window.next_wake().has_value(),
            "static progress style must revoke its retained frame source");

    (*progress).overlay_style = ProgressBarOverlayStyle::moving_stripes;
    // The stripes are an effect program: the bar's paint enlists them on the
    // window's motion clock.
    StripePainter striped_painter;
    (*progress).on_paint(striped_painter, {0.0, 0.0, 240.0, 20.0});
    require(window.next_wake().has_value(),
            "moving stripes must animate a determinate progress style");
    (*progress).animation_enabled = false;
    require(!window.next_wake().has_value() && (*progress).animation_phase() == 0.0,
            "disabled stripe motion must paint statically without an idle wake");
    (*progress).animation_enabled = true;
    (*progress).motion_policy = MotionPolicy{true, true, false};
    require(!window.next_wake().has_value(), "paused stripes must leave the motion clock");
    (*progress).motion_policy = MotionPolicy{true, false, false};
    StripePainter resumed_painter;
    (*progress).on_paint(resumed_painter, {0.0, 0.0, 240.0, 20.0});
    require(window.next_wake().has_value(), "resumed stripes must enlist on the motion clock again when painted");
    // The stripes are an effect program on the window's motion clock
    // (effect_program_tests); the bar's own timer lease (ADR-016) now drives
    // the marquee alone, which the rest of this test follows.
    (*progress).overlay_style = ProgressBarOverlayStyle::none;
    (*progress).visual_style = ProgressBarVisualStyle::marquee;
    (*progress).advance_animation(FrameClock::now() + 325ms);
    const double retained_phase = (*progress).animation_phase();
    (*progress).motion_policy = MotionPolicy{true, true, false};
    require(!window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), retained_phase),
            "paused progress motion must revoke its lease without destroying phase");
    (*progress).motion_policy = MotionPolicy{true, false, false};
    require(window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), retained_phase),
            "resumed progress motion must retain phase before its next frame");
    (*progress).advance_animation(*window.next_wake());
    require((*progress).animation_phase() > retained_phase,
            "the first resumed frame must advance from the retained phase");
    const double pre_reduced_phase = (*progress).animation_phase();
    const FrameTime before_reduced = FrameClock::now();
    (*progress).motion_policy = MotionPolicy{true, false, true};
    const FrameTime after_reduced = FrameClock::now();
    (*progress).value = 73.0;
    require(window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), pre_reduced_phase) &&
                close_to((*progress).value.read(), 73.0) &&
                has_semantic_state((*progress).semantic_descriptor().states,
                                   SemanticState::busy),
            "reduced motion must retain animation and state updates without resetting phase");
    const FrameTime reduced_wake = *window.next_wake();
    (*progress).advance_animation(reduced_wake);
    const double reduced_phase = (*progress).animation_phase();
    // Reduced motion scales the speed by 0.35 (and paces the timer at its
    // 100 ms floor): the first tick advances the phase 0.35 of the time
    // since the policy change, in periods of 1 second.
    const double reduced_step = reduced_phase - pre_reduced_phase;
    const double longest = std::chrono::duration<double>(reduced_wake - before_reduced).count();
    const double shortest = std::chrono::duration<double>(reduced_wake - after_reduced).count();
    require(reduced_phase > pre_reduced_phase && reduced_step <= 0.35 * longest + 1e-9 &&
                reduced_step >= 0.35 * shortest - 1e-9,
            "reduced progress motion must advance visibly at its calmer speed");
    (*progress).motion_policy = MotionPolicy{true, false, false};
    require(window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), reduced_phase),
            "leaving reduced motion must retain the reduced timeline position");
    const std::uint64_t requests_before_batch =
        window.metrics_snapshot().scheduled_frame_requests;
    (*progress).motion_policy = MotionPolicy{true, true, true};
    require(!window.next_wake().has_value(),
            "batched pause plus reduced motion must quiesce without a transient lease");
    (*progress).motion_policy = MotionPolicy{true, false, false};
    require(window.next_wake().has_value() &&
                window.metrics_snapshot().scheduled_frame_requests ==
                    requests_before_batch + 1U,
            "batched live policy must publish exactly one replacement lease");

    const double phase_before_master_gate = (*progress).animation_phase();
    (*progress).motion_policy = MotionPolicy{false, false, false};
    require(!window.next_wake().has_value() &&
                (*progress).motion_policy.read() == MotionPolicy{false, false, false} &&
                close_to((*progress).animation_phase(), phase_before_master_gate) &&
                !has_semantic_state((*progress).semantic_descriptor().states,
                                    SemanticState::busy),
            "a disabled application motion policy must revoke its lease without mutating phase");
    (*progress).motion_policy = MotionPolicy{true, false, false};
    require(window.next_wake().has_value() &&
                close_to((*progress).animation_phase(), phase_before_master_gate) &&
                has_semantic_state((*progress).semantic_descriptor().states,
                                   SemanticState::busy),
            "re-enabling the application motion policy must resume from retained phase");

    for (std::size_t cycle = 0U; cycle < 16U; ++cycle) {
        (*progress).motion_policy = MotionPolicy{true, true, true};
        const double compound_phase = (*progress).animation_phase();
        require(!window.next_wake().has_value() && (*progress).motion_policy.read().paused &&
                    (*progress).motion_policy.read().reduced &&
                    !has_semantic_state((*progress).semantic_descriptor().states,
                                        SemanticState::busy),
                "compound pause plus reduced motion must remain a single quiescent policy");
        (*progress).motion_policy = MotionPolicy{true, false, true};
        require(window.next_wake().has_value() &&
                    close_to((*progress).animation_phase(), compound_phase) &&
                    has_semantic_state((*progress).semantic_descriptor().states,
                                       SemanticState::busy),
                "resuming while reduced must arm one calm animation lease");
        (*progress).motion_policy = MotionPolicy{true, true, false};
        require(!window.next_wake().has_value() &&
                    close_to((*progress).animation_phase(), compound_phase),
                "leaving reduced motion while paused must retain the suspended phase");
        (*progress).motion_policy = MotionPolicy{true, false, false};
        require(window.next_wake().has_value() &&
                    close_to((*progress).animation_phase(), compound_phase),
                "full motion must restore its frame lease from the retained phase");
    }
    PresentationSettings calm = window.presentation_settings();
    calm.reduced_motion = true;
    (*progress).motion_policy = MotionPolicy{true, false, false};
    window.set_presentation_settings(calm);
    require(!(*progress).motion_policy.read().reduced &&
                (*progress).effective_motion_policy().reduced &&
                window.next_wake().has_value(),
            "window reduced-motion preference must calm progress without overwriting local policy or stopping it");
    calm.reduced_motion = false;
    window.set_presentation_settings(calm);
    require(!(*progress).effective_motion_policy().reduced &&
                window.next_wake().has_value(),
            "clearing the window preference must restore full motion in one transition");
    (*progress).visual_style = ProgressBarVisualStyle::continuous;
    require(!window.next_wake().has_value(),
            "removing the last animated progress feature must revoke its frame source");
}

void test_progress_stripes_are_clipped_and_configurable() {
    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId("animation.stripes"));
    (*progress).set_requested_bounds({0.0, 0.0, 200.0, 24.0});
    (*progress).value = 50.0;
    (*progress).overlay_style = ProgressBarOverlayStyle::moving_stripes;
    (*progress).stripe_width = 6.0;
    Window window(progress, {200.0, 24.0});
    window.perform_layout();
    StripePainter painter;
    (*progress).on_paint(painter, {0.0, 0.0, 200.0, 24.0});
    require(painter.saves >= 1 && painter.restores == painter.saves &&
                painter.stripe_lines > 1 &&
                painter.clip == Rect{2.0, 2.0, 98.0, 20.0} &&
                close_to(painter.line_width, 6.0) && painter.slopes_down,
            "striped progress must clip diagonal bands to the determinate fill");

    bool narrow_rejected = false;
    try {
        (*progress).stripe_width = 1.0;
    } catch (const std::invalid_argument&) {
        narrow_rejected = true;
    }
    require(narrow_rejected && close_to((*progress).stripe_width.read(), 6.0),
            "stripe geometry must reject unusable widths without mutation");
}

class ProgressEffectsPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect bounds) override { clips.push_back(bounds); }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void fill_linear_gradient(Rect rect, Point, Point,
                              std::span<const GradientStop> stops) override {
        ++linear_gradients;
        if (stops.size() == 5U) ++five_stop_linear_gradients;
        if (stops.size() == 4U) ++four_stop_linear_gradients;
        last_linear = rect;
        largest_stop_count = std::max(largest_stop_count, stops.size());
    }
    void fill_linear_gradient_spread(
        Rect, Point start, Point end, std::span<const GradientStop> stops,
        GradientSpreadMode spread) override {
        ++spread_gradients;
        if (stops.size() == 5U) ++five_stop_spread_gradients;
        spread_mode = spread;
        spread_axis = {end.x - start.x, end.y - start.y};
        largest_stop_count = std::max(largest_stop_count, stops.size());
    }
    void fill_radial_gradient(Rect, Point, Size,
                              std::span<const GradientStop> stops) override {
        ++radial_gradients;
        largest_stop_count = std::max(largest_stop_count, stops.size());
    }
    void draw_line(Point from, Point to, Color, double width) override {
        if (width <= 1.5) {
            ++sparks;
            maximum_spark_length = std::max(
                maximum_spark_length,
                std::hypot(to.x - from.x, to.y - from.y));
        }
        else ++bands;
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    int saves{};
    int restores{};
    int linear_gradients{};
    int spread_gradients{};
    int radial_gradients{};
    int five_stop_linear_gradients{};
    int four_stop_linear_gradients{};
    int five_stop_spread_gradients{};
    int sparks{};
    int bands{};
    double maximum_spark_length{};
    std::size_t largest_stop_count{};
    GradientSpreadMode spread_mode{GradientSpreadMode::pad};
    Point spread_axis{};
    Rect last_linear{};
    std::vector<Rect> clips;
};

void test_progress_animation_style_family() {
    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(
        StableId("animation.progress-style-family"));
    (*progress).set_requested_bounds({0.0, 0.0, 240.0, 24.0});
    (*progress).value = 64.0;
    (*progress).animation_period = 1000ms;
    Window window(progress, {240.0, 24.0});
    window.perform_layout();

    ProgressBarAnimationAppearance appearance =
        (*progress).animation_appearance.read();
    appearance.pulse_extent = 0.4;
    appearance.laser_edge_extent = 13.0;
    appearance.laser_phase_pitch = 12.0;
    (*progress).animation_appearance = appearance;
    require((*progress).animation_appearance.read() == appearance,
            "progress animation appearance must replace atomically");

    (*progress).visual_style = ProgressBarVisualStyle::luminance_pulse;
    const FrameTime origin = FrameClock::now();
    (*progress).advance_animation(origin);
    (*progress).advance_animation(origin + 250ms);
    ProgressEffectsPainter pulse;
    (*progress).on_paint(pulse, {0.0, 0.0, 240.0, 24.0});
    require(window.next_wake().has_value() &&
                pulse.five_stop_linear_gradients == 1 &&
                pulse.five_stop_spread_gradients == 0 &&
                std::find(pulse.clips.begin(), pulse.clips.end(),
                          Rect{2.0, 2.0, 151.04, 20.0}) != pulse.clips.end() &&
                pulse.largest_stop_count == 5U &&
                pulse.last_linear.width >= 8.0,
            "luminance progress must own one clipped, forward-moving soft pulse");

    (*progress).visual_style = ProgressBarVisualStyle::marching_stripes;
    (*progress).advance_animation(origin + 500ms);
    ProgressEffectsPainter stripes;
    (*progress).on_paint(stripes, {0.0, 0.0, 240.0, 24.0});
    require(stripes.bands > 2 && stripes.saves >= 1 &&
                stripes.restores == stripes.saves &&
                std::find(stripes.clips.begin(), stripes.clips.end(),
                          Rect{2.0, 2.0, 151.04, 20.0}) != stripes.clips.end(),
            "marching-stripe progress must remain clipped to determinate fill");

    (*progress).visual_style = ProgressBarVisualStyle::laser_etch;
    (*progress).advance_animation(origin + 750ms);
    ProgressEffectsPainter laser;
    (*progress).on_paint(laser, {0.0, 0.0, 240.0, 24.0});
    require(laser.five_stop_spread_gradients == 1 &&
                laser.four_stop_linear_gradients >= 1 &&
                laser.spread_mode == GradientSpreadMode::repeat &&
                close_to(laser.spread_axis.x, 0.0) &&
                close_to(laser.spread_axis.y, appearance.laser_phase_pitch) &&
                laser.radial_gradients == 5 && laser.sparks >= 7 &&
                laser.maximum_spark_length < 5.0 &&
                std::find(laser.clips.begin(), laser.clips.end(),
                          Rect{2.0, 2.0, 151.04, 20.0}) != laser.clips.end(),
            "laser progress must combine a repeating phase with a compact white-hot corona and short sparks");

    (*progress).motion_policy = MotionPolicy{true, false, true};
    ProgressEffectsPainter reduced_laser;
    (*progress).on_paint(reduced_laser, {0.0, 0.0, 240.0, 24.0});
    require(reduced_laser.five_stop_spread_gradients == 1 &&
                reduced_laser.radial_gradients == 3 &&
                reduced_laser.sparks + 4 == laser.sparks &&
                reduced_laser.maximum_spark_length < 3.0,
            "reduced laser motion must preserve meaning with a calmer spark field");

    const ProgressBarAnimationAppearance retained =
        (*progress).animation_appearance.read();
    bool invalid_rejected = false;
    try {
        ProgressBarAnimationAppearance invalid = retained;
        invalid.laser_phase_pitch = 0.0;
        (*progress).animation_appearance = invalid;
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    require(invalid_rejected && (*progress).animation_appearance.read() == retained,
            "invalid progress effect geometry must not partially mutate appearance");

    (*progress).visual_style = ProgressBarVisualStyle::continuous;
    require(!window.next_wake().has_value(),
            "returning to a static progress style must revoke the frame lease");
}

void test_visual_style_round_trips() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("animation.button"), "Button");
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("animation.check"), "Check");
    std::shared_ptr<gui_forms::RadioButton> radio = make_control<RadioButton>(StableId("animation.radio"), "Radio");
    std::shared_ptr<gui_forms::TrackBar> slider = make_control<TrackBar>(StableId("animation.slider"));
    (*button).visual_style = ButtonVisualStyle::accent;
    (*check).indicator_style = ChoiceIndicatorStyle::toggle;
    (*radio).indicator_style = ChoiceIndicatorStyle::modern;
    (*slider).visual_style = TrackBarVisualStyle::filled;
    require((*button).visual_style.read() == ButtonVisualStyle::accent &&
                (*check).indicator_style.read() == ChoiceIndicatorStyle::toggle &&
                (*radio).indicator_style.read() == ChoiceIndicatorStyle::modern &&
                (*slider).visual_style.read() == TrackBarVisualStyle::filled,
            "public control visual variants must retain exact state");
}

void test_public_easing_preview_owns_scheduler_policy_and_semantics() {
    std::shared_ptr<gui_forms::EasingPreview> preview = make_control<EasingPreview>(
        StableId("animation.public.preview"));
    (*preview).set_requested_bounds({0.0, 0.0, 500.0, 340.0});
    (*preview).title = "Public easing proof";
    (*preview).marker_size = 18.0;
    Window window(preview, {500.0, 340.0});
    require((*preview).tracks().size() == 8U && (*preview).marker_size.read() == 18.0 &&
                window.next_wake().has_value() &&
                has_semantic_state((*preview).semantic_descriptor().states,
                                   SemanticState::busy),
            "EasingPreview must own all public curves, one frame lease, and busy semantics");

    PresentationSettings calm = window.presentation_settings();
    calm.reduced_motion = true;
    window.set_presentation_settings(calm);
    require((*preview).effective_motion_policy().reduced &&
                !(*preview).motion_policy.read().reduced && window.next_wake().has_value(),
            "window reduced-motion preference must calm EasingPreview without replacing its authored policy");
    calm.reduced_motion = false;
    window.set_presentation_settings(calm);

    const double before = (*preview).phase();
    (*preview).motion_policy = {true, false, true};
    require(window.next_wake().has_value() && (*preview).phase() == before,
            "reduced EasingPreview motion must retain phase and remain scheduled");
    (*preview).advance_animation(FrameClock::now() + 300ms);
    require((*preview).phase() > before &&
                (*preview).semantic_descriptor().numeric_value.has_value(),
            "public EasingPreview frames must advance reusable timeline state");

    (*preview).motion_policy = {true, true, true};
    require(!window.next_wake().has_value() &&
                !has_semantic_state((*preview).semantic_descriptor().states,
                                    SemanticState::busy),
            "paused EasingPreview motion must revoke its lease and busy state");
    (*preview).set_tracks({
        {EasingCurve::linear, "Linear only", Color::rgba(20, 80, 140)},
    });
    require((*preview).tracks().size() == 1U &&
                (*preview).tracks().front().label == "Linear only",
            "EasingPreview track collection must be reusable application configuration");
    bool empty_tracks_rejected = false;
    try {
        (*preview).set_tracks({});
    } catch (const std::invalid_argument&) {
        empty_tracks_rejected = true;
    }
    require(empty_tracks_rejected,
            "EasingPreview must reject an empty conformance surface");
}

} // namespace

int main() {
    try {
        test_easing_endpoints_and_curves();
        test_timeline_delay_repeat_direction_and_finish();
        test_timeline_pause_resume_retains_phase_without_catch_up();
        test_invalid_timeline_contract();
        test_reduced_motion_remains_active_and_calm();
        test_animation_timer_is_paint_gated();
        test_progress_animation_and_hidden_suspension();
        test_progress_animation_damages_what_moves();
        test_progress_stripes_are_clipped_and_configurable();
        test_progress_animation_style_family();
        test_visual_style_round_trips();
        test_public_easing_preview_owns_scheduler_policy_and_semantics();
        std::cout << "gui_forms_animation_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_animation_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
