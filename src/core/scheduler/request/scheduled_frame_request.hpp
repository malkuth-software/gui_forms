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

private:
    bool connected_{true};
};

} // namespace gui_forms::detail
