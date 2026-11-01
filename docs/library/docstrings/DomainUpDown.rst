A text field with up and down buttons that steps through a list of strings.

A domain up-down shows one of its `items` in an `editor` text box beside a
pair of spin buttons. The items are a list read top to bottom: the upper
button, the Up key and the wheel turned up move to the previous item, and the
lower button, the Down key and the wheel turned down move to the next. Use it
for a short, ordered set of choices where stepping is natural, such as modes
or units; use a `ComboBox` when the user should see the whole list.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The editor is created
    as ``<stable_id>.editor`` and the spin buttons as ``<stable_id>.spinner``.

Properties
----------
items
    The strings, in display order. `set_items` replaces them, `add_item`
    appends one and `remove_item_at` removes one; an index past the end
    throws `std::out_of_range`.
selected_index : default none
    The item the text equals, or none. `set_selected_index` selects an item
    and shows its text, or clears the selection and keeps the text; an index
    past the end throws `std::out_of_range`.
text
    The editor's text. `set_text` sets it and selects the item it equals,
    even while `read_only` is on.
wrap : default ``false``
    Whether stepping past the last item continues at the first, and before
    the first at the last. Without it a step stops at the end.
read_only : default ``false``
    Whether the field refuses text. A typed character then selects the next
    item that starts with it, ignoring ASCII case and wrapping past the last
    item; otherwise the value changes only by stepping or from the program.
sorted : default ``false``
    Whether the items are kept sorted: ignoring ASCII case, then by byte, so
    ``"Apple"`` sorts before ``"apple"``. Turning it on sorts the list;
    turning it off keeps the current order.
button_width : default ``18``
    The width of the spin buttons, from 12 to 64; a width outside that range
    throws `std::out_of_range`.
editor
    The `TextBox` that shows the text.

Events
------
selected_index_changed
    Raised after `selected_index` changes, whatever the cause: stepping,
    typing, a setter or a change to the items. It carries the new selection.

Methods
-------
up_button
    Selects the previous item, or the last when nothing is selected.
down_button
    Selects the next item, or the first when nothing is selected.
initialize_control_tree
    Creates the editor and the spin buttons. `make_control` calls it; do not
    call it yourself.

See Also
--------
NumericUpDown : The same field and buttons for a number.
ComboBox : A field with a drop-down list of choices.

Notes
-----
Its `tool_tip_text` shows as grey hint text in the edit field when the
field is empty and has focus, in place of a tooltip bubble.

The selection follows the text. Text typed into the editor selects the item
it equals, ignoring ASCII case, and clears the selection when it equals no
item; the text the user typed stays as typed. Removing the selected item
clears the selection and keeps its text in the field.

A read-only field keeps its editor writable and refuses every edit through
the editor's edit filter (`TextBox::set_edit_filter`), which also sees the
character the user typed: that is how the field finds the next item starting
with it. The editor is not exposed to accessibility, so the read-only state
is published by the control itself.

The control publishes the accessibility role `combo_box` with the text as its
value and the read-only state. `SemanticAction::increment` moves to the next
item and `SemanticAction::decrement` to the previous; `SemanticAction::set_value`
selects the item that equals its text, sets the text of an editable field to
anything else, and refuses non-matching text on a read-only field.

The spin buttons are drawn by the by-role paint path: each is an `actuator`
part with an arrow `glyph`, published as ``up_arrow``, ``down_arrow``,
``up_glyph`` and ``down_glyph`` with their hot and pressed states.

Examples
--------
A mode field that wraps from the last mode back to the first:

.. code-block:: cpp

   std::shared_ptr<gf::DomainUpDown> mode =
       gf::make_control<gf::DomainUpDown>(gf::StableId("radio.mode"));
   (*mode).set_items({"AM", "FM", "USB", "LSB"});
   (*mode).set_wrap(true);
   (*mode).set_selected_index(3U);   // the field shows "LSB"
   (*mode).down_button();            // wraps: the field shows "AM"
