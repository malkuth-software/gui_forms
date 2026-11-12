// The motion clock and control-owned motion values (T2 plan, phase E3;
// GIVEN D1-1 to D4-1): zero frames once every value has settled; one frame
// request per window however many values move; a retarget mid-flight keeps
// the value (and a spring's velocity) continuous; reduced motion and an
// occluded window end every value in one call and schedule nothing; every
// duration is clamped to the 400 ms ceiling; overshoot stays within 8 %;
// the settled conditions in device pixels; a value whose owner has gone is
// never advanced; and a fixed table of samples has the same bits on every
// CI platform. Time comes from a test source, never from the wall clock.
// GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed hash instead of
// checking it.

#include "gui_forms/control.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_motion_clock_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

// The test's time: the clock reads it when a move starts, and the test
// polls the window at it.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A step as a listener saw it.
struct Seen final {
    const MotionValue* value{};
    double previous{};
    double current{};
    bool settled{};
    MotionStepCause cause{MotionStepCause::advanced};
};

// A control owning two values and, optionally, a third it can drop while
// it moves. Its listener records every step, and may run one extra action.
class Mover final : public Control {
public:
    enum class Action : std::uint8_t { none, finish_other_value, retarget_self, start_other };

    explicit Mover(StableId stable_id)
        : Control(std::move(stable_id)), first(*this, MotionCurve::timed(milliseconds(180), MotionEasing::smoothstep)),
          second(*this, MotionCurve::follow()) {
        first.set_listener(MotionListener::bind<Mover, &Mover::on_step>(*this));
        second.set_listener(MotionListener::bind<Mover, &Mover::on_step>(*this));
    }

    void make_extra() {
        extra = std::make_unique<MotionValue>(*this, MotionCurve::timed(milliseconds(200), MotionEasing::linear));
        (*extra).set_listener(MotionListener::bind<Mover, &Mover::on_step>(*this));
    }

    void on_step(const MotionStep& step) {
        Seen seen{};
        seen.value = step.value;
        seen.previous = step.previous;
        seen.current = step.current;
        seen.settled = step.settled;
        seen.cause = step.cause;
        steps.push_back(seen);
        const Action now = action;
        action = Action::none;
        if (now == Action::finish_other_value) {
            MotionValue& sibling = step.value == &first ? second : first;
            sibling.finish();
        }
        if (now == Action::retarget_self) static_cast<void>((*step.value).animate_to(-1.0));
        if (now == Action::start_other && other != nullptr) static_cast<void>((*other).first.animate_to(1.0));
    }

    [[nodiscard]] std::size_t steps_of(const MotionValue& value) const {
        std::size_t count = 0U;
        for (const Seen& seen : steps) {
            if (seen.value == &value) ++count;
        }
        return count;
    }

    MotionValue first;
    MotionValue second;
    std::unique_ptr<MotionValue> extra{};
    std::vector<Seen> steps{};
    Action action{Action::none};
    Mover* other{};
};

struct Fixture final {
    ManualTime time{};
    std::shared_ptr<Mover> root;
    std::vector<std::shared_ptr<Mover>> movers{};
    std::unique_ptr<Window> window;
};

// A window whose root holds `count` movers, on the test's time.
std::unique_ptr<Fixture> make_fixture(std::size_t count) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).root = make_control<Mover>(StableId("root"));
    for (std::size_t index = 0; index < count; ++index) {
        std::shared_ptr<Mover> mover = make_control<Mover>(StableId("mover." + std::to_string(index)));
        (*(*fixture).root).add_child(mover);
        (*fixture).movers.push_back(mover);
    }
    (*fixture).window = std::make_unique<Window>((*fixture).root, Size{400.0, 300.0});
    (*(*fixture).window).motion_clock().set_time_source(&(*fixture).time);
    return fixture;
}

