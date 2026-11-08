A view of many items, shown as a grid of icons or as rows with details columns.

An object view presents a collection such as the contents of a folder or a
set of search results. Each item is an `ObjectViewItem` with a string ID, a
name, secondary text, a description and a glyph or image. In
`ObjectViewMode::icons` the items wrap in a grid of cells; in
`ObjectViewMode::details` they are rows, and with `details_columns` the rows
are divided into cells under a header that the user clicks to sort and drags
to resize. The user selects one or several items with the pointer and
keyboard, activates an item by double-clicking it or pressing Enter, and
asks for a context menu with the secondary button. Only the items in view
are painted and published, and no child control is created per item. Use a
`TreeView` for a hierarchy, a `ListBox` for plain text rows, and a
`CorrespondenceView` for records that expand to show more detail.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
items
    The items in display order. Empty by default. `set_items` replaces the
    items and keeps the current `details_columns`, so every item needs one
    cell per column. Item IDs must be unique and follow the
    `StableId` segment rule; the
    ID, name, secondary text and description at most 65536 bytes each and
    valid UTF-8; an `image_key` at most 256 bytes; at most 1,000,000 items;
    and all text together at most `maximum_details_text_bytes`. Otherwise
    `std::invalid_argument` is thrown and nothing changes. Selected, focused
    and anchor IDs that are still present are kept, the first item in view
    stays in view, and `selection_changed` is raised if selected items were
    removed or now come in a different order.
details_columns
    The columns of the details mode, set together with the items by
    `set_details_model`. Empty by default; without columns, a details row
    shows the glyph, the name and the secondary text.
details_sort
    The sort indicator shown in the header: a column ID and an
    `ObjectSortDirection`. An empty column ID, the default, shows no
    indicator. Writing `details_sort` changes only the indicator and never
    reorders the items. A column that does not exist or is not sortable, or
    an invalid direction, throws `std::invalid_argument`.
horizontal_offset : default ``0.0``
    How far the details columns are scrolled to the left, in logical
    pixels. It is clamped so the columns never scroll past their total
    width. A negative or non-finite value throws `std::invalid_argument`.
details_paint_work
    Counts of rows, cells and text preparations in the last paint of the
    details columns, for diagnostics.
view_mode : default ``ObjectViewMode::icons``
    Whether items are shown as a grid of icon cells or as details rows.
    Switching keeps the selection and the first item in view.
selected_id : default ``""``
    The primary selected item, normally the one selected last, or empty
    when nothing is selected. Setting it replaces
    the whole selection with that one item, and an empty ID clears it. An
    ID that is not in `items` throws `std::out_of_range`.
selected_ids
    Every selected ID, in the order of `items`. `set_selected_ids` replaces
    the selection; its `primary_id` must be one of the IDs or empty, in
    which case the current primary is kept if it is still selected and the
    first selected item is used otherwise. An unknown ID throws
    `std::out_of_range`, and a `primary_id` that is not selected, or more
    than one ID in `ObjectSelectionMode::single` mode, throws
    `std::invalid_argument`.
selection_mode : default ``ObjectSelectionMode::multiple``
    Whether several items can be selected at once. Switching to `single`
    while several are selected keeps only the primary.
selection_anchor_id
    The item a Shift range starts from. Plain and toggling selections move
    it to the item selected.
focused_id
    The item with the keyboard focus rectangle, from which the arrow keys
    move. A click or key that selects an item focuses it, and Ctrl or
    Command with an arrow key moves only the focus. When the view gains
    focus with no focused item, the primary selection, or else the first
    item, is focused.
icon_cell_size : default ``Size{112.0, 91.0}``
    Size of each cell in icon mode, in logical pixels before text scaling.
    As many cells fit in a row as the width allows. Sizes outside 86 by 78
    to 512 by 512, or not finite, throw `std::invalid_argument`.
details_row_height : default ``28.0``
    Height of each details row, and of the header, in logical pixels before
    text scaling. Values outside 20 to 256 throw `std::invalid_argument`.
show_secondary_text : default ``true``
    Whether the secondary text is drawn: under the name in icon cells at
    least 90 pixels high, and beside the name in details rows without
    columns that are at least 112 pixels wide.
top_row : default ``0``
    The first row in view: a row of cells in icon mode, an item in details
    mode. The mouse wheel moves it by two rows. A row outside the items
    throws `std::out_of_range`; a row past the last full page is clamped.
