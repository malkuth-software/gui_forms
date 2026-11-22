#include "gui_forms/scheduler/display_timing/display_timing.hpp"

#include <chrono>

namespace gui_forms {

DisplayTiming DisplayTiming::assumed() noexcept {
    const DisplayTiming result{};
    return result;
}

DisplayTiming DisplayTiming::fixed_rate(FrameInterval period, FrameTime phase) noexcept {
    DisplayTiming result;
    result.refresh_period = period;
    result.presentation_phase = phase;
    result.source = DisplayTimingSource::fixed;
    return result;
}

DisplayTiming DisplayTiming::reported(DisplayTimingSource source, FrameInterval period,
                                      FrameTime presentation) noexcept {
    DisplayTiming result;
    result.refresh_period = period;
    result.presentation_phase = presentation;
    result.source = source;
    return result;
}

bool DisplayTiming::paced() const noexcept {
    const bool result = source != DisplayTimingSource::assumed;
    return result;
}

double DisplayTiming::refresh_hertz() const noexcept {
    if (!paced() || refresh_period <= FrameInterval::zero()) return 0.0;
    const double seconds = std::chrono::duration<double>(refresh_period).count();
    const double result = 1.0 / seconds;
    return result;
}

bool DisplayTiming::valid() const noexcept {
    if (!paced()) return true;
    const bool result = refresh_period >= minimum_refresh_period && refresh_period <= maximum_refresh_period;
    return result;
}

FrameTime next_on_grid(FrameTime phase, FrameInterval period, FrameTime after) noexcept {
    const FrameInterval offset = after - phase;
    // Whole periods from the phase to the grid point at or before `after`:
    // division truncates toward zero, so an offset before the phase that is
    // not a whole number of periods steps one further back.
    FrameInterval::rep steps = offset / period;
    const bool before_phase = offset < FrameInterval::zero();
    const bool between_points = offset % period != FrameInterval::zero();
    if (before_phase && between_points) steps = steps - 1;
    const FrameTime result = phase + period * (steps + 1);
    return result;
}

FrameTime next_presentation(const DisplayTiming& timing, FrameTime now) noexcept {
    if (!timing.paced() || timing.refresh_period <= FrameInterval::zero()) return now;
    const FrameTime result = next_on_grid(timing.presentation_phase, timing.refresh_period, now);
    return result;
}

const char* display_timing_source_name(DisplayTimingSource source) noexcept {
    switch (source) {
    case DisplayTimingSource::display:
        return "display";
    case DisplayTimingSource::measured:
        return "measured";
    case DisplayTimingSource::fixed:
        return "fixed";
    case DisplayTimingSource::assumed:
    default:
        return "assumed";
    }
}

} // namespace gui_forms
