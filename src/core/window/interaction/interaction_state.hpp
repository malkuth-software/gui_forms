#pragma once
// One window's input state (APP_WINDOW_RULES_DESIGN C.2, as the owner's
// mouse-event rule of 2026-11-08 leaves it): held keys, modifiers, the
// pointer and its buttons, and the window's hover timing where the host
// does not report hover itself. Private to the core. It recognizes no
// gestures and counts no clicks: the host reports the click count.
//
// Ownership: the window (SurfaceRoot) owns it, made on the first pointer
// or key event it dispatches, so a window that never sees input allocates
// none. It observes controls through weak references only.
//
// Execution: every member runs on the window's UI thread and calls out to
// nothing; the window raises the events.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/window/interaction/interaction_snapshot.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gui_forms::detail {

class InteractionState final {
public:
    InteractionState() noexcept = default;
    InteractionState(const InteractionState&) = delete;
    InteractionState& operator=(const InteractionState&) = delete;

    // Raw tracking, before any control sees the event.
    void note_pointer(const PointerEvent& event) noexcept;
    void note_key(const KeyEvent& event, FrameTime now) noexcept;

    // Hover (MouseHover, raised once per entry): the hovered control became
    // `control` (null when none) with the pointer at `position`.
    void hover_entered(const Control::Ptr& control, Point position, FrameTime now,
                       const PointerSettings& settings) noexcept;
    // The pointer moved to `position`: leaving the hover rectangle around
    // where it last rested starts the rest again.
    void hover_moved(Point position, FrameTime now, const PointerSettings& settings) noexcept;
    // Whether the hover timer is wanted, and when it is due.
    [[nodiscard]] bool hover_pending() const noexcept;
    [[nodiscard]] FrameTime hover_deadline() const noexcept { return hover_deadline_; }
    // The control MouseHover is due on at `now` (null when none), marked
    // raised. `native` takes the host's report instead of the deadline.
    [[nodiscard]] Control::Ptr take_hover(FrameTime now, bool native) noexcept;

    // The window lost its activation: keys and buttons are no longer known
    // to be down.
    void deactivated() noexcept;

    // The click count the host reported for the last press: a release's
    // MouseEventArgs carries the count of the press it ends.
    [[nodiscard]] std::uint32_t last_click_count() const noexcept { return last_click_count_; }

    // Fills the input fields of `snapshot`.
    void fill_snapshot(InteractionSnapshot& snapshot) const;

private:
    std::array<HeldKey, maximum_held_keys> held_keys_{};
    std::size_t held_key_count_{};
    std::uint32_t untracked_key_presses_{};
    Modifier modifiers_{Modifier::none};
    Point pointer_position_{};
    bool pointer_inside_{};
    bool primary_down_{};
    bool secondary_down_{};
    bool middle_down_{};
    std::uint32_t last_click_count_{};

    Control::WeakPtr hover_control_{};
    Point hover_anchor_{};
    FrameTime hover_deadline_{};
    bool hover_raised_{};
};

} // namespace gui_forms::detail
