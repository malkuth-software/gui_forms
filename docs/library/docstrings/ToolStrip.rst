A command strip of buttons, labels, separators and drop-down buttons.

A tool strip lays out a row of items described by `ToolStripItemSpec`
records: buttons, labels, separators, drop-down buttons that open a menu,
split buttons whose arrow opens a menu while the rest clicks, and hosted
combo boxes, text boxes and progress bars. Items that do not
fit move to an overflow menu opened from a button at the strip's end. Give an
item a shared `Command` and the strip shows the command's text, enabled,
visible and checked state, and executes it on a click; the same command can sit
in a `MenuStrip`, a `ContextMenu` and a `CommandBar`.

Parameters
----------
stable_id
    Identity of the strip, unique within its window. Its menus are shown by a
    `ContextMenu` named ``<stable_id>.menu``.

Properties
----------
content_length
    The length along the strip's axis (its width, or its height when
    vertical) that its visible items need with none overflowed: the edge
    insets, the grip, the end reserve or sizing grip and every visible item
    at its own size. A `ToolStripPanel` gives each strip but the last of a
    shared row this much.
items
    The items, in strip order. `set_items` replaces them and throws
    `std::invalid_argument` for a repeated ID or one that breaks the
    `StableId` segment rule (hosted controls and overflow menu rows are named
    after it), invalid UTF-8, a
    menu on an item that has none, a drop-down or split button without one,
    a menu item ID that repeats an item ID, a hosted item with a command or
    check, choices on an item that is not a combo box, or a width outside 0
    to 1024.
grip_visible : default ``true``
    Whether the grip shows at the strip's start.
font
    The font of item text.
orientation : default ``Orientation::horizontal``
    Horizontal strips run left to right. Vertical strips run top to bottom,
    one item per row, and are as wide as their widest item; a
    `ToolStripPanel` on the left or right turns its strips vertical. A
    `StatusStrip` stays horizontal and throws `std::logic_error` when set
    vertical.
overflow_item_ids
    The items currently in the overflow menu, in strip order.
drop_down_open
    Whether a drop-down, split or overflow menu is open.

Events
------
item_clicked
    Raised after a button or split button is clicked, by pointer, keyboard,
    accessibility or the overflow menu, and after any command it carries has
    executed. It names the strip and the item.
drop_down_item_invoked
    Raised when the user chooses from a drop-down or split button's menu, or
    from the overflow menu's copy of one. It names the item whose menu it was
    and carries the `MenuItemInvocation`.

Methods
-------
item_checked
    The checked state an item shows: its command's when it has one. An
    unknown ID throws `std::out_of_range`, as does `set_item_checked`.
set_item_text
    Replaces one item's text (a hosted item's accessible name). Throws
    `std::out_of_range` for an unknown ID and `std::invalid_argument` for
    invalid UTF-8.
combo_box
    The `ComboBox` a combo box item holds. `text_box` and `progress_bar`
    return a text box or progress bar item's control. Each throws
    `std::out_of_range` for an ID that names no item of that kind.
show_drop_down
    Opens a drop-down or split button's menu below it. Returns false for an
    item that has no menu, is disabled or is in the overflow menu.
show_overflow
    Opens the overflow menu. Returns false when nothing overflows.
close_drop_down
    Closes any open menu.

See Also
--------
StatusStrip : A strip laid out for the bottom of a window.
ToolStripContainer : Panels that hold strips around a content panel.
MenuStrip : The application's menu bar.
CommandBar : A row of command buttons that are child controls.
Command : The shared action an item carries.

Notes
-----
Items are records, not child controls: the strip lays them out as its
``item`` item system and paints every part through the by-role paint path
(`paint_part`). Each item publishes ``item_face``, ``item_icon``,
``item_label``, ``item_arrow`` (a split button's arrow), ``item_arrow_glyph``,
``item_separator`` and ``item_focus``, with the states hot, pressed, checked,
open, disabled and focus_visible. The strip itself publishes ``region``,
``face``, ``grip``, ``overflow_button``, ``overflow_glyph`` (the overflow mark,
a double chevron) and ``focus``.
Items are flat at rest: the theme's menu-item plate is transparent until an
item is hot, pressed or checked.

A click is a press and release on the same part of the same item. A check-on-
click item toggles its checked state, or its command's, before
`item_clicked`. Items overflow from the last one back, skipping items whose
`overflow` is `ToolStripOverflow::never`. In the overflow menu a button
without a command is given a private one for its row, a drop-down or split
button becomes a submenu of its menu, and labels are left out.

With focus, Left and Right move between enabled buttons, Enter and Space
click (or open a drop-down button's menu), Down opens a menu and Escape
closes it. On a vertical strip Up and Down move between buttons and Right
opens a menu, which opens beside the item. The strip publishes the accessibility role `toolbar`, with a
child per item: `button`, `static_text` or `separator`. A button that checks
on click, or is checked, also reports the checkable state.

Hosted items hold real child controls (`ComboBox`, `TextBox`,
`ProgressBar`) named ``<stable_id>.<item id>``, placed at the item's bounds
by the strip's layout. The item's `text` is the control's accessible name,
a combo box's `choices` are its items, and `width` sets the width (120 for a
combo box and 100 otherwise when zero). The application drives the control
through `combo_box`, `text_box` or `progress_bar`; `set_items` keeps a
control, with its state, while an item of the same ID and kind remains.
Hosted items publish ``item_combo_box``, ``item_text_box`` or
``item_progress_bar``, paint themselves, and describe themselves to
accessibility as children of the strip. They never move to the overflow
menu, whatever their `overflow` setting: a menu row cannot hold a control
(Windows Forms shows them inside its overflow drop-down). Arrow keys skip
them; Tab reaches them.

Not supported yet: hosted controls in the overflow menu, tool tips on items,
and dragging the grip.

Examples
--------
A strip over shared commands, with a drop-down button:

.. code-block:: cpp

   std::shared_ptr<gf::Command> save = std::make_shared<gf::Command>("doc.save", "Save");
   std::shared_ptr<gf::Command> pdf = std::make_shared<gf::Command>("doc.pdf", "PDF");
   std::shared_ptr<gf::ToolStrip> strip =
       gf::make_control<gf::ToolStrip>(gf::StableId("doc.tools"));
   std::vector<gf::ToolStripItemSpec> items;
   items.push_back({.stable_id = "tools.save", .command = save});
   items.push_back({.stable_id = "tools.rule", .kind = gf::ToolStripItemKind::separator});
   items.push_back({.stable_id = "tools.export", .kind = gf::ToolStripItemKind::drop_down_button,
                    .text = "Export",
                    .drop_down = {gf::MenuItemSpec("tools.export.pdf", gf::MenuItemKind::command, pdf)}});
   (*strip).set_items(std::move(items));