// Advances the test's time by `step` and polls the window there.
FramePollResult frame(Fixture& fixture, milliseconds step) {
    fixture.time.time += step;
    const FramePollResult result = (*fixture.window).poll_frame_schedule(fixture.time.time);
    return result;
}

double smoothstep(double p) {
    const double eased = p * p * (3.0 - 2.0 * p);
    return eased;
}

// -- settling and zero frames ----------------------------------------------------

void zero_frames_after_settling() {
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    Window& window = *(*fixture).window;
    Mover& mover = *(*fixture).movers[0];
    const MetricsSnapshot before = window.metrics_snapshot();
    require(!window.motion_clock().armed() && !window.next_wake(), "a window at rest schedules nothing");
    require(mover.first.animate_to(1.0) == MotionStart::started && window.motion_clock().armed() &&
                window.motion_clock().moving_count() == 1U,
            "a move arms the clock's frame request");
    require(mover.first.animate_to(1.0) == MotionStart::unchanged, "the same target again changes nothing");
    std::size_t frames = 0U;
    for (int k = 1; k <= 40; ++k) {
        const FramePollResult result = frame(*fixture, milliseconds(16));
        frames += result.motion_clock_ticks;
        if (mover.first.settled()) break;
        const double expected = smoothstep(16.0 * k / 180.0);
        require(std::fabs(mover.first.current() - expected) < 1e-12, "each frame samples smoothstep at its time");
    }
    require(frames == 12U, "180 ms at 16 ms frames takes 12 frames: 11 in flight and the one that settles");
    require(mover.first.current() == 1.0 && mover.steps.back().settled && mover.steps.back().current == 1.0,
            "the last frame stands exactly at the target");
    require(!window.motion_clock().armed() && !window.next_wake() && window.motion_clock().moving_count() == 0U,
            "once settled the clock disconnects its request");
    const MetricsSnapshot settled = window.metrics_snapshot();
    for (int k = 0; k < 10; ++k) {
        const FramePollResult idle = frame(*fixture, milliseconds(16));
        require(idle.motion_clock_ticks == 0U && !idle.next_wake, "a settled window polls no motion frames");
    }
    const MetricsSnapshot after = window.metrics_snapshot();
    require(after.motion_clock_ticks == settled.motion_clock_ticks && after.motion_values_moving == 0U &&
                after.motion_clock_ticks - before.motion_clock_ticks == 12U &&
                after.motion_values_settled - before.motion_values_settled == 1U,
            "the window counters report zero frames after settling");
}

void one_request_for_many_values() {
    std::unique_ptr<Fixture> fixture = make_fixture(8U);
    Window& window = *(*fixture).window;
    const std::uint64_t requests = window.metrics_snapshot().scheduled_frame_requests;
    for (const std::shared_ptr<Mover>& mover : (*fixture).movers) {
        require((*mover).first.animate_to(1.0) == MotionStart::started &&
                    (*mover).second.animate_to(50.0) == MotionStart::started,
                "every value starts");
    }
    require(window.metrics_snapshot().scheduled_frame_requests - requests == 1U && window.motion_clock().moving_count() == 16U,
            "sixteen values moving share one frame request");
    for (int k = 0; k < 4; ++k) {
        const FramePollResult result = frame(*fixture, milliseconds(16));
        require(result.motion_clock_ticks == 1U && result.ui_timer_ticks == 0U,
                "each poll runs one motion frame for all of them");
    }
    for (const std::shared_ptr<Mover>& mover : (*fixture).movers) {
        require((*mover).steps_of((*mover).first) == 4U && (*mover).steps_of((*mover).second) == 4U,
                "every value advanced on every frame");
    }
    for (int k = 0; k < 200 && window.motion_clock().armed(); ++k) static_cast<void>(frame(*fixture, milliseconds(16)));
    require(!window.motion_clock().armed() && window.metrics_snapshot().scheduled_frame_requests - requests == 1U,
            "the one request ends when the last value settles");
}

// -- retargeting -----------------------------------------------------------------

