#pragma once
// The WinForms mouse event records a control raises (owner's mouse-event
// rule, 2026-11-08): MouseEventArgs for MouseDown, MouseUp, MouseMove,
// MouseWheel, MouseClick and MouseDoubleClick, and ContextMenuRequest for
// ContextMenuRequested. The click count comes from the host (Windows and
// macOS report it; the Linux host computes it from the desktop's
// double-click time and distance); the core counts no clicks.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>

namespace gui_forms {

// One mouse event as WinForms' MouseEventArgs gives it, with the routed
// pointer event's own fields. The WinForms members: the button (Button;
// none for a move or wheel without one), the click count the host
// reported for the press (Clicks: 1, 2 for the second press of a
// double-click, and so on; a release carries its press's count; 0 for a
// move or wheel), the position in the control's own coordinates (Location)
// and in the window's, the wheel delta (Delta; logical pixels or lines,
// positive away from the user and to the right) and the modifiers held.
// handled is HandledMouseEventArgs.Handled: a raiser that sets it ends the
// pointer event's route at its target, so no ancestor's bubble handler sees
// it (the subscribers still run when the raiser calls its base); it
// suppresses nothing else. suppress_click, set by a MouseUp raiser or its
// override, cancels the release's Click and MouseClick (DoubleClick and
// MouseDoubleClick on a double-click's second press) and the activation
// (the owner's input order, rule 3); elsewhere it is ignored. The rest
// have no WinForms counterpart: what the pointer did (action; enter and
// leave reach no MouseEventArgs), the host's pointer identity (pointer_id;
// one for the mouse), the route phase the raiser runs in (phase; target
// for the raisers), what wheel_delta counts (wheel_unit) and the event's
// place in a scroll gesture (wheel_phase). timestamp_nanoseconds is when
// the host took the event (PointerEvent: FrameClock nanoseconds, monotonic,
// the same clock on every host), so a fling's speed is measured between
// events and not by when a handler runs. The hosts
// report no touch or pen data yet, so the record carries none.
struct GUI_FORMS_API MouseEventArgs final {
    PointerButton button{PointerButton::none};
    std::uint32_t clicks{};
    Point location{};
    Point window_position{};
    Point wheel_delta{};
    Modifier modifiers{Modifier::none};
    PointerAction action{PointerAction::move};
    WheelDeltaUnit wheel_unit{WheelDeltaUnit::lines};
    std::uint32_t pointer_id{};
    EventPhase phase{EventPhase::target};
    bool handled{};
    bool suppress_click{};
    WheelPhase wheel_phase{WheelPhase::none};
    std::uint64_t timestamp_nanoseconds{};
};

// The routed pointer event a MouseEventArgs carries, for code that takes
// the routed form (a gesture engine, ScrollableControl's bar and wheel
// handling): the window position, the action, the button and the rest,
// with click_count from clicks. The caller copies handled back.
[[nodiscard]] inline PointerEvent pointer_event_of(const MouseEventArgs& arguments) noexcept {
    PointerEvent event{};
    event.action = arguments.action;
    event.button = arguments.button;
    event.position = arguments.window_position;
    event.wheel_delta = arguments.wheel_delta;
    event.modifiers = arguments.modifiers;
    event.pointer_id = arguments.pointer_id;
    event.phase = arguments.phase;
    event.handled = arguments.handled;
    event.click_count = arguments.clicks;
    event.wheel_unit = arguments.wheel_unit;
    event.wheel_phase = arguments.wheel_phase;
    event.timestamp_nanoseconds = arguments.timestamp_nanoseconds;
    return event;
}

// The MouseEventArgs a routed pointer event carries on a control whose
// absolute bounds start at `control_origin` (window coordinates): the
// record the window hands the mouse raisers. location is the event's
// position less control_origin, window_position the event's position;
// clicks is `press_clicks` for a press or release (the window passes the
// count the host reported for the press, which a release carries) and
// zero otherwise; the button, wheel delta, modifiers, action, wheel unit,
// wheel phase, timestamp, pointer identity, phase and handled are the event's, and suppress_click
// is false. The window builds its raisers' arguments with this, so the
// routed and raiser forms cannot drift apart; Control::
// mouse_event_arguments fills the origin and click count for a handler of
// the routed form. The caller copies handled (and a release's
// suppress_click) back.
[[nodiscard]] inline MouseEventArgs mouse_event_arguments(const PointerEvent& event, Point control_origin,
                                                          std::uint32_t press_clicks) noexcept {
    const bool press = event.action == PointerAction::down;
    const bool release = event.action == PointerAction::up;
    MouseEventArgs arguments{};
    arguments.button = event.button;
    arguments.clicks = press || release ? press_clicks : 0U;
    arguments.location = Point{event.position.x - control_origin.x, event.position.y - control_origin.y};
    arguments.window_position = event.position;
    arguments.wheel_delta = event.wheel_delta;
    arguments.modifiers = event.modifiers;
    arguments.action = event.action;
    arguments.wheel_unit = event.wheel_unit;
    arguments.wheel_phase = event.wheel_phase;
    arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
    arguments.pointer_id = event.pointer_id;
    arguments.phase = event.phase;
    arguments.handled = event.handled;
    return arguments;
}

// Where the pointer is, as a window last saw it: the position of the last
// pointer event the window dispatched, which is the event being dispatched
// while its raisers run. MouseEnter and MouseLeave carry no arguments
// (Windows Forms' EventArgs), so their handlers read the crossing here:
// on_mouse_leave and on_mouse_enter run during the move that crossed (the
// leave first, then the enter, both at that move's position), and the
// window's own leave raises on_mouse_leave at the position the host
// reported for it. location is in the asking control's coordinates (the
// window's when the window is asked), window_position in the window's;
// inside is false once the pointer has left the window, and before the
// window has seen a pointer event (all zero then). A detached control
// reports all zero.
struct GUI_FORMS_API PointerLocation final {
    Point location{};
    Point window_position{};
    bool inside{};

    friend bool operator==(const PointerLocation& left, const PointerLocation& right) noexcept {
        return left.location == right.location && left.window_position == right.window_position &&
               left.inside == right.inside;
    }
};

// Where a context-menu request came from: the pointer (a right press or
// release, a Control-click on macOS) or the keyboard (Shift+F10 or the
// Menu key, where the host's platform reports or emulates them).
enum class ContextRequestOrigin : std::uint8_t {
    pointer,
    keyboard,
};

// A request for a context menu, raised on the control under the pointer
// (or the focused control for a keyboard request) and then on each
// ancestor until a handler sets `handled`. `location` is in the control's
// coordinates and `window_position` in the window's; a keyboard request
// has no pointer position and carries the control's own origin, so the
// control chooses its anchor (its selection, its keyboard item).
struct GUI_FORMS_API ContextMenuRequest final {
    ContextRequestOrigin origin{ContextRequestOrigin::pointer};
    Point location{};
    Point window_position{};
    bool handled{};
};

} // namespace gui_forms
