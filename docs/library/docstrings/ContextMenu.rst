An overlay menu of commands that the application opens at a point, such as on a right-click.

A context menu holds a tree of `MenuItemSpec` items: commands, check and
radio items, separators and submenus, each bound to a shared `Command`. You
open it with `show` next to the control it belongs to; the user picks an
item with the pointer, the keyboard or a mnemonic, the item's command runs,
and the menu closes. Clicking elsewhere or pressing Escape closes it without
running anything. The menu is a `Component`, not a control: it takes no
space in your layout and appears as a separate overlay over the window. Use a
`MenuStrip` for a window's menu bar, which opens the same kind of menu from
its top-level items.

Parameters
----------
stable_id
    Identity of the menu, following the `StableId` segment rule, or the
    constructor throws `std::invalid_argument`. The overlay's controls are named after it:
    ``<stable_id>.overlay.panel.<depth>`` for each open menu level and
    ``<stable_id>.overlay.row.<item id>`` for each row.

Properties
----------
stable_id
    The identity given to the constructor.
items
    The menu's item tree; empty by default. `set_items` validates the whole
    tree before changing anything and throws `std::invalid_argument` unless
    every item has a stable ID that is unique in the tree and follows the
    `StableId` segment rule (its row's control is named after it), and valid
    UTF-8 text; a separator has no command and no children; a submenu has
    text and children but no command; every other item has exactly one
    command and no children; nesting is at most 8 levels deep; and there are
    at most 1024 items in all. Replacing the items closes an open menu.
preferred_width : default ``120.0``
    The narrowest each menu level is, multiplied by the window's text scale;
    a level is as wide as its widest row needs (icon gutter, label, shortcut
    column and submenu arrow) and never wider than the window's client area
    less 8 pixels. It must be
    finite and between 120 and 1024, or `std::invalid_argument` is thrown.
    Changing it closes an open menu.
is_open : default ``false``
    Whether the menu is showing.

Events
------
item_invoked
    Raised after the user activates a command, check or radio item and its
    command runs. It carries the menu's ID, the item's ID, the command's ID
    and the row's control ID. It is raised after the command's own `invoked`
    event and before the menu closes; it is not raised when the command
    refuses to run because it is disabled or disposed.
open_changed
    Raised with true after the menu opens and with false after it closes,
    however it closes: by an invoked item, Escape, a click outside, `close`,
    `set_items`, or the window removing the overlay because its owner went
    away. Showing a menu that is already open closes it first, so false is
    raised before true.

Methods
-------
show
    `show(owner, window_position, mode)` opens the menu with its top-left
    corner at a point in window client coordinates, moved as needed to keep
    it 4 pixels inside the client area. `owner` is the control the menu
    belongs to and must be attached to a window, or `std::logic_error` is
    thrown. The items' command state is read at this moment: items whose
    command is hidden are left out, an item without text shows its
    command's text, submenus left empty are dropped, and separators at the
    ends or next to each other are removed; if nothing is left,
    `std::logic_error` is thrown. `MenuOpenMode::keyboard`, the default,
    selects the first enabled item; `MenuOpenMode::pointer` selects nothing,
    so Down then selects the first item and Up the last. A position that is
    not finite or an unknown mode throws `std::invalid_argument` without
    closing a menu that is already open.
close
    Closes the menu and returns focus to where it was before the menu
    opened. Does nothing when the menu is closed.

See Also
--------
MenuStrip : A menu bar whose top-level items open menus like this one.
Command : The shared command each item runs.
MenuItemSpec : The description of one menu item.
DropDownButton : A button that asks the application to open a menu.

Notes
-----
While the menu is open it holds keyboard focus. Up and Down move between the
enabled items and wrap around, Home and End jump to the first and last, Right
opens a submenu and selects its first item, Left closes the submenu (or, at
the top level, the menu), Enter or Space activates the selected item, and
Escape closes the whole menu. An
`&` in an item's text marks its mnemonic: the letter typed alone, or with
Alt, activates the item (a submenu opens). When several enabled items in the
menu share the letter, typing it selects the next of them instead, in turn.
Moving the pointer over an item selects it and opens or closes
submenus; pressing and releasing the primary button on the same item
activates it; the wheel scrolls a menu that is taller than the window; and
pressing outside the menu closes it. A submenu opens beside its item, to the
right when there is room and otherwise to the left.

Focus returns to where it was before the menu opened just before the command
runs, so a command that moves focus somewhere else keeps it there.

Activating a check or radio item runs its command but does not change the
command's checked state; the command's handler sets it with
`Command::set_checked`. A checked command shows a check mark or a radio dot.
The command's `shortcut` text appears at the right of its item, a default
action is drawn in bold and a destructive command in red. Items whose
command is disabled are drawn dimmed, cannot be selected or activated, and
a submenu whose items are all disabled is disabled too.

The overlay publishes each menu level with the accessibility role `menu` and
the name "Menu", and each item as a `menu_item` or `separator` named by its
text without the `&`. An item's description combines the command's
description, its shortcut and, when it is disabled, its availability reason.
Check and radio items, and checked command items, report the checkable
state and the checked state; the character after an item's `&` is its access
key; and a submenu reports whether it is expanded. Enabled items offer the focus and press actions; submenus offer
focus, expand and collapse; disabled items offer none.

Destroying or disposing the menu closes it. A disposed menu throws
`std::logic_error` from `set_items`, `set_preferred_width` and `show`.

Examples
--------
A context menu for a note. Opened from the keyboard, its first item is
selected, so Enter runs it:

.. code-block:: cpp

   struct Notes final : gf::Component {
       std::shared_ptr<gf::Panel> root =
           gf::make_control<gf::Panel>(gf::StableId("notes"));
       std::shared_ptr<gf::Button> note =
           gf::make_control<gf::Button>(gf::StableId("notes.item"), "Shopping list");
       std::shared_ptr<gf::Command> rename_note =
           std::make_shared<gf::Command>("note.rename", "Rename");
       std::shared_ptr<gf::Command> delete_note =
           std::make_shared<gf::Command>("note.delete", "Delete");
       gf::ContextMenu menu{"notes.context"};
       std::string last_item;

       Notes() {
           (*note).set_requested_bounds({20.0, 20.0, 160.0, 32.0});
           (*root).add_child(note);
           (*delete_note).set_destructive(true);
           menu.set_items({
               {"rename", gf::MenuItemKind::command, rename_note, "&Rename"},
               {"separator", gf::MenuItemKind::separator},
               {"delete", gf::MenuItemKind::command, delete_note, "&Delete"},
           });
           gf::on(menu.item_invoked(), *this, &Notes::on_item);
       }
       void on_item(const gf::MenuItemInvocation& invocation) {
           last_item = invocation.item_id;
       }
   };

   void example() {
       Notes notes;
       gf::Window window(notes.root, {400.0, 300.0});
       notes.menu.show(notes.note, {100.0, 60.0});
       gf::KeyEvent enter;
       enter.physical_key = gf::PhysicalKey::enter;
       static_cast<void>(window.dispatch_key(enter));
       // Rename ran, notes.last_item is "rename" and the menu is closed.
   }