void retarget_keeps_continuity() {
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    Mover& mover = *(*fixture).movers[0];
    // follow, 0 to 100 logical units: x(t) = 100 (1 − e^(−t/75 ms)).
    require(mover.second.animate_to(100.0) == MotionStart::started, "the follow starts");
    for (int k = 0; k < 3; ++k) static_cast<void>(frame(*fixture, milliseconds(16)));
    const double at_48 = mover.second.current();
    require(std::fabs(at_48 - 100.0 * (1.0 - std::exp(-48.0 / 75.0))) < 1e-9, "the follow is an exponential approach");
    // Between frames, 5 ms later, it turns back.
    (*fixture).time.time += milliseconds(5);
    const double expected_53 = 100.0 * (1.0 - std::exp(-53.0 / 75.0));
    require(mover.second.animate_to(-50.0) == MotionStart::started, "the retarget starts");
    require(std::fabs(mover.second.current() - expected_53) < 1e-9,
            "the retarget starts from where the value stands at that moment");
    static_cast<void>(frame(*fixture, milliseconds(11)));
    const double expected_64 = -50.0 + (expected_53 + 50.0) * std::exp(-11.0 / 75.0);
    require(std::fabs(mover.second.current() - expected_64) < 1e-9, "and approaches the new target from there");

    // A spring keeps its velocity across a retarget.
    mover.second.set_curve(MotionCurve::spring(milliseconds(40)));
    mover.second.jump_to(0.0);
    require(mover.second.animate_to(100.0) == MotionStart::started, "the spring starts");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    static_cast<void>(frame(*fixture, milliseconds(16)));
    const double position = mover.second.current();
    const double velocity = mover.second.velocity();
    require(velocity > 0.0, "the spring is moving toward its target");
    require(mover.second.animate_to(0.0) == MotionStart::started, "the spring retargets at a frame's time");
    require(mover.second.current() == position && std::fabs(mover.second.velocity() - velocity) < 1e-9,
            "position and velocity are continuous across the retarget");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require(mover.second.current() > position, "its momentum carries it on before it turns");

    // A timed move retargets from where it stands.
    mover.first.jump_to(0.0);
    require(mover.first.animate_to(1.0) == MotionStart::started, "the timed move starts");
    static_cast<void>(frame(*fixture, milliseconds(90)));
    const double halfway = mover.first.current();
    require(std::fabs(halfway - 0.5) < 1e-12, "halfway through smoothstep is one half");
    require(mover.first.animate_to(0.0) == MotionStart::started && mover.first.current() == halfway,
            "a timed retarget starts where the value stands");
    // 16 ms on, smoothstep has covered 2.2 % of the way back.
    static_cast<void>(frame(*fixture, milliseconds(16)));
    const double back = halfway - halfway * smoothstep(16.0 / 180.0);
    require(std::fabs(mover.first.current() - back) < 1e-12, "and eases back from there without a jump");
}

// -- reduced motion and occlusion -------------------------------------------------

void reduced_motion_jumps_to_the_end() {
    std::unique_ptr<Fixture> fixture = make_fixture(3U);
    Window& window = *(*fixture).window;
    for (const std::shared_ptr<Mover>& mover : (*fixture).movers) {
        static_cast<void>((*mover).first.animate_to(1.0));
        static_cast<void>((*mover).second.animate_to(80.0));
    }
    static_cast<void>(frame(*fixture, milliseconds(16)));
    static_cast<void>(frame(*fixture, milliseconds(16)));
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    for (const std::shared_ptr<Mover>& mover : (*fixture).movers) {
        require((*mover).first.current() == 1.0 && (*mover).second.current() == 80.0 && (*mover).first.settled() &&
                    (*mover).second.settled(),
                "turning reduced motion on puts every value at its end in the same call");
        require((*mover).steps.back().cause == MotionStepCause::reduced_motion && (*mover).steps.back().settled,
                "each listener is told why, once");
    }
    require(!window.motion_clock().armed() && !window.next_wake(), "nothing stays scheduled");
    Mover& mover = *(*fixture).movers[0];
    mover.first.jump_to(0.0);
    require(mover.first.animate_to(1.0) == MotionStart::finished && mover.first.current() == 1.0 &&
                !window.motion_clock().armed(),
            "under reduced motion a new move reaches its end at once");
    const FramePollResult result = frame(*fixture, milliseconds(16));
    require(result.motion_clock_ticks == 0U && !result.next_wake, "and no frame runs");
}