font : default ``FontSpec{FontRole::content, 11.0, 400, false}``
    The font of item names and cell text. An invalid specification throws
    `std::invalid_argument`.
image_list
    The `ImageList` whose images the items show, looked up by each item's
    `image_key`; an item without an image shows its `glyph`. Empty by
    default. The list must be alive and, while the view is in a window,
    belong to that window, or `std::invalid_argument` is thrown.

Events
------
sort_requested
    Raised when the user clicks a sortable column header, or presses Enter
    or Space on the focused header. It carries the requested
    `ObjectDetailsSort`: that column, descending if the column is now
    sorted ascending and ascending otherwise. The view does not sort:
    reorder the items, then publish them with `set_details_model` and the
    accepted sort, or write `details_sort`.
selection_changed
    Raised after the selected IDs or the primary selection change, by
    input, by a setter, or by a model replacement that removed or reordered
    selected items. It carries an `ObjectSelectionChange` with the previous
    and current primary IDs and ID lists.
item_activated
    Raised when the user activates an item: on the release of a
    double-click on it, on Enter for the focused item, or on a semantic
    press. It carries the item ID. Disabled items are not activated.
context_requested
    Raised on the release of a secondary click on an item, on a secondary
    click on the background, and on a semantic `show_menu`. It carries an
    `ObjectContextRequest` with the item ID, empty for the background, and
    the position to open a menu at, in window coordinates despite its name
    `screen_position`; pass it to `ContextMenu`. A secondary press on an
    unselected item selects it first; a background click clears the
    selection first.

Methods
-------
item_id_at
    The ID of the item under a point in window coordinates, or empty when
    there is none. Use it to find the target of a drag.
set_details_model
    Replaces the columns and the items together. Each item must have
    exactly one cell for each column, naming the column by ID in any
    order; the cells are stored in column order. There may be at most 64
    columns, with unique IDs of at most 256 bytes that follow the
    `StableId` segment rule, a minimum width
    of at least 40, a maximum width of at most 4096, and a width between
    them; a cell that is not `ObjectCellAvailability::available` must have
    text. The item rules of `items` apply. With an `accepted_sort`, the
    sort indicator is set too, and must name a sortable column or be empty;
    without one, the current indicator is kept if its column is still
    sortable. Any violation throws `std::invalid_argument` and changes
    nothing.
set_details_column_width
    Sets the width of one column. An unknown column, or a width outside the
    column's minimum and maximum, throws `std::invalid_argument`.
clear_selection
    Deselects every item.
select_all
    Selects every enabled item, or the first enabled item in `single` mode,
    and makes the first of them primary and focused.
arrange
    Lays out the view, keeping the first item in view when the number of
    icon columns changes.
on_pointer
    Handles the pointer input described under Notes. The window calls it.
on_key
    Handles the keyboard input described under Notes. The window calls it.
on_text_input
    Selects by typed prefix, as described under Notes. The window calls
    it.
semantic_descriptor
    Publishes the role `table` in details mode with columns (with the item
    count and the column count), otherwise `list`; the accessible name and
    description; `multiselectable` with multiple selection; and as value
    the primary ID, or ``<n> selected`` when several items are selected.
semantic_virtual_children
    Publishes the items in view named by their name, with the secondary
    text as value, and the `selected` and `focused` states: as `row` with
    their row index and a `cell` per column in details mode with columns,
    after a `column_header` per column (a sortable column's offering
    press); otherwise as `list_item`.
on_semantic_child_action
    Handles focus, select, press and show_menu on an item: the view takes
    focus; focus moves the focus rectangle, select and press select the
    item, press also activates it, and show_menu raises
    `context_requested`. A press on a sortable column's header focuses the
    header and raises `sort_requested`, as clicking it does.

See Also
--------
TreeView : A hierarchy of items, such as the folders above these objects.
CorrespondenceView : Records that expand to show matched evidence.
ListBox : A plain list of text rows.
ImageList : Shared images looked up by key.
ContextMenu : An overlay menu for the target of `context_requested`.

Notes
-----
A primary press on an item focuses the view and selects the item. In
`multiple` mode Ctrl or Command toggles the item, Shift selects the range
from the anchor instead of the current selection, and Ctrl+Shift adds that
range. A plain press on the background clears the selection. Releasing a
double-click on the same item raises `item_activated`. Disabled items
cannot be selected by input. In details mode with columns, Shift with the
wheel, or a horizontal wheel, scrolls the columns.

