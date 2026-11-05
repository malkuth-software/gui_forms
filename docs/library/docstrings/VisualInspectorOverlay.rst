A transparent diagnostic layer that outlines one control's bounds and clip.

A visual inspector overlay draws three outlines around a target control,
found by stable ID: blue for its arranged bounds, magenta for its visual
bounds, which include focus rings, shadows and other decoration drawn
outside the layout, and gold for its effective clip, the part its ancestors
let it show. It paints on the overlay plane, never receives the pointer and
is hidden from assistive technology, so it can cover a whole page without
changing how the page works. Pair it with a `VisualInspectorView`, which lists
the same values as text.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
target_stable_id
    The stable ID of the control to outline, empty by default.

Properties
----------
target_stable_id
    The stable ID of the outlined control. Nothing is drawn while it is
    empty, while no control in the window has it, or when it names the
    overlay itself.

Methods
-------
start_polling
    `start_polling(FrameInterval interval)` polls the inspected target every
    `interval` on a paint-gated `AnimationTimer` while the overlay is shown, and
    repaints only when what it shows has changed, so a settled overlay costs
    one inspection snapshot a poll and no paint. A hidden overlay or covered
    window holds no wake. An interval below 1 ms throws
    `std::invalid_argument`.
stop_polling
    Stops polling.
refresh
    Repaints the overlay from a new snapshot. Call it after the target moves
    or changes size.

See Also
--------
VisualInspectorView : Lists the target's layout, state and paint record.
DrawingSurface : A control that draws through a paint callback.

Notes
-----
The overlay draws only within its own bounds, clipped by its ancestors, so
give it bounds that cover the target, typically those of the page or of the
window's root. It converts the target's window coordinates into its own, so
it may sit anywhere in the tree.

The overlay does not observe its target: it reads a snapshot of the window
each time it paints. Call `refresh` when the target changes, or keep it live
with `start_polling`. Hide it by writing `visible = false` when it is not
needed.

Examples
--------
Outline a button with an overlay that covers the whole window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("lab"));
   std::shared_ptr<gf::Button> apply =
       gf::make_control<gf::Button>(gf::StableId("lab.apply"), "Apply");
   std::shared_ptr<gf::VisualInspectorOverlay> overlay =
       gf::make_control<gf::VisualInspectorOverlay>(gf::StableId("lab.overlay"), "lab.apply");
   (*apply).set_requested_bounds({20.0, 20.0, 96.0, 32.0});
   (*overlay).set_requested_bounds({0.0, 0.0, 320.0, 120.0});
   (*root).add_child(apply);
   (*root).add_child(overlay);
   gf::Window window(root, gf::Size{320.0, 120.0});   // presses still reach the button
   window.perform_layout();

   (*apply).set_requested_bounds({40.0, 20.0, 96.0, 32.0});
   window.perform_layout();
   (*overlay).refresh();   // redraw the outlines at the button's new place