void occluded_window_schedules_nothing() {
    std::unique_ptr<Fixture> fixture = make_fixture(2U);
    Window& window = *(*fixture).window;
    Mover& mover = *(*fixture).movers[0];
    window.set_occluded(true, (*fixture).time.time);
    require(mover.first.animate_to(1.0) == MotionStart::finished && mover.first.current() == 1.0,
            "in an occluded window a move is at its end at once");
    require(!window.motion_clock().armed() && !window.next_wake(), "and nothing is scheduled");
    require(frame(*fixture, milliseconds(16)).motion_clock_ticks == 0U, "no frame runs");
    window.set_occluded(false, (*fixture).time.time);
    Mover& other = *(*fixture).movers[1];
    require(other.second.animate_to(40.0) == MotionStart::started, "a visible window moves again");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    window.set_occluded(true, (*fixture).time.time);
    require(other.second.current() == 40.0 && other.steps.back().cause == MotionStepCause::occluded &&
                !window.motion_clock().armed(),
            "occlusion mid-flight puts the value at its end and disconnects the request");
    window.set_occluded(false, (*fixture).time.time);
    require(!window.motion_clock().armed(), "showing the window again restarts nothing");
}

// -- durations, easings and settled conditions -----------------------------------

void durations_are_clamped() {
    require(MotionCurve::timed(milliseconds(1000), MotionEasing::linear).duration == motion_duration_ceiling &&
                MotionCurve::follow(milliseconds(900)).duration == motion_duration_ceiling &&
                MotionCurve::spring(milliseconds(500)).duration == motion_duration_ceiling &&
                MotionCurve::decay(milliseconds(401)).duration == motion_duration_ceiling,
            "every duration is clamped to the 400 ms ceiling");
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    Mover& mover = *(*fixture).movers[0];
    mover.first.set_curve(MotionCurve::timed(std::chrono::seconds(2), MotionEasing::linear));
    require(mover.first.animate_to(1.0) == MotionStart::started, "a long move starts");
    std::size_t frames = 0U;
    for (int k = 0; k < 100 && !mover.first.settled(); ++k) frames += frame(*fixture, milliseconds(16)).motion_clock_ticks;
    require(frames == 25U && mover.first.current() == 1.0, "a 2 s move ends at 400 ms");
    mover.first.set_curve(MotionCurve::timed(milliseconds(0), MotionEasing::linear));
    require(mover.first.animate_to(0.0) == MotionStart::finished && mover.first.current() == 0.0,
            "a zero duration is at its end at once");
}

