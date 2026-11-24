#pragma once

#include "gui_forms/export/application_export.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <stxlib/optional.hpp>

namespace gui_forms {

class Window;

namespace detail {
class ScheduledFrameRequest;
struct GUI_FORMS_API WindowLifetime final {
    Window* window{};
};
}

using FrameClock = std::chrono::steady_clock;
using FrameTime = FrameClock::time_point;
using FrameInterval = FrameClock::duration;

inline constexpr std::size_t maximum_scheduled_frame_requests = 256;
inline constexpr FrameInterval minimum_ui_timer_interval =
    std::chrono::milliseconds(1);

struct GUI_FORMS_API FramePollResult final {
    std::uint64_t deadlines_fired{};
    std::uint64_t ui_timer_ticks{};
    // Frames of the window's motion clock: at most one per poll, however
    // many values move.
    std::uint64_t motion_clock_ticks{};
    std::uint64_t coalesced_requests{};
    bool reentrant_poll_deferred{};
    bool damage_pending{};
    bool suppressed_by_occlusion{};
    stxlib::Optional<FrameTime> next_wake;
    // The time the frames this poll produced are shown: the window's next
    // presentation after the poll's time when its display timing is paced
    // (SurfaceRoot::display_timing), else the poll's time. The motion clock
    // and display-paced animation timers were advanced to it.
    FrameTime presentation_time{};
};

} // namespace gui_forms
