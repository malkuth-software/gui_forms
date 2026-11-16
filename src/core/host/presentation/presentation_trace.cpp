#include "presentation_trace.hpp"

namespace gui_forms::host::presentation {

namespace {

// Where check_present_trace is within a frame.
enum class TraceState : std::uint8_t {
    // A frame may begin: before its first step, or after a completed one.
    start,
    // The frame is complete; only between-frame notices may follow in it.
    done,
    // A direct buffer is held, before its damage.
    acquired,
    // The direct damage is written, before the present.
    direct_damaged,
    // The direct present was lost or failed: a fallback must follow (a lost
    // record may come first).
    direct_failed,
    // The direct path refused: a fallback must follow.
    refused,
    // The copy part is due (after a fallback, or a copy scroll step).
    copy_expected,
    // The copy damage is handed over, before the present.
    copy_damaged,
};

// A frame may end in start or done, or after a refusal with no copy path
// behind it (the window manager keeps its damage and tries again).
bool frame_in_progress(TraceState state) noexcept {
    const bool open = state != TraceState::start && state != TraceState::done && state != TraceState::refused;
    return open;
}

bool is_scroll_step(PresentTraceEvent event) noexcept {
    const bool scroll = event == PresentTraceEvent::scroll || event == PresentTraceEvent::scroll_refused;
    return scroll;
}

bool is_between_frames(PresentTraceEvent event) noexcept {
    const bool notice = event == PresentTraceEvent::lost || event == PresentTraceEvent::release ||
                        event == PresentTraceEvent::released_by_host;
    return notice;
}

bool shim_path(PresentPath path) noexcept {
    const bool admitted = path == PresentPath::none || path == PresentPath::direct;
    return admitted;
}

PresentTraceCheck violation_at(PresentTraceViolation violation, std::size_t index) noexcept {
    const PresentTraceCheck check{violation, index};
    return check;
}

} // namespace

void PresentTrace::record(const PresentTraceRecord& record) noexcept {
    if (!enabled_) return;
    ring_[next_] = record;
    next_ = (next_ + 1U) % capacity;
    if (size_ < capacity) ++size_;
    ++total_;
}

void PresentTrace::clear() noexcept {
    next_ = 0U;
    size_ = 0U;
    total_ = 0U;
}

std::size_t PresentTrace::fill(std::span<PresentTraceRecord> out) const noexcept {
    const std::size_t count = size_ < out.size() ? size_ : out.size();
    const std::size_t oldest = (next_ + capacity - size_) % capacity;
    for (std::size_t index = 0U; index < count; ++index) out[index] = ring_[(oldest + index) % capacity];
    return count;
}

PresentTraceCheck check_present_trace(std::span<const PresentTraceRecord> records) noexcept {
    TraceState state = TraceState::start;
    // A loss reported between frames lets the next frame begin with a
    // fallback.
    bool loss_pending = false;
    // The frame's direct present failed: a fallback may follow it.
    bool failed_in_frame = false;
    std::uint64_t frame = 0U;
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const PresentTraceRecord& record = records[index];
        if (index > 0U && record.frame < frame) return violation_at(PresentTraceViolation::frame_decreased, index);
        if (index == 0U || record.frame > frame) {
            if (frame_in_progress(state)) return violation_at(PresentTraceViolation::incomplete_frame, index);
            state = TraceState::start;
            failed_in_frame = false;
            frame = record.frame;
        }
        const PresentTraceEvent event = record.event;
        const PresentPath path = record.path;
        switch (state) {
        case TraceState::start:
            if (event == PresentTraceEvent::resume) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                continue;
            }
            if (event == PresentTraceEvent::acquire || event == PresentTraceEvent::refuse) {
                if (path != PresentPath::direct) return violation_at(PresentTraceViolation::wrong_path, index);
                state = event == PresentTraceEvent::acquire ? TraceState::acquired : TraceState::refused;
                if (event == PresentTraceEvent::acquire) loss_pending = false;
                continue;
            }
            if (event == PresentTraceEvent::fallback) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                if (!loss_pending) return violation_at(PresentTraceViolation::fallback_without_cause, index);
                loss_pending = false;
                state = TraceState::copy_expected;
                continue;
            }
            if (is_scroll_step(event) || event == PresentTraceEvent::damage) {
                if (path != PresentPath::copy) return violation_at(PresentTraceViolation::wrong_path, index);
                state = event == PresentTraceEvent::damage ? TraceState::copy_damaged : TraceState::copy_expected;
                continue;
            }
            if (is_between_frames(event)) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                if (event == PresentTraceEvent::lost) loss_pending = true;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::done:
            if (is_between_frames(event)) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                if (event == PresentTraceEvent::lost) loss_pending = true;
                continue;
            }
            if (event == PresentTraceEvent::fallback) {
                if (!failed_in_frame) return violation_at(PresentTraceViolation::fallback_without_cause, index);
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                failed_in_frame = false;
                state = TraceState::copy_expected;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::acquired:
            if (is_scroll_step(event) || event == PresentTraceEvent::damage) {
                if (path != PresentPath::direct) return violation_at(PresentTraceViolation::wrong_path, index);
                if (event == PresentTraceEvent::damage) state = TraceState::direct_damaged;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::direct_damaged:
            if (event == PresentTraceEvent::present) {
                if (path != PresentPath::direct) return violation_at(PresentTraceViolation::wrong_path, index);
                // A lost buffer must fall back; a failed present may (a path
                // with a copy behind it) or may end the frame (the window
                // manager, which keeps the damage and tries again).
                const bool lost = record.detail == static_cast<std::uint8_t>(PresentOutcome::lost);
                failed_in_frame = record.detail == static_cast<std::uint8_t>(PresentOutcome::failed);
                state = lost ? TraceState::direct_failed : TraceState::done;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::direct_failed:
            if (event == PresentTraceEvent::lost || event == PresentTraceEvent::release) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                continue;
            }
            if (event == PresentTraceEvent::fallback) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                state = TraceState::copy_expected;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::refused:
            if (is_between_frames(event)) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                continue;
            }
            if (event == PresentTraceEvent::fallback) {
                if (!shim_path(path)) return violation_at(PresentTraceViolation::wrong_path, index);
                state = TraceState::copy_expected;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::copy_expected:
            if (is_scroll_step(event) || event == PresentTraceEvent::damage) {
                if (path != PresentPath::copy) return violation_at(PresentTraceViolation::wrong_path, index);
                if (event == PresentTraceEvent::damage) state = TraceState::copy_damaged;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        case TraceState::copy_damaged:
            if (event == PresentTraceEvent::present) {
                if (path != PresentPath::copy) return violation_at(PresentTraceViolation::wrong_path, index);
                state = TraceState::done;
                continue;
            }
            return violation_at(PresentTraceViolation::out_of_order, index);
        default:
            break;
        }
        // An unknown state: the one fallback for the impossible case.
        return violation_at(PresentTraceViolation::out_of_order, index);
    }
    if (frame_in_progress(state)) return violation_at(PresentTraceViolation::incomplete_frame, records.size());
    return PresentTraceCheck{};
}

bool present_traces_equivalent(std::span<const PresentTraceRecord> a,
                               std::span<const PresentTraceRecord> b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t index = 0U; index < a.size(); ++index) {
        const PresentTraceRecord& left = a[index];
        const PresentTraceRecord& right = b[index];
        if (left.event != right.event || left.path != right.path) return false;
        const bool age = left.event == PresentTraceEvent::acquire;
        if (!age && left.detail != right.detail) return false;
    }
    return true;
}

} // namespace gui_forms::host::presentation
