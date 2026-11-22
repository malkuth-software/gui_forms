#include "gui_forms/scheduler/display_timing/display_timing.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>

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
    case DisplayTimingSource::nominal:
        return "nominal";
    case DisplayTimingSource::fixed:
        return "fixed";
    case DisplayTimingSource::assumed:
    default:
        return "assumed";
    }
}

namespace {

// The refreshes after which a counter timing's anchor moves on, and the
// least span its next anchor needs.
constexpr std::uint64_t anchor_span_limit = 240U;
constexpr std::uint64_t next_anchor_span = 120U;

} // namespace

stxlib::Optional<DisplayTiming> RefreshCounterTiming::take(FrameTime shown, std::uint64_t count) noexcept {
    stxlib::Optional<DisplayTiming> result;
    const Stamp stamp{shown, count};
    const bool advanced = primed_ && count > last_.count && shown > last_.shown;
    if (!advanced) {
        anchor_ = stamp;
        next_anchor_ = stamp;
        last_ = stamp;
        primed_ = true;
        return result;
    }
    last_ = stamp;
    const std::uint64_t span = count - anchor_.count;
    const FrameInterval elapsed = shown - anchor_.shown;
    const FrameInterval period = elapsed / static_cast<FrameInterval::rep>(span);
    if (next_anchor_.count == anchor_.count && span >= next_anchor_span) next_anchor_ = stamp;
    if (span >= anchor_span_limit) {
        anchor_ = next_anchor_;
        next_anchor_ = stamp;
    }
    result.set(DisplayTiming::reported(DisplayTimingSource::display, period, shown));
    return result;
}

void RefreshCounterTiming::reset() noexcept {
    anchor_ = Stamp{};
    next_anchor_ = Stamp{};
    last_ = Stamp{};
    primed_ = false;
}

namespace {

// Whether `interval` lies within a tenth of `period`.
bool within_a_tenth(FrameInterval interval, FrameInterval period) noexcept {
    const FrameInterval difference = interval > period ? interval - period : period - interval;
    const bool result = difference * 10 <= period;
    return result;
}

// The intervals an estimator needs before it judges a long one against the
// median.
constexpr std::size_t judged_after = 3U;

} // namespace

stxlib::Optional<DisplayTiming> DisplayTimingEstimator::take(FrameTime completed) noexcept {
    stxlib::Optional<DisplayTiming> result;
    if (!primed_) {
        last_ = completed;
        primed_ = true;
        return result;
    }
    if (completed < last_) {
        reset();
        last_ = completed;
        primed_ = true;
        return result;
    }
    FrameInterval interval = completed - last_;
    // Two stamps for one refresh: the earlier one stays the interval's start.
    if (interval < minimum_refresh_period) return result;
    last_ = completed;
    // The window rested: this completion starts the next interval.
    if (interval > maximum_refresh_period) return result;
    if (count_ >= judged_after) {
        const FrameInterval period = median();
        // Longer than 1.75 periods: whole refreshes the window drew nothing
        // in, or a stall that is not on the grid.
        if (interval * 4 > period * 7) {
            const FrameInterval::rep refreshes = (interval + period / 2) / period;
            const FrameInterval each = interval / refreshes;
            if (!within_a_tenth(each, period)) return result;
            interval = each;
        }
    }
    intervals_[next_] = interval;
    next_ = (next_ + 1U) % interval_capacity;
    if (count_ < interval_capacity) count_ = count_ + 1U;
    if (count_ < agreeing_intervals) return result;
    const FrameInterval period = median();
    std::size_t agreeing = 0U;
    for (std::size_t index = 0U; index < count_; ++index) {
        if (within_a_tenth(intervals_[index], period)) agreeing = agreeing + 1U;
    }
    if (agreeing < agreeing_intervals) return result;
    if (period < minimum_refresh_period || period > maximum_refresh_period) return result;
    result.set(DisplayTiming::reported(DisplayTimingSource::measured, period, completed));
    return result;
}

void DisplayTimingEstimator::reset() noexcept {
    intervals_ = std::array<FrameInterval, interval_capacity>{};
    count_ = 0U;
    next_ = 0U;
    last_ = FrameTime{};
    primed_ = false;
}

FrameInterval DisplayTimingEstimator::median() const noexcept {
    std::array<FrameInterval, interval_capacity> sorted = intervals_;
    const std::size_t middle = count_ / 2U;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(middle),
                     sorted.begin() + static_cast<std::ptrdiff_t>(count_));
    const FrameInterval result = sorted[middle];
    return result;
}

} // namespace gui_forms
