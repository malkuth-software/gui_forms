A grid of typed columns over rows the grid owns, virtual rows or a binding source.

A data grid view shows rows of cells under a row of column headers, with an
optional column of row headers at the left. Each `DataGridViewColumn` is a
text, check box, combo box, button, image or link column with a name, header text,
width, read-only flag and cell styles. An image column's cells hold `ImageId`
values, images of the grid's window, placed by the column's `image_layout`
(``normal``: at the image's own size, centred and cut at the cell's edges;
``stretch``; ``zoom``: as large as fits, its shape kept), and its
`image_description` stands for the image as the cell's text. A link column's
cells show their text as a link, or the column's `link_text` when empty, and
with `track_visited_state` a followed link becomes visited. The rows come from one of three sources: unbound
rows the grid owns (`DataGridViewRow` records with stable IDs and
`BindingValue` cells), virtual rows whose order the application publishes as
stable row IDs and whose values an application `DataGridViewCellValueProvider`
supplies by ID as they are shown, or the records of a `BindingSource`. Every
row has a stable ID (`DataGridViewRowId`), and the grid keeps its selection,
current cell and edit by ID; a row number in the API is a presentation
position (`DataGridViewRowPosition`), which inserting, removing and sorting
rows change. Use it for tabular data the user reads, selects and edits in
place; use an `ObjectView` for a list of objects with icons.

Parameters
----------
stable_id
    Identity of the grid, unique within its window. The editor is created as
    ``<stable_id>.editor``, the scroll bars as ``<stable_id>.vertical-scroll``
    and ``<stable_id>.horizontal-scroll``, and combo box choices are shown by
    a `ContextMenu` named ``<stable_id>.choices``.

Properties
----------
columns
    The columns, in display order. `set_columns` replaces them, `add_column`
    appends one and `remove_column_at` removes one. A column name must be
    unique, not empty and without ``.``; its width finite and at least its
    `minimum_width`; and only a combo box column may have `items`. Anything
    else throws `std::invalid_argument`. Replacing or removing columns ends
    any edit and clears the selection and current cell.
rows
    The unbound rows, edited in place. Read them with `rows.read()` or
    `rows[i]`; write them with `rows = {...}` (every row, ending any edit
    and clearing the selection), `rows.insert(i, row)`, `rows.append(row)`
    or `rows.append(rows)`, `rows.erase(i, n)`, `rows.replace(i, row)` and
    `rows.clear()`. An empty stable ID is replaced by a generated one
    (``row.1``, ``row.2``, ...), and a replacement keeps the row's ID unless
    it names another; a repeated ID, one that breaks the `StableId` segment
    rule, invalid UTF-8 or more values than columns throws
    `std::invalid_argument`, and a position past the unbound rows (the new
    row included) `std::out_of_range`. Virtual and bound rows refuse every
    edit with `std::logic_error`. The current cell and the selection stay
    on their rows; an erased row leaves the selection, and when it held
    the current cell, the row that takes its position, or the new last
    row, becomes current. A refused edit changes nothing. An edit
    repaints, at the next layout, only the rows shown that it reaches.
row_source : default unbound
    Where the rows come from: `DataGridViewRowSource::unbound`,
    `virtual_rows` or `binding_source`.
row_count
    The number of rows. `set_row_count` grows unbound rows with generated
    rows or cuts them from the end; virtual rows are counted by their order
    and bound rows by their source, so both refuse it with
    `std::logic_error`.
data_source : default none
    The `BindingSource` whose records are the rows, or none. Assigning a
    source shows its records as rows; assigning none returns to empty
    unbound rows. Every assignment resets the rows, even of the same
    source.
column_headers_visible : default ``true``
    Whether the column header row shows.
row_headers_visible : default ``true``
    Whether the row header column shows.
column_headers_height : default ``23``
    The height of the column header row, from 4 to 256.
row_headers_width : default ``41``
    The width of the row header column, from 4 to 1024.
row_height : default ``22``
    The height of every row without its own, from 4 to 256.
font
    The font of cell and header text.
read_only : default ``false``
    Whether no cell of the grid edits, toggles or chooses.
allow_user_to_resize_columns : default ``true``
    Whether dragging a column header's divider resizes the column.
allow_user_to_resize_rows : default ``true``
    Whether dragging a row header's bottom edge (the ``row_grip`` part)
    gives the row its own height, while rows do not size themselves.
