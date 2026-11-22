A multi-column lookup: a drop-down field that chooses a row by its key and
shows one of the row's cells, with a list of every column under headers
when open.

The field is not editable. While the list is open, typing filters it to the
rows any of whose cells contain the typed text (ignoring ASCII case),
Backspace removes the last typed character, and the filter shows in the
field. Use a `ComboBox` for a list of single values.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
selected_key : default empty
    The key of the chosen row, or empty for none. A key no row has throws
    `std::invalid_argument`.
display_column : default ``0``
    The column whose cell the field shows for the chosen row. A column the
    rows lack shows nothing.
max_drop_down_rows : default ``8``
    The rows the open list shows before it scrolls, from 1 to 64; outside
    that range throws `std::invalid_argument`.
columns
    The columns (`LookupColumn`: a header and a width in logical units).
    `set_columns` replaces them; a header that is not valid UTF-8 or a width
    that is not finite and positive throws `std::invalid_argument` and
    changes nothing.
rows
    The rows (`LookupRow`: the key the application stores and one cell per
    column; missing cells show empty). `set_rows` replaces them; keys must
    be distinct and not empty and every text valid UTF-8, or it throws
    `std::invalid_argument` and changes nothing. A selected key no row has
    any more is cleared, raising `selected_key_changed`.
shown_text
    The text the field shows: the filter while the list is open and typed
    into, else the chosen row's display cell.
is_dropped_down
    Whether the list is open.
filter
    The typed filter while the list is open; empty when closed.
filtered_rows
    The rows the open list shows, as indices into `rows`, in order.
highlighted
    The highlighted position in `filtered_rows`, or none.

Events
------
selected_key_changed
    Raised after `selected_key` changes, with the new key.
drop_down_changed
    Raised when the list opens (true) or closes (false).

Methods
-------
row_of
    The index of the row with a key, or none.
open_drop_down
    Opens the list.
close_drop_down
    Closes the list without choosing.

See Also
--------
ComboBox : A drop-down list of single values.

Notes
-----
The keys follow a Windows Forms `ComboBox` in its drop-down-list style:
Alt+Down or F4 opens and closes the list; with it closed, Up and Down choose
the previous or next row at once; with it open, Up, Down, Page Up, Page
Down, Home and End move the highlight, Enter or Alt+Up chooses the
highlighted row and closes, and Escape closes without choosing. A press on
the field toggles the list, a press on a row chooses it, and a press
elsewhere closes the list. Keyboard focus stays on the field throughout.

To assistive technology it is a combo box whose value is the shown cell,
expanded while the list is open. A theme draws the field as ``well`` and
the drop-down ``actuator`` with its chevron ``glyph`` (choice family); the
open list's ``face`` (menu-item family), its ``header`` captions, its rows
(``item``, selected when highlighted) and their cells' ``label`` parts.
