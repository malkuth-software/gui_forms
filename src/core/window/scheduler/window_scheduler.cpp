#include "gui_forms/window.hpp"

#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "../../failure/fail_fast.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// The named target the fail-fast boundary calls for one UI timer tick.
void run_ui_timer(std::function<void(FrameTime)>& callback, FrameTime now) {
    callback(now);
}

} // namespace

bool SurfaceRoot::needs_frame() const noexcept {
    return paint_pending();
}

stxlib::Optional<FrameTime> SurfaceRoot::next_wake() const noexcept {
    stxlib::Optional<FrameTime> result;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if (!(*request).connected()) {
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::ui_timer) {
            const bool resting = occluded_ && (*request).rests_while_occluded;
            if (!resting && (result.absent() || (*request).deadline < *result.value())) {
                result.set((*request).deadline);
            }
            continue;
        }
        if (occluded_) {
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::motion_clock ||
            (*request).kind == detail::FrameRequestKind::engine_idle) {
            if (result.absent() || (*request).deadline < *result.value()) {
                result.set((*request).deadline);
            }
            continue;
        }
        const std::shared_ptr<gui_forms::Control> target = (*request).target.lock();
        if (!target || !(*target).is_alive() || (*target).surface_root_ != this ||
            !(*target).effectively_visible()) {
            continue;
        }
        if (result.absent() || (*request).deadline < *result.value()) {
            result.set((*request).deadline);
        }
    }
    return result;
}

FrameRequestToken SurfaceRoot::schedule_paint(const Control::Ptr& control,
                                         FrameTime deadline) {
    require_root_thread("frame deadline scheduling");
    if (!control || !(*control).is_alive() || (*control).surface_root_ != this) {
        throw std::logic_error("GUI.Forms frame deadline requires an attached control");
    }
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    std::shared_ptr<gui_forms::detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        detail::FrameRequestKind::deadline, control, deadline);
    frame_requests_.push_back(request);
    (*control).own_revocable(request);
    metrics_.record_frame_request();
    request_schedule_wake();
    return FrameRequestToken(request);
}

FrameRequestToken SurfaceRoot::request_motion_frames(FrameTime first_deadline) {
    require_root_thread("motion clock scheduling");
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    // Paced: the first refresh after one period before the deadline asked
    // for (the clock asks for one period after its now), on the display's
    // grid; assumed: as asked.
    const FrameInterval period = motion_frame_period();
    const FrameTime deadline = display_timing_.paced()
        ? next_on_grid(display_timing_.presentation_phase, period, first_deadline - period)
        : first_deadline;
    std::shared_ptr<gui_forms::detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        detail::FrameRequestKind::motion_clock, Control::WeakPtr{}, deadline, period);
    (*request).paced_to_display = true;
    (*request).requested_interval = FrameInterval::zero();
    frame_requests_.push_back(request);
    metrics_.record_frame_request();
    request_schedule_wake();
    return FrameRequestToken(request);
}

void SurfaceRoot::set_motion_frame_deadline(FrameTime deadline) noexcept {
    // Paced: the first refresh at or after the deadline.
    const FrameTime due = display_timing_.paced()
        ? next_on_grid(display_timing_.presentation_phase, display_timing_.refresh_period,
                       deadline - FrameInterval(1))
        : deadline;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if ((*request).connected() && (*request).kind == detail::FrameRequestKind::motion_clock) {
            (*request).deadline = due;
        }
    }
    request_schedule_wake();
}

FrameInterval SurfaceRoot::motion_frame_period() const noexcept {
    const FrameInterval result = display_timing_.paced() ? display_timing_.refresh_period : motion_frame_interval;
    return result;
}

FrameInterval SurfaceRoot::display_paced_interval(FrameInterval requested) const noexcept {
    const FrameInterval display = display_timing_.refresh_period;
    if (requested <= FrameInterval::zero()) return display;
    if (!display_timing_.paced() && requested < display) return requested;
    const FrameInterval half = display / 2;
    const FrameInterval::rep frames = std::max<FrameInterval::rep>((requested + half) / display, 1);
    const FrameInterval period = display * frames;
    return period;
}

