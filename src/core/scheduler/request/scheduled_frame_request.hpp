#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/scheduler.hpp"

#include <functional>

namespace gui_forms::detail {

enum class FrameRequestKind : std::uint8_t {
    deadline,
    ui_timer,
    // The window's motion clock: no target and no callback; each frame
    // advances the clock.
    motion_clock,
    // The window's idle engine work (E7r): no target and no callback; each
    // frame in which no motion runs and nothing waits to be painted bakes
    // one queued keyframe, and the request ends with the queue.
    engine_idle,
};

class ScheduledFrameRequest final : public Revocable {
public:
    ScheduledFrameRequest(FrameRequestKind request_kind,
                          Control::WeakPtr request_target,
                          FrameTime request_deadline,
                          FrameInterval request_interval = {}) noexcept;
    ScheduledFrameRequest(
        FrameTime request_deadline, FrameInterval request_interval,
        std::function<void(FrameTime)> request_callback) noexcept;

    void disconnect() noexcept override;
    [[nodiscard]] bool connected() const noexcept override;

    FrameRequestKind kind;
    Control::WeakPtr target;
    FrameTime deadline;
    FrameInterval interval;
    std::function<void(FrameTime)> callback;
    // A UI timer that rests while its window is occluded: no tick and no
    // wake until the window is shown again, when its cadence restarts
    // (SurfaceRoot::schedule_resting_ui_timer, the caret blink). Other
    // UI timers run while the window is hidden.
    bool rests_while_occluded{};
    // Paced to the window's display (the motion clock's request and the
    // animation timers'): `interval` is derived from `requested_interval`
    // and the window's refresh period and re-derived when that changes, the
    // deadline stays on the display's grid, and a UI timer's callback
    // receives the frame's presentation time rather than the poll's.
    bool paced_to_display{};
    // The interval asked for, before pacing; zero asks for every refresh.
    FrameInterval requested_interval{};
    // An ambient effect's wake: the user sees it, but it never holds the
    // window from settling (the window band's march or sweep, which the
    // automation's in-use sign runs while a model acts and waits for idle).
    // SurfaceRoot::next_settling_wake leaves it out.
    bool ambient{};

private:
    bool connected_{true};
};

} // namespace gui_forms::detail
