#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <chrono>
#include <cstdint>

namespace gui_forms {

// Where a window's display timing came from (DisplayTiming::source).
enum class DisplayTimingSource : std::uint8_t {
    // No host reported: frames every assumed_refresh_period, each drawn for
    // the time it is polled at (the cadence before display timing existed).
    assumed,
    // The display's own refresh timing: a display link, the compositor's
    // timing, or presentation completions the system stamps.
    display,
    // Estimated from the times the host's presentations completed, where the
    // system reports no timing.
    measured,
    // A rate chosen by a test, a bench or an offscreen host.
    fixed,
};

// The cadence a window without display timing paces its frames at.
inline constexpr FrameInterval assumed_refresh_period = std::chrono::milliseconds(16);
// The bounds a reported refresh period must lie within (1000 Hz to 1 Hz);
// SurfaceRoot::set_display_timing rejects a period outside them.
inline constexpr FrameInterval minimum_refresh_period = std::chrono::milliseconds(1);
inline constexpr FrameInterval maximum_refresh_period = std::chrono::seconds(1);

// One window's display timing as its host last reported it (design note
// frame-source, section 2): frames are shown at presentation_phase + k *
// refresh_period for whole k. A display link reports its target time as the
// phase on every tick; a fixed rate has its phase at the clock's epoch.
struct GUI_FORMS_API DisplayTiming final {
    // The current refresh interval; on a variable-rate display, the rate the
    // system chose last.
    FrameInterval refresh_period{assumed_refresh_period};
    // Any instant at which a frame is shown, in the frame clock's domain.
    FrameTime presentation_phase{};
    DisplayTimingSource source{DisplayTimingSource::assumed};

    // No report: assumed_refresh_period at the epoch, poll-time frames.
    [[nodiscard]] static DisplayTiming assumed() noexcept;
    // A fixed rate for tests, benches and offscreen hosts: frames every
    // `period` from `phase`.
    [[nodiscard]] static DisplayTiming fixed_rate(FrameInterval period, FrameTime phase = FrameTime{}) noexcept;
    // A host's report: `source` timing with frames every `period` and one
    // shown at `presentation`.
    [[nodiscard]] static DisplayTiming reported(DisplayTimingSource source, FrameInterval period,
                                                FrameTime presentation) noexcept;

    // Frames are drawn for the time they are shown: any source but assumed.
    [[nodiscard]] bool paced() const noexcept;
    // Refreshes per second; 0 for assumed timing.
    [[nodiscard]] double refresh_hertz() const noexcept;
    // Whether a window may hold this timing: assumed, or a period within
    // minimum_refresh_period and maximum_refresh_period.
    [[nodiscard]] bool valid() const noexcept;
};

// The first instant phase + k * period strictly after `after` (k may be
// negative). `period` must be positive.
[[nodiscard]] GUI_FORMS_API FrameTime next_on_grid(FrameTime phase, FrameInterval period, FrameTime after) noexcept;
// The time a frame drawn at `now` is shown: the first presentation strictly
// after `now`, or `now` itself for timing that is not paced.
[[nodiscard]] GUI_FORMS_API FrameTime next_presentation(const DisplayTiming& timing, FrameTime now) noexcept;
// A short label of the source for diagnostics and benches: "assumed",
// "display", "measured" or "fixed".
[[nodiscard]] GUI_FORMS_API const char* display_timing_source_name(DisplayTimingSource source) noexcept;

} // namespace gui_forms