void easings_and_overshoot() {
    const MotionEasing easings[] = {MotionEasing::smoothstep, MotionEasing::exponential_approach, MotionEasing::linear,
                                    MotionEasing::ease, MotionEasing::overshoot};
    for (const MotionEasing easing : easings) {
        require(motion_easing(easing, 0.0) == 0.0 && motion_easing(easing, 1.0) == 1.0,
                "every easing runs from exactly 0 to exactly 1");
    }
    double peak = 0.0;
    for (int step = 0; step <= 10000; ++step) {
        const double eased = motion_easing(MotionEasing::overshoot, step / 10000.0);
        if (eased > peak) peak = eased;
    }
    require(peak <= 1.08 + 1e-12 && peak > 1.0799, "the overshoot easing overshoots by 8 %");
    require(std::fabs(motion_easing(MotionEasing::ease, 0.5) - 0.5) < 1e-15 &&
                std::fabs(motion_easing(MotionEasing::smoothstep, 0.25) - 0.15625) < 1e-15,
            "ease and smoothstep have their stated shapes");

    require(MotionCurve::spring(milliseconds(40), 0.5).overshoot == maximum_motion_overshoot &&
                MotionCurve::spring(milliseconds(40), -1.0).overshoot == 0.0,
            "a spring's overshoot is clamped to [0, 8 %]");
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    Mover& mover = *(*fixture).movers[0];
    // Large extent, so the settled condition does not end the motion early.
    require(mover.second.set_extent(1000.0), "the extent is accepted");
    require(!mover.second.set_extent(0.0) && !mover.second.set_extent(std::nan("")) && mover.second.extent() == 1000.0,
            "an invalid extent is refused and changes nothing");
    mover.second.set_curve(MotionCurve::spring(milliseconds(30), 0.08));
    require(mover.second.animate_to(1.0) == MotionStart::started, "the springy move starts");
    double spring_peak = 0.0;
    for (int k = 0; k < 2000 && !mover.second.settled(); ++k) {
        static_cast<void>(frame(*fixture, milliseconds(16)));
        if (mover.second.current() > spring_peak) spring_peak = mover.second.current();
    }
    require(spring_peak <= 1.08 + 1e-9 && spring_peak > 1.07, "a spring asked for 8 % overshoots by 8 %");
    require(mover.second.settled() && mover.second.current() == 1.0, "and settles exactly at its target");
    mover.second.set_curve(MotionCurve::spring(milliseconds(30)));
    mover.second.jump_to(0.0);
    require(mover.second.animate_to(1.0) == MotionStart::started, "the critically damped move starts");
    double previous = 0.0;
    for (int k = 0; k < 2000 && !mover.second.settled(); ++k) {
        static_cast<void>(frame(*fixture, milliseconds(16)));
        require(mover.second.current() >= previous && mover.second.current() <= 1.0,
                "a critically damped spring never overshoots");
        previous = mover.second.current();
    }
}

void follow_decay_and_settled_conditions() {
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    Window& window = *(*fixture).window;
    Mover& mover = *(*fixture).movers[0];
    // A fixed fraction of the gap per 75 ms (GIVEN D3-1): at 75 ms, 1 − 1/e.
    require(mover.second.set_extent(1000.0), "a progress over 1000 units");
    require(mover.second.animate_to(1.0) == MotionStart::started, "the rise starts");
    static_cast<void>(frame(*fixture, milliseconds(75)));
    require(std::fabs(mover.second.current() - (1.0 - std::exp(-1.0))) < 1e-12,
            "a follow covers 63 % of the gap each 75 ms");
    static_cast<void>(frame(*fixture, milliseconds(75)));
    require(std::fabs(mover.second.current() - (1.0 - std::exp(-2.0))) < 1e-12, "and 63 % of what remains next");

    // The settled condition is half a device pixel at the window's scale.
    window.set_scale(2.0);
    require(mover.second.set_extent(1.0), "a value in logical units");
    mover.second.jump_to(0.0);
    require(mover.second.animate_to(10.0) == MotionStart::started, "a 10-unit follow starts");
    // The gap reaches 0.25 logical units (half a device pixel at scale 2)
    // after 75 ln(40) ms, about 276.7 ms: the frame at 288 ms (the 18th)
    // settles, and the one at 272 ms does not.
    int frames = 0;
    while (frames < 100 && !mover.second.settled()) {
        frames += static_cast<int>(frame(*fixture, milliseconds(16)).motion_clock_ticks);
        if (mover.second.settled()) break;
        require(10.0 - mover.second.current() > 0.25, "an unsettled follow is more than half a device pixel away");
    }
    require(frames == 18 && mover.second.current() == 10.0, "it settles at 288 ms and snaps exactly to the target");

    // Decay: momentum with friction comes to rest where the velocity
    // carries it, vτ further on.
    require(mover.second.fling(100.0) == MotionStart::refused && mover.second.current() == 10.0,
            "a fling needs a decay curve");
    mover.second.set_curve(MotionCurve::decay(milliseconds(100)));
    require(mover.second.fling(1000.0) == MotionStart::started && mover.second.target() == 110.0,
            "a fling of 1000 units/s with τ 100 ms rests 100 units on");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require(std::fabs(mover.second.velocity() - 1000.0 * std::exp(-0.16)) < 1e-9, "its velocity decays by e per τ");
    for (int k = 0; k < 400 && !mover.second.settled(); ++k) static_cast<void>(frame(*fixture, milliseconds(16)));
    require(mover.second.settled() && mover.second.current() == 110.0, "and it rests exactly there");
    require(mover.second.animate_to(std::nan("")) == MotionStart::refused &&
                mover.second.animate_to(1e300) == MotionStart::refused && mover.second.current() == 110.0,
            "a non-finite or huge target is refused and changes nothing");
}

