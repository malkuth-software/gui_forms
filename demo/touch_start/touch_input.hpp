#pragma once

#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// The touch demonstrators' input seams (owner, 2026-11-21; notes:planning/
// stages/TC-touch/README.md): the one place a pointer's press, motion and
// release become a gesture, and the one place a pan keeps moving after the
// finger lifts and stops at the surface's ends.
//
// What works now, for the mouse and the trackpad: a press that lifts within
// the slop is a tap; a press that travels past it pans directly (the surface
// follows the pointer) and ends where it was let go. What touch adds is
// marked TODO(touch) with the intended behaviour beside it: the flick (a
// release with velocity, carried on by inertia), the press-and-hold, the
// rubber-band overscroll and the touch slop. Filling those in changes the
// bodies below and the pointer kind the host reports, nothing else: the
// tile field already routes every press, move and release through
// GestureClassifier and every frame of a moving pan through PanInertia and
// settle_pan_offset.
//
// The phase names mirror the gesture family's GesturePhase (claude/
// magnify-input: began, changed, ended, cancelled, momentum,
// momentum_ended), so a host's touch events and its trackpad gestures can
// reach the same classifier. The demonstrator does not depend on that
// branch: main has no GestureEvent yet.
namespace gui_forms::touch_start {

// Where a gesture stands, as GesturePhase spells it.
enum class TouchPhase : std::uint8_t {
    began,
    changed,
    ended,
    cancelled,
    momentum,
    momentum_ended,
};

// What reported the press. TODO(touch): PointerEvent carries no pointer
// kind yet (TC-touch's future PointerEvent kind and touch fields); the
// field passes mouse until the hosts report touch and pen.
enum class PointerKind : std::uint8_t {
    mouse,
    pen,
    touch,
};

// One pointer sample in the field's coordinates, with the event's
// timestamp (MouseEventArgs::timestamp_nanoseconds; zero when the host
// gave none).
struct TouchSample final {
    Point position{};
    std::uint64_t timestamp_nanoseconds{};
};

// The distances and times that separate the gestures, in logical pixels,
// pixels per second and nanoseconds.
struct TouchThresholds final {
    // Travel within which a press is still a tap: the mouse's.
    double mouse_slop{4.0};
    // A finger's, larger (TC-touch: slop separates a tap from a pan);
    // press() takes it for PointerKind::touch, which no host reports yet.
    double touch_slop{10.0};
    // TODO(touch): the release speed above which a pan ends as a flick.
    double flick_speed{350.0};
    // TODO(touch): how long a still press takes to become a press-and-hold
    // (TC-touch: long press stands in for hover, for tool tips).
    std::uint64_t hold_nanoseconds{500'000'000U};
};

// What a press became, reported by release().
enum class GestureKind : std::uint8_t {
    // No press was down, or it was cancelled.
    none,
    // Lifted within the slop: select (TC-touch's press rule: on lift).
    tap,
    // Travelled past the slop and let go slowly: the pan ends there.
    pan,
    // Travelled past the slop and let go fast: the pan carries on with
    // `velocity` (TODO(touch): release() never reports it yet).
    flick,
    // Held still past hold_nanoseconds (TODO(touch): never reported yet).
    press_and_hold,
};

// A finished gesture: its kind, where it ended and, for a flick, the
// release velocity in logical pixels per second.
struct GestureClassification final {
    GestureKind kind{GestureKind::none};
    Point position{};
    Point velocity{};

    [[nodiscard]] static GestureClassification none() noexcept;
    [[nodiscard]] static GestureClassification tap(Point position) noexcept;
    [[nodiscard]] static GestureClassification pan(Point position) noexcept;
    [[nodiscard]] static GestureClassification flick(Point position, Point velocity) noexcept;
    [[nodiscard]] static GestureClassification press_and_hold(Point position) noexcept;
};

// One move's effect: whether the press is now a pan, its phase, and how far
// the surface follows since the last move.
struct PanStep final {
    bool panning{};
    TouchPhase phase{TouchPhase::began};
    Point delta{};
};

// The classifier one control keeps for its pointer: press, any number of
// moves, then release or cancel.
class GestureClassifier final {
public:
    // The most recent samples kept for the release velocity.
    static constexpr std::size_t sample_capacity = 8U;

    GestureClassifier() noexcept = default;
    explicit GestureClassifier(TouchThresholds thresholds) noexcept;

    // Pointer down. A press while one is down restarts.
    void press(PointerKind kind, TouchSample sample) noexcept;
    // Pointer moved while down: nothing until the travel passes the slop,
    // then the pan's steps.
    [[nodiscard]] PanStep move(TouchSample sample) noexcept;
    // Pointer up: what the press was. The classifier is idle afterwards.
    [[nodiscard]] GestureClassification release(TouchSample sample) noexcept;
    // The press was lost (capture lost, the window deactivated).
    void cancel() noexcept;

    [[nodiscard]] bool pressed() const noexcept;
    [[nodiscard]] bool panning() const noexcept;
    [[nodiscard]] PointerKind kind() const noexcept;
    // The slop in effect for the current press.
    [[nodiscard]] double slop() const noexcept;
    // The release velocity the kept samples give, logical pixels per
    // second. TODO(touch): a least-squares fit over the samples in the last
    // 100 ms (older ones are a pause, not a throw), zero when the pointer
    // stopped before lifting; zero for now.
    [[nodiscard]] Point estimated_velocity() const noexcept;

private:
    void keep(TouchSample sample) noexcept;

    TouchThresholds thresholds_{};
    PointerKind kind_{PointerKind::mouse};
    bool pressed_{};
    bool panning_{};
    TouchSample origin_{};
    TouchSample last_{};
    std::array<TouchSample, sample_capacity> samples_{};
    std::size_t sample_count_{};
    std::size_t next_sample_{};
};

// The momentum a flick leaves on a pan, along the surface's one axis.
// TODO(touch): start() keeps the release velocity and step() returns the
// distance an exponential decay covers in `seconds` (velocity times
// exp(-t / time_constant), time_constant about 325 ms, stopping below
// stop_speed), so the tile field adds it to its offset each frame with the
// phase momentum, then momentum_ended. Until then it is never active and
// a pan stops where it was let go.
class PanInertia final {
public:
    static constexpr double time_constant_seconds = 0.325;
    static constexpr double stop_speed = 20.0;

    void start(double velocity) noexcept;
    // The distance covered in the next `seconds`; zero when not active.
    [[nodiscard]] double step(double seconds) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] double velocity() const noexcept;

private:
    double velocity_{};
    bool active_{};
};

// The offset a pan may take on a surface whose offsets run from zero to
// `maximum`: clamped hard at both ends. TODO(touch): past an end, touch
// gives resistance instead (the excess scaled by 0.55 and by how far past
// it already is, at most a fifth of `viewport`), and spring_back_target
// then returns the end the surface eases back to once the finger lifts.
[[nodiscard]] double settle_pan_offset(double requested, double maximum, double viewport) noexcept;
// Where an overscrolled offset returns to: the nearer end, or the offset
// itself when it is within the ends.
[[nodiscard]] double spring_back_target(double offset, double maximum) noexcept;

} // namespace gui_forms::touch_start
