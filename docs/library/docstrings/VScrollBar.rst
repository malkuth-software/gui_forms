A scroll bar that runs vertically.

VScrollBar is a `ScrollBar` created with `Orientation::vertical`, for
content your own control scrolls up and down. Everything else, including
input, events and accessibility, is described under `ScrollBar` and
`RangeControl`. To scroll child controls, turn on `auto_scroll` on a `Panel`
instead.

Parameters
----------
stable_id
    Identity of the scroll bar, unique within its window.

See Also
--------
ScrollBar : The behaviour and properties of every scroll bar.
HScrollBar : The horizontal scroll bar.

Notes
-----
The orientation is only where the bar starts: the inherited
`set_orientation` can still turn it horizontal. Without a requested size it
asks for 18 by 180 logical pixels. Up and Down step it from the keyboard, as
do Left and Right, and the mouse wheel moves it: a positive vertical wheel
delta steps it back towards the minimum.

Examples
--------
A vertical bar beside a list that shows 12 of 100 rows, bound to the
application's scroll position:

.. code-block:: cpp

   gf::Value<double> first_row(0.0);
   std::shared_ptr<gf::VScrollBar> bar =
       gf::make_control<gf::VScrollBar>(gf::StableId("rows.vertical"));
   (*bar).set_requested_bounds({300.0, 0.0, 18.0, 240.0});
   (*bar).set_range(0.0, 88.0);     // the last 12 rows are visible at 88
   (*bar).large_change = 12.0;
   (*bar).bind(first_row);          // the bar and first_row now follow each other
   first_row.set(40.0);             // the thumb moves to row 40
