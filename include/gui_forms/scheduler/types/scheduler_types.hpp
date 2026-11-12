#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms {

class Window;

namespace detail {
class ScheduledFrameRequest;
struct WindowLifetime final {
    Window* window{};
};
}

using FrameClock = std::chrono::steady_clock;
using FrameTime = FrameClock::time_point;
using FrameInterval = FrameClock::duration;

inline constexpr std::size_t maximum_scheduled_frame_requests = 256;
inline constexpr FrameInterval minimum_ui_timer_interval =
    std::chrono::milliseconds(1);

struct FramePollResult final {
    std::uint64_t deadlines_fired{};
    std::uint64_t ui_timer_ticks{};
    // Frames of the window's motion clock: at most one per poll, however
    // many values move.
    std::uint64_t motion_clock_ticks{};
    std::uint64_t coalesced_requests{};
    bool reentrant_poll_deferred{};
    bool damage_pending{};
    bool suppressed_by_occlusion{};
    std::optional<FrameTime> next_wake;
};

} // namespace gui_forms
