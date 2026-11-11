#pragma once

#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gui_forms {

class Control;
class Window;
class SurfaceRoot;

// Where a clock reads the time a motion starts. Steady-clock time unless a
// source is installed: tests install one so that motion is sampled from a
// time they choose, never from the wall clock. Its times must be in the
// same domain as the times the window is polled with.
class MotionTimeSource {
public:
    virtual ~MotionTimeSource() = default;
    [[nodiscard]] virtual FrameTime now() const noexcept = 0;
};

// The clock's frame cadence while something moves.
inline constexpr FrameInterval motion_frame_interval = std::chrono::milliseconds(16);

// One window's motion clock (T2 plan, MotionClock; GIVEN D1-1). The window
// owns it; it observes the window. It holds at most one frame request,
// armed only while some value it holds has not settled, however many values
// move; the request is disconnected once the last one settles, so a window
// at rest schedules no frames.
//
// Execution: every member runs on the window's UI thread. Frames, settling
// and the listeners' damage all happen inside the window's frame poll.
//
// Event order on a frame: each value enlisted before the frame starts is
// advanced to the frame's time, in the order of its record's slot (the same
// for the same sequence of starts and releases); a value that
// settles is released before its listener runs; then the owner is
// invalidated as the value declared, then its listener runs. Values started
// by a listener are first advanced on the next frame. A listener that
// throws is counted as a frame callback fault; its value jumps to its end.
//
// Ending motion (all by name):
// - finish_all: reduced motion turned on (GIVEN D2-1) or the window
//   occluded or minimized (GIVEN D4-1). Every value jumps to its end and its
//   listener runs once, with the cause. While either holds, a new move
//   jumps to its end at once and nothing is scheduled.
// - release_owner: a control left the window. Its values jump to their
//   ends without listeners.
// - cancel: the window closes (or cancels its frame requests). Every value
//   jumps to its end without listeners, and the request is disconnected.
//
// A record is named by its handle; a stale handle (its record released) is
// rejected, so a value whose owner has gone is never advanced.
//
// Ambient effects (T2 plan, phase E7; design language §9.4) share the same
// request. The engine enlists a part with an ambient effect while it draws
// it; on each frame at least the effect's frame interval after its last,
// the part's extent repaints. An effect whose part was not repainted since
// its last frame (the part is no longer shown) is dropped, so an effect can
// never keep a window painting by itself. finish_all and cancel drop every
// effect too; finish_all repaints each part once, at its static pose.
//
// A control hosting an effect program (a progress bar's stripes or bubbles)
// enlists itself the same way (enlist_control_ambient): on each frame due,
// its on_ambient_frame advances and invalidates it, and a control that was
// not painted since its last frame is dropped. The clock observes it: one
// that died or left the window is dropped unasked. finish_all and stop
// park the controls (each repaints once, at its still pose, under
// finish_all); repaint_parked repaints them when ambient motion may resume,
// and their paints enlist them again.
class MotionClock final {
public:
    explicit MotionClock(SurfaceRoot& root) noexcept;
    ~MotionClock();
    MotionClock(const MotionClock&) = delete;
    MotionClock& operator=(const MotionClock&) = delete;

    // Borrows `source` (it must outlive its installation); null restores the
    // steady clock.
    void set_time_source(const MotionTimeSource* source) noexcept { time_source_ = source; }
    // The time a move starting now starts at.
    [[nodiscard]] FrameTime now() const noexcept;

    // Whether the clock holds its frame request.
    [[nodiscard]] bool armed() const noexcept { return request_.connected(); }
    // Values moving on the clock now.
    [[nodiscard]] std::size_t moving_count() const noexcept { return moving_; }
    // Whether a move would jump to its end at once: reduced motion, or an
    // occluded or minimized window.
    [[nodiscard]] bool suppressed() const noexcept;
    // Whether ambient effects are stopped: as suppressed(), or the
    // application's motion switch off (PresentationSettings::ambient_motion).
    [[nodiscard]] bool ambient_suppressed() const noexcept;
    // Ambient effects running on the clock now (parts' and controls').
    [[nodiscard]] std::size_t ambient_count() const noexcept { return ambient_live_; }

