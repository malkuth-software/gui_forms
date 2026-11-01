A band along one edge that holds ToolStrips.

A tool strip panel stacks the strips joined to it. A top or bottom panel
puts each strip in its own row across the panel; a left or right panel
turns its strips vertical and puts each in its own column. The panel is as
thick as its strips together and has no thickness without them. A
`ToolStripContainer` owns four of them.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.
edge : default ``ToolStripPanelEdge::top``
    The side the panel lines.

Properties
----------
edge
    The side the panel lines. `set_edge` turns the joined strips to its
    orientation, and throws `std::logic_error` when a joined `StatusStrip`
    would have to turn vertical.
strips
    The joined strips, in order.

Methods
-------
join
    Adds a strip after the others, turns it to the panel's orientation and
    takes ownership of it. Throws `std::invalid_argument` for a null strip or
    one that already has a parent, and `std::logic_error` for a `StatusStrip`
    on a left or right panel.
insert
    Joins a strip as `join` does, at a row of the joined strips (0 first,
    their count last). Throws `std::out_of_range` for a row past that count,
    before anything changes.
remove
    Removes a joined strip and returns it; null when the strip is not joined
    to this panel.

See Also
--------
ToolStripContainer : Four panels around a content panel.
ToolStrip : The strips a panel holds.

Notes
-----
The panel paints its ``face`` through the by-role paint path and publishes
``region`` and ``face``. Strips do not share rows yet: each joined strip
takes the whole length of the panel, and strips cannot be dragged between
panels. The panel is not exposed to accessibility itself; its strips are.

Examples
--------
Two strips in one top panel:

.. code-block:: cpp

   std::shared_ptr<gf::ToolStripPanel> panel =
       gf::make_control<gf::ToolStripPanel>(gf::StableId("tools.panel"));
   std::shared_ptr<gf::ToolStrip> file =
       gf::make_control<gf::ToolStrip>(gf::StableId("tools.file"));
   std::shared_ptr<gf::ToolStrip> edit =
       gf::make_control<gf::ToolStrip>(gf::StableId("tools.edit"));
   (*panel).join(file);
   (*panel).join(edit);
