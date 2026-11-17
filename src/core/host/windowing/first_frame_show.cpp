#include "gui_forms/host/windowing/first_frame_show.hpp"

#include <algorithm>

namespace gui_forms {
namespace {
FrameInterval clamp_ceiling(FrameInterval ceiling) noexcept {
    const FrameInterval maximum = std::chrono::duration_cast<FrameInterval>(maximum_first_frame_show_ceiling);
    const FrameInterval clamped = std::clamp(ceiling, FrameInterval::zero(), maximum);
    return clamped;
}
} // namespace

const char* first_frame_show_state_name(FirstFrameShowState state) noexcept {
    switch (state) {
    case FirstFrameShowState::idle: return "idle";
    case FirstFrameShowState::waiting: return "waiting";
    case FirstFrameShowState::shown: return "shown";
    default: break;
    }
    return "unknown";
}

const char* first_frame_show_cause_name(FirstFrameShowCause cause) noexcept {
    switch (cause) {
    case FirstFrameShowCause::none: return "none";
    case FirstFrameShowCause::frame_presented: return "frame_presented";
    case FirstFrameShowCause::deadline: return "deadline";
    case FirstFrameShowCause::immediate: return "immediate";
    default: break;
    }
    return "unknown";
}

FirstFrameShowGate::FirstFrameShowGate(FrameInterval ceiling) noexcept {
    snapshot_.ceiling = clamp_ceiling(ceiling);
}

void FirstFrameShowGate::set_ceiling(FrameInterval ceiling) noexcept {
    snapshot_.ceiling = clamp_ceiling(ceiling);
}

FirstFrameShowStep FirstFrameShowGate::request_show(FrameTime now) noexcept {
    ++snapshot_.requests;
    FirstFrameShowStep step{};
    switch (snapshot_.state) {
    case FirstFrameShowState::waiting:
        // The first request's deadline stands.
        return step;
    case FirstFrameShowState::shown:
        requested_at_ = now;
        step = show(FirstFrameShowCause::immediate, now);
        return step;
    case FirstFrameShowState::idle:
    default:
        break;
    }
    requested_at_ = now;
    if (snapshot_.frame_presented) {
        step = show(FirstFrameShowCause::frame_presented, now);
        return step;
    }
    if (snapshot_.ceiling == FrameInterval::zero()) {
        step = show(FirstFrameShowCause::immediate, now);
        return step;
    }
    deadline_ = now + snapshot_.ceiling;
    snapshot_.state = FirstFrameShowState::waiting;
    return step;
}

FirstFrameShowStep FirstFrameShowGate::report_frame_presented(FrameTime now) noexcept {
    ++snapshot_.presented_frames;
    snapshot_.frame_presented = true;
    FirstFrameShowStep step{};
    if (snapshot_.state != FirstFrameShowState::waiting) return step;
    step = show(FirstFrameShowCause::frame_presented, now);
    return step;
}

FirstFrameShowStep FirstFrameShowGate::poll(FrameTime now) noexcept {
    FirstFrameShowStep step{};
    if (snapshot_.state != FirstFrameShowState::waiting || now < deadline_) return step;
    step = show(FirstFrameShowCause::deadline, now);
    return step;
}

bool FirstFrameShowGate::cancel() noexcept {
    if (snapshot_.state != FirstFrameShowState::waiting) return false;
    snapshot_.state = FirstFrameShowState::idle;
    ++snapshot_.cancelled_waits;
    return true;
}

stxlib::Optional<FrameTime> FirstFrameShowGate::deadline() const noexcept {
    stxlib::Optional<FrameTime> result{};
    if (snapshot_.state == FirstFrameShowState::waiting) result.set(deadline_);
    return result;
}

FirstFrameShowStep FirstFrameShowGate::show(FirstFrameShowCause cause, FrameTime now) noexcept {
    snapshot_.state = FirstFrameShowState::shown;
    snapshot_.last_cause = cause;
    // A clock that went backwards (a host passing an earlier time) waits
    // zero, never a negative interval.
    const FrameInterval waited = now > requested_at_ ? now - requested_at_ : FrameInterval::zero();
    snapshot_.last_wait = waited;
    switch (cause) {
    case FirstFrameShowCause::frame_presented: ++snapshot_.frame_shows; break;
    case FirstFrameShowCause::deadline: ++snapshot_.deadline_shows; break;
    case FirstFrameShowCause::immediate: ++snapshot_.immediate_shows; break;
    case FirstFrameShowCause::none:
    default: break;
    }
    FirstFrameShowStep step{};
    step.show = true;
    step.cause = cause;
    return step;
}

} // namespace gui_forms
