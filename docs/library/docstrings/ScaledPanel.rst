A panel that places its children in a fixed design space and scales them with the panel.

You give a scaled panel a design size, such as 100 by 100, and give each
child a rectangle in that space with `add_at`. At every layout the panel
stretches the design space over its own bounds, so each child keeps its
proportional position and size as the panel is resized. Use it for fixed
compositions, such as a diagram, an instrument face or a mock screen, that
must keep their proportions without per-child anchors. Use a
`GridPanel` instead when children must keep their natural size, and
`ScaledGroupBox` for the same layout inside a captioned frame.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.
initial_design_size
    The width and height of the design space, by default 1 by 1. Both must
    be finite and positive, or the constructor throws
    `std::invalid_argument`.

Properties
----------
design_size : default ``{1.0, 1.0}``
    The width and height of the design space. Both must be finite and
    positive, or `std::invalid_argument` is thrown.

Methods
-------
add_at
    `add_at(child, design_bounds)` adds `child` to the panel and gives it a
    rectangle in design coordinates. The position may be negative, so a
    child can overhang the panel; the width and height must be finite and
    nonnegative. A null child or invalid bounds throw
    `std::invalid_argument`.
set_design_bounds
    Moves or resizes an existing child in design coordinates. Invalid bounds
    throw `std::invalid_argument`, and a control that is not a direct child
    of the panel throws `std::logic_error`. A child added with plain
    `add_child` gains a design rectangle this way.
design_bounds
    The design rectangle of `child`, or no value if it has none. The
    rectangles of removed children are discarded at the next layout.

See Also
--------
ScaledGroupBox : The same scaled layout inside a captioned group box.
Panel : Absolute placement with Dock and Anchor.
GridPanel : A grid of rows and columns that keeps natural sizes.

Notes
-----
The horizontal and vertical scale factors are independent: a child's
rectangle is multiplied by the panel's width divided by the design width,
and by its height divided by the design height. A design that must keep its
aspect ratio therefore needs a panel with the same aspect ratio.

The scaled rectangle is the child's layout slot: margins, Dock and Anchor
are not applied, and the child's own `requested_bounds` are never rewritten.
A child without a design rectangle is not scaled and stays at its requested
bounds. Changing the design size or a design rectangle takes effect at the
next layout pass.

The panel publishes the accessibility role `group` only when it has an
accessible name or description.

Examples
--------
Place a button in a 100 by 100 design space. When the panel is 200 wide and
100 high, the button's design rectangle doubles in width only:

.. code-block:: cpp

   std::shared_ptr<gf::ScaledPanel> face =
       gf::make_control<gf::ScaledPanel>(gf::StableId("face"), gf::Size{100.0, 100.0});
   std::shared_ptr<gf::Button> start =
       gf::make_control<gf::Button>(gf::StableId("face.start"), "Start");
   (*face).add_at(start, {10.0, 20.0, 30.0, 40.0});

   gf::Window window(face, {200.0, 100.0});
   window.perform_layout();
   // start is arranged at {20, 20, 60, 40}

   (*face).set_design_bounds(*start, {20.0, 10.0, 40.0, 20.0});
   window.perform_layout();
   // and now at {40, 10, 80, 20}
