#include "gui_forms/window.hpp"

#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "../../failure/fail_fast.hpp"

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

std::optional<FrameTime> SurfaceRoot::next_wake() const noexcept {
    std::optional<FrameTime> result;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if (!(*request).connected()) {
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::ui_timer) {
            if (!result || (*request).deadline < *result) {
                result = (*request).deadline;
            }
            continue;
        }
        if (occluded_) {
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::motion_clock ||
            (*request).kind == detail::FrameRequestKind::engine_idle) {
            if (!result || (*request).deadline < *result) {
                result = (*request).deadline;
            }
            continue;
        }
        const std::shared_ptr<gui_forms::Control> target = (*request).target.lock();
        if (!target || !(*target).is_alive() || (*target).surface_root_ != this ||
            !(*target).effectively_visible()) {
            continue;
        }
        if (!result || (*request).deadline < *result) {
            result = (*request).deadline;
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
    return FrameRequestToken(request);
}

FrameRequestToken SurfaceRoot::request_motion_frames(FrameTime first_deadline) {
    require_root_thread("motion clock scheduling");
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    std::shared_ptr<gui_forms::detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        detail::FrameRequestKind::motion_clock, Control::WeakPtr{}, first_deadline, motion_frame_interval);
    frame_requests_.push_back(request);
    metrics_.record_frame_request();
    return FrameRequestToken(request);
}

void SurfaceRoot::set_motion_frame_deadline(FrameTime deadline) noexcept {
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if ((*request).connected() && (*request).kind == detail::FrameRequestKind::motion_clock) {
            (*request).deadline = deadline;
        }
    }
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
    return FrameRequestToken(request);
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
        return deferred;
    }
    in_frame_poll_ = true;
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
            if (callback) gui_forms::detail::call_failing_fast("scheduler.ui_timer", &run_ui_timer, callback, now);
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
            motion_clock_.advance(now);
            continue;
        }
        if ((*request).kind == detail::FrameRequestKind::engine_idle) {
            (*request).deadline = now + (*request).interval;
            // Idle: no motion runs and nothing waits to be painted. One
            // procedural pattern (E10), else one keyframe, per idle frame,
            // so that none stalls a frame; patterns first, since parts
            // show their fills' ground until theirs are made.
            if (motion_clock_.armed() || paint_pending()) continue;
            if (engine_parts_.patterns_waiting()) {
                const PatternGeneration generation = engine_parts_.generate_pattern(paint_leases_completed_);
                damage_pattern_waiters(generation);
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

void SurfaceRoot::set_occluded(bool occluded, FrameTime transition_time) {
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
        // Ambient effects stopped while hidden resume with their parts'
        // next paint (E7).
        resume_engine_ambient();
        if (paint_pending()) request_paint_wake();
    }
    // A flashing band rests lit while hidden and flashes again after.
    refresh_band();
}

} // namespace gui_forms
