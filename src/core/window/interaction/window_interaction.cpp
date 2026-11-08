// The window's side of its input state (APP_WINDOW_RULES_DESIGN C.2, the
// owner's mouse-event rule of 2026-11-08): making it, the hover wake and
// MouseHover, context-menu requests and the snapshot. The pointer and key
// dispatch paths that feed it, and the other WinForms mouse events, are in
// window.cpp.

#include "gui_forms/window.hpp"
#include "interaction_state.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>

namespace gui_forms {

namespace detail {

// The hover wake's target: the window whose pending hover is due.
struct InteractionHoverWake final {
    SurfaceRoot* root{};

    void operator()(FrameTime now) const { (*root).interaction_hover_due(now); }
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

void SurfaceRoot::sync_hover_wake() {
    // A wake only where MouseHover has a listener: the control's mouse
    // events, made by their first subscription (a mouse_hover rule
    // subscribes too), so resting over a control nobody listens to arms
    // nothing.
    bool listened = false;
    if (interaction_ && (*interaction_).hover_pending()) {
        const Control::Ptr control = (*interaction_).hover_control();
        listened = control && (*control).mouse_events_;
    }
    const bool wanted = listened && !presentation_settings_.pointer.native_hover;
    if (!wanted) {
        hover_wake_.disconnect();
        return;
    }
    // The deadline moves later while the pointer wanders; a wake armed for
    // an earlier deadline finds the hover not yet due and re-arms then, so
    // a move allocates no new wake.
    if (hover_wake_.connected()) return;
    hover_wake_ = request_one_shot_wake((*interaction_).hover_deadline(), detail::InteractionHoverWake{this});
}

void SurfaceRoot::interaction_hover_due(FrameTime now) {
    hover_wake_.disconnect();
    if (!interaction_) return;
    const Control::Ptr control = (*interaction_).take_hover(now, false);
    // Not yet due (an earlier poll): wake again at the deadline.
    sync_hover_wake();
    raise_mouse_hover(control);
}

void SurfaceRoot::raise_mouse_hover(const Control::Ptr& control) {
    if (!control || !eligible(control) || !(*control).mouse_events_) return;
    metrics_.record_callback_emitted();
    (*(*control).mouse_events_).hover.emit();
}

void SurfaceRoot::dispatch_host_hover(Point position) {
    require_root_thread("hover dispatch");
    static_cast<void>(position);
    if (!interaction_) return;
    const Control::Ptr control = (*interaction_).take_hover(input_now(), true);
    raise_mouse_hover(control);
}

bool SurfaceRoot::dispatch_context_request(ContextRequestOrigin origin, Point position) {
    require_root_thread("context request dispatch");
    metrics_.record_input();
    const bool keyboard = origin == ContextRequestOrigin::keyboard;
    Control::Ptr target = keyboard ? focused_.lock() : captured_.lock();
    if (!target && !keyboard) target = hit_test(position);
    if (!target || !eligible(target)) return false;
    // A keyboard request has no pointer position: it carries the focused
    // control's origin.
    const Rect target_bounds = (*target).absolute_bounds();
    ContextMenuRequest request{};
    request.origin = origin;
    request.window_position = keyboard ? Point{target_bounds.x, target_bounds.y} : position;
    // From the target outward, until a control handles it.
    const std::vector<Control::Ptr> route = route_to(target);
    for (ControlList::const_reverse_iterator iterator = route.rbegin(); iterator != route.rend(); ++iterator) {
        const Control::Ptr& control = *iterator;
        if (!eligible(control)) continue;
        const Rect bounds = (*control).absolute_bounds();
        request.location = Point{request.window_position.x - bounds.x, request.window_position.y - bounds.y};
        metrics_.record_callback_emitted();
        (*control).on_context_menu_requested(request);
        if (request.handled) return true;
        if ((*control).mouse_events_) (*(*control).mouse_events_).context_menu_requested.emit(request);
        if (request.handled) return true;
    }
    return false;
}

InteractionSnapshot SurfaceRoot::interaction_snapshot() const {
    InteractionSnapshot snapshot{};
    if (interaction_) (*interaction_).fill_snapshot(snapshot);
    snapshot.hover_pending = snapshot.hover_pending && hover_wake_.connected();
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
