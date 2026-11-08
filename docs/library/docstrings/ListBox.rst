A scrolling list of text rows from which the user selects one row or several.

A list box shows every item at once, one row each, and lets the user pick by
clicking a row or moving through the rows with the arrow keys. In
`ListSelectionMode::multiple_extended` mode, Shift extends the selection to a
range and Ctrl or Command toggles single rows. Items are plain strings,
addressed by their index; give them stable accessibility IDs with
`set_item_stable_ids`. Use a `ComboBox` when space is short and one choice
is enough, a `CheckedListBox` when each row carries its own on/off state,
and an `ObjectView` for items with icons, columns or string identities.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID, and rows without
    their own IDs are named after it.

Properties
----------
items
    The rows, in display order, edited in place. Empty by default. Read
    them with `items.read()` (a span) or `items[i]`; write them with
    `items = {...}` (every row), `items.insert(i, text)`,
    `items.append(text)` or `items.append(rows)` (at the end),
    `items.erase(i, n)`, `items.replace(i, text)` and `items.clear()`.
    Each edit keeps the selection on its rows: an insertion or erasure
    moves the selected, active and anchor indices after it, an erasure
    drops those it removes, a replacement keeps them, and an assignment
    keeps each selected index that still exists. `selection_changed` is
    raised when the selected indices changed. An assignment or insertion
    discards IDs set with `set_item_stable_ids`; an erasure removes the
    erased rows' IDs and a replacement keeps the row's. Every item must be
    valid UTF-8, or `std::invalid_argument` is thrown, and a position
    outside the items throws `std::out_of_range`; a refused edit changes
    nothing. An edit repaints, at the next layout, only the rows in view
    it reaches: a replaced row, or the rows from an insertion or erasure
    down; an edit below the rows in view repaints nothing.
selection_mode : default ``ListSelectionMode::one``
    `ListSelectionMode::one` allows at most one selected row;
    `ListSelectionMode::multiple_extended` allows several. Switching to
    `one` while several rows are selected keeps only the active row (or
    the first selected row), and raises `selection_changed`.
selected_indices
    The indices of all selected rows, in ascending order.
selected_index
    The lowest selected index, or no value when nothing is selected. In
    `one` mode this is the selected row.
top_index : default ``0``
    The index of the first row shown at the top of the list. The mouse
    wheel moves it by three rows, and selecting a row scrolls it into view.
    Setting an index outside the items throws `std::out_of_range`; for an
    empty list only `0` is accepted.
item_height : default ``26.0``
    Height of each row in logical pixels, before text scaling. Values
    outside 12 to 256, or not finite, throw `std::invalid_argument`.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    The font of the row text. An invalid specification throws
    `std::invalid_argument`.

Events
------
selection_changed
    Raised after the set of selected rows or the active row changes,
    whether by input, by a selection method, or by an edit of `items`
    changing the selected indices. It carries a
    `ListSelectionChange` with the `previous` and `current` selected
    indices and the new `active_index`.
item_activated
    Raised when the user activates a row: on release of a primary click
    that started on the same row, on Enter, or on a semantic press. It
    carries the row index and follows the `selection_changed` the press
    caused, if any. Selection methods never raise it.

Methods
-------
set_item_stable_ids
    Gives each row its own accessibility ID, in row order. There must be
    exactly one nonempty, unique, valid UTF-8 ID per item, or
    `std::invalid_argument` is thrown. Call it after every assignment or
    insertion of `items`.
item_stable_id
    The accessibility ID of a row: its ID from `set_item_stable_ids`, or
    otherwise the list's ID followed by ``.item.`` and the index, as in
    ``"files.item.3"``. An index outside the items throws
    `std::out_of_range`.
select_index
    Selects a row and makes it active, as a click does. In `one` mode the
    row replaces the selection. In `multiple_extended` mode, `extend`
    selects the range from the anchor row to this one, `toggle` adds or
    removes just this row, and neither replaces the selection with this
    row; a plain or toggling selection also moves the anchor. The row is
    scrolled into view. An index outside the items throws
    `std::out_of_range`.
clear_selection
    Deselects every row and clears the active row and the anchor.
on_paint
    Paints the rows in view, with hover, selection and focus highlights.
arrange
    Lays out the list, scrolls the active row into view and repaints the
    rows in view that edits of `items` reached since the last layout.
on_pointer
    Handles the pointer input described under Notes. The window calls it.
on_key
    Handles the keyboard input described under Notes. The window calls it.
semantic_descriptor
    Publishes the role `list`, the accessible name and description, and as
    value the text of the lowest selected row.
semantic_virtual_children
    Publishes every row, not only those in view, as a `list_item` named by
    its text, with the `selected`, `focused` and `visible` states.
on_semantic_child_action
    Handles focus, select and press on a row: the list takes focus and the
    row replaces the selection; press then raises `item_activated`.

See Also
--------
CheckedListBox : A list box with a check box on each row.
ComboBox : One choice from a list that drops down.
ObjectView : Items with icons, details columns and string identities.
TreeView : Items arranged in an expandable hierarchy.

Notes
-----
A primary press on a row focuses the list and selects the row, using Shift
and Ctrl or Command as described for `select_index`; releasing the button
on the same row raises `item_activated`, so a single click both selects and
activates. In `one` mode the modifiers are ignored.

With focus, Up and Down move to the previous or next row, Home and End to
the first and last, and Page Up and Page Down by a page. Each key selects
the row it reaches; with Shift in `multiple_extended` mode it selects the
range from the anchor instead. When no row is active the keys count from
the first row, so Down then selects the second row. Enter raises
`item_activated` for the active row. There is no type-ahead search, and
Space does nothing.

Selection follows the rows through insertions, erasures and replacements.
After an assignment of every row a surviving selected index refers to
whichever new item now has that index.

The control publishes the accessibility role `list`; its rows are virtual
`list_item` children rather than child controls.

Examples
--------
Select a range of rows in an extended-selection list:

.. code-block:: cpp

   std::shared_ptr<gf::ListBox> languages =
       gf::make_control<gf::ListBox>(gf::StableId("options.languages"));
   (*languages).items = {"English", "French", "German", "Italian"};
   (*languages).set_selection_mode(gf::ListSelectionMode::multiple_extended);
   (*languages).select_index(1U);         // French, which becomes the anchor
   (*languages).select_index(3U, true);   // French through Italian
   // (*languages).selected_indices() is now {1, 2, 3}

Observe selection and activation with owned handlers, and give the rows
stable accessibility IDs:

.. code-block:: cpp

   struct LocationPicker final : gf::Component {
       std::shared_ptr<gf::ListBox> list =
           gf::make_control<gf::ListBox>(gf::StableId("picker.locations"));
       std::string status;
       std::size_t opened = 0;

       LocationPicker() {
           (*list).items = {"Desktop", "Documents", "Downloads"};
           (*list).set_item_stable_ids({"desktop", "documents", "downloads"});
           gf::on((*list).selection_changed(), *this, &LocationPicker::on_selection);
           gf::on((*list).item_activated(), *this, &LocationPicker::on_open);
       }
       void on_selection(const gf::ListSelectionChange& change) {
           status = change.current.empty() ? std::string("Nothing selected")
                                           : (*list).items[change.current.front()];
       }
       void on_open(std::size_t index) { opened = index; }
   };

   void example() {
       LocationPicker picker;
       (*picker.list).select_index(1U);   // picker.status is now "Documents"
   }
