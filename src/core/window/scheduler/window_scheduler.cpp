#include "gui_forms/window.hpp"

#include "../../scheduler/request/scheduled_frame_request.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

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
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{in_frame_poll_};
    compact_frame_requests();

    FramePollResult result;
    result.suppressed_by_occlusion = occluded_;
    if (occluded_) metrics_.record_occluded_frame_poll();
    std::unordered_set<std::uint64_t> invalidated_controls;
    const std::vector<std::shared_ptr<detail::ScheduledFrameRequest>> requests = frame_requests_;
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : requests) {
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
            const std::function<void(FrameTime)> callback = (*request).callback;
            if (callback) {
                try {
                    callback(now);
                } catch (...) {
                    (*request).disconnect();
                    ++result.callback_faults;
                    metrics_.record_frame_callback_fault();
                }
            }
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
            !invalidated_controls.insert((*target).runtime_id().value).second;
        if (already_requested) {
            ++result.coalesced_requests;
        } else {
            (*target).invalidate(invalidation::paint_only);
        }
    }

    compact_frame_requests();
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
}

} // namespace gui_forms