auto_size_rows_mode : default ``none``
    How rows size their heights: to their headers, cells or both, over
    every row or the rows shown.
allow_user_to_order_columns : default ``false``
    Whether dragging a column header moves the column.
allow_user_to_add_rows : default ``true``
    Whether an unbound or bound grid shows the new row after the last row.
allow_user_to_delete_rows : default ``true``
    Whether Delete removes the selected rows.
default_cell_style
    The grid's cell style (`DataGridViewCellStyle`), last in every cell's
    inheritance chain; empty by default.
rows_default_cell_style
    The style of every row's cells, before the column's.
alternating_rows_default_cell_style
    The style of odd positions' cells, before the rows'.
column_headers_default_cell_style
    The column headers' style, before the grid's.
row_headers_default_cell_style
    The row headers' style, before the grid's.
inherited_row_header_style
    The style row headers draw with: the row headers', then the grid's.
enable_headers_visual_styles : default ``true``
    Whether the theme draws the headers, their styles' colours unused.
grid_color : default none
    The grid lines' colour, an authored colour (owner decision T4-1).
background_color : default none
    The colour where no cell is; the theme's ground stays under the cells.
border_style : default ``BorderStyle::line``
    The grid's frame; ``none`` publishes no ``edge`` part.
cell_border_style : default single
    Which lines the cells draw: all, vertical, horizontal or none.
column_headers_border_style : default raised
    The lines between column headers; ``none`` keeps the resize grips.
row_headers_border_style : default raised
    The lines between row headers.
draw_mode : default ``ItemDrawMode::normal``
    ``owner_draw`` raises `draw_item_content` for each data cell.
sorted_column : default none
    The column the last sort ordered by, while the rows keep its order.
sort_order : default ``DataGridViewSortDirection::none``
    That sort's direction.
auto_size_columns_mode : default ``none``
    The auto size mode of columns whose own is ``not_set``.
clipboard_copy_mode : default ``enable_with_auto_header_text``
    Which header texts a copy includes, or ``disable``.
clipboard_content : default none
    The selection as tab-separated text, one line per row ending ``\r\n``,
    with the header texts the copy mode includes; none when copying is
    disabled or nothing is selected.
first_displayed_row
    The position of the first row shown under the column headers.
    `set_first_displayed_row` scrolls to a position; layout keeps the last
    rows filling the grid.
horizontal_scroll_offset
    How far the cells are scrolled left, in logical units at text scale 1.
displayed_row_end
    One past the last row shown, partly shown rows included.
vertical_scroll_bar
    The child `VScrollBar` that scrolls by rows.
horizontal_scroll_bar
    The child `HScrollBar` that scrolls by logical units.
selection_mode : default cell_select
    `DataGridViewSelectionMode::cell_select` selects cells,
    `full_row_select` whole rows. Changing it selects the current cell (or
    row) alone.
multi_select : default ``true``
    Whether Ctrl and Shift add to the selection.
current_cell
    The cell keyboard input acts on, at its row's present position, or none.
    `set_current_cell` moves it, committing any edit first, and selects it
    alone; it returns false and changes nothing when the edit does not
    commit.
current_row_id
    The current cell's row ID, or none.
selected_cells
    The selected cells at their rows' present positions, by row, then
    column; in full-row mode every cell of every selected row.
selected_rows
    The positions of the selected rows: in full-row mode the selected rows,
    in cell mode the rows whose every cell is selected.
is_current_cell_in_edit_mode
    Whether a text cell is in edit or a combo box cell's choices are open.
editing_control
    The `TextBox` a text cell edits in. It is laid out over the current cell
    only during an edit.
drop_down_open
    Whether a combo box cell's choices are open.
virtual_row_window_source : default none
    The `DataGridViewRowWindowSource` window mode asks for row IDs. Taking it
    away (null) in window mode first takes every ID from the old source and
    goes to edit mode; rows that are not virtual refuse it with
    `std::logic_error`.
row_order_mode : default ``DataGridViewRowOrderMode::edit``
    How the virtual rows' order is held: ``edit`` (every ID, in a chunked
    order index) or ``window`` (the IDs around the displayed rows).
row_order_policy : default ``DataGridViewRowOrderPolicy::automatic``
    Who chooses the mode: the grid, by the thresholds, or the program,
    pinning ``edit`` or ``window``, which is switched to at once. Pinning
    ``window`` without a window source throws `std::logic_error`.
