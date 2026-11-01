A list of items shown as details rows, a list, small icons, large icons or tiles.

A list view shows records described by `ListViewItem`: a stable ID, the
item's text, its sub-items (the further columns of the details view), an
image key and a checked state. The details view shows `ListViewColumn`
headers above the rows; the list, small-icon, large-icon and tile views show
one cell per item. Only the visible items are laid out, painted and described to
accessibility. In virtual mode the application keeps the items and the view
asks a `ListViewItemSource` for the visible ones only, so a list of millions
of rows costs a screenful.

Parameters
----------
stable_id
    Identity of the view, unique within its window. Its scroll bars are
    child controls named ``<stable_id>.vertical-scroll-bar`` and
    ``<stable_id>.horizontal-scroll-bar``; the label editor, made on the
    view's first label edit, is ``<stable_id>.label-editor``.

Properties
----------
view : default ``ListViewView::details``
    How the items are arranged: ``details``, ``list`` (columns filled top to
    bottom, scrolled sideways), ``small_icon``, ``large_icon`` or ``tile``
    (rows filled left to right). A tile shows the large icon at its left
    and, beside it, the item's text over one line for each column after the
    first whose sub-item is not empty, as many as the tile's height has
    room for; the block of lines is centred on the tile.
columns
    The details view's columns. `set_columns` replaces them and throws
    `std::invalid_argument` for a repeated ID or one that breaks the
    `StableId` segment rule, invalid UTF-8, a width outside
    `minimum_column_width` and `maximum_column_width`, or more than
    `maximum_columns` columns. The details view shows nothing without columns.
items
    The items of an ordinary view; empty in virtual mode. `set_items`
    replaces them, keeps the selection, the focused item and the anchor on
    their items by stable ID, and throws `std::invalid_argument` for a
    repeated ID or one that breaks the `StableId` segment rule or invalid
    UTF-8, and `std::logic_error` in virtual mode.
item_count
    The number of items: the ordinary items, or the count the source last
    reported.
virtual_mode
    Whether a `ListViewItemSource` supplies the items.
virtual_source
    The source, or null. `set_virtual_source` drops the ordinary items,
    clears the selection and reads the source's count.
groups
    The groups (`ListViewGroup`: an ID, a header and its alignment). Items
    name their group in `ListViewItem::group`. `set_groups` throws
    `std::invalid_argument` for a repeated ID or one that breaks the
    `StableId` segment rule, invalid UTF-8, an unknown alignment, or more than
    `maximum_groups` groups.
show_groups : default ``true``
    Whether the view shows its items in groups while it has groups. Groups
    are not shown in the list view or in virtual mode.
default_group_header : default ``"Default"``
    The header of the default group, which holds the items whose group is
    empty or names no group. Invalid UTF-8 throws `std::invalid_argument`.
grouped
    Whether the items are shown in groups now.
check_boxes : default ``false``
    Whether each item shows a check box.
multi_select : default ``true``
    Whether more than one item can be selected. Turning it off keeps one
    selected item.
full_row_select : default ``false``
    Whether the details view highlights the whole row rather than the first
    column's text.
selected_count
    The number of selected items.
selected_indices
    The selected items' indices, ascending.
focused_index
    The item with the focus cue and the keyboard's position, or none.
    `set_focused_index` moves it and scrolls the item into view.
top_index
    The first item laid out in the visible area.
horizontal_offset
    The details view's sideways scroll, in logical units.
sort_column
    The column showing the sort glyph, or none.
sort_order : default ``ListViewSortOrder::none``
    The order the sort glyph shows.
small_image_list
    The images of the details, list and small-icon views, looked up by
    each item's `image_key`. Without it those views show no icons.
large_image_list
    The images of the large-icon and tile views.
font
    The font of item and header text.
tile_size : default empty (automatic)
    The tile view's tile, in logical units at text scale 1 (Windows Forms'
    `TileSize`). An empty size makes tiles as wide as the large icon and
    168 units of text, and as tall as the icon or three lines of text,
    whichever is taller. `set_tile_size` throws `std::invalid_argument`
    unless both sides are zero, or both are finite and between
    `minimum_tile_side` and `maximum_tile_side`.
draw_mode : default ``ItemDrawMode::normal``
    With ``ItemDrawMode::owner_draw`` the application draws the items'
    content (Windows Forms' `OwnerDraw`): see `draw_item_content`. Throws
    `std::invalid_argument` for an unknown mode.
insertion_mark : default none
    Where the insertion mark stands, as an item's index and whether the mark
    is after it, or none. See `show_insertion_mark`.
label_edit : default ``false``
    Whether the user may edit items' labels in place (Windows Forms'
    `LabelEdit`): F2 on the focused item begins an edit. Turning it off
    cancels an edit in progress.
