#pragma once

// A test control's routed pointer and key handling over the raisers. Probes
// written before the raisers took the target phase handled one routed
// event (on_pointer, on_key); this base rebuilds that event from each
// raiser's arguments and hands it to routed_pointer or routed_key, so a
// probe keeps one handler. Base is the control the probe derives from.
//
// Order: a mouse raiser runs the probe's handler first and raises the
// event (Base's raiser) after, unless the handler disposed the control. A
// key raiser raises the event first; a key the subscribers left unhandled
// reaches the handler, and a key released by deactivation does not (as
// before). Enter and leave come from the window's hover tracking
// (on_mouse_enter, on_mouse_leave) with no position.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"

namespace gui_forms::tests {

template <typename Base>
class RoutedInputProbe : public Base {
public:
    using Base::Base;

protected:
    virtual void routed_pointer(PointerEvent& event) { static_cast<void>(event); }
    virtual void routed_key(KeyEvent& event) { static_cast<void>(event); }

    void on_mouse_down(MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive()) Base::on_mouse_down(arguments);
    }
    void on_mouse_up(MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive()) Base::on_mouse_up(arguments);
    }
    void on_mouse_move(MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive()) Base::on_mouse_move(arguments);
    }
    void on_mouse_wheel(MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive()) Base::on_mouse_wheel(arguments);
    }
    void on_mouse_enter() override {
        route_crossing(PointerAction::enter);
        if ((*this).is_alive()) Base::on_mouse_enter();
    }
    void on_mouse_leave() override {
        route_crossing(PointerAction::leave);
        if ((*this).is_alive()) Base::on_mouse_leave();
    }
    void on_key_down(KeyEventArgs& arguments) override {
        Base::on_key_down(arguments);
        route_key(arguments, KeyAction::down);
    }
    void on_key_up(KeyEventArgs& arguments) override {
        Base::on_key_up(arguments);
        if (arguments.released_by_deactivation) return;
        route_key(arguments, KeyAction::up);
    }

private:
    void route_mouse(MouseEventArgs& arguments) {
        PointerEvent event = pointer_event_of(arguments);
        routed_pointer(event);
        arguments.handled = event.handled;
    }
    void route_crossing(PointerAction action) {
        PointerEvent event{};
        event.action = action;
        event.phase = EventPhase::target;
        routed_pointer(event);
    }
    void route_key(KeyEventArgs& arguments, KeyAction action) {
        if (arguments.handled || !(*this).is_alive()) return;
        KeyEvent event = key_event_of(arguments, action);
        routed_key(event);
        if (event.handled) arguments.handled = true;
    }
};

} // namespace gui_forms::tests