row_order_thresholds
    The automatic policy's `DataGridViewRowOrderThresholds`: window mode
    from ``window_rows`` rows (250000) or ``window_changes`` reported changes
    in one layout pass (20000); edit mode again at most ``edit_rows`` rows
    (100000) with at most ``edit_changes`` changes (1000) in the last pass
    and this one; ``window_margin`` rows (64) held past each end of the
    displayed rows. New thresholds apply at once. Edit thresholds above
    window thresholds throw `std::invalid_argument`.
row_order_statistics
    The order's work counters (`DataGridViewRowOrderStatistics`): reported
    changes, index writes, slot moves, chunk-table rebuilds, whole builds,
    window requests, IDs requested, position queries and mode switches.

Events
------
selection_changed
    Raised once after an operation changes which cells or rows are selected.
current_cell_changed
    Raised after the current cell moves to another cell, with the new cell or
    none; not when only its row's position changes.
cell_begin_edit
    Raised before an edit begins, by F2, typing, a double click, a combo box
    cell opening, or `begin_edit`. Setting ``cancel`` keeps the cell out of
    edit.
cell_end_edit
    Raised when an edit ends, committed or discarded.
cell_validating
    Raised before an edited text cell commits, with the text as typed.
    Setting ``cancel`` refuses it: the cell stays in edit and publishes the
    invalid state.
cell_value_changed
    Raised after a cell's value changes: a committed edit, a toggled check
    box, a chosen item or `set_cell_value`.
cell_content_click
    Raised when the user clicks a button cell, an image cell's image or a
    link cell's link, or toggles a check box cell.
column_header_click
    Raised when a column header is clicked, with the column's index, before
    the automatic sort the click makes.
sort_compare
    Raised for each comparison a column sort makes, when it has a handler:
    a handler that sets ``sort_result`` and ``handled`` decides it.
sorted
    Raised after a sort ordered the rows.
column_width_changed
    Raised after a column's width changes, with the column's index.
column_display_index_changed
    Raised after a column's display index changes, with the column's index.
row_height_changed
    Raised after a row's own height changes, with the row.
cell_error_text_needed
    Raised when the grid reads a cell's error text in virtual or bound rows,
    with the text it holds: a handler may give the application's own.
row_error_text_needed
    As ``cell_error_text_needed``, for a row's error text.
cell_formatting
    Raised when the grid needs a cell's text, with its value and inherited
    style: a handler may give the text, change the value or the style.
cell_parsing
    Raised when an edited text cell stores its text: a handler may give the
    value.
draw_item_content
    Raised in ``owner_draw`` for each data cell painted, to draw its content.
row_order_mode_changed
    Raised after the virtual rows' order switched mode, with the old and new
    mode, the reason (``row_count``, ``change_rate``, ``policy`` or
    ``source``), the row count and this pass's reported changes.

Methods
-------
column_index
    The position of the column with a name, or none.
set_column_width
    Sets a column's width; an unknown column throws `std::out_of_range` and a
    width below the column's minimum `std::invalid_argument`.
set_column_sort_glyph
    Shows an ascending or descending sort glyph on a column header, or none.
set_column_read_only
    Makes a column's cells read-only, ending an edit in that column.
clear_rows
    Removes every row of any source and returns to unbound rows.
row_id
    The stable ID of the row at a position: an unbound row's own, the
    application's for a virtual row, the record's for a bound row. A position
    past the end throws `std::out_of_range`.
row_position
    The present position of the row with a stable ID, or none.
cell_value
    A cell's value. A cell past the rows or columns throws
    `std::out_of_range`.
cell_text
    The text a cell shows: the text a `cell_formatting` handler gives, else
    the value in its inherited style's format (numbers otherwise in their
    shortest exact form) or the style's null value for an empty value; a
    button column's `button_text` for an empty button cell, an image
    column's `image_description` for an image cell holding an image (empty
    otherwise), and a link column's `link_text` for an empty link cell.
set_cell_style
    Sets a cell's own style, kept by the row's ID; `cell_style` reads it.
set_row_default_cell_style
    Sets a row's style, kept by its ID; `row_default_cell_style` reads it.
set_column_default_cell_style
    Sets a column's cells' style.
set_column_header_cell_style
    Sets a column header's style.
inherited_cell_style
    The style a cell draws with: each property from the first of the cell's,
    its row's, the alternating rows' (odd positions), the rows', its
    column's and the grid's styles that sets it.
