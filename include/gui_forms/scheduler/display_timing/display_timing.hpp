#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <chrono>
#include <cstdint>

#include <stxlib/optional.hpp>

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
    // The display mode's nominal refresh rate, without the display's phase:
    // the frames keep the phase already held (a monitor DWM does not time,
    // RandR's mode rate).
    nominal,
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
// "display", "measured", "nominal" or "fixed".
[[nodiscard]] GUI_FORMS_API const char* display_timing_source_name(DisplayTimingSource source) noexcept;

// Display timing from a presenter's completion stamps that count the
// display's refreshes (X11 Present's ust and msc; design note frame-source,
// section 3): each stamp is the time a frame was shown and the refresh it
// was shown on. The period is the time between an anchor stamp and the
// latest over the refreshes between them, so it is as fine as a long span
// makes it; the anchor moves forward every 240 refreshes, to a stamp taken
// 120 or more after it, so the span stays between 120 and 240 refreshes
// once that many have passed. One instance per window, on its thread.
class GUI_FORMS_API RefreshCounterTiming final {
public:
    // Takes a frame shown at `shown` on refresh `count`. Returns display
    // timing with that period, phased at `shown`; nothing for the first
    // stamp, and nothing (starting again from this stamp) when the count
    // does not advance or the time does not (a reset presenter, another
    // display).
    [[nodiscard]] stxlib::Optional<DisplayTiming> take(FrameTime shown, std::uint64_t count) noexcept;
    // Forgets every stamp (the window moved to another display).
    void reset() noexcept;

private:
    struct Stamp final {
        FrameTime shown{};
        std::uint64_t count{};
    };
    Stamp anchor_{};
    // The stamp the anchor moves to next; the anchor itself until one 120
    // refreshes on is taken.
    Stamp next_anchor_{};
    Stamp last_{};
    bool primed_{};
};

} // namespace gui_forms
