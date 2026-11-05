#include "gui_forms/live_surface/pacer/live_surface_pacer.hpp"

#include "gui_forms/live_surface/surface/live_surface.hpp"

#include "../state/live_surface_state.hpp"

#include <chrono>
#include <mutex>
#include <utility>

namespace gui_forms {

namespace {

// The refresh assumed without display timing: 60 Hz.
constexpr FrameInterval fallback_refresh_period =
    std::chrono::duration_cast<FrameInterval>(std::chrono::nanoseconds(16'666'667));

// The vertical blank after `now` in the presenter's phase, or `now` when no
// presenter has reported.
[[nodiscard]] FrameTime next_vblank(FrameTime now, const LiveDisplayTiming& timing, FrameInterval period) noexcept {
    if (timing.source == LiveTimingSource::none || timing.refresh_period <= FrameInterval::zero()) return now;
    if (timing.last_vblank > now) return timing.last_vblank;
    const FrameInterval elapsed = now - timing.last_vblank;
    const std::int64_t periods = elapsed / period + 1;
    const FrameTime next = timing.last_vblank + period * periods;
    return next;
}

// Wakes a pacer's wait when its stop token fires. Runs on the thread that
// requests the stop (or in the callback's constructor, before the waiter
// takes the lock); it takes the surface's lock so the wake cannot fall
// between the waiter's check and its wait.
class NotifyPacerStop final {
public:
    explicit NotifyPacerStop(detail::LiveSurfaceState& state) noexcept : state_(&state) {}
    void operator()() const noexcept {
        {
            const std::scoped_lock lock((*state_).mutex);
        }
        (*state_).presented.notify_all();
    }

private:
    detail::LiveSurfaceState* state_;
};

} // namespace

LiveSurfacePacer::LiveSurfacePacer(std::shared_ptr<LiveSurface> surface) noexcept
    : surface_(std::move(surface)) {}

FrameTime LiveSurfacePacer::display_time(FrameTime now) const {
    if (!surface_ || !(*surface_).state_) return now;
    detail::LiveSurfaceState& state = *(*surface_).state_;
    const std::scoped_lock lock(state.mutex);
    const LiveDisplayTiming timing = state.presentation_record.timing;
    const FrameInterval period =
        timing.refresh_period > FrameInterval::zero() ? timing.refresh_period : fallback_refresh_period;
    const FrameTime shown = next_vblank(now, timing, period);
    return shown;
}

LivePacingResult LiveSurfacePacer::plan(FrameTime now) {
    if (!surface_ || !(*surface_).state_) {
        LivePacingResult result;
        result.outcome = LivePacingOutcome::frame;
        result.frame_time = now;
        return result;
    }
    detail::LiveSurfaceState& state = *(*surface_).state_;
    const std::scoped_lock lock(state.mutex);
    const LivePacingResult result = plan_locked(now, state.generation, state.presentation_record);
    return result;
}

LivePacingResult LiveSurfacePacer::plan_locked(FrameTime now, std::uint64_t published,
                                               const LivePresentationRecord& record) {
    const FrameInterval period =
        record.timing.refresh_period > FrameInterval::zero() ? record.timing.refresh_period : fallback_refresh_period;
    LivePacingResult result;
    result.frame_time = next_vblank(now, record.timing, period);
    result.presented_generation = record.presented_generation;
    if (idle_.load(std::memory_order_acquire)) {
        result.outcome = LivePacingOutcome::pending;
        result.wake_at = FrameTime::max();
        return result;
    }
    // Shown: the presenter showed the last frame published, or, when
    // nothing was published since the last frame, a tick passed.
    bool shown = !decided_once_;
    if (decided_once_ && published > published_at_frame_) {
        shown = record.presented_generation >= published;
    } else if (decided_once_) {
        shown = record.presents > presents_at_frame_;
    }
    if (!shown) {
        if (!waiting_) {
            waiting_ = true;
            waiting_since_ = now;
        }
        const FrameTime deadline = waiting_since_ + period + period / 2;
        if (now < deadline) {
            result.outcome = LivePacingOutcome::pending;
            result.wake_at = deadline;
            return result;
        }
        const std::int64_t waited = (now - waiting_since_) / period;
        result.outcome = LivePacingOutcome::deadline;
        result.missed_frames = waited > 1 ? static_cast<std::uint64_t>(waited - 1) : 0U;
    } else {
        result.outcome = LivePacingOutcome::frame;
    }
    published_at_frame_ = published;
    presents_at_frame_ = record.presents;
    decided_once_ = true;
    waiting_ = false;
    return result;
}

LivePacingResult LiveSurfacePacer::wait_for_frame(const std::stop_token& stop) {
    if (!surface_ || !(*surface_).state_) {
        LivePacingResult result;
        result.outcome = stop.stop_requested() ? LivePacingOutcome::stopped : LivePacingOutcome::frame;
        result.frame_time = FrameClock::now();
        return result;
    }
    detail::LiveSurfaceState& state = *(*surface_).state_;
    // Made before the lock is taken: a token already stopped runs the
    // callback here, and the callback takes the lock.
    const std::stop_callback<NotifyPacerStop> on_stop(stop, NotifyPacerStop(state));
    std::unique_lock<std::mutex> lock(state.mutex);
    for (;;) {
        if (stop.stop_requested()) {
            LivePacingResult stopped;
            stopped.outcome = LivePacingOutcome::stopped;
            stopped.frame_time = FrameClock::now();
            stopped.presented_generation = state.presentation_record.presented_generation;
            return stopped;
        }
        const LivePacingResult result = plan_locked(FrameClock::now(), state.generation, state.presentation_record);
        if (result.outcome != LivePacingOutcome::pending) return result;
        // One wait per frame: woken by a report, arm() or the stop token,
        // or at the deadline. On Windows the deadline's timeout is bound to
        // the system timer grain; frames normally come from the reports.
        if (result.wake_at == FrameTime::max()) {
            state.presented.wait(lock);
        } else {
            static_cast<void>(state.presented.wait_until(lock, result.wake_at));
        }
    }
}

void LiveSurfacePacer::idle() noexcept {
    idle_.store(true, std::memory_order_release);
}

void LiveSurfacePacer::arm() noexcept {
    idle_.store(false, std::memory_order_release);
    if (!surface_ || !(*surface_).state_) return;
    detail::LiveSurfaceState& state = *(*surface_).state_;
    {
        const std::scoped_lock lock(state.mutex);
    }
    state.presented.notify_all();
}

} // namespace gui_forms
