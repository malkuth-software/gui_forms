A scroll bar that runs horizontally.

HScrollBar is a `ScrollBar` created with `Orientation::horizontal`, for
content your own control scrolls from side to side. Everything else,
including input, events and accessibility, is described under `ScrollBar`
and `RangeControl`. To scroll child controls, turn on `auto_scroll` on a
`Panel` instead.

Parameters
----------
stable_id
    Identity of the scroll bar, unique within its window.

See Also
--------
ScrollBar : The behaviour and properties of every scroll bar.
VScrollBar : The vertical scroll bar.

Notes
-----
The orientation is only where the bar starts: the inherited
`set_orientation` can still turn it vertical. Without a requested size it
asks for 180 by 18 logical pixels. Left and Right step it from the keyboard,
as do Up and Down, and a vertical wheel turn moves it.

Examples
--------
A horizontal bar below a 600-pixel-wide view of a 2000-pixel-wide image:

.. code-block:: cpp

   std::shared_ptr<gf::HScrollBar> bar =
       gf::make_control<gf::HScrollBar>(gf::StableId("image.horizontal"));
   (*bar).set_requested_bounds({0.0, 400.0, 600.0, 18.0});
   (*bar).set_range(0.0, 1400.0);    // the right edge is visible at 1400
   (*bar).large_change = 600.0;
   (*bar).small_change = 40.0;
