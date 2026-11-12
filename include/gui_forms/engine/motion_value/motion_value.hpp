#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {

class Control;
class MotionClock;
class MotionValue;

// Control-owned motion values (T2 plan, phase E3; GIVEN D1-1). A control
// keeps presentational motion it computes itself (a docking stamp's size, a
// flyout's slide) in a MotionValue, sets its target, and reads its current
// value during layout and paint. The window's MotionClock advances every
// value that has not settled, once per frame, and holds no frame request
// once they all have.
//
// Values are plain doubles in the owner's units: a progress from 0 to 1, a
// length in logical units, an amplitude. Velocities are value units per
// second.

// The ceiling on every duration (design language §9.2; the 400 ms starting
// token). A longer duration or time constant is clamped to it.
inline constexpr FrameInterval motion_duration_ceiling = std::chrono::milliseconds(400);

// The exponential approach's time constant (GIVEN D3-1, "a fixed fraction of
// the remaining gap per about 75 ms"): each 75 ms the gap shrinks by a factor
// of e, so 63 % of what remains is covered.
inline constexpr FrameInterval follow_time_constant = std::chrono::milliseconds(75);

// The largest overshoot a spring or the overshoot easing shows, as a
// fraction of the distance travelled (the 8 % starting token). A larger
// request is clamped to it.
inline constexpr double maximum_motion_overshoot = 0.08;

// The settled conditions' tolerance: half a device pixel at the window's
// scale. A settling value whose remaining gap (and, for a spring, whose
// movement over one 60 Hz frame) is within it snaps to its target.
inline constexpr double motion_settle_tolerance = 0.5;
inline constexpr double motion_settle_frame_seconds = 1.0 / 60.0;

// The bounds of a value's extent (MotionValue::set_extent).
inline constexpr double minimum_motion_extent = 1e-6;
inline constexpr double maximum_motion_extent = 1e6;

enum class MotionKind : std::uint8_t {
    // Exponential approach: the gap shrinks by e each time constant. It
    // keeps no velocity of its own.
    follow,
    // A damped spring toward the target, from the value's position and
    // velocity. Critically damped unless an overshoot is asked for.
    spring,
    // Momentum with friction: the velocity shrinks by e each time constant,
    // and the value comes to rest where that carries it.
    decay,
    // An eased transition from where the value stands to the target over a
    // duration.
    timed,
};

// The easings of a timed transition (design language §9.2), as functions of
// the progress p from 0 to 1. Each starts at 0 and ends at exactly 1.
enum class MotionEasing : std::uint8_t {
    // 3p² − 2p³: opening and closing surfaces.
    smoothstep,
    // (1 − e^(−5p)) / (1 − e^(−5)): the exponential approach normalized to
    // end at 1, the gap shrinking by e each fifth of the duration.
    exponential_approach,
    // p.
    linear,
    // Cubic ease-in-out: 4p³ below one half, 1 − (2 − 2p)³ / 2 above.
    ease,
    // Back-out, 1 + 2.5 (p − 1)³ + 1.5 (p − 1)²: one overshoot of exactly
    // 8 %, at p = 0.6, then settling at 1. An engine capability for loud
    // themes; the default theme does not use it.
    overshoot,
    // CSS cubic-bezier(x1, y1, x2, y2) (E9.6), the curve's control points
    // in MotionCurve::bezier: the curve's y where its x is p. Its x is
    // solved by 40 bisections, so every host gives the same value.
    cubic_bezier,
    // CSS steps(n) (E9.6): n levels of 1/n, held between jumps
    // (MotionCurve::steps and step_position).
    steps,
};

// Where steps(n) jumps (CSS jump-end and jump-start): at the end of each
// of its n intervals, so that it starts at 0; or at the start, so that it
// starts at 1/n. Either ends at 1.
enum class MotionStepPosition : std::uint8_t { end, start };

// The most levels steps() takes.
inline constexpr std::uint32_t maximum_motion_steps = 32U;

