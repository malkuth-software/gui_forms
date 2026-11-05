A window's menu bar: a row of top-level menus such as File, Edit and View.

A menu strip shows one item per top-level menu. Clicking an item, pressing
its mnemonic, or focusing the strip and pressing Down opens that menu below
it; the menu itself is a `ContextMenu` overlay that runs the shared `Command`
of the item the user picks. While a menu is open, moving the pointer over
another top-level item or pressing Left or Right switches to that menu. Dock
the strip to the top of the window's root container. Use a `ContextMenu` on
its own for a menu that opens at a point, and a `CommandBar` for a row of
command buttons.

Parameters
----------
stable_id
    Identity of the strip, unique within its window. Its overlay menu is named
    ``<stable_id>.menu``.

Properties
----------
items
    The top-level menus, empty by default. Each `MenuStripItemSpec` has a
    stable ID, the text shown on the bar, its menu's `MenuItemSpec` items, and
    `enabled` and `visible` flags. `set_items` throws `std::invalid_argument`
    unless every top-level item has a unique stable ID following the
    `StableId` segment rule, nonempty valid UTF-8 text, and at least one menu
    item, and each menu is a valid `ContextMenu` item tree. Replacing the
    items closes an open menu and clears `selected_item_id` if that item is
    gone or hidden.
use_mnemonic : default ``true``
    Whether an `&` in an item's text marks its mnemonic. When false, the
    `&` is shown literally and Alt with a letter opens nothing.
item_padding : default ``11.0``
    Space on each side of an item's text, scaled with the window's text
    scale. It must be finite and between 4 and 64. Changing it closes an
    open menu.
font : default ``FontSpec{FontRole::control, 10.5, 400, false, 0.12}``
    The font of the item text. The open and the selected item are drawn
    bold, and each item is wide enough for its bold text, so items do not
    move when their weight changes. An invalid font throws
    `std::invalid_argument`. Changing it closes an open menu.
active_index : default ``std::nullopt``
    The index in `items` of the menu that is open, or no value.
selected_item_id : default ``""``
    The stable ID of a top-level item drawn as selected, independently of
    which menu is open, for example to show the current section. It must
    name a visible item, or `std::out_of_range` is thrown; an empty ID
    clears the selection.
is_open : default ``false``
    Whether one of the strip's menus is open.

Events
------
item_invoked
    Raised after the user picks a command, check or radio item in one of the
    strip's menus and its command runs. It carries the strip's ID, the
    top-level item's ID and the `ContextMenu` invocation. It follows the
    command's own `invoked` event and comes before the menu closes.
open_changed
    Raised with the index of the menu that opens and with no value when the
    open menu closes. Switching directly from one menu to another raises it
    once, with the new index.

Methods
-------
open
    `open(index, mode)` opens the menu of the item at `index` and returns
    true. It returns false, and opens nothing, when the index is out of
    range, the item is hidden or disabled, or the strip is not attached to a
    window; opening the menu that is already open returns true and does
    nothing. `MenuOpenMode::keyboard`, the default, selects the menu's first
    enabled item; `MenuOpenMode::pointer` selects nothing. An unknown mode
    throws `std::invalid_argument` without touching an open menu.
close
    Closes the open menu, if any.

See Also
--------
ContextMenu : The overlay menu each top-level item opens.
MenuStripItemSpec : The description of one top-level menu.
Command : The shared command each menu item runs.
CommandBar : A row of command buttons.

Notes
-----
The strip is as tall as its font needs, at least 24 pixels at a text scale
of 1, and as wide as the space it is given; items are laid out from the left
in order and hidden items take no space.

Input on the bar: clicking an item opens its menu, and clicking the open item
closes it. While the strip has focus, Left and Right move between the enabled
items, or switch menus when one is open; Down, Enter and Space open the
current item's menu; and Escape closes it. Alt with an item's mnemonic focuses
the strip and opens that menu; pressing it again cycles through the items that
share the mnemonic. Inside an open menu, Left and Right at the top level
switch to the neighbouring menu (Right on a submenu item opens the submenu
instead), and all other keys behave as in a `ContextMenu`. After an item is
picked or the menu is closed, focus returns to where it was before the menu
opened.

Disabled items are drawn dimmed and cannot be opened. A menu's items read
their commands' enabled, checked and visible state each time the menu
opens.

The strip publishes the accessibility role `menu_bar`, named "Application
menu" unless it has an accessible name. Each visible top-level item is a
child with the role `menu_bar_item`, named by its text without the `&`, with
the character after the `&` as its access key,
reporting the selected, expanded and focused states and offering the focus,
press, expand and collapse actions. Press and expand open the menu, and
collapse closes it.

Disposing the strip closes its menu.

Examples
--------
A File and a View menu bound to shared commands. Opening File from the
keyboard selects Save, so Enter runs it:

.. code-block:: cpp

   struct Editor final : gf::Component {
       std::shared_ptr<gf::Panel> root =
           gf::make_control<gf::Panel>(gf::StableId("editor"));
       std::shared_ptr<gf::MenuStrip> menu =
           gf::make_control<gf::MenuStrip>(gf::StableId("editor.menu"));
       std::shared_ptr<gf::Command> save =
           std::make_shared<gf::Command>("file.save", "Save");
       std::shared_ptr<gf::Command> quit =
           std::make_shared<gf::Command>("file.quit", "Exit");
       std::shared_ptr<gf::Command> wrap =
           std::make_shared<gf::Command>("view.wrap", "Word wrap");
       int saves = 0;

       Editor() {
           (*save).set_shortcut("Ctrl+S");
           (*wrap).set_checked(true);
           (*menu).set_items({
               {"editor.menu.file", "&File", {
                   {"file.save", gf::MenuItemKind::command, save, "&Save"},
                   {"file.separator", gf::MenuItemKind::separator},
                   {"file.quit", gf::MenuItemKind::command, quit, "E&xit"},
               }},
               {"editor.menu.view", "&View", {
                   {"view.wrap", gf::MenuItemKind::check, wrap, "&Word wrap"},
               }},
           });
           (*menu).dock = gf::DockStyle::top;
           (*root).add_child(menu);
           gf::on((*save).invoked(), *this, &Editor::on_save);
       }
       void on_save() { ++saves; }
   };

   void example() {
       Editor editor;
       gf::Window window(editor.root, {480.0, 320.0});
       static_cast<void>((*editor.menu).open(0U));
       gf::KeyEvent enter;
       enter.physical_key = gf::PhysicalKey::enter;
       static_cast<void>(window.dispatch_key(enter));
       // editor.saves is 1 and the menu is closed.
   }
