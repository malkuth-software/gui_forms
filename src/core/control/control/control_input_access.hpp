#pragma once

// The internal entry for one control's target phase of a routed pointer
// or key event, which Control::on_pointer and Control::on_key were before
// the raisers took the target phase: it raises the raiser the event's
// action names, with arguments built from the routed form, and copies
// handled back. The window raises the target phase itself
// (SurfaceRoot::route_pointer and SurfaceRoot::raise_control_key, which
// add the click count the window tracks and the metrics); this entry is
// for code that hands one control an event directly, such as a test that
// drives a control without routing.
//
// Holder: none. The functions are static and keep nothing.
//
// Stale use: the caller holds the control for the call. A handler may
// dispose the control; the call touches it no further after the raiser
// returns.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

namespace gui_forms::detail {

class ControlInputAccess final {
public:
    ControlInputAccess() = delete;

    // An enter or leave raises on_mouse_enter or on_mouse_leave, which
    // carry no arguments (a handler's Control::pointer_location reads the
    // window's last pointer event, not this one: nothing is dispatched). A
    // press, release, move or wheel raises its mouse raiser with
    // mouse_event_arguments: location relative to the control's absolute
    // bounds, clicks the event's click_count for a press or release (zero
    // otherwise), the target phase.
    static void on_pointer(Control& target, PointerEvent& event) {
        if (event.action == PointerAction::enter) {
            target.on_mouse_enter();
            return;
        }
        if (event.action == PointerAction::leave) {
            target.on_mouse_leave();
            return;
        }
        const Rect bounds = target.absolute_bounds();
        const Point origin{bounds.x, bounds.y};
        MouseEventArgs arguments = mouse_event_arguments(event, origin, event.click_count);
        arguments.phase = EventPhase::target;
        switch (event.action) {
        case PointerAction::down:
            target.on_mouse_down(arguments);
            break;
        case PointerAction::up:
            target.on_mouse_up(arguments);
            break;
        case PointerAction::move:
            target.on_mouse_move(arguments);
            break;
        case PointerAction::wheel:
            target.on_mouse_wheel(arguments);
            break;
        case PointerAction::enter:
        case PointerAction::leave:
        default:
            // Answered above; nothing is raised.
            break;
        }
        event.handled = arguments.handled;
    }

    // A down raises on_key_down, an up on_key_up. suppress_key_press set
    // by a handler implies handled.
    static void on_key(Control& target, KeyEvent& event) {
        KeyEventArgs arguments = key_event_arguments(event);
        if (event.action == KeyAction::down) {
            target.on_key_down(arguments);
        } else {
            target.on_key_up(arguments);
        }
        event.handled = arguments.handled || arguments.suppress_key_press;
    }
};

} // namespace gui_forms::detail