// One intermediate stop of a timed move (E9.6, multi-stop @keyframes): the
// value passes `value` at `offset` of the move's duration, 0 < offset < 1.
// Between stops the move is eased by its curve's easing.
struct GUI_FORMS_API MotionStop final {
    double offset{};
    double value{};
    // The value's em coefficient (T3 P8c): a theme's stop is `value` plus
    // the part's em times it; the theme engine resolves it before the stop
    // reaches a motion value, which reads `value` alone.
    double value_em{};

    friend bool operator==(const MotionStop& left, const MotionStop& right) noexcept {
        return left.offset == right.offset && left.value == right.value && left.value_em == right.value_em;
    }
};

// The most stops one move passes.
inline constexpr std::size_t maximum_motion_stops = 4U;

// How a value moves. Make it with the named factories, which keep the
// fields that belong together consistent.
struct GUI_FORMS_API MotionCurve final {
    MotionKind kind{MotionKind::timed};
    // timed: the duration. follow and decay: the time constant. spring: the
    // response time, 1/ω for the natural angular frequency ω. Clamped to
    // [0, motion_duration_ceiling]; zero means the value jumps to its end.
    FrameInterval duration{std::chrono::milliseconds(180)};
    // timed only.
    MotionEasing easing{MotionEasing::smoothstep};
    // spring only: the overshoot of a move from rest, as a fraction of the
    // distance, from 0 (critically damped) to maximum_motion_overshoot.
    double overshoot{};
    // timed with cubic_bezier: x1, y1, x2, y2. Each x is clamped to [0, 1]
    // and each y to [−maximum_motion_overshoot, 1 + maximum_motion_overshoot],
    // so the curve overshoots by at most 8 %; a non-finite point is the
    // linear curve's.
    std::array<double, 4> bezier{0.0, 0.0, 1.0, 1.0};
    // timed with steps: the levels, clamped to [1, maximum_motion_steps],
    // and where they jump.
    std::uint32_t steps{1U};
    MotionStepPosition step_position{MotionStepPosition::end};
    // Every curve (E9.6, CSS transition-delay): how long a move holds the
    // value still before it starts, at no velocity. Clamped to [0, the
    // ceiling less the duration], so that a delayed move still ends within
    // motion_duration_ceiling.
    FrameInterval delay{};

    [[nodiscard]] static MotionCurve timed(FrameInterval duration, MotionEasing easing) noexcept;
    [[nodiscard]] static MotionCurve cubic_bezier(FrameInterval duration, double x1, double y1, double x2,
                                                  double y2) noexcept;
    [[nodiscard]] static MotionCurve stepped(FrameInterval duration, std::uint32_t steps,
                                             MotionStepPosition position = MotionStepPosition::end) noexcept;
    [[nodiscard]] static MotionCurve follow(FrameInterval time_constant = follow_time_constant) noexcept;
    [[nodiscard]] static MotionCurve spring(FrameInterval response, double overshoot = 0.0) noexcept;
    [[nodiscard]] static MotionCurve decay(FrameInterval time_constant) noexcept;
    // This curve with `delay` before each move, clamped as `delay` is.
    [[nodiscard]] MotionCurve delayed(FrameInterval delay) const noexcept;

    friend bool operator==(const MotionCurve& left, const MotionCurve& right) noexcept {
        return left.kind == right.kind && left.duration == right.duration && left.easing == right.easing &&
               left.overshoot == right.overshoot && left.bezier == right.bezier && left.steps == right.steps &&
               left.step_position == right.step_position && left.delay == right.delay;
    }
};

// The eased progress for `progress` in [0, 1] (clamped), for tests and for
// later engine transitions. cubic_bezier and steps take their defaults
// here: the linear curve and one step; motion_curve_easing reads a curve's
// own.
[[nodiscard]] GUI_FORMS_API double motion_easing(MotionEasing easing, double progress) noexcept;
// The eased progress of a timed `curve` (its easing with its control points
// or steps) for `progress` in [0, 1] (clamped).
[[nodiscard]] GUI_FORMS_API double motion_curve_easing(const MotionCurve& curve, double progress) noexcept;

// Why a listener is told of a step.
enum class MotionStepCause : std::uint8_t {
    // A frame of the clock advanced the value to the frame's time.
    advanced,
    // Reduced motion was turned on (GIVEN D2-1): the value jumped to its end.
    reduced_motion,
    // The window became occluded or minimized (GIVEN D4-1): the value jumped
    // to its end.
    occluded,
};

