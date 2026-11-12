#pragma once
// The caret blink phase the text controls read (audit N75): the window's
// CaretSettings, which the host reports from the platform's setting.

#include "gui_forms/window.hpp"
#include "../../../core/surface_root/access/surface_schedule_access.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace gui_forms::detail {

// How long the caret of a text control in `window` stays shown or hidden
// before it toggles, or nullopt when the platform says the caret never
// blinks (the caret then stays shown and no timer runs). Read each time a
// control schedules its blink, so a changed setting applies from the next
// caret movement or focus.
[[nodiscard]] inline std::optional<FrameInterval> caret_blink_phase(const Window& window) {
    const std::uint32_t milliseconds = window.presentation_settings().caret.blink_milliseconds;
    if (milliseconds == 0U) {
        return std::nullopt;
    }
    const FrameInterval phase = std::chrono::milliseconds(milliseconds);
    return phase;
}

// Schedules a text control's caret blink in `window`: a UI timer that rests
// while the window is occluded (SurfaceRoot::schedule_resting_ui_timer), so
// a hidden, minimized or covered window holds no caret wake from the moment
// it is occluded. Without it the blink ran on until its paint gate stopped
// it, up to two phases later, which the macOS idle-visibility probe could
// count as a hidden wake.
struct CaretBlinkSchedule final {
    SurfaceRoot* window{};

    [[nodiscard]] FrameRequestToken operator()(Component& owner, FrameInterval phase, FrameTime first_deadline,
                                               std::function<void(FrameTime)> tick) const {
        FrameRequestToken token =
            SurfaceScheduleAccess::schedule_resting_ui_timer(*window, owner, phase, first_deadline, std::move(tick));
        return token;
    }
};

} // namespace gui_forms::detail
