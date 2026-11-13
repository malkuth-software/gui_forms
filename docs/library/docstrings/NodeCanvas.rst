An editor over a node graph: nodes with ports and lamps, joined by links,
on an unbounded canvas that pans and zooms deeply.

The canvas borrows a `NodeGraph`; it owns its view, its selection and its
caches, which are keyed on the graph's revision, so a lamp change repaints
without rebuilding them. It finds what a point, a rectangle or a stroke
meets through two spatial indexes (nodes and links), and paints only what
lies in view, at a level of detail chosen from the zoom with hysteresis:
an overview of plain rectangles, silhouettes, compact nodes with ports, and
full nodes with captions, port labels and the application's own body
drawing. Every colour, line and the grid come from the theme's canvas look
(`canvas_look.hpp`, the `node-canvas` parts): a `blue`, `green` or `grey`
class picks a grid variant. It is a GUI.Forms control; Windows Forms has no
equivalent.

Parameters
----------
stable_id
    Identity of the canvas, unique within its window. Its nodes are
    published to accessibility as ``<stable_id>.node-<slot>-<generation>``,
    their ports as ``<node>.port-<n>``, followed by a live region named
    ``<stable_id>.announcements``.

Properties
----------
graph
    The graph shown and edited, or none. The canvas does not own it: the
    graph must outlive the canvas or be replaced first. Replacing it cancels
    a gesture or an edit in progress.
callbacks
    The application's targets: ``check_link`` answers whether two socket
    types may be linked (equal types by default), ``edit`` supplies the
    editor a double-click or F2 opens, and ``paint_body`` draws a node's
    body at full detail.
view
    The view transform: the world point at the top-left corner and the
    zoom, held between 1/64 and 8. Mouse wheel zooms about the pointer;
    the middle button, or Space with the primary button, pans.
detail_level
    The level of detail the last frame painted.
link_style
    ``soft`` curves leave and enter along each port's edge; ``routed`` links
    take the link router's obstacle-avoiding routes.
snap_to_grid
    Whether moves snap to the grid (on by default; Ctrl inverts it during a
    drag).
selection
    The selected nodes, or a selected link. A press selects, Shift adds,
    Ctrl toggles; a drag on empty canvas draws a marquee that selects the
    nodes it contains, or with Alt those it touches.
focused_node
    The keyboard's current node.
focused_port
    The current port inside the focused node, or none.
announcement
    The live region's current text.
flow_running
    Whether the flow is on the window's motion clock now.
link_preview_state
    The dragged link's preview state: seeking, accepted by the input that
    captured it, or refused by the port under it; none without a drag.
statistics
    Work counters: frames, nodes, links and ports painted, captions drawn
    and text too small to draw, index builds and in-place moves, gestures,
    connections, cuts and suppressed clicks.

Events
------
selection_changed
    Raised after the selection changes.
link_connected
    Raised after a link drag or a keyboard link connects two ports.
graph_edited
    Raised after an operation of the canvas changed the graph: a move, a
    connection or disconnection, a removal, an undo or redo, an edit's end.
view_changed
    Raised after the view transform changes.
palette_requested
    Raised after a double-click on empty canvas, Shift+F10, or a link drag
    released off every port; the request names where a new node would go
    and, after a link drag, the output it would connect to.
edit_ended
    Raised after an edit ends, with whether it was committed or cancelled.

Methods
-------
grid_spacing
    The grid's spacing in world units, which is also the snap cell of a
    move: the canvas's own when set, else the theme's (16 in the GUI.Forms
    theme).
set_grid_spacing
    Sets the canvas's own spacing; refuses one that is not finite or is
    under one unit.
clear_grid_spacing
    Returns to the theme's spacing.
canvas_look
    The theme's look the canvas paints from, resolved again only when its
    theme, colour scheme, contrast, classes or `back_color` change.
set_link_flowing
    Marks a link as carrying data: while it is shown, the theme's flow marks
    travel along it (the link part's `flow`), on the window's motion clock
    only while motion is neither reduced nor switched off. Refuses a stale
    link; a removed link stops flowing.
link_flowing
    Whether a link is marked flowing.

Notes
-----
A gesture is one undo step: a move drag, a detach and its reconnection, an
edit from its opening to its commit. Escape during a gesture, or a
cancelled edit, rolls its changes back. The release that ends a drag is no
click, so a drag of several selected nodes keeps them selected; a press and
release without a drag narrows the selection to the node clicked.

The keyboard: arrows go to the nearest node in their direction, Ctrl+arrows
move the selection one grid cell, Enter walks into a node's ports, L links
from the current output (arrows or a title's first letter choose the
input, Enter connects), Delete removes the selection or a focused input's
link, Ctrl+Z and Ctrl+Shift+Z undo and redo, Home frames the graph and F
the selection.

Each node, port and link is an item of the theme's `:item()` rules: a
node's ID is its key, a port's is `<node key>.<port name>` with the class
`socket-<socket type>`, and a link's is its key with its output's socket
class, so a theme can colour ports and links by data type. Keyboard focus
is drawn as the theme's focus ring around the focused node or port.

A subclass may override the protected raisers (`on_selection_changed`,
`on_link_connected`, `on_graph_edited`, `on_view_changed`,
`on_palette_requested`, `on_edit_ended`) and call the base, which raises
the event; the C and C# class tables reach the same raisers.

Accessibility has no flow relations yet: an input port names its source in
its text.