// One step of a value, as its listener sees it. `value` is the value that
// moved; it is valid for the call.
struct GUI_FORMS_API MotionStep final {
    MotionValue* value{};
    double previous{};
    double current{};
    // The value reached its settled condition (or jumped to its end) and
    // stands exactly at its target; the clock no longer advances it.
    bool settled{};
    MotionStepCause cause{MotionStepCause::advanced};
};

// A value's listener: a named member or function of the owner, bound with
// Delegate::bind. It runs on the UI thread, from the clock's frame or from
// the window event that ended the motion, after the value moved and after
// the owner was invalidated as declared. It may retarget, finish or jump
// any value, its own included; a value it starts is first advanced on the
// next frame.
using MotionListener = Delegate<const MotionStep&>;

// What animate_to, fling and adopt did.
enum class MotionStart : std::uint8_t {
    // The value is moving on its owner's window clock.
    started,
    // Nothing changed: the value already moves to, or stands settled at,
    // the target.
    unchanged,
    // The value jumped to its end at once: reduced motion, an occluded or
    // minimized window, or a zero duration. No listener runs; the caller
    // reads the value.
    finished,
    // The owner is in no window, so there is no clock: the value jumped to
    // its end.
    detached,
    // The target or velocity is not finite, or fling was asked of a curve
    // other than decay. The value is unchanged.
    refused,
};

// Which record of a clock a value holds while it moves: a slot and the
// slot's generation when the record was made. A released record's handle is
// stale and names nothing, even after its slot is reused.
struct GUI_FORMS_API MotionRecordHandle final {
    std::uint32_t slot{};
    std::uint32_t generation{};

    friend constexpr bool operator==(MotionRecordHandle left, MotionRecordHandle right) noexcept {
        return left.slot == right.slot && left.generation == right.generation;
    }
};

// One value. Ownership: a control owns it as a member (or through something
// the control owns), and names itself as the owner, so the owner outlives
// it. It neither copies nor moves, so the clock's record of it stays valid;
// it releases that record when it is destroyed. All of it runs on the UI
// thread of the owner's window.
//
// The settled conditions, in device pixels at the window's scale (extent
// times scale converts value units):
// - timed: the duration has elapsed;
// - follow and decay: the remaining gap is within motion_settle_tolerance;
// - spring: the gap is within it, and so is the movement over one 60 Hz
//   frame at the current velocity.
// On settling the value snaps exactly to its target, so the last frame is
// the static rendering.
//
// Every move starts from where the value stands at that moment, with its
// velocity: a retarget mid-flight never jumps (design language §9.1).
class GUI_FORMS_API MotionValue final {
public:
    // A settled value at `initial`, owned by `owner`.
    MotionValue(Control& owner, MotionCurve curve, double initial = 0.0) noexcept;
    ~MotionValue();
    MotionValue(const MotionValue&) = delete;
    MotionValue& operator=(const MotionValue&) = delete;

    // The curve of the next move; a move under way keeps its own. Durations
    // and overshoot are clamped (see MotionCurve).
    void set_curve(MotionCurve curve) noexcept;
    // The logical length one unit of the value spans, for the settled
    // conditions (default 1: the value is a logical length). A progress
    // across a 150-unit slide has extent 150. Refused (returns false, the
    // extent unchanged) outside [minimum_motion_extent,
    // maximum_motion_extent] or when not finite.
    bool set_extent(double logical_units) noexcept;
    void set_listener(MotionListener listener) noexcept { listener_ = listener; }
    // What the clock invalidates on the owner after each step; none by
    // default (the listener invalidates what it moved).
    void set_invalidation(Dirty dirty) noexcept { invalidation_ = dirty; }