With focus, Left and Right move to the previous or next item, Up and Down
by one grid row (one item in details mode), Home and End to the first and
last item, and Page Up and Page Down by a page. Each key selects the item it
reaches, with the same Shift and Ctrl+Shift ranges as the pointer; with Ctrl
or Command alone it moves only the focus. Space selects the focused item,
and Ctrl+Space toggles it. Ctrl+A or Command+A selects all in `multiple`
mode. Enter activates the focused item. The keys stop at a disabled item.

Typing selects the next item, after the focused one, whose name starts with
the typed characters. Characters typed within 850 milliseconds of each
other form one prefix; letters compare without regard to ASCII case.

In details mode with columns, F6 moves the keyboard between the rows and
the header. In the header, Left, Right, Home and End move between columns,
Enter or Space requests a sort, Alt+Shift+Left and Alt+Shift+Right narrow or
widen the column by 8 pixels, and Escape or Down returns to the rows; Tab
returns to the rows and moves on to the next control. In the rows,
Alt+Shift+Left and Alt+Shift+Right scroll the columns. Dragging within 4
pixels of a column's right edge resizes it, and Escape during the drag
restores its width.

In details mode with columns the item `name` is not drawn; give the name a
column of its own. The first column's cells carry the glyph or image. A
name cut short in an icon cell, or in a details row without columns, is
shown in full in a bubble while the pointer is over the item or the item
shows the keyboard focus.

In details mode with columns the control publishes the accessibility role
`table`: the columns are `column_header` children with their column index
and sort direction (a sortable column's header takes `press`, which sorts
as a click does), and each item in view is a `row` holding a `cell` per
column with the cell text. Otherwise it publishes `list`, and each item in
view is a virtual `list_item`. Indices count from 1.

Examples
--------
Show three items as icons and select two of them, the second as primary:

.. code-block:: cpp

   std::shared_ptr<gf::ObjectView> files =
       gf::make_control<gf::ObjectView>(gf::StableId("browser.files"));
   std::vector<gf::ObjectViewItem> items;
   items.push_back({"notes", "Notes", "3 items", "Folder", gf::ObjectGlyph::folder});
   items.push_back({"report", "Report.pdf", "84 KB", "PDF document"});
   items.push_back({"photo", "Harbour.png", "2.1 MB", "PNG image", gf::ObjectGlyph::image});
   (*files).set_items(items);
   (*files).set_selected_ids({"report", "photo"}, "photo");
   // (*files).selected_id() is "photo"

Show details columns and answer sort requests. The view only asks; the
owner reorders the rows and publishes them with the sort it accepted. In a
window, clicking the Name header raises `sort_requested`; here the handler
is called directly:

.. code-block:: cpp

   struct FileTable final : gf::Component {
       std::shared_ptr<gf::ObjectView> view =
           gf::make_control<gf::ObjectView>(gf::StableId("browser.table"));
       std::vector<gf::ObjectDetailsColumn> columns;
       std::vector<gf::ObjectViewItem> rows;

       FileTable() {
           columns.push_back({.id = {"name"}, .label = "Name", .width = 180.0});
           columns.push_back({.id = {"size"}, .label = "Size", .width = 80.0,
                              .alignment = gf::ObjectColumnAlignment::right});
           rows.push_back(row("budget", "Budget.ods", "12 KB"));
           rows.push_back(row("agenda", "Agenda.txt", "1 KB"));
           (*view).view_mode = gf::ObjectViewMode::details;
           (*view).set_details_model(columns, rows);
           gf::on((*view).sort_requested(), *this, &FileTable::on_sort);
       }
       static gf::ObjectViewItem row(std::string id, std::string name, std::string size) {
           gf::ObjectViewItem item;
           item.stable_id = id;
           item.name = name;
           item.cells.push_back({{"name"}, name});
           item.cells.push_back({{"size"}, size});
           return item;
       }
       void on_sort(const gf::ObjectDetailsSort& request) {
           std::swap(rows[0], rows[1]);   // Agenda.txt before Budget.ods
           (*view).set_details_model(columns, rows, request);
       }
   };

   void example() {
       FileTable table;
       table.on_sort({{"name"}, gf::ObjectSortDirection::ascending});
       // (*table.view).details_sort.read() now shows Name, ascending
   }
