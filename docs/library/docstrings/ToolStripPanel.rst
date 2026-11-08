A band along one edge that holds ToolStrips.

A tool strip panel stacks the strips joined to it in rows: across a top or
bottom panel, or as columns down a left or right panel, whose strips turn
vertical (columns are called rows here too). A row holds one strip or
several side by side: each strip but the last takes its `content_length`,
no more than the row has left, and the last takes the rest, so a strip
alone fills its row. A row is as thick as its thickest strip, and the panel
as its rows together; it has no thickness without strips. A
`ToolStripContainer` owns four of them.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.
initial_edge : default ``ToolStripPanelEdge::top``
    The first `edge`.

Properties
----------
edge : default ``ToolStripPanelEdge::top``
    The side the panel lines. A write turns the joined strips to its
    orientation, and throws `std::logic_error` when a joined `StatusStrip`
    would have to turn vertical.
strips
    The joined strips, in order, row by row.
rows
    The joined strips by row, each row in order along the panel.
row_count
    How many rows the panel has.

Methods
-------
join
    Adds a strip after the others, turns it to the panel's orientation and
    takes ownership of it. Throws `std::invalid_argument` for a null strip or
    one that already has a parent, and `std::logic_error` for a `StatusStrip`
    on a left or right panel.
insert
    Joins a strip as `join` does, in a new row at a row index (0 first, the
    row count last). Throws `std::out_of_range` for a row past the row
    count, before anything changes.
join_row
    Joins a strip as `join` does, into an existing row at a position among
    its strips (0 first, their count last). Throws `std::out_of_range` for a
    row or position past those, before anything changes.
remove
    Removes a joined strip and returns it; null when the strip is not joined
    to this panel. A row it leaves empty goes, and the strip after it starts
    the row when it started it.

See Also
--------
ToolStripContainer : Four panels around a content panel.
ToolStrip : The strips a panel holds.

Notes
-----
The panel paints its ``face`` through the by-role paint path and publishes
``region`` and ``face``. Strips are dragged between rows and panels by the
`ToolStripContainer` that owns the panel. The panel is not exposed to accessibility itself; its strips are.

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