FrameTime SurfaceRoot::presentation_time_for(FrameTime now) const noexcept {
    if (!display_timing_.paced()) return now;
    const FrameTime shown = next_presentation(display_timing_, now);
    const FrameTime result = std::max(shown, last_presentation_time_);
    return result;
}

FrameRequestToken SurfaceRoot::schedule_animation_frames(Component& owner, FrameInterval requested,
                                                         std::function<void(FrameTime)> callback) {
    // Timers whose periods are multiples of one another tick on the same
    // grid points, phased to the display (the clock's epoch while the
    // timing is assumed), so they fall due in the same frame poll.
    const FrameInterval period = display_paced_interval(requested);
    const FrameTime first_tick = next_on_grid(display_timing_.presentation_phase, period, FrameClock::now());
    FrameRequestToken token = schedule_ui_timer(owner, period, first_tick, std::move(callback));
    // schedule_ui_timer appends its request last; it returned, so the
    // request is there.
    detail::ScheduledFrameRequest& request = *frame_requests_.back();
    request.paced_to_display = true;
    request.requested_interval = requested;
    return token;
}

bool SurfaceRoot::set_display_timing(const DisplayTiming& timing) {
    require_root_thread("display timing");
    if (!timing.valid()) return false;
    DisplayTiming accepted = timing.paced() ? timing : DisplayTiming::assumed();
    // A display link measures each refresh afresh, a few nanoseconds apart:
    // a period within a thousandth of the held one keeps the held one, so
    // such a report re-derives no interval.
    if (accepted.paced() && display_timing_.paced()) {
        const FrameInterval held = display_timing_.refresh_period;
        const FrameInterval change =
            accepted.refresh_period > held ? accepted.refresh_period - held : held - accepted.refresh_period;
        if (change * 1000 <= held) accepted.refresh_period = held;
    }
    display_timing_ = accepted;
    bool moved = false;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if (!(*request).connected() || !(*request).paced_to_display) continue;
        const FrameInterval interval = (*request).kind == detail::FrameRequestKind::motion_clock
            ? motion_frame_period()
            : display_paced_interval((*request).requested_interval);
        // The nearest point of the new grid: the first after half a period
        // before the deadline.
        const FrameTime nearest =
            next_on_grid(display_timing_.presentation_phase, interval, (*request).deadline - interval / 2);
        const FrameInterval shift = nearest > (*request).deadline ? nearest - (*request).deadline
                                                                   : (*request).deadline - nearest;
        const bool within_jitter = shift <= interval / 8;
        if (interval == (*request).interval && within_jitter) continue;
        (*request).interval = interval;
        if (!within_jitter) (*request).deadline = nearest;
        moved = true;
    }
    if (moved) request_schedule_wake();
    return true;
}

bool SurfaceRoot::note_presentation_completed(FrameTime completed) {
    require_root_thread("presentation completion");
    const stxlib::Optional<DisplayTiming> measured = presentation_estimator_.take(completed);
    if (measured.absent()) return false;
    const DisplayTimingSource held = display_timing_.source;
    const bool replaceable = held == DisplayTimingSource::assumed || held == DisplayTimingSource::nominal ||
        held == DisplayTimingSource::measured;
    if (!replaceable) return false;
    const bool result = set_display_timing(*measured.value());
    return result;
}

void SurfaceRoot::reset_presentation_estimate() noexcept { presentation_estimator_.reset(); }

bool SurfaceRoot::wants_display_frames() const noexcept {
    if (occluded_) return false;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if ((*request).connected() && (*request).paced_to_display) return true;
    }
    return false;
}

FrameRequestToken SurfaceRoot::schedule_ui_timer(
    Component& owner, FrameInterval interval, FrameTime first_deadline,
    std::function<void(FrameTime)> callback) {
    require_root_thread("UI timer scheduling");
    if (!owner.is_alive()) {
        throw std::logic_error("GUI.Forms UI timer requires a live component owner");
    }
    if (interval < minimum_ui_timer_interval) {
        throw std::invalid_argument("GUI.Forms UI timer interval is below the bound");
    }
    if (!callback) {
        throw std::invalid_argument("GUI.Forms UI timer requires a callback");
    }
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    std::shared_ptr<gui_forms::detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        first_deadline, interval, std::move(callback));
    frame_requests_.push_back(request);
    owner.own_revocable(request);
    metrics_.record_frame_request();
    // A Timer started or restarted outside a tick (posted work, another
    // window's callback): the host re-arms for it.
    request_schedule_wake();
    return FrameRequestToken(request);
}