label_edit_index
    The item in edit, or none.
label_editor
    The `TextBox` of a label edit, or null before the view's first edit.
realized_items
    The visible items' parts and states, in paint order.
realized_sub_items
    The visible details cells past the first column.
realized_columns
    The visible column headers.
realized_groups
    The visible group header lines: each group's band, caption and rule.

Events
------
draw_item_content
    Raised while the view paints in the owner-draw mode, with an
    `ItemContentPaint`: once for each painted item's own content (column 0,
    its icon and text area) and, in the details view, once for each visible
    sub-item cell (its column), and in the tile view once for each sub-item
    line shown (its column), in paint order (Windows Forms' `DrawItem` and
    `DrawSubItem`). The painter is clipped to the content rectangle; the
    item's plate, check box and focus cue are already painted by role.
    `text` and `font` are the default content's. Set `draw_default` to add
    the default content. Column headers are not owner-drawn. A handler
    paints only; one that changes the items, their order, the columns or
    the view, or disposes the view, ends that paint, and the view is painted
    again.
selection_changed
    Raised after the set of selected items changes, by the user or the
    program. It carries the selected count and the focused item; read the
    indices with `selected_indices`.
item_activated
    Raised on a double click, on Enter, and on the accessibility press of an
    item in a view without check boxes.
item_checking
    Raised before the user toggles a check box. A handler may cancel the
    change or alter its new value. In virtual mode the handler stores the new
    value in the application's data.
item_checked
    Raised after a check box changes, by the user or by `set_item_checked`.
column_click
    Raised when a column header is clicked or pressed through accessibility.
    The view does not sort.
column_width_changed
    Raised when a divider drag ends with a new width, or `set_column_width`
    changes one.
before_label_edit
    Raised before an item enters label edit, with its index and ID and no
    label. A handler may cancel, and the item stays out of edit.
after_label_edit
    Raised once for each ended label edit, after the editor has closed, with
    the edited text as the label, or no label when the edit was cancelled. A
    handler may cancel to refuse the text, and the item keeps its text;
    otherwise the label becomes an ordinary item's text. In virtual mode the
    handler stores the label in the application's data.

Methods
-------
set_column_width
    Sets one column's width in logical units. Throws `std::out_of_range` for
    a column past the end and `std::invalid_argument` for a width outside
    the bounds.
index_of
    The index of the item with an ID in an ordinary view; none in virtual
    mode.
refresh_virtual_items
    Re-reads the source's count after the application changed its data,
    keeps the selection, focus and anchor below the new count and repaints.
    Throws `std::logic_error` outside virtual mode.
item_checked
    Whether an item is checked. `set_item_checked` changes an ordinary item
    and throws `std::logic_error` in virtual mode, where the source owns the
    state.
is_selected
    Whether an item is selected. `set_selected` selects or deselects one
    item; in single-selection mode selecting an item deselects the rest.
select_all
    Selects every item (one item in single-selection mode).
clear_selection
    Deselects every item.
ensure_visible
    Scrolls until an item is fully visible.
set_sort_indicator
    Shows the sort glyph on one column, or on none. It never reorders items.
sort_items
    A helper for a column_click handler: stable-sorts an ordinary view's
    items by one column's text (ASCII case folded, then byte order) and shows
    the sort glyph. The selection and focus follow their items. Throws
    `std::logic_error` in virtual mode.
item_index_at
    The item under a point in window coordinates, or none.
