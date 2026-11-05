A tree with tabular columns: nodes in a hierarchy, each with cells beside it.

A tree list view shows records described by `TreeListNode`: a stable ID,
the node's text, its cells (the further columns), its depth, its expansion
and an image key. The nodes are given in preorder with their depths, as
`TreeView`'s items are. Column 0 of the `TreeListColumn` headers is the tree
column: it indents each level and shows an expander on nodes that can be
expanded. Only the rows in view (nodes not hidden under a collapsed node)
are laid out, painted and described to accessibility, so a tree of a
million nodes costs a screenful. It is a GUI.Forms control; Windows Forms
has no equivalent.

Parameters
----------
stable_id
    Identity of the view, unique within its window. Its scroll bars are
    child controls named ``<stable_id>.vertical-scroll-bar`` and
    ``<stable_id>.horizontal-scroll-bar``.

Properties
----------
columns
    The columns, column 0 first. `set_columns` replaces them, resets the
    display order and throws `std::invalid_argument` for a repeated ID or one
    that breaks the `StableId` segment rule, invalid UTF-8, a width outside
    `minimum_column_width` and `maximum_column_width`, or more than
    `maximum_columns` columns. The view shows no rows without columns.
column_order
    The columns in display order: ``column_order()[position]`` is the column
    shown at that position. Position 0 always holds the tree column.
nodes
    The nodes in the order given, whatever the sort. `set_nodes` replaces
    them, keeps the selection and the current node on their IDs, and throws
    `std::invalid_argument` for a repeated ID or one that breaks the
    `StableId` segment rule, invalid UTF-8, an image key longer than 256
    bytes, more than `maximum_nodes` nodes, or a depth sequence that is not a
    preorder (the first node at depth 0, each next node at most one level
    deeper); the old nodes then stay.
row_count
    The number of rows: the nodes not hidden under a collapsed node.
top_row
    The first row in view. `set_top_row` scrolls a row to the top, clamped
    so the last page stays full.
horizontal_offset
    The sideways scroll, in logical units, clamped to the columns' width.
selected_id
    The selected node's ID, or none; ``selected_id.read()`` returns a copy.
    Writing ``selected_id = "id"`` selects a node (expanding its ancestors
    and scrolling it into view), ``selected_id = std::nullopt`` selects
    nothing, and a write throws `std::out_of_range` for an unknown node.
current_id
    The node with the keyboard's position and the focus cue, or none.
sort_column
    The column the siblings are sorted by, or none.
sort_order : default ``TreeListSortOrder::none``
    The order the siblings are shown in.
indentation : default ``18``
    The step of each tree level in logical units, also the width of the
    expander's slot, from `minimum_indentation` to `maximum_indentation`; a
    write outside them throws `std::invalid_argument`.
image_list
    The images shown before each node's text, looked up by `image_key`.
    Without it no icons are shown. A write throws `std::invalid_argument`
    for a list that is not alive or belongs to another window.
font
    The font of node, cell and header text.
realized_rows
    The visible rows' parts and states, in paint order.
realized_cells
    The visible cells past the tree column.
realized_columns
    The visible column headers, with their display positions.

Events
------
selection_changed
    Raised after the selected node changes, by the user or the program,
    including when a collapse moves the selection to the collapsed node or
    `set_nodes` drops the selected node. It carries the previous and current
    IDs.
expansion_changed
    Raised after a node is expanded or collapsed, by the user or the
    program. Expanding hidden ancestors raises it once per ancestor,
    outermost first, before the selection change that caused it.
item_activated
    Raised on a double click, on Enter, and on the accessibility press of a
    row.
sort_changed
    Raised after the sort changes: by a header click, the accessibility
    press of a header, or `set_sort`.
column_width_changed
    Raised when a divider drag ends with a new width, or `set_column_width`
    changes one.
column_moved
    Raised when a header drag or `move_column` moves a column to a new
    display position.

Methods
-------
set_column_width
    Sets one column's width in logical units. Throws `std::out_of_range` for
    a column past the end and `std::invalid_argument` for a width outside
    the bounds.
move_column
    Moves a column to a display position. Throws `std::out_of_range` for a
    column or position past the end and `std::invalid_argument` for the tree
    column or position 0.
index_of
    The model index of the node with an ID, or none.