    // A control's ambient effect, its frames at least `interval` apart (see
    // the class comment). Returns true when the control was not enlisted
    // (its effect's time starts now), false when it was (the interval is
    // replaced). Nothing is enlisted, and false returned, while ambient
    // motion is suppressed. If the record's place or the frame request
    // cannot be made, the exception propagates and nothing changes. The
    // control must be held by a shared pointer and attached to this clock's
    // window.
    bool enlist_control_ambient(Control& control, FrameInterval interval);
    // Ends the control's ambient effect, if any (parked too). Never fails.
    void withdraw_control_ambient(const Control& control) noexcept;
    // Whether the control is enlisted now.
    [[nodiscard]] bool control_ambient(const Control& control) const noexcept;

private:
    friend class MotionValue;
    friend class Window;
    friend class SurfaceRoot;

    struct Record final {
        MotionValue* value{};
        // The owner, observed: checked before every step, so a value whose
        // owner has gone or left the window is released, never advanced.
        std::weak_ptr<Control> owner{};
        const Control* owner_address{};
        // Starts at 1, so that a default handle (generation 0) is never live.
        std::uint32_t generation{1U};
        bool live{};
    };

    // For MotionValue. enlist() makes the value's record (if it has none)
    // and arms the frame request; on failure (allocation, the window's
    // request limit) it changes nothing and rethrows. release() frees the
    // value's record; it never allocates.
    void enlist(MotionValue& value);
    void release(MotionValue& value) noexcept;

    // For Window: an ambient effect on the part with record `part`, its
    // frames at least `interval` apart. Enlisting again changes the
    // interval only. If the record's place or the frame request cannot be
    // made, the exception propagates and nothing changes.
    void enlist_ambient(std::uint32_t part_slot, std::uint32_t part_generation, FrameInterval interval);
    void withdraw_ambient(std::uint32_t part_slot, std::uint32_t part_generation) noexcept;
    // Drops every ambient effect; with `repaint`, each part repaints once.
    // Controls are parked (see the class comment).
    void stop_ambient(bool repaint);
    // Repaints the parked controls once and forgets them: ambient motion
    // may resume, and each one's paint enlists it again.
    void repaint_parked();

    // For Window.
    // The frame at `now`: advances every value (see the event order), then
    // runs the ambient effects that are due.
    void advance(FrameTime now);
    void finish_all(MotionStepCause cause);
    void release_owner(const Control& owner) noexcept;
    void cancel() noexcept;

    [[nodiscard]] Record* find(MotionRecordHandle handle) noexcept;
    // Ends a record's lifetime and the value's link to it.
    void release_slot(std::uint32_t slot) noexcept;
    // Collects the live records' handles into handles_scratch_, in slot
    // order.
    void collect_handles();
    void disarm_when_idle() noexcept;
    void publish_moving() noexcept;
    // One step's invalidation and listener (through the fail-fast
    // boundary).
    void notify(Control& owner, MotionValue& value, const MotionStep& step);

    // One ambient effect: its part's record, its frame interval, when its
    // next frame is due, and the paint its last frame was answered by
    // (none_seen before its first frame).
    // A control's effect has `control` set (observed, with its address for
    // lookup) and no part; `parked` marks one stopped until repaint_parked.
    struct AmbientRecord final {
        std::uint32_t part_slot{};
        std::uint32_t part_generation{};
        std::weak_ptr<Control> control{};
        const Control* control_address{};
        FrameInterval interval{};
        FrameTime next{};
        std::uint64_t seen{};
        bool live{};
        bool parked{};
    };
    static constexpr std::uint64_t none_seen = ~std::uint64_t{0};

    void advance_ambient(FrameTime now);
    // A free ambient record's index (one is made, reserved first, when none
    // is free); the frame request is armed. Changes nothing on failure.
    std::size_t ambient_place();
    // One control's frame: whether it is still shown.
    [[nodiscard]] bool control_ambient_frame(AmbientRecord& effect, FrameTime now);
    // While only ambient effects run, moves the request's next frame to the
    // earliest effect's.
    void align_to_ambient() noexcept;

    // The root whose motion this clock advances; it owns the clock.
    SurfaceRoot* window_;
    const MotionTimeSource* time_source_{};
    std::vector<AmbientRecord> ambient_;
    std::size_t ambient_live_{};
    std::vector<Record> slots_;
    std::vector<std::uint32_t> free_slots_;
    // Reused by every frame, so that a frame allocates nothing once it has
    // grown to the most values moving at once.
    std::vector<MotionRecordHandle> handles_scratch_;
    std::size_t moving_{};
    FrameRequestToken request_{};
};

} // namespace gui_forms