// -- owners, listeners and lifetimes ----------------------------------------------

void owners_and_stale_records() {
    std::unique_ptr<Fixture> fixture = make_fixture(2U);
    Window& window = *(*fixture).window;
    std::shared_ptr<Mover> leaving = (*fixture).movers[1];
    static_cast<void>((*leaving).first.animate_to(1.0));
    static_cast<void>(frame(*fixture, milliseconds(16)));
    const std::size_t seen = (*leaving).steps.size();
    static_cast<void>((*(*fixture).root).remove_child((*leaving).runtime_id()));
    require((*leaving).first.settled() && (*leaving).first.current() == 1.0 && !(*leaving).first.enlisted() &&
                (*leaving).steps.size() == seen && !window.motion_clock().armed(),
            "a control leaving the window takes its values to their ends, without listeners");
    require((*leaving).first.animate_to(0.0) == MotionStart::detached && (*leaving).first.current() == 0.0,
            "a value of a control in no window jumps to its end");

    // A value destroyed while it moves releases its record.
    Mover& mover = *(*fixture).movers[0];
    mover.make_extra();
    static_cast<void>((*mover.extra).animate_to(1.0));
    static_cast<void>(mover.first.animate_to(1.0));
    require(window.motion_clock().moving_count() == 2U, "two values move");
    mover.extra.reset();
    require(window.motion_clock().moving_count() == 1U, "the destroyed value's record is released");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require(mover.steps_of(mover.first) == 1U, "the others keep moving");

    // A listener that finishes the other value during a frame (whichever
    // steps first): that value's stale record is not advanced.
    mover.first.jump_to(0.0);
    mover.second.jump_to(0.0);
    mover.steps.clear();
    static_cast<void>(mover.first.animate_to(1.0));
    static_cast<void>(mover.second.animate_to(10.0));
    mover.action = Mover::Action::finish_other_value;
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require(mover.steps.size() == 1U && mover.first.settled() != mover.second.settled(),
            "a value released earlier in the frame is not advanced");
    // Keep the timed value moving for what follows.
    if (mover.first.settled()) {
        mover.first.jump_to(0.0);
        static_cast<void>(mover.first.animate_to(1.0));
    }
    mover.second.finish();
    require(mover.second.current() == 10.0, "the finished value stands at its target");

    // A listener that retargets its own value keeps it moving; one that
    // starts another control's value has it advanced from the next frame.
    std::shared_ptr<Mover> third = make_control<Mover>(StableId("mover.third"));
    (*(*fixture).root).add_child(third);
    mover.other = third.get();
    mover.action = Mover::Action::start_other;
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require((*third).first.enlisted() && (*third).steps.empty(), "a value started by a listener waits for the next frame");
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require((*third).steps.size() == 1U, "and is advanced on it");
    mover.action = Mover::Action::retarget_self;
    static_cast<void>(frame(*fixture, milliseconds(16)));
    require(mover.first.target() == -1.0 && mover.first.enlisted(), "a listener may retarget its own value");
    // A throwing listener ends the process at the fail-fast boundary
    // (fail_fast_tests, motion_listener).
}