set_cell_text
    Edits one cell (column 0 is the node's text). The siblings are sorted
    again when the view is sorted by that column. Throws `std::out_of_range`
    for an unknown node or a column past the end.
is_expanded
    Whether a node is expanded. Throws `std::out_of_range` for an unknown
    node.
set_expanded
    Expands or collapses one node; a node that cannot be expanded is left
    alone. Throws `std::out_of_range` for an unknown node.
expand_subtree
    Expands a node and every expandable node under it.
row_of
    The row showing a node, or none when it is hidden or unknown.
row_node_id
    The ID of the node a row shows. Throws `std::out_of_range` for a row
    past the end.
ensure_visible
    Expands a node's ancestors and scrolls it fully into view.
set_sort
    Orders every node's children, and the roots, by one column's text (ASCII
    case folded, then byte order; equal texts keep the order given), or
    returns to the order given with no column or
    `TreeListSortOrder::none`. The tree structure is kept and the model is
    not reordered. Throws `std::out_of_range` for a column past the end.
node_id_at
    The node under a point in window coordinates, or none.
initialize_control_tree
    Creates the scroll bars. `make_control` calls it; do not call it
    yourself.

See Also
--------
TreeView : A hierarchical list without columns.
ListView : A flat list with details columns.
ObjectView : A stable-ID object collection with icon and details projections.

Notes
-----
Selection (one node) and the current node are kept by node, not by row:
they stay on their node while rows move through expansion, collapse,
sorting and edits. A collapse that hides the selected or current node moves
it to the collapsed node, as `TreeView` does. A node with children can
always be expanded; `TreeListNode::expandable` shows the expander on a node
whose children are not loaded yet.

Nodes are records, not child controls, and every part is painted through
the by-role paint path (`paint_part`). The view publishes ``region``,
``well``, ``header``, ``vertical_scroll_bar``, ``horizontal_scroll_bar``,
``scroll_corner`` and ``focus``. Three item systems describe the visible
rows: ``row`` (``row``, ``row_face``, ``expander``, ``icon``, ``text`` and
``row_focus``, with the states selected, hot, current, open, closed,
disabled and focus_visible), ``cell`` (``cell`` and ``cell_text``, selected
and disabled) and ``column`` (named as ListView's and ObjectView's:
``column_header``, ``column_label``, ``sort_glyph``, ``column_divider`` and
``column_grip``, hot, pressed and captured, with the custom states
sorted_ascending and sorted_descending). The expander is a ``disclosure``
part showing the ``chevron_right`` or ``chevron_down`` mark. Row faces use
the theme's selection family, transparent until a row is hot or selected;
header cells use the menu-item family, flat at rest.

A click selects a row; a click on the expander expands or collapses without
selecting; a double click activates. Disabled nodes are not selected by the
pointer or the keyboard. A click on a header sorts by that column, ascending
first, then descending. Dragging a header (except the tree column's) moves
its column; dragging a divider resizes its column, and Escape during the
drag restores the width. The wheel scrolls three rows; with Shift, or
sideways, it scrolls the columns.

With focus, Up and Down move through the rows, Home and End go to the first
and last, Page Up and Page Down move a page. Right expands a collapsed node
or moves to the first child of an expanded one; Left collapses an expanded
node or moves to the parent. Keypad plus and minus expand and collapse, and
the asterisk expands everything under the current node. Control with Left
or Right scrolls the columns sideways. Enter activates. Typing the first
letters of a node's text selects the next row that starts with them.

The view publishes the accessibility role `tree_grid`, with the shown row
count and the column count. Its children are the visible column headers, as
`column_header` with their column index (counting from 1; a press sorts),
and the visible rows as `tree_item`, with their level (1 for a root), their
row index among the shown rows, the position among the siblings and the
sibling count in `position_in_set` and `set_size`, and the expanded state.
Each row holds a `cell` per visible column, named and valued with the cell
text. Rows offer the focus, select, press, and expand or collapse actions;
cells offer none.

Not supported yet: multiple selection, check boxes, label editing, owner
drawing, a virtual node source, an insertion mark while a header is
dragged, moving the tree column, and type-ahead over hidden nodes.

Examples
--------
A project tree with a size column, sorted by name within each folder:

.. code-block:: cpp

   std::shared_ptr<gf::TreeListView> tree =
       gf::make_control<gf::TreeListView>(gf::StableId("project"));
   (*tree).set_columns({
       {.stable_id = "name", .text = "Name", .width = 220.0},
       {.stable_id = "size", .text = "Size", .width = 80.0,
        .alignment = gf::TreeListAlignment::right},
   });
   (*tree).set_nodes({
       {.stable_id = "src", .text = "src", .depth = 0U, .expanded = true},
       {.stable_id = "main", .text = "main.cpp", .cells = {"4 KB"}, .depth = 1U},
       {.stable_id = "app", .text = "app.cpp", .cells = {"9 KB"}, .depth = 1U},
   });
   (*tree).set_sort(std::size_t{0U}, gf::TreeListSortOrder::ascending);
   // The rows are now src, app.cpp, main.cpp; the nodes keep their order.
