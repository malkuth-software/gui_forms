#include "interaction_state.hpp"

#include <cmath>

namespace gui_forms::detail {

namespace {

// The larger of the two axis distances between `left` and `right`: the
// thresholds bound a square around the press point (input_settings.hpp).
double axis_distance(Point left, Point right) noexcept {
    const double across = std::fabs(left.x - right.x);
    const double down = std::fabs(left.y - right.y);
    return across > down ? across : down;
}

FrameInterval milliseconds(std::uint32_t count) noexcept {
    const std::chrono::milliseconds duration(count);
    return std::chrono::duration_cast<FrameInterval>(duration);
}

} // namespace

void InteractionBatch::append(const InteractionEvent& event) noexcept {
    // The recognizer raises at most `capacity` per step by construction;
    // a further one would be a recognizer defect and is dropped.
    if (count == capacity) return;
    events[count] = event;
    ++count;
}

void InteractionState::note_pointer(const PointerEvent& event) noexcept {
    modifiers_ = event.modifiers;
    pointer_position_ = event.position;
    pointer_inside_ = event.action != PointerAction::leave;
    const bool down = event.action == PointerAction::down;
    const bool up = event.action == PointerAction::up;
    // Only a press or release changes button state; one naming no button
    // changes none.
    if (!down && !up) return;
    if (event.button == PointerButton::primary) primary_down_ = down;
    if (event.button == PointerButton::secondary) secondary_down_ = down;
    if (event.button == PointerButton::middle) middle_down_ = down;
}

void InteractionState::note_key(const KeyEvent& event, FrameTime now) noexcept {
    modifiers_ = event.modifiers;
    std::size_t index = 0U;
    while (index < held_key_count_ && held_keys_[index].physical_key != event.physical_key) {
        ++index;
    }
    const bool held = index < held_key_count_;
    if (event.action == KeyAction::down) {
        if (held || event.repeat) return;
        if (held_key_count_ == held_keys_.size()) {
            ++untracked_key_presses_;
            return;
        }
        held_keys_[held_key_count_] = HeldKey{event.physical_key, now};
        ++held_key_count_;
        return;
    }
    if (!held) return;
    // Keep press order: close the gap.
    for (std::size_t later = index + 1U; later < held_key_count_; ++later) {
        held_keys_[later - 1U] = held_keys_[later];
    }
    --held_key_count_;
    held_keys_[held_key_count_] = HeldKey{};
}

InteractionEvent InteractionState::make_event(InteractionKind kind, Point position,
                                              FrameTime now) const noexcept {
    InteractionEvent event{};
    event.kind = kind;
    event.button = button_;
    event.position = position;
    event.origin = origin_;
    event.modifiers = modifiers_;
    event.pointer_id = pointer_id_;
    event.time = now;
    return event;
}

InteractionBatch InteractionState::pressed(const Control::Ptr& control, const PointerEvent& event,
                                           FrameTime now, const PresentationSettings& settings) {
    InteractionBatch batch{};
    if (phase_ != GesturePhase::none || !control || event.button == PointerButton::none) return batch;
    const GestureSettings& gestures = settings.gestures;
    button_ = event.button;
    control_ = control;
    origin_ = event.position;
    pointer_id_ = event.pointer_id;
    pressed_at_ = now;
    left_slop_ = false;
    releasing_ = false;
    context_raised_ = false;
    slop_ = gestures.slop;
    drag_threshold_ = gestures.drag_threshold;
    phase_ = GesturePhase::pressed;
    accepts_drag_ = false;
    click_count_ = 0U;
    if (button_ == PointerButton::primary) {
        accepts_drag_ = (*control).accepts_drag_gestures();
        hold_deadline_ = now + milliseconds(gestures.hold_milliseconds);
        const Control::Ptr last_control = last_click_.control.lock();
        const bool same_control = last_click_.valid && last_control == control;
        const bool in_time = now - last_click_.pressed_at <= milliseconds(settings.double_click_milliseconds);
        const bool in_slop = axis_distance(origin_, last_click_.origin) <= slop_;
        click_count_ = same_control && in_time && in_slop ? 2U : 1U;
    }
    batch.control = control;
    batch.append(make_event(InteractionKind::press, origin_, now));
    if (button_ == PointerButton::secondary && gestures.context_request == ContextRequestEdge::press) {
        context_raised_ = true;
        batch.append(make_event(InteractionKind::context_request, origin_, now));
    }
    return batch;
}

InteractionBatch InteractionState::moved(const PointerEvent& event, FrameTime now) {
    InteractionBatch batch{};
    if (phase_ == GesturePhase::none) return batch;
    const Control::Ptr control = control_.lock();
    if (!control) return batch;
    const double distance = axis_distance(event.position, origin_);
    if (distance > slop_) left_slop_ = true;
    if (button_ != PointerButton::primary || !accepts_drag_) return batch;
    batch.control = control;
    if (phase_ == GesturePhase::dragging) {
        batch.append(make_event(InteractionKind::drag, event.position, now));
    } else if (distance > drag_threshold_) {
        phase_ = GesturePhase::dragging;
        // A drag is not a click, nor the first click of a double-click.
        click_count_ = 0U;
        last_click_ = LastClick{};
        batch.append(make_event(InteractionKind::drag_start, event.position, now));
    }
    return batch;
}

InteractionBatch InteractionState::released(const PointerEvent& event, const Control::Ptr& released_over,
                                            FrameTime now) {
    InteractionBatch batch{};
    releasing_ = false;
    if (phase_ == GesturePhase::none || event.button != button_) return batch;
    const Control::Ptr control = control_.lock();
    if (!control) {
        end_gesture();
        return batch;
    }
    batch.control = control;
    batch.append(make_event(InteractionKind::release, event.position, now));
    const bool over = released_over == control;
    if (phase_ == GesturePhase::dragging) {
        batch.append(make_event(InteractionKind::drag_end, event.position, now));
    } else if (button_ == PointerButton::primary && over) {
        InteractionEvent click = make_event(InteractionKind::click, event.position, now);
        click.click_count = click_count_;
        batch.append(click);
        if (click_count_ == 2U) {
            InteractionEvent second = make_event(InteractionKind::double_click, event.position, now);
            second.click_count = 2U;
            batch.append(second);
            last_click_ = LastClick{};
        } else {
            last_click_ = LastClick{control_, origin_, pressed_at_, true};
        }
    } else if (button_ == PointerButton::primary) {
        last_click_ = LastClick{};
    }
    if (button_ == PointerButton::secondary && !context_raised_ && over) {
        batch.append(make_event(InteractionKind::context_request, event.position, now));
    }
    end_gesture();
    return batch;
}

bool InteractionState::hold_pending() const noexcept {
    return phase_ == GesturePhase::pressed && button_ == PointerButton::primary && !left_slop_;
}

InteractionBatch InteractionState::hold_due(FrameTime now) {
    InteractionBatch batch{};
    if (!hold_pending() || now < hold_deadline_) return batch;
    const Control::Ptr control = control_.lock();
    if (!control) return batch;
    phase_ = GesturePhase::held;
    batch.control = control;
    batch.append(make_event(InteractionKind::hold, pointer_position_, now));
    return batch;
}

void InteractionState::capture_changed(const Control::Ptr& previous) noexcept {
    if (releasing_) {
        releasing_ = false;
        return;
    }
    if (phase_ == GesturePhase::none || button_ != PointerButton::primary) return;
    const Control::Ptr control = control_.lock();
    if (!control || control == previous) cancel_gesture();
}

void InteractionState::deactivated() noexcept {
    held_keys_ = {};
    held_key_count_ = 0U;
    modifiers_ = Modifier::none;
    primary_down_ = false;
    secondary_down_ = false;
    middle_down_ = false;
    cancel_gesture();
}

void InteractionState::cancel_gesture() noexcept {
    if (phase_ == GesturePhase::none) return;
    ++gestures_cancelled_;
    last_click_ = LastClick{};
    end_gesture();
}

void InteractionState::end_gesture() noexcept {
    phase_ = GesturePhase::none;
    button_ = PointerButton::none;
    control_.reset();
    click_count_ = 0U;
    left_slop_ = false;
    releasing_ = false;
    accepts_drag_ = false;
    context_raised_ = false;
}

void InteractionState::fill_snapshot(InteractionSnapshot& snapshot) const {
    snapshot.held_keys.assign(held_keys_.begin(), held_keys_.begin() + static_cast<std::ptrdiff_t>(held_key_count_));
    snapshot.untracked_key_presses = untracked_key_presses_;
    snapshot.modifiers = modifiers_;
    snapshot.pointer_position = pointer_position_;
    snapshot.pointer_inside = pointer_inside_;
    snapshot.primary_down = primary_down_;
    snapshot.secondary_down = secondary_down_;
    snapshot.middle_down = middle_down_;
    snapshot.gesture = phase_;
    snapshot.gesture_button = button_;
    const Control::Ptr control = control_.lock();
    snapshot.gesture_control = control ? (*control).runtime_id() : RuntimeId{};
    snapshot.gesture_origin = origin_;
    snapshot.gesture_click_count = click_count_;
    snapshot.interactions_raised = interactions_raised_;
    snapshot.gestures_cancelled = gestures_cancelled_;
}

} // namespace gui_forms::detail
