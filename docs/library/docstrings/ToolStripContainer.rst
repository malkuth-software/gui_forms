Four ToolStripPanels around a content panel.

A tool strip container frames a window's content with strip panels on its
top, bottom, left and right. The top and bottom panels span the width and
are as tall as their strips; the left and right panels fill the height
between them and are as wide as their (vertical) strips; the content panel
takes the rest. Join strips to a panel and put the window's controls in the
content panel.

Strips are dragged by their grips with the docking drag's mechanics: a press
on a grip that moves 4 units drags the strip, whose stamp (its picture,
shrinking to a miniature at the pointer) follows the pointer while a line
marks where it would go in the shown panel under the pointer: between rows
for a new row, or across a row between its strips to join it (the middle
half of a row's thickness joins it). A release
there moves the strip and turns it to that panel's orientation. Strips never
leave the container, enter dock stacks or float: a release elsewhere, with
Ctrl held, after Escape or when the capture is lost leaves the strip where
it was. A `StatusStrip` has no left or right target. An empty panel's band
reaches 12 units in from its edge, so a strip can be dropped on it.

Parameters
----------
stable_id
    Identity of the container, unique within its window. Its panels are
    child controls named ``<stable_id>.top``, ``.bottom``, ``.left``,
    ``.right`` and ``.content``.

Properties
----------
top_panel
    The top `ToolStripPanel`. `bottom_panel`, `left_panel` and `right_panel`
    are the others.
content_panel
    The `Panel` between the strip panels.
top_panel_visible : default ``true``
    Whether the top panel is shown. A hidden panel takes no room and hides
    its strips. `bottom_panel_visible`, `left_panel_visible` and
    `right_panel_visible` do the same for the others.
drag_phase
    Where a strip drag stands: ``idle``, ``armed`` (pressed on a grip, not
    yet moved the threshold), ``dragging`` or ``settling`` (the stamp moving
    into the strip's place).
dragged_strip
    The strip being dragged, or null.
drag_target
    Where a release would put the strip: a `ToolStripDropTarget` with the
    panel's edge, the row (rows counted without the dragged strip), whether
    it joins that row and at which position, and the insertion line; none
    when nothing accepts.
drag_stamp
    The stamp's rectangle while dragging or settling; empty otherwise.
drag_suppressed
    Whether Ctrl is suppressing the targets.
drag_threshold : default ``4``
    How far the pointer moves from a grip press before it drags. A host may
    set 1 to 64; `std::out_of_range` otherwise.

Events
------
strip_moved
    Raised after a strip changed panel or row, by a drag or by `move_strip`,
    with a `ToolStripMove`: the strip's identity and its edge, row and
    position in the row before and after.

Methods
-------
move_strip
    Moves a strip joined to one of the panels into a new row of the panel on
    an edge (rows counted without the strip), as a drop would, and raises
    `strip_moved`. True when the strip is
    there afterwards (also when it already was, which raises nothing); false,
    changing nothing, for a strip not joined here, an invalid edge, a row
    past the end, or a `StatusStrip` sent to the left or right. A drag under
    way is cancelled first.
move_strip_into_row
    As `move_strip`, into an existing row at a position among its strips;
    false also for a position past the row's strips.
cancel_drag
    Ends an armed drag, or a drag under way as Escape does; false when there
    is none.

See Also
--------
ToolStripPanel : One band of strips.
ToolStrip : The strips the panels hold.
StatusStrip : A strip for the bottom panel.

Notes
-----
The container paints nothing of its own; its panels and content do. It
publishes ``region`` and the composed parts ``top_panel``,
``bottom_panel``, ``left_panel``, ``right_panel`` (each a
``tool_strip_panel``) and ``content_panel`` (a ``panel``); an empty or
hidden panel is absent. While a strip is dragged it also publishes
``drag_stamp`` and ``drag_stamp_image`` (the stamp's card face and the
strip's picture) and, while a target is chosen, ``drop_marker`` (the
insertion line, ``drop_accepted``), painted on a layer above the panels. The content panel comes first among the children,
so Tab reaches the content before the strips. It is a group to
accessibility, exposed when it has an accessible name.

Keyboard move: the docking ``move_panel`` chord of the host key map
(Ctrl+Alt+Shift+M by default), pressed with the focus in a strip, starts
a keyboard move of that strip; ``begin_keyboard_move`` starts one
directly and ``keyboard_moving`` reports one in progress. Arrows across
the panel step between a new row before each row and joining a row;
arrows along a joined row step between positions; Tab and Shift+Tab step
between the four panels; Enter drops the strip at the marked place and
Escape cancels. Pointer input is ignored meanwhile, and the focus returns
to the strip when the move ends.

Not supported yet: rafting panels outside a container.

Examples
--------
A frame with a top strip and a status strip:

.. code-block:: cpp

   std::shared_ptr<gf::ToolStripContainer> frame =
       gf::make_control<gf::ToolStripContainer>(gf::StableId("frame"));
   std::shared_ptr<gf::ToolStrip> tools =
       gf::make_control<gf::ToolStrip>(gf::StableId("frame.tools"));
   std::shared_ptr<gf::StatusStrip> status =
       gf::make_control<gf::StatusStrip>(gf::StableId("frame.status"));
   (*(*frame).top_panel()).join(tools);
   (*(*frame).bottom_panel()).join(status);
