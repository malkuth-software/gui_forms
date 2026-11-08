#pragma once
// The window's interactions (APP_WINDOW_RULES_DESIGN C.2, FD-6 section 5):
// what its gesture recognizer raises to a control from raw pointer events,
// with one definition for every control.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>

namespace gui_forms {

// What the recognizer raises. For one gesture they come in this order:
// - press, when a button goes down on a control that accepts it;
// - hold, once, when the primary press has rested within the slop for the
//   hold time (GestureSettings);
// - drag_start, then drag on each later move, when the primary press moves
//   past the drag threshold on a control that takes drag gestures
//   (Control::accepts_drag_gestures); drag_start cancels the pending click;
// - release, when the button comes up;
// - after release: click when the primary button came up over the pressed
//   control and no drag started, then double_click when that click is the
//   second of a double-click; drag_end when a drag started;
//   context_request for the secondary button, after its release over the
//   pressed control, or right after its press where the host reports
//   ContextRequestEdge::press.
// The middle button raises press and release only. One gesture runs at a
// time: a button pressed while another gesture runs raises nothing. Losing
// the pointer capture, or the window's activation, cancels the gesture in
// progress and raises nothing more for it.
enum class InteractionKind : std::uint8_t {
    press,
    release,
    click,
    double_click,
    hold,
    drag_start,
    drag,
    drag_end,
    context_request,
};

// One raised interaction, delivered to the gesture's control
// (Control::on_interaction) after the raw pointer event that caused it has
// finished its route. `position` is the pointer's position now and
// `origin` the press position, both in window coordinates. `click_count`
// is 1 or 2 for click and double_click (2 for the second click of a
// double-click) and 0 otherwise. `time` is the recognizer's time
// (SurfaceRoot::set_input_time_source) when the interaction was raised.
struct InteractionEvent final {
    InteractionKind kind{InteractionKind::press};
    PointerButton button{PointerButton::none};
    Point position{};
    Point origin{};
    Modifier modifiers{Modifier::none};
    std::uint32_t pointer_id{};
    std::uint32_t click_count{};
    FrameTime time{};
};

// Where the window's interaction state reads the time of an input event.
// Steady-clock time unless a source is installed: tests install one so
// that intervals and hold times are measured from times they choose. Its
// times must be in the same domain as the times the window is polled with
// (SurfaceRoot::poll_frame_schedule), which wakes a pending hold.
class InputTimeSource {
public:
    virtual ~InputTimeSource() = default;
    [[nodiscard]] virtual FrameTime now() const noexcept = 0;
};

} // namespace gui_forms
