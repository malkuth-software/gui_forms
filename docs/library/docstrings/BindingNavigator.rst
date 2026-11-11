A ToolStrip that moves through a BindingSource.

A binding navigator is a tool strip with the standard record controls:
first, previous, a position text box, the record count, next, last, add and
delete. Each action is a shared `Command`, so the same action can also sit
in a `MenuStrip` or a `CommandBar`; the navigator enables each command only
while it can act on the source's current position, count and permissions.

Parameters
----------
stable_id
    Identity of the navigator, unique within its window. Its commands are
    named ``<stable_id>.move-first``, ``.move-previous``, ``.move-next``,
    ``.move-last``, ``.add-new`` and ``.delete``.

Properties
----------
binding_source
    The `BindingSource` the navigator moves through, or null. Setting it
    shows the source's position and count at once; when the source is
    disposed the navigator lets it go and disables every action.
move_first_command
    The command that moves to the first record, enabled after the first.
    `move_previous_command` is enabled under the same rule.
move_next_command
    The command that moves to the next record, enabled before the last.
    `move_last_command` is enabled under the same rule.
add_new_command
    The command that adds a record, enabled while the source allows
    additions.
delete_command
    The command that removes the current record, enabled while there is
    one and the source allows removals.

Events
------
adding_new
    Raised by the add command. A handler supplies the row to add, a
    `BindingRow` with a value per column of the source's schema, in
    `BindingNavigatorAddNew::row`; without one nothing is added. The
    navigator adds it to the end of the source (which throws
    `std::invalid_argument` when the provider refuses it, as for a repeated
    stable ID) and moves to it, unless the filter hides it.

See Also
--------
BindingSource : The rows and currency the navigator moves through.
ToolStrip : The strip the navigator is.
Command : The shared actions.

Notes
-----
The standard items have the IDs ``move_first``, ``move_previous``,
``position`` (a hosted `TextBox`), ``count`` (a label reading "of n"),
``move_next``, ``move_last``, ``add_new`` and ``delete``, with separators
between the groups. The buttons show words (First, Previous, Next, Last,
Add, Delete): GUI.Forms ships no stock arrow images. The position box shows
the one-based position, or 0 without records. Typing a position and
pressing Enter moves there; a number outside 1 to the count, or text that
is not a number, is refused and the position shown again. Escape restores
the shown position. Replacing the items with `set_items` drops the standard
items left out; the navigator keeps updating those that remain.

The navigator publishes ToolStrip's anatomy under its own id,
``binding_navigator``, and is a `toolbar` to accessibility, named "Binding
navigator" unless it has an accessible name.

Examples
--------
A navigator whose next command also sits in a menu:

.. code-block:: cpp

   std::shared_ptr<gf::BindingNavigator> navigator =
       gf::make_control<gf::BindingNavigator>(gf::StableId("people.navigator"));
   const std::shared_ptr<gf::Command>& next = (*navigator).move_next_command();
   gf::MenuItemSpec menu_item("people.menu.next", gf::MenuItemKind::command, next, "Next person");
