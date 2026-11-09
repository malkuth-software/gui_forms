#include "interaction_state.hpp"

#include <chrono>
#include <cmath>

namespace gui_forms::detail {

namespace {

// Whether `position` lies within the hover rectangle (PointerSettings)
// centred on `anchor`.
bool within_hover(Point position, Point anchor, const PointerSettings& settings) noexcept {
    const bool across = std::fabs(position.x - anchor.x) <= settings.hover_width * 0.5;
    const bool down = std::fabs(position.y - anchor.y) <= settings.hover_height * 0.5;
    return across && down;
}

FrameInterval milliseconds(std::uint32_t count) noexcept {
    const std::chrono::milliseconds duration(count);
    return std::chrono::duration_cast<FrameInterval>(duration);
}

} // namespace

void InteractionState::note_pointer(const PointerEvent& event) noexcept {
    modifiers_ = event.modifiers;
    pointer_position_ = event.position;
    pointer_inside_ = event.action != PointerAction::leave;
    const bool down = event.action == PointerAction::down;
    const bool up = event.action == PointerAction::up;
    // Only a press or release changes button state; one naming no button
    // changes none.
    if (!down && !up) return;
    if (down) last_click_count_ = event.click_count;
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

void InteractionState::hover_entered(const Control::Ptr& control, Point position, FrameTime now,
                                     const PointerSettings& settings) noexcept {
    hover_control_ = control;
    hover_anchor_ = position;
    hover_raised_ = false;
    hover_deadline_ = now + milliseconds(settings.hover_milliseconds);
}

void InteractionState::hover_moved(Point position, FrameTime now, const PointerSettings& settings) noexcept {
    if (hover_raised_ || within_hover(position, hover_anchor_, settings)) return;
    hover_anchor_ = position;
    hover_deadline_ = now + milliseconds(settings.hover_milliseconds);
}

bool InteractionState::hover_pending() const noexcept {
    return !hover_raised_ && !hover_control_.expired();
}

Control::Ptr InteractionState::take_hover(FrameTime now, bool native) noexcept {
    if (hover_raised_) return {};
    if (!native && now < hover_deadline_) return {};
    Control::Ptr control = hover_control_.lock();
    if (control) hover_raised_ = true;
    return control;
}

void InteractionState::deactivated() noexcept {
    held_keys_ = {};
    held_key_count_ = 0U;
    modifiers_ = Modifier::none;
    primary_down_ = false;
    secondary_down_ = false;
    middle_down_ = false;
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
    snapshot.last_click_count = last_click_count_;
    snapshot.hover_raised = hover_raised_;
    snapshot.hover_pending = hover_pending();
}

} // namespace gui_forms::detail
