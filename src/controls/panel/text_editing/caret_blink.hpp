#pragma once
// The caret blink phase the text controls read (audit N75): the window's
// CaretSettings, which the host reports from the platform's setting.

#include "gui_forms/window.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

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

} // namespace gui_forms::detail