inherited_column_header_style
    A column header's style: its own, the column headers', the grid's.
link_visited
    Whether a link cell's link is visited. A cell past the rows or columns
    throws `std::out_of_range`, and one outside a link column
    `std::invalid_argument`.
set_link_visited
    Marks a link cell's link visited or not and repaints it; the same
    refusals as `link_visited`. The state stays on its row when rows move
    and ends with its row, when the rows or columns are replaced and when a
    column is removed.
set_cell_value
    Stores a value from the program, read-only or not, and raises
    `cell_value_changed` when it differs. Returns false when a provider or
    binding source refuses it.
sort
    Orders unbound rows stably by a column: empty values first, then
    booleans, numbers and text, unless `sort_compare` decides; or by a
    `DataGridViewRowComparer`. A column sort shows the glyph on that column
    and makes it `sorted_column`; a comparer's clears every glyph. The
    current cell and the selection stay on their rows. Raises `sorted`.
    Returns false for virtual or bound rows, or when an edit in progress does
    not commit.
column_sort_mode
    A column's `DataGridViewColumnSortMode`: its own, else its kind's
    default (``automatic`` for text columns, ``not_sortable`` otherwise).
set_column_sort_mode
    Sets a column's sort mode, or none for its kind's default.
column_auto_size_mode
    A column's `DataGridViewAutoSizeColumnMode`, the grid's when its own is
    ``not_set``.
set_column_auto_size_mode
    Sets a column's auto size mode.
set_column_fill_weight
    Sets a fill column's share of the width the other columns leave.
auto_resize_column
    Sizes a column once to its cells or header, raising
    `column_width_changed`.
auto_resize_columns
    Sizes every column once.
column_display_index
    A column's place in the order the grid shows its columns.
set_column_display_index
    Moves a column to a display index, the columns between shifting by one.
column_at_display_index
    The column shown at a display index.
row_height_at
    A row's height: its own, or `row_height`.
set_row_height_at
    Gives a row its own height, from 4 to 4096, kept with the row's ID.
reset_row_height_at
    Returns a row to `row_height`.
auto_resize_row
    Gives a row its preferred height once, raising `row_height_changed`.
auto_resize_rows
    Sizes every row, or the rows shown, once.
