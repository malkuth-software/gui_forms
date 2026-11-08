// The window's side of its interaction state (APP_WINDOW_RULES_DESIGN
// C.2): making it, delivering what its recognizer raises, the hold wake and
// the snapshot. The dispatch paths that feed it are in window.cpp.

#include "gui_forms/window.hpp"
#include "interaction_state.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>

namespace gui_forms {

namespace detail {

// The hold wake's target: the window whose pending hold is due.
struct InteractionHoldWake final {
    SurfaceRoot* root{};

    void operator()(FrameTime now) const { (*root).interaction_hold_due(now); }
};

} // namespace detail

FrameRequestToken SurfaceRoot::request_one_shot_wake(FrameTime deadline, std::function<void(FrameTime)> callback) {
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    // A one-shot: the requester disconnects the request when it wakes, so
    // the interval is never reached.
    std::shared_ptr<detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        deadline, std::chrono::duration_cast<FrameInterval>(std::chrono::hours(1)), std::move(callback));
    frame_requests_.push_back(request);
    metrics_.record_frame_request();
    return FrameRequestToken(request);
}

detail::InteractionState& SurfaceRoot::interaction_state() {
    if (!interaction_) interaction_ = std::make_unique<detail::InteractionState>();
    return *interaction_;
}

FrameTime SurfaceRoot::input_now() const noexcept {
    if (input_time_source_ != nullptr) return (*input_time_source_).now();
    return FrameClock::now();
}

void SurfaceRoot::deliver_interactions(const detail::InteractionBatch& batch) {
    const Control::Ptr& control = batch.control;
    for (std::size_t index = 0U; index < batch.count; ++index) {
        if (!control || !eligible(control)) return;
        const InteractionEvent& event = batch.events[index];
        metrics_.record_callback_emitted();
        if (interaction_) (*interaction_).count_delivered();
        (*control).on_interaction(event);
        const bool activation = event.kind == InteractionKind::click && event.button == PointerButton::primary;
        if (activation && eligible(control)) {
            metrics_.record_callback_emitted();
            (*control).on_activate();
            metrics_.record_activation();
        }
    }
}

void SurfaceRoot::sync_hold_wake() {
    const bool wanted = interaction_ && (*interaction_).hold_pending();
    if (!wanted) {
        hold_wake_.disconnect();
        return;
    }
    if (hold_wake_.connected()) return;
    hold_wake_ = request_one_shot_wake((*interaction_).hold_deadline(), detail::InteractionHoldWake{this});
}

void SurfaceRoot::interaction_hold_due(FrameTime now) {
    hold_wake_.disconnect();
    if (!interaction_) return;
    const detail::InteractionBatch batch = (*interaction_).hold_due(now);
    // Not yet due (an earlier poll): wake again at the deadline.
    sync_hold_wake();
    deliver_interactions(batch);
}

InteractionSnapshot SurfaceRoot::interaction_snapshot() const {
    InteractionSnapshot snapshot{};
    if (interaction_) (*interaction_).fill_snapshot(snapshot);
    const Control::Ptr focused = focused_.lock();
    const Control::Ptr captured = captured_.lock();
    const Control::Ptr pressed = pressed_.lock();
    const Control::Ptr hovered = hovered_.lock();
    snapshot.focused = focused ? (*focused).runtime_id() : RuntimeId{};
    snapshot.captured = captured ? (*captured).runtime_id() : RuntimeId{};
    snapshot.pressed = pressed ? (*pressed).runtime_id() : RuntimeId{};
    snapshot.hovered = hovered ? (*hovered).runtime_id() : RuntimeId{};
    return snapshot;
}

} // namespace gui_forms