void window_closing() {
    std::unique_ptr<Fixture> fixture = make_fixture(1U);
    std::shared_ptr<Mover> kept = (*fixture).movers[0];
    static_cast<void>((*kept).first.animate_to(1.0));
    static_cast<void>((*kept).second.animate_to(5.0));
    (*(*fixture).window).cancel_frame_requests();
    require((*kept).first.current() == 1.0 && (*kept).second.current() == 5.0 &&
                !(*(*fixture).window).motion_clock().armed(),
            "cancelling the window's frame requests cancels the clock: values at their ends");
    static_cast<void>((*kept).first.animate_to(0.0));
    require((*kept).first.enlisted(), "a value moves again");
    // The window goes first; the control and its values live on.
    (*fixture).window.reset();
    require(!(*kept).first.enlisted() && (*kept).first.current() == 0.0, "closing the window ends the motion");
    kept.reset();
}

// -- identical samples on every host ---------------------------------------------

// FNV-1a over the bytes of each double, in order.
struct BitHash final {
    std::uint64_t value{14695981039346656037ULL};

    void add(double number) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &number, sizeof bits);
        for (int shift = 0; shift < 64; shift += 8) {
            const std::uint64_t byte = (bits >> shift) & 0xFFU;
            value = value ^ byte;
            value = value * 1099511628211ULL;
        }
    }
};

// The bits of a fixed table of samples, the same on every host. The
// expected value was produced on macOS arm64 when the motion clock began
// keeping its phase (2026-11-01; Windows x64 produced the one before) and
// is checked by every CI platform.
constexpr std::uint64_t expected_motion_hash = 0x2f342903f8ff3083ULL;

void identical_samples() {
    BitHash hash{};
    const MotionCurve curves[] = {
        MotionCurve::timed(milliseconds(180), MotionEasing::smoothstep),
        MotionCurve::timed(milliseconds(280), MotionEasing::exponential_approach),
        MotionCurve::timed(milliseconds(100), MotionEasing::linear),
        MotionCurve::timed(milliseconds(150), MotionEasing::ease),
        MotionCurve::timed(milliseconds(400), MotionEasing::overshoot),
        MotionCurve::follow(),
        MotionCurve::spring(milliseconds(35)),
        MotionCurve::spring(milliseconds(35), 0.08),
    };
    for (const MotionCurve& curve : curves) {
        std::unique_ptr<Fixture> fixture = make_fixture(1U);
        Mover& mover = *(*fixture).movers[0];
        require(mover.second.set_extent(100.0), "extent");
        mover.second.set_curve(curve);
        static_cast<void>(mover.second.animate_to(1.0));
        for (int k = 0; k < 40 && !mover.second.settled(); ++k) {
            static_cast<void>(frame(*fixture, milliseconds(7)));
            hash.add(mover.second.current());
            hash.add(mover.second.velocity());
        }
    }
    if (print_mode()) {
        std::cout << "motion sample hash 0x" << std::hex << hash.value << std::dec << '\n';
        return;
    }
    require(hash.value == expected_motion_hash, "the motion samples have the same bits on every host");
}

} // namespace

int main() {
    zero_frames_after_settling();
    one_request_for_many_values();
    retarget_keeps_continuity();
    reduced_motion_jumps_to_the_end();
    occluded_window_schedules_nothing();
    durations_are_clamped();
    easings_and_overshoot();
    follow_decay_and_settled_conditions();
    owners_and_stale_records();
    window_closing();
    identical_samples();
    std::cout << "motion clock: zero frames after settling, one request, continuity, reduced motion, occlusion, "
                 "the ceiling, easings and overshoot, settled conditions, owners and lifetimes, identical samples "
                 "passed\n";
    return 0;
}