FrameRequestToken SurfaceRoot::schedule_resting_ui_timer(
    Component& owner, FrameInterval interval, FrameTime first_deadline,
    std::function<void(FrameTime)> callback) {
    FrameRequestToken token = schedule_ui_timer(owner, interval, first_deadline, std::move(callback));
    // schedule_ui_timer appends its request last; it returned, so the
    // request is there.
    (*frame_requests_.back()).rests_while_occluded = true;
    return token;
}

FramePollResult SurfaceRoot::poll_frame_schedule(FrameTime now) {
    require_root_thread("frame schedule polling");
    if (in_frame_poll_) {
        metrics_.record_reentrant_frame_poll();
        FramePollResult deferred;
        deferred.reentrant_poll_deferred = true;
        deferred.damage_pending = needs_frame();
        deferred.suppressed_by_occlusion = occluded_;
        deferred.next_wake = next_wake();
        deferred.presentation_time = presentation_time_for(now);
        return deferred;
    }
    in_frame_poll_ = true;
    // The host is polling, so it arms its wake after this: an owed schedule
    // wake is answered.
    schedule_wake_pending_ = false;
    // Ends the poll on every path: the scratch is emptied (keeping its
    // capacity) and the copied requests are released.
    struct Reset final {
        bool& value;
        FrameRequestList& requests;
        std::unordered_set<std::uint64_t>& invalidated;
        ~Reset() {
            value = false;
            requests.clear();
            invalidated.clear();
        }
    } reset{in_frame_poll_, frame_poll_requests_, frame_poll_invalidated_};
    compact_frame_requests();

    FramePollResult result;
    // The time this poll's frames are shown: motion and display-paced
    // timers are evaluated at it (poll time while the timing is assumed).
    const FrameTime presented = presentation_time_for(now);
    if (display_timing_.paced()) last_presentation_time_ = presented;
    result.presentation_time = presented;
    // The latest a due timer or motion frame was polled after its deadline.
    FrameInterval worst_lateness = FrameInterval::zero();
    result.suppressed_by_occlusion = occluded_;
    if (occluded_) metrics_.record_occluded_frame_poll();
    frame_poll_invalidated_.clear();
    frame_poll_requests_.assign(frame_requests_.begin(), frame_requests_.end());
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_poll_requests_) {
        if (!(*request).connected() || (*request).deadline > now) {
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::ui_timer) {
            if (occluded_ && (*request).rests_while_occluded) {
                result.suppressed_by_occlusion = true;
                continue;
            }
            ++result.ui_timer_ticks;
            const FrameInterval lateness = now - (*request).deadline;
            const FrameInterval::rep skipped =
                lateness / (*request).interval;
            result.coalesced_requests += static_cast<std::uint64_t>(skipped);
            (*request).deadline += (*request).interval * (skipped + 1);
            metrics_.record_callback_emitted();
            // The callback is moved out while it runs (audit C10: no copy
            // per tick), since a timer that stops itself disconnects its
            // request, which empties the request's callback; it is moved
            // back only while the request stays connected and empty.
            std::function<void(FrameTime)> callback = std::move((*request).callback);
            (*request).callback = {};
            if (lateness > worst_lateness) worst_lateness = lateness;
            // The timer's callback is application code: an exception from
            // it ends the process at the fail-fast boundary.
            const FrameTime tick_time = (*request).paced_to_display ? presented : now;
            if (callback) gui_forms::detail::call_failing_fast("scheduler.ui_timer", &run_ui_timer, callback, tick_time);
            if ((*request).connected() && !(*request).callback) (*request).callback = std::move(callback);
            continue;
        }
        if (occluded_) {
            result.suppressed_by_occlusion = true;
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::motion_clock) {
            // The motion clock's one request: one frame advances every value
            // it holds. It keeps its phase, like a UI timer: the next frame is
            // the first deadline of its cadence after now, so a late frame is
            // one frame and never a burst (re-arming at now + interval would
            // add each poll's lateness to every interval).
            ++result.motion_clock_ticks;
            const FrameInterval lateness = now - (*request).deadline;
            const FrameInterval::rep skipped = lateness / (*request).interval;
            result.coalesced_requests += static_cast<std::uint64_t>(skipped);
            (*request).deadline += (*request).interval * (skipped + 1);
            if (lateness > worst_lateness) worst_lateness = lateness;
            metrics_.record_callback_emitted();
            motion_clock_.advance(presented);
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::engine_idle) {
            (*request).deadline = now + (*request).interval;
            // Idle: no motion runs and nothing waits to be painted. One
            // procedural pattern (E10), else an idle frame's finish tiles
            // (cp31), else one keyframe, per idle frame, so that none
            // stalls a frame; patterns and tiles first, since parts show
            // their fills' ground, or no finish, until theirs are made.
            if (motion_clock_.armed() || paint_pending()) continue;
            if (engine_parts_.patterns_waiting()) {
                const PatternGeneration generation = engine_parts_.generate_pattern(paint_leases_completed_);
                damage_pattern_waiters(generation);
                continue;
            }
            if (engine_parts_.finish_tiles_waiting()) {
                const bool drained = engine_parts_.generate_finish_tiles(paint_leases_completed_);
                if (drained) damage_finish_tile_waiters();
                continue;
            }
            const bool waiting = engine_parts_.bake_idle_keyframe(&image_resources(), paint_leases_completed_);
            if (!waiting) (*request).disconnect();
            continue;
        }
        const std::shared_ptr<gui_forms::Control> target = (*request).target.lock();
        if (!target || !(*target).is_alive() || (*target).surface_root_ != this) {
            (*request).disconnect();
            continue;
        }
        // A deadline for a hidden control waits until it is shown.
        if (!(*target).effectively_visible()) continue;
        ++result.deadlines_fired;
        (*request).disconnect();
        const bool already_requested = has_dirty((*target).dirty_, Dirty::paint) ||
            !frame_poll_invalidated_.insert((*target).runtime_id().value).second;
        if (already_requested) {
            ++result.coalesced_requests;
        } else {
            (*target).invalidate(invalidation::paint_only);
        }
    }

    compact_frame_requests();
    // wake_on_time: the latest due timer or motion frame was polled within
    // the tolerance of its deadline (one check per poll).
    const std::uint64_t late_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(worst_lateness).count());
    const std::uint64_t tolerance = metrics_.expectation_thresholds().late_wake_nanoseconds;
    if (late_nanoseconds > tolerance) {
        metrics_.record_expectation_miss(HousekeepingExpectation::wake_on_time, tolerance, late_nanoseconds);
    }
    metrics_.record_frame_poll(result.deadlines_fired, result.motion_clock_ticks, result.ui_timer_ticks,
                               result.coalesced_requests);
    result.damage_pending = needs_frame();
    result.next_wake = next_wake();
    return result;
}