preferred_cell_height
    The height a cell asks for: its content (a wrapped cell's lines),
    padding and grid line.
preferred_row_header_height
    The height a row header asks for.
preferred_cell_width
    The width a cell asks for: its content, padding and grid line.
preferred_column_header_width
    The width a column header asks for, with a sortable column's glyph lane.
set_virtual_rows
    Switches to virtual rows: the application's row IDs in presentation
    order, or a `DataGridViewRowWindowSource` that keeps the order, with
    values asked of a provider by ID. Unbound rows are discarded, and any
    edit, the selection and the current cell end. Given the order, the grid
    starts in edit mode without a window source; given a source, in the
    mode its policy chooses. A null provider or source, or an empty,
    repeated or invalid UTF-8 ID, throws `std::invalid_argument` and changes
    nothing.
set_virtual_row_order
    Replaces the virtual rows' order after the application inserted, removed,
    filtered or sorted rows. The selection, the current cell, an edit in
    progress and the first displayed row stay on their IDs. A selected row
    that is gone leaves the selection; when the current row is gone, its
    edit is discarded and the row now at its position, or the new last row,
    becomes current. It is a reset: the order is rebuilt in edit mode, and
    the policy may move it to window mode again. Rows that are not virtual
    refuse it with `std::logic_error`; an invalid ID throws
    `std::invalid_argument` and changes nothing.
insert_virtual_rows
    Reports rows the application inserted: their IDs now stand at the given
    position onwards. Costs in proportion to the change.
remove_virtual_rows
    Reports rows the application removed. Only they leave the selection;
    when the current row is among them, its edit is discarded and the row
    now at its position, or the new last row, becomes current.
move_virtual_row
    Reports a row the application moved: it now stands at the position.
    The three reports keep the selection, the current cell, an edit and the
    first displayed row on their IDs. In edit mode an invalid report (an
    empty, repeated or invalid UTF-8 ID, a removed or moved row that is not
    there) throws `std::invalid_argument` and a position past the end
    `std::out_of_range`, changing nothing; in window mode the source is
    trusted. Rows that are not virtual refuse them with `std::logic_error`.
refresh_rows
    Repaints after the provider's values changed outside the grid.
is_cell_selected
    Whether a cell is selected; in full-row mode, whether its row is.
is_row_selected
    Whether a row is selected; in cell mode, whether all its cells are.
set_cell_selected
    Selects or deselects a cell (its row in full-row mode). Without
    `multi_select`, selecting clears the rest.
set_row_selected
    Selects or deselects a row, or all its cells in cell mode.
clear_selection
    Deselects everything.
select_all
    Selects every cell, or every row; nothing without `multi_select`.
begin_edit
    Puts the current text cell in edit, its text selected or the caret at
    its end, or opens the current combo box cell's choices. Returns false
    for no current cell, a read-only cell, a check box or button cell, or a
    cancelled `cell_begin_edit`.
end_edit
    Commits the edit in progress: `cell_validating`, conversion to the
    column's `value_kind`, then the value. Returns false, leaving the cell in
    edit and invalid, when any step refuses.
cancel_edit
    Discards the edit in progress.
close_drop_down
    Closes a combo box cell's choices, ending its edit.
initialize_control_tree
    Creates the editor and the scroll bars. `make_control` calls it; do not
    call it yourself.

See Also
--------
ObjectView : A virtual list of objects with icon and details views.
BindingSource : Records with a current position, shared by bound controls.
TextBox : The editor text cells edit in.
ContextMenu : The overlay that shows a combo box cell's choices.

Notes
-----
Cells are records, not child controls. Only the rows and columns in view are
realized: the grid publishes them as three item systems, ``row`` (the row
header with ``row_header``, ``row_header_label``, ``row_glyph`` and
``row_rule``), ``cell`` (``cell``, ``cell_selection``, ``cell_content``,
``cell_image``, ``cell_link``, ``cell_link_underline``, ``cell_check_well``, ``cell_check_indicator``, ``cell_button_face``,
``cell_button_label``, ``cell_drop_down``, ``cell_drop_down_glyph``,
``cell_vertical_rule``, ``cell_horizontal_rule`` and ``cell_focus``) and
``column`` (``column_header``, ``column_label``, ``sort_glyph``,
``column_divider`` and ``column_grip``), and paints every part through the
by-role paint path (`paint_part`). Cells take the states selected, current,
read_only, invalid, checked, pressed, open and focus_visible, and a link
cell the grid's own ``visited``; rows selected,
current and read_only; column headers hot, pressed and captured, and the
application-defined ``sorted_ascending`` and ``sorted_descending``. The
column header row and the row header column stay in place while the cells
scroll beneath them.

Rows are identified as in `ObjectView`: by stable ID, with no second
row-ownership model. A virtual grid holds the application's order of row
IDs and an index from ID to position; the application owns the rows and their
values, sorts and filters them itself, and gives the grid the order in full
(`set_virtual_row_order`, a reset), by change (`insert_virtual_rows`,
`remove_virtual_rows`, `move_virtual_row`), or on demand through a window
source.

The order has two modes (owner decision W-5; `DataGridViewRowOrder`). In
edit mode the grid holds every ID in chunks of at most 512 with a Fenwick
tree of their sizes and an ID-to-chunk index, so a reported change costs in
proportion to the change: O(changed rows + 256 + log chunks), plus the
number of chunks when one splits, merges or empties, while a reset costs the
row count. In window mode it holds only the IDs of the displayed rows and
64 rows past each end, fetched from the window source in one request when
the displayed rows leave the window, and asks the source for an ID's
position or a far row's ID one at a time; a reported change only discards
the window. The automatic policy goes to window mode when the rows reach
``window_rows`` or one layout pass's reported changes reach
``window_changes``, and back to edit mode (taking every ID from the source
once) when the rows are at most ``edit_rows`` and the last pass and this one
had at most ``edit_changes`` changes; it decides after each report, reset,
and policy or source change, never during layout, and reports each switch.
A pass counts when the grid is laid out. The selection, the current cell, an
edit, focus and the first displayed row follow their IDs in both modes and
across switches. The grid asks its provider, by ID, only for the
cells it shows, the current cell, and cells the program or accessibility
reads, and realizes only the rows in view: painting a window of a million
rows asks for the values of that window alone (the tests check it). Cell
events carry the row's ID beside its position. A bound grid shows the field
each column's
`data_property_name` names. Its current row follows the source's position
and moves it, and a committed edit moves the position to its row and sets
the field; a record the source does not allow to be edited is read-only.

Keys: the arrows move the current cell, Shift extends the selection from the
anchor and Ctrl jumps to the edge; Tab and Shift+Tab move across and wrap to
the next or previous row, leaving the grid at either end; Home and End go to
the row's first and last cell (the first and last row in full-row mode),
Ctrl+Home and Ctrl+End to the grid's; Page Up and Page Down move by the rows
in view; Enter moves down; Ctrl+A selects all. F2, typing or a double click
edits a text cell; Enter, Tab, an arrow up or down, a click elsewhere or
leaving the grid commits, and Escape discards. When focus leaves for another
control and the text does not commit, the edit is discarded. Space toggles a
check box cell, clicks a button cell and follows a link cell's link; F4 or Alt+Down opens a combo box
cell's choices. `cell_validating` is raised only for an edit being committed.

The grid publishes the accessibility role `table`, with the row count and
the column count, and is `multiselectable` while `multi_select` is on. The
visible column headers are `column_header` children, and each visible row
is a `row` holding its row header (a `row_header` with its row index,
while row headers are shown; its stable ID is the grid's ID,
``.row-header.`` and the row's ID, and `press` clicks it) and then its
cells: each a `cell` with its row and column index (counting from 1),
whatever its kind, keeping its kind's behaviour as states and actions (a
check box cell is `checkable` and `checked`). A cell's stable ID is its
row's ID, ``.`` and its column's name. Cells take
`SemanticAction::set_value` (text through the same validation and conversion
as typing), `press` (toggle, click or follow a link) and `expand` (open the
choices). An image cell's value is its description; it takes no
`set_value`. A link cell is named by its link's text and its value is
``visited`` or ``unvisited``, as a `LinkLabel`'s is.

An image cell draws its image through the ``image`` role, whole, under a clip
at the cell's edges, so an image wider than its cell or one partly scrolled
away keeps its shape; its ``cell_image`` part is the image as it is seen. The
image's pixels are logical units, as in `PictureBox`. A cell whose value is
empty, is not an `ImageId`, or names an image the window does not hold
shows nothing. An image cell neither edits nor toggles.

A link cell draws its text through the ``content`` role in the ``link``
family, or ``visited_link`` once visited (owner decision W-Q5), over the
selection plate. Its ``cell_link`` part is the text's extent, placed by the
column's alignment and cut at the content area; only a click on it follows
the link, and a click beside it makes the cell current. The pointer is the
hand over the link. A link cell does not edit. Its ``cell_link_underline``
part, one unit high just below the text's baseline and as wide as the link,
is drawn by the ``glyph`` role with the ``underline`` mark in the same
family, so the underline takes the link's colour.

A commit converts the typed text to the column's value kind and stores it.
When a value does not reach its cell, ``data_error`` reports why
(``parsing`` or ``commit``) with the text given; with no handler, or with
``cancel`` left set, the cell stays in edit, marked invalid, and keeps its
old value (no dialog is shown); clearing ``cancel`` discards the edit.
``cell_validated`` follows a stored edit. ``is_current_cell_dirty`` is true
once the user changes the editor's text (``current_cell_dirty_state_changed``
reports each change), ``is_current_row_dirty`` once the row holds a change.
``commit_edit`` stores without leaving edit and ``refresh_edit`` shows the
stored value again. ``edit_mode`` chooses which keys begin an edit: typing,
F2, both (the default), on becoming current, or only ``begin_edit``.

