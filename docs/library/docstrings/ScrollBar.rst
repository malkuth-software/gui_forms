A standalone scroll bar that sets a value within a range.

A scroll bar has an arrow button at each end and a thumb that slides along
the track between them. The user moves the value by clicking or holding an
arrow, clicking the track, dragging the thumb, turning the mouse wheel over
it, or with the keyboard while it has focus. It inherits its range, value,
steps and events from `RangeControl`. Use one when your own control does
the scrolling, such as a canvas that draws part of a large document; to
scroll child controls, turn on `auto_scroll` on a `Panel`, whose built-in
bars are not ScrollBar controls. Use `HScrollBar` or `VScrollBar` to fix the
orientation in the type, and a `TrackBar` for choosing a setting.

Parameters
----------
stable_id
    Identity of the scroll bar, unique within its window.
initial_orientation
    Whether the bar runs horizontally or vertically. It can be changed later
    through the inherited `orientation` property.

Properties
----------
button_extent : default ``17.0``
    The length of each arrow button along the bar, in logical pixels; on a
    short bar each button takes at most half its length. A value outside 8
    to 64, or not finite, throws `std::invalid_argument`.
minimum_thumb_extent : default ``18.0``
    The shortest the thumb may be, in logical pixels, unless the track is
    shorter. A value outside 8 to 128 throws `std::invalid_argument`.
initial_repeat_delay : default ``std::chrono::milliseconds(400)``
    How long an arrow or the track must be held before the step repeats. A
    value outside 100 milliseconds to 2 seconds throws
    `std::invalid_argument`.
repeat_interval : default ``std::chrono::milliseconds(60)``
    The time between repeated steps while an arrow or the track is held. A
    value outside 16 to 500 milliseconds throws `std::invalid_argument`.
decrement_button_bounds
    The left or top arrow button, in the bar's coordinates.
increment_button_bounds
    The right or bottom arrow button, in the bar's coordinates.
track_bounds
    The track between the two buttons.
thumb_bounds
    The thumb. Its length is the track length times the large change over
    the range plus the large change, at least `minimum_thumb_extent`, and
    its position along the track follows the value.

Methods
-------
part_at
    Which part is under a point in the bar's coordinates: one of the
    buttons, the track before or after the thumb, the thumb, or
    `ScrollBarPart::none` outside the bar.
measure
    Uses the requested width and height where they are set; otherwise a
    horizontal bar asks for 180 by 18 logical pixels and a vertical one for
    18 by 180.

See Also
--------
HScrollBar : A scroll bar that starts horizontal.
VScrollBar : A scroll bar that starts vertical.
RangeControl : The range, value and events a scroll bar inherits.
TrackBar : A slider for choosing a value.
ScrollableControl : The built-in scroll bars of panels and containers.

Notes
-----
The value can reach the whole range from minimum to maximum: dragging the
thumb to the end of the track sets the maximum. The large change sets the
size of the thumb and the step of a track click; it does not reduce the
largest reachable value.

A primary press focuses the bar and captures the pointer. On an arrow it
steps the value by the small change, on the track by the large change
towards the press, and while the press is held the step repeats after
`initial_repeat_delay`, every `repeat_interval`, until the value reaches its
limit; a held track press also stops once the thumb reaches the pointer.
Dragging the thumb moves the value continuously. Releasing the button, or the bar losing focus, ends the
interaction. Each wheel event over the bar steps by the small change,
backwards for a positive vertical delta, whatever the bar's orientation; a
horizontal wheel delta is ignored.

With focus, Left and Up step back by the small change and Right and Down
forward, Page Up and Page Down step by the large change, and Home and End
go to the minimum and maximum.

All input raises the inherited `scroll` first, with the old and new value
and a `RangeAction` (`RangeAction::small_increment`,
`RangeAction::thumb_track` and so on), and then `value_changed`. Input that
would not change the value, such as an arrow press at the limit, raises
neither. `set_value` raises only `value_changed`. There is no separate event
when a thumb drag ends.

The bar is published to accessibility with the role `scroll_bar`, its
value as text and as a number with the range, the small and large change
as its steps, and the focus, increment, decrement (by the small change) and
set-value actions. Accessibility actions
raise `scroll` like other input; a set-value outside the range is clamped.

Examples
--------
A vertical bar for a 1000-pixel document shown 200 pixels at a time:

.. code-block:: cpp

   std::shared_ptr<gf::ScrollBar> bar = gf::make_control<gf::ScrollBar>(
       gf::StableId("document.vertical"), gf::Orientation::vertical);
   (*bar).set_requested_bounds({0.0, 0.0, 18.0, 240.0});
   (*bar).set_range(0.0, 800.0);    // the last 200 pixels are visible at 800
   (*bar).set_large_change(200.0);  // one page per track click, and the thumb size
   (*bar).set_small_change(20.0);   // one line per arrow click

Scroll your own drawing in response to the user, through an owned handler
on `scroll`, which reports input only:

.. code-block:: cpp

   struct Canvas final : gf::Component {
       std::shared_ptr<gf::ScrollBar> bar = gf::make_control<gf::ScrollBar>(
           gf::StableId("canvas.vertical"), gf::Orientation::vertical);
       double offset = 0.0;

       Canvas() { gf::on((*bar).scroll(), *this, &Canvas::on_scroll); }
       void on_scroll(const gf::RangeScrollEvent& event) { offset = event.new_value; }
   };

   void example() {
       Canvas canvas;
       (*canvas.bar).set_requested_bounds({0.0, 0.0, 18.0, 240.0});
       gf::Window window(canvas.bar, {18.0, 240.0});
       window.request_focus(canvas.bar);
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::page_down});
       // scroll reported a large increment: canvas.offset is 10
       (*canvas.bar).set_value(0.0);   // value_changed only; canvas.offset stays 10
   }
