A drop-down field whose list has a tick box on each item.

The field looks like a drop-down-list ComboBox and shows the checked items'
texts, in item order, joined by `separator`; its list is a CheckedListBox
in an anchored overlay below it. It chooses several items in the room of
one field, such as the columns to show. Windows Forms has no counterpart.
Items are `CheckedComboItem` records kept by stable ID.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
separator : default ``", "``
    What the field's text puts between the checked items' texts. Invalid
    UTF-8 throws `std::invalid_argument`.
placeholder_text : default empty
    The hint the field shows while nothing is checked; it describes the
    field to assistive technology.
max_drop_down_items : default 8
    The items the open list shows before it scrolls, from 1 to 64
    (`std::invalid_argument`); read when the list opens.
items
    The items, in order; `set_items` replaces them (an open list closes
    first) and throws `std::invalid_argument` for an empty, invalid or
    repeated ID, invalid UTF-8, or more than `maximum_items` items, the old
    items then staying.
dropped_down
    Whether the list is open.
drop_down_list
    The open CheckedListBox, or null while the list is closed.
checked_ids
    The checked items' IDs, in item order.
text
    The checked items' texts joined by the separator; empty while none is
    checked.

Events
------
item_check_changed
    Raised after an item is checked or unchecked (`CheckedComboItemChange`),
    from the list or from code.
drop_down_changed
    Raised after the list opens (true) or closes (false).

Methods
-------
set_item_checked
    Checks or unchecks one item. Throws `std::out_of_range` for an unknown
    item.
item_checked
    Whether an item is checked. Throws `std::out_of_range` for an unknown
    item.
set_all_checked
    Checks or unchecks every item, raising `item_check_changed` for each
    that changes.
index_of
    The position of an item, or none.
show_drop_down
    Opens the list, which takes the keyboard focus. Returns false when it
    is open, the box is not in a window or not enabled, or there are no
    items.
close_drop_down
    Closes the open list. Returns false when it is not open.

See Also
--------
CheckedListBox : The list the drop-down shows.
ComboBox : A drop-down choosing one item.
PopupContainerBox : A drop-down showing any control.

Notes
-----
A click on the field, F4, Alt+Down or Alt+Up, or the semantic expand opens
the list; Space or a click on an item toggles it, and each change applies
at once. Escape, a click away, the same keys on the field, or the semantic
collapse close it, and the focus returns to the field. The well takes the
theme's ``editor`` family and the drop-down button the ``choice`` family;
the placeholder is drawn in the secondary tone. To assistive technology the
field is a read-only ``combo_box`` node with a pop-up whose value is its
text, expanded while open and controlling the list.

Examples
--------
A field of weekdays:

.. code-block:: cpp

   std::shared_ptr<gf::CheckedComboBox> days = gf::make_control<gf::CheckedComboBox>(gf::StableId("task.days"));
   (*days).set_items({{.stable_id = "mon", .text = "Monday", .checked = true},
                      {.stable_id = "tue", .text = "Tuesday", .checked = false},
                      {.stable_id = "wed", .text = "Wednesday", .checked = false}});
   (*days).set_item_checked("wed", true);
   const std::string shown = (*days).text();
   // shown is "Monday, Wednesday"
