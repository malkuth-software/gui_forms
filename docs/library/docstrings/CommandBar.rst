A toolbar row of command buttons built from a list of items.

Give the bar a list of `CommandItem` records with `set_items`; it creates one
`Button` per item and lays them out in a single row of equal widths. Clicking
an item, or pressing Space or Enter on it, runs the item's bound `Command` and
then raises `invoked` with the item. Inside the bar the arrow keys move focus
between items, and Tab leaves the bar. An item with a `shortcut` also runs its
command from the keyboard while the bar is in a window. Use a `MenuStrip` for
a menu bar, and a `CommandOverflowPanel` for groups of commands that collapse
behind an overflow button when space runs out.

Parameters
----------
id
    Identity of the bar, unique within its window. Each item's button gets
    the identity `<id>.item.<item id>`.

Properties
----------
items
    The item records, in display order; empty until the first `set_items`.
    The returned view stays valid until the next `set_items`. `set_items`
    replaces the whole list. Each record's `label` becomes its button's
    caption, prefixed by `glyph` and a space when a glyph is given, and its
    `accessible_name`, or else its label, becomes the accessible name. An item
    whose `id` was already present keeps its button, with its state and
    customisation; buttons of removed items are taken out of the bar.
    Duplicate ids, text that is not valid UTF-8, or a `shortcut` without a
    `command` throw `std::invalid_argument` before anything changes. Calling
    `set_items` from a callback that runs during a replacement throws
    `std::logic_error`; calling it from an `invoked` handler is allowed.

Events
------
invoked
    Raised when the user activates an item, after the item's bound command
    has run. It carries the item record. The reference remains valid for the
    whole notification even if a handler replaces the items, but do not keep
    it afterwards. A `shortcut` runs the command without raising `invoked`.

Methods
-------
item
    The button for the item at an index, for styling: visual style, font,
    images, recipes and so on. The reference lasts until the next
    `set_items`. An index outside the list throws `std::out_of_range`.
part
    The named part `"item"` at an index, which is `item(index)`; any other
    name returns null.

See Also
--------
Command : The action an item runs, with enabled and checked state.
Button : The control each item is presented with.
CommandOverflowPanel : Command groups that collapse behind an overflow button.
MenuStrip : A menu bar.

Notes
-----
A `CommandItem` holds `id`, `label`, `glyph`, `accessible_name`, an optional
`shortcut` and a borrowed `command`. Each item's button is bound to its
command, so the button is disabled while the command is, and shows the
selected look while a checkable command is checked. The command must outlive
the item, or be disposed first; a disposed command is never invoked.

When the user activates an item, the order is: the bound command executes,
then `invoked` is raised. An item without a command raises only `invoked`.
Shortcuts are installed when the bar is attached to a window and removed when
it is detached; they execute the command with the source id `"shortcut"`.

The bar has a single tab stop: Tab reaches one item, the first available one
until the user moves within the bar. Left and Up move focus to the previous
available item, Right and Down to the next, wrapping at the ends and skipping
items that are disabled or hidden. The bar itself never takes focus.

The bar does not size itself to its items: give it requested bounds or a
layout slot, and the items share its width equally and take its full height.

The bar publishes the accessibility role `toolbar`; each item is an ordinary
`button`.

Examples
--------
A bar with a command-backed Save and a plain Help item:

.. code-block:: cpp

   struct Editor final : gf::Component {
       gf::Command save{"file.save", "Save"};
       std::shared_ptr<gf::CommandBar> bar =
           gf::make_control<gf::CommandBar>(gf::StableId("editor.commands"));
       int saves = 0;
       int last_item = 0;

       Editor() {
           (*bar).set_items({{.id = 1, .label = "Save", .command = &save},
                             {.id = 2, .label = "Help"}});
           gf::on(save.invoked(), *this, &Editor::on_save);
           gf::on((*bar).invoked(), *this, &Editor::on_item);
       }
       void on_save() { ++saves; }
       void on_item(const gf::CommandItem& item) { last_item = item.id; }
   };

   void example() {
       Editor editor;
       (*editor.bar).item(0).perform_click();   // editor.saves is 1, then last_item is 1
       (*editor.bar).item(1).perform_click();   // last_item is 2
   }

Give an item a keyboard shortcut. It is active while the bar is in a window:

.. code-block:: cpp

   gf::Command save("file.save", "Save");
   std::shared_ptr<gf::CommandBar> bar =
       gf::make_control<gf::CommandBar>(gf::StableId("editor.commands"));
   gf::Window window(bar, gf::Size{240.0, 32.0});
   (*bar).set_items({{.id = 1,
                      .label = "Save",
                      .shortcut = stxlib::Optional<gf::KeyGesture>::of(gf::KeyGesture{gf::PhysicalKey::s, gf::Modifier::control}),
                      .command = &save}});
   window.dispatch_key(gf::KeyEvent{gf::KeyAction::down, gf::PhysicalKey::s,
                                    gf::Modifier::control});   // executes save