void SurfaceRoot::cancel_frame_requests() {
    require_root_thread("frame schedule cancellation");
    // Animation timers waiting for a paint read the count and stay stopped.
    ++frame_request_cancellations_;
    // Motion ends with the requests: every value jumps to its end.
    motion_clock_.cancel();
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        (*request).disconnect();
    }
    frame_requests_.clear();
}

void SurfaceRoot::set_occluded(bool occluded, FrameTime) {
    require_root_thread("occlusion transition");
    if (occluded_ == occluded) {
        return;
    }
    occluded_ = occluded;
    metrics_.record_occlusion_transition(occluded);
    // An occluded or minimized window shows no motion (GIVEN D4-1): every
    // value jumps to its end now, and none is scheduled while it stays so.
    if (occluded) motion_clock_.finish_all(MotionStepCause::occluded);
    if (!occluded) {
        // A resting UI timer (the caret blink) restarts its cadence: a
        // deadline that passed while hidden moves to one interval from now,
        // so it is neither polled late nor run as a burst.
        const FrameTime shown = FrameClock::now();
        for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
            const bool resting = (*request).connected() && (*request).rests_while_occluded;
            if (resting && (*request).deadline < shown) (*request).deadline = shown + (*request).interval;
        }
        // Ambient effects stopped while hidden resume with their parts'
        // next paint (E7).
        resume_engine_ambient();
        if (paint_pending()) request_paint_wake();
    }
    // A flashing band rests lit while hidden and flashes again after.
    refresh_band();
}

} // namespace gui_forms