begin_label_edit
    Puts an item in label edit (Windows Forms' `ListViewItem.BeginEdit`),
    after committing any other edit: raises `before_label_edit`, scrolls the
    item into view and places the editor, with the item's text selected,
    over its ``text`` part. Returns false when `label_edit` is off, the view
    does not show the item, or a handler cancelled or changed the items.
    Throws `std::out_of_range` for an index past the end.
end_label_edit
    Commits the edit in progress, if any.
cancel_label_edit
    Cancels the edit in progress, if any.
show_insertion_mark
    Shows the insertion mark, the bar an application shows in the gap where
    a dragged item would drop: before or after an item, which is above or
    below it in the details and list views and left or right of it in the
    icon views. It is painted by the theme's drop role and published as the
    `insertion_mark` part. Any change to the items or their order clears
    it. Returns false, clearing any mark, for an index past the items.
clear_insertion_mark
    Removes the insertion mark.
insertion_mark_at
    The mark nearest a point in window coordinates (Windows Forms'
    `InsertionMark.NearestIndex`), with the point clamped into the item
    area: the item under or nearest it, and after it when the point is past
    the item's middle across the gap. None when no item is shown.

See Also
--------
ObjectView : A stable-ID object collection with icon and details projections.
CheckedListBox : A single-column list with three-state check boxes.
ListBox : A single-column list.
TreeView : A hierarchical list.

Notes
-----
When the pointer is over an item whose text its cell cuts off, in the
details view (any column) or the list view, the whole text shows in place
over the row, as wide as it needs and in the row's own states. Nothing needs
to be set: items have no tooltips, and the reveal is not a tooltip, so the
tooltip switches do not affect it. It ignores the pointer, and a press,
scroll, selection or item change hides it until the pointer moves again.
Owner-drawn content is never revealed.

Items are records, not child controls, and every part is painted through the
by-role paint path (`paint_part`). The view publishes ``region``, ``well``,
``header``, ``vertical_scroll_bar``, ``horizontal_scroll_bar``,
``scroll_corner``, ``focus``, ``insertion_mark`` and ``label_editor``. Three item systems describe the visible
items: ``item`` (``cell``, ``item_face``, ``check_well``, ``check``,
``icon``, ``text`` and ``cell_focus``, with the states selected, hot,
current, checked, disabled and focus_visible), ``sub_item`` (``sub_item_cell``
and ``sub_item_text``, selected and disabled) and ``column`` (named as
ObjectView's: ``column_header``, ``column_label``, ``sort_glyph``,
``column_divider`` and ``column_grip``, hot, pressed and captured, with the
custom states sorted_ascending and sorted_descending). Item faces use the
theme's selection family, which is transparent until an item is hot or
selected; header cells use the menu-item family, flat at rest.

A click selects an item; Control-click toggles it and Shift-click selects
the range from the anchor (Control and Shift adds the range). A click on a
check box toggles it without changing the selection. Dragging a header's
divider resizes its column; Escape during the drag restores the width. The
wheel scrolls three lines; with Shift, or sideways, it scrolls the details
view's columns.

With focus, the arrows move through the items as the view lays them out
(Left and Right scroll the details view sideways), Home and End go to the
first and last item, Page Up and Page Down move a page, Shift extends the
selection and Control moves the focus without selecting. Space toggles the
focused item's check box (or selects it without check boxes); Control and
Space toggles its selection; Control and A selects everything; Enter
activates. Typing the first letters of an item's text selects the next
matching item; virtual views do not search yet.

With `label_edit` on, F2 begins editing the focused item's label in a
`TextBox` over its text. So does a slow second click, as in Windows: a
single primary press without modifiers on the text of the item that was
already focused and selected begins its edit once the window's double-click
interval (`PresentationSettings::double_click_milliseconds`) passes. A second
press within the interval is a double click and activates the item instead;
a key, the wheel, another press, a change to the items, focus leaving the
view or turning `label_edit` off in the wait drops the slow click.

Enter, a press elsewhere in the view, or focus leaving the editor commits
the edit; Escape cancels it. Replacing or sorting the items, a new virtual
source or `refresh_virtual_items` cancels an edit once the change is made.
Scrolling keeps the edit: the editor follows its item, at no size while the
item is out of view.

In the details view the control publishes the accessibility role `table`,
with the item count and the column count; in the other views it publishes
`list`. It is `multiselectable` while `multi_select` is on. In the details
view its children are the visible column headers, as `column_header` with
their column index (counting from 1), and the visible items as `row`, with
their row index and a `cell` child per visible column that holds the
sub-item text. In the other views the items are `list_item`, or
`check_list_item` with check boxes. Items with check boxes are `checkable`.
The select action selects an item; press activates it, or toggles a
check-box item.

In a grouped view each group that holds items has a header line: its
header in bold, aligned as the group asks, over a rule across the view.
The default group comes first, then the groups in their order; a group
without items is not shown. A group always starts a new line, and the
items keep their model order within it. Up and Down move to the item line
above or below, across headers, and Left and Right through the shown
order in the icon and tile views; a Shift range, Home, End and type-ahead
follow the shown order. Accessibility publishes each shown group as a
``group`` node, named by its header, holding its items; details rows count
in the shown order. Groups do not collapse, and group footers, subtitles
and task links are not provided.

Not supported yet: item tooltips, hot-tracking activation, column
reordering, type-ahead in virtual mode, and wrapping large-icon labels
over two lines.

Examples
--------
A details view whose header click sorts:

.. code-block:: cpp

   std::shared_ptr<gf::ListView> files =
       gf::make_control<gf::ListView>(gf::StableId("files"));
   (*files).set_columns({
       {.stable_id = "name", .text = "Name", .width = 200.0},
       {.stable_id = "size", .text = "Size", .width = 80.0,
        .alignment = gf::ListViewAlignment::right},
   });
   (*files).set_items({
       {.stable_id = "a", .text = "notes.txt", .sub_items = {"4 KB"}},
       {.stable_id = "b", .text = "logo.png", .sub_items = {"12 KB"}},
   });
   (*files).sort_items(0U, gf::ListViewSortOrder::ascending);