    // Moves toward `target` from where the value stands now. A decay comes
    // to rest at the target. If the clock's record or its frame request
    // cannot be made, the exception propagates and the value is unchanged.
    MotionStart animate_to(double target);
    // A timed move from where the value stands now through `stops` to
    // `target` (E9.6): each stop's offset is a fraction of the duration,
    // strictly increasing within (0, 1). It starts a new move even toward
    // the target already set. Refused,
    // the value unchanged, for a curve that is not timed, more than
    // maximum_motion_stops stops, an offset out of order or out of range,
    // or a value or target that is not finite. While it runs, animate_to
    // the same target leaves it running.
    MotionStart animate_through(std::span<const MotionStop> stops, double target);
    // decay only: launches the value at `velocity` (value units per second);
    // it comes to rest where friction stops it.
    MotionStart fling(double velocity);
    // Moves as `source` moves: its curve, its current move and its time, on
    // this value's own clock (a docking guest showing another window's
    // stamp). A settled source makes this value jump to its value.
    MotionStart adopt(const MotionValue& source);
    // Ends the move: the value stands at its target, settled. No listener
    // runs.
    void finish() noexcept;
    // Stands the value at `value`, settled (a non-finite value is ignored).
    // No listener runs.
    void jump_to(double value) noexcept;

    // The value at the last frame, start or jump.
    [[nodiscard]] double current() const noexcept { return current_; }
    // Where the value is going (a decay: where it comes to rest).
    [[nodiscard]] double target() const noexcept { return segment_.target; }
    [[nodiscard]] double velocity() const noexcept { return velocity_; }
    [[nodiscard]] bool settled() const noexcept { return !moving_; }
    [[nodiscard]] const MotionCurve& curve() const noexcept { return curve_; }
    [[nodiscard]] double extent() const noexcept { return extent_; }
    [[nodiscard]] Control& owner() const noexcept { return *owner_; }
    // Whether a clock holds this value's record (it is moving on it).
    [[nodiscard]] bool enlisted() const noexcept { return clock_ != nullptr; }

private:
    friend class MotionClock;

    // One move: where it started, when, with which velocity, toward what,
    // and the constants its curve derived at the start.
    struct Segment final {
        FrameTime start{};
        double origin{};
        double origin_velocity{};
        double target{};
        MotionCurve curve{};
        // follow and decay: 1/τ. spring: ζω, the envelope's decay rate.
        double rate{};
        // spring: ω, and the damped frequency ω√(1 − ζ²) (0 when critically
        // damped).
        double natural{};
        double damped{};
        // timed: the duration in seconds.
        double seconds{};
        // The delay before the move starts (MotionCurve::delay), kept in
        // clock ticks so that the move's end is measured exactly: a delayed
        // move ends on the frame at its delay plus its duration.
        FrameInterval delay{};
        // timed: the stops the move passes, `stop_count` of them live.
        std::array<MotionStop, maximum_motion_stops> stops{};
        std::size_t stop_count{};
    };

    // The position and velocity of a segment `elapsed` seconds after its
    // move started (after its delay), and whether a timed segment has run
    // its duration.
    struct Sample final {
        double position{};
        double velocity{};
        bool elapsed_all{};
    };

    [[nodiscard]] static Sample evaluate(const Segment& segment, double elapsed) noexcept;
    [[nodiscard]] static Segment make_segment(const MotionCurve& curve, FrameTime start, double origin,
                                              double velocity, double target) noexcept;
    // Where the value stands at `now`: its moving segment sampled, or its
    // settled value at rest.
    [[nodiscard]] Sample state_at(FrameTime now) const noexcept;
    // Starts `next` on the owner's clock (shared by animate_to, fling and
    // adopt). Validated and enlisted before anything changes.
    MotionStart begin(const Segment& next, double position, double velocity);
    // Advances to `now` at `scale`; returns true when the value settled
    // (and snapped to its target). Called by the clock.
    bool advance(FrameTime now, double scale) noexcept;
    // Stands the value at its target, settled, without touching the clock.
    void stop() noexcept;

    Control* owner_;
    MotionCurve curve_{};
    MotionListener listener_{};
    Dirty invalidation_{Dirty::none};
    double extent_{1.0};
    Segment segment_{};
    double current_{};
    double velocity_{};
    bool moving_{};
    // The clock holding this value's record while it moves, and the record.
    // Both are cleared by the clock when it releases the record.
    MotionClock* clock_{};
    MotionRecordHandle record_{};
};

} // namespace gui_forms
