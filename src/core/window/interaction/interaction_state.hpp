#pragma once
// One window's interaction state and gesture recognizer
// (APP_WINDOW_RULES_DESIGN C.2, FD-6 section 5). Private to the core.
//
// Ownership: the window (SurfaceRoot) owns it, made on the first pointer
// or key event it dispatches, so a window that never sees input allocates
// none. It observes controls through weak references only.
//
// Execution: every member runs on the window's UI thread. The recognizer
// never calls out: each step updates the state completely and returns the
// interactions to raise (InteractionBatch), which the window delivers
// afterwards. A handler that starts or cancels a gesture while a batch is
// delivered therefore sees consistent state; the rest of the batch is
// still delivered to its control while that control stays eligible.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/window/interaction/interaction_snapshot.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace gui_forms::detail {

// The interactions one input step raises, in delivery order, and the
// control they go to (held strongly while they are delivered). At most
// three come from one step (release, click, double_click).
struct InteractionBatch final {
    static constexpr std::size_t capacity = 3U;
    Control::Ptr control{};
    std::array<InteractionEvent, capacity> events{};
    std::size_t count{};

    void append(const InteractionEvent& event) noexcept;
};

class InteractionState final {
public:
    InteractionState() noexcept = default;
    InteractionState(const InteractionState&) = delete;
    InteractionState& operator=(const InteractionState&) = delete;

    // Raw tracking, before any control sees the event.
    void note_pointer(const PointerEvent& event) noexcept;
    void note_key(const KeyEvent& event, FrameTime now) noexcept;

    // A button went down on `control`, which accepted it. Starts a gesture
    // unless one runs; raises press (and context_request for a secondary
    // press when `settings` says so).
    [[nodiscard]] InteractionBatch pressed(const Control::Ptr& control, const PointerEvent& event,
                                           FrameTime now, const PresentationSettings& settings);
    // The pointer moved: may leave the slop (no hold any more) or start or
    // continue a drag on a control that takes drag gestures.
    [[nodiscard]] InteractionBatch moved(const PointerEvent& event, FrameTime now);
    // The window is about to release the capture for the gesture's button
    // coming up; the capture change that follows is not a loss.
    void begin_release() noexcept { releasing_ = true; }
    // The gesture's button came up over `released_over`. Ends the gesture.
    [[nodiscard]] InteractionBatch released(const PointerEvent& event, const Control::Ptr& released_over,
                                            FrameTime now);
    // The hold wake fired at `now`: raises hold when the primary press still
    // rests within the slop and its hold time has passed.
    [[nodiscard]] InteractionBatch hold_due(FrameTime now);

    // When a hold wake is wanted, and whether one is.
    [[nodiscard]] bool hold_pending() const noexcept;
    [[nodiscard]] FrameTime hold_deadline() const noexcept { return hold_deadline_; }

    // The capture moved off `previous`: cancels the gesture on it unless
    // the window is releasing it for the button coming up.
    void capture_changed(const Control::Ptr& previous) noexcept;
    // The window lost its activation: keys and buttons are no longer known
    // to be down, and the gesture in progress is cancelled.
    void deactivated() noexcept;

    // Fills the input and gesture fields of `snapshot`.
    void fill_snapshot(InteractionSnapshot& snapshot) const;
    // Counts one delivered interaction.
    void count_delivered() noexcept { ++interactions_raised_; }

private:
    struct LastClick final {
        Control::WeakPtr control{};
        Point origin{};
        FrameTime pressed_at{};
        bool valid{};
    };

    void cancel_gesture() noexcept;
    void end_gesture() noexcept;
    [[nodiscard]] InteractionEvent make_event(InteractionKind kind, Point position,
                                              FrameTime now) const noexcept;

    std::array<HeldKey, maximum_held_keys> held_keys_{};
    std::size_t held_key_count_{};
    std::uint32_t untracked_key_presses_{};
    Modifier modifiers_{Modifier::none};
    Point pointer_position_{};
    bool pointer_inside_{};
    bool primary_down_{};
    bool secondary_down_{};
    bool middle_down_{};

    GesturePhase phase_{GesturePhase::none};
    PointerButton button_{PointerButton::none};
    Control::WeakPtr control_{};
    Point origin_{};
    std::uint32_t pointer_id_{};
    std::uint32_t click_count_{};
    FrameTime pressed_at_{};
    FrameTime hold_deadline_{};
    // Set once the pointer leaves the slop: no hold and no double-click
    // second press any more.
    bool left_slop_{};
    bool releasing_{};
    // Whether the gesture's control takes drag gestures, read at the press.
    bool accepts_drag_{};
    // Set once a secondary press raised context_request on its press.
    bool context_raised_{};
    // The thresholds in force for the gesture, read at the press.
    double slop_{default_gesture_slop};
    double drag_threshold_{default_drag_threshold};
    LastClick last_click_{};

    std::uint64_t interactions_raised_{};
    std::uint64_t gestures_cancelled_{};
};

} // namespace gui_forms::detail