Moving the current cell commits the edit, raises ``row_validating`` when
the row changes (a handler may keep the row), then ``cell_leave``,
``row_leave``, ``row_validated``, ``row_enter``, ``default_values_needed``
(on the new row), ``cell_enter`` and ``current_cell_changed``. Their
handlers may not call ``set_current_cell``. When focus leaves the grid and
its editor (not for a combo box cell's open choices), the edit commits and
the current row validates: ``row_validating``, then ``row_validated`` unless
a handler cancelled. Focus is not held on a cancel, where Windows Forms
keeps it in the grid: a recorded divergence (``manifests/winforms-divergences.json``,
listed under Windows Forms divergences in the control guide).

With ``allow_user_to_add_rows``, on by default as in Windows Forms, an
unbound or bound grid shows a new row after the last; its row header shows
a star. Turn it off where the rows are fixed. The first value the user
commits to it adds a row (or a record) with the new row's ID, raising
``user_added_row``; Escape, or ``cancel_row_edit``, discards it until the
current cell leaves it. Virtual rows show the new row when their provider's
``can_add_rows`` says so: the first committed value asks the provider's
``add_row`` for the row (owner decision, 2026-11-02), which names it or
refuses; the grid inserts the ID in edit mode, and in window mode the
application reports the insertion as soon as ``add_row`` returns. The C#
facade can raise Windows Forms' ``NewRowNeeded`` and ``CellValuePushed`` on
top of it. Each error glyph, the grid's own interface rather than its data,
shows its error text as a tooltip (owner decision, 2026-11-02); cells carry
no tooltips of their own, so over them the grid's own ``tool_tip_text``
shows. With ``allow_user_to_delete_rows``, also on by
default, Delete removes the selected rows, each after ``user_deleting_row``.
The current row's header shows a pencil while the row is dirty
(``show_editing_icon``).

``set_cell_error_text`` and ``set_row_error_text`` give cells and rows error
text (a bound row also shows its record's errors). It is drawn as the error
glyph, the ``cell_error`` and ``row_error`` parts, at the cell's end and in
the row header, published as the semantic description, and makes the cell or
row ``invalid``. The arrow, pencil, star and error glyphs are glyph marks
(``pencil``, ``asterisk``, ``error``) whose look belongs to the theme.

Cell styles (`DataGridViewCellStyle`, Windows Forms' DataGridViewCellStyle)
follow the T4 style mapping. Every property is optional, and one no style
in a cell's chain sets is the theme's, so an unstyled grid looks like the
rest of its theme. ``font`` is the content's font; ``alignment`` places it
across (start, centre, end) and up and down (top, middle, bottom);
``padding`` replaces the grid's own margins around it; ``format`` formats
numbers with the invariant fixed formats ``F0`` to ``F12`` (those `Binding`
accepts); ``null_value`` is shown for an empty value, and typed text equal
to it stores ``data_source_null_value``. Header styles give the header
labels their font, alignment and padding. Styles are values: the grid keeps
a copy. ``wrap_mode`` wraps a text cell's text at its width, at the text
engine's line-break opportunities and at line ends; the lines sit up and
down as a block and are cut at the cell's bottom (rows do not grow to fit
them yet). Other kinds and the headers keep one line.
The colours are authored colours (owner decision T4-1): over the theme's
ordinary colours, under its important ones, and dropped in high contrast;
a disabled grid stays greyed. ``back_color`` fills the cell and
``fore_color`` draws a text, combo box or button cell's text (links keep
their link colours, check boxes their theme's); while a cell is selected
the selection colours stand in for them, and an unset one leaves the
theme's selection look. With `enable_headers_visual_styles` off, the header
styles' colours paint the headers, their labels and glyphs.
Raised and sunken cell and header lines draw as the theme's divider, as
single ones do, where Windows Forms draws bevels. An edit the user did not
change stores nothing, so a formatted text need not convert back.

Sorting follows Windows Forms. A click on the header of an ``automatic``
column (a text column's default) sorts unbound rows ascending, then toggles;
``programmatic`` columns leave the order to the application, and
``not_sortable`` ones (other kinds' default) only raise
`column_header_click`. `sort_compare` handlers and comparers may not change
the rows, columns or values while the sort runs (`std::logic_error`); a
comparer need not be a consistent order. Replacing the rows ends a sort:
`sorted_column` becomes none and an automatic column's glyph goes. Ctrl+C
(Command+C on macOS) and Ctrl+Insert copy `clipboard_content` to the host's
clipboard as text; ``clipboard_copy_mode`` (default
``enable_with_auto_header_text``, which adds the row headers in full-row
selection) chooses the header texts.

Columns size themselves as Windows Forms' do. A column's ``auto_size_mode``
(else the grid's `auto_size_columns_mode`) sizes it to its cells' preferred
widths over every row or the rows shown, with or without its header's, to
its header alone, or as a ``fill`` column sharing the width the others
leave by ``fill_weight``. Automatic widths are applied when the grid lays
out, never below ``minimum_width``, without raising `column_width_changed`,
and the user cannot resize an auto-sized column (a fill column included,
where Windows Forms moves the weights). The cell modes measure every row's
formatted cell, so virtual rows ask their provider for each.

Any row may have its own height (Windows Forms' DataGridViewRow.Height), of
any row source, virtual rows included (owner decision, 2026-11-02): it stays
with the row's ID while rows move and ends with the row. Rows without one
have `row_height`. The grid finds rows by offset through a Fenwick tree of
the heights' differences from `row_height` (`DataGridViewRowHeights`), so
scrolling, hit testing and painting cost a logarithmic search, not a walk
over the rows; while no row has its own height nothing is stored. A change
to the rows rebuilds the tree in one pass over them while any row has its
own height. Scrolling stays by whole rows, as in Windows Forms: the first
displayed row is shown from its top, and the last row ends at the bottom
when scrolled to the end.

Rows size themselves as Windows Forms' do. With an ``all`` mode of
`auto_size_rows_mode` every row is measured, its header's or cells'
preferred height (a wrapped text cell's lines at its column's width), after
a change to the values, rows, columns, column widths, styles or text scale;
virtual rows ask their provider for every cell. A ``displayed`` mode
measures from the first displayed row down until the viewport is full, on
every layout pass, and rows keep their measured heights once scrolled away.
Automatic heights raise no `row_height_changed`, override own heights (which
return when the mode is ``none`` again), and rows are not resized by the
user meanwhile.

Columns have a display order apart from their indices (Windows Forms'
DisplayIndex): `set_column_display_index` moves one, and with
`allow_user_to_order_columns` the user drags a header, the
``column_drop_indicator`` part showing where it goes; a drag is no header
click. What the user sees and walks follows display order: the columns'
places, the arrow keys, Tab, Home and End, range selection, the semantic
column indices and the clipboard's fields. Cell addresses and events keep
column indices. Setting the columns resets the order; an added column goes
last.

Not supported yet: wrapped header text, the style
changed events, per-row header cell styles, Custom (advanced) border
styles, `RowPrePaint` and `RowPostPaint`, `LinkBehavior` (the
link is always underlined), an image column's default
image for empty cells and icon values, frozen rows and columns beyond the headers, the
clipboard's HTML and CSV formats, automatic sorting of virtual and bound
rows (the application sorts them), frozen columns,
header size modes,
and cell tool tips.

Examples
--------
A grid of stations with a check box and a combo box column, sorted by name:

.. code-block:: cpp

   std::shared_ptr<gf::DataGridView> grid =
       gf::make_control<gf::DataGridView>(gf::StableId("radio.stations"));
   (*grid).set_columns({
       {.name = "name", .header_text = "Station", .width = 160.0},
       {.name = "favourite", .header_text = "Favourite", .kind = gf::DataGridViewColumnKind::check_box,
        .width = 70.0},
       {.name = "mode", .header_text = "Mode", .kind = gf::DataGridViewColumnKind::combo_box,
        .width = 90.0, .items = {"AM", "FM", "USB"}},
   });
   (*grid).rows.append({.stable_id = {"wqxr"}, .cells = {gf::BindingValue{std::string("WQXR")},
                                                          gf::BindingValue{true},
                                                          gf::BindingValue{std::string("FM")}}});
   (*grid).rows.append({.stable_id = {"bbc4"}, .cells = {gf::BindingValue{std::string("BBC Radio 4")},
                                                          gf::BindingValue{false},
                                                          gf::BindingValue{std::string("AM")}}});
   (*grid).sort(0U, gf::DataGridViewSortDirection::ascending);   // BBC Radio 4 first
   // row_position({"wqxr"}) is now position 1

Virtual rows: the application owns the rows and their order, and the grid
asks for the values it shows by row ID:

.. code-block:: cpp

   struct Stations final : gf::DataGridViewCellValueProvider {
       gf::BindingValue cell_value(const gf::DataGridViewRowId& row, std::size_t) const override {
           return gf::BindingValue{"Station " + row.value};
       }
       bool push_cell_value(const gf::DataGridViewRowId&, std::size_t, const gf::BindingValue&) override {
           return false;
       }
   };

   void example() {
       std::shared_ptr<gf::DataGridView> grid =
           gf::make_control<gf::DataGridView>(gf::StableId("radio.virtual"));
       (*grid).set_columns({{.name = "name", .header_text = "Station", .width = 160.0}});
       (*grid).set_virtual_rows(std::make_shared<Stations>(), {{"a"}, {"b"}, {"c"}});
       (*grid).set_current_cell(gf::DataGridViewCellAddress{gf::DataGridViewRowPosition{1U}, 0U});
       (*grid).set_virtual_row_order({{"c"}, {"d"}, {"b"}, {"a"}});   // the application sorted and added
       // current_row_id() is still {"b"}, now at position 2
   }
