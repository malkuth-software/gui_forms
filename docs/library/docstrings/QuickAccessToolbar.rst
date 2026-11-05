The Quick Access Toolbar of a ribbon window: a small strip of command buttons the user chooses.

A Quick Access Toolbar is a `ToolStrip` of flat command buttons with a
customize button at its end. The application declares up to 20 commands with
`set_commands`; the customize menu lists every declared command with a check
that shows or hides it, then a row that moves the toolbar above or below the
ribbon. Buttons that do not fit move to the strip's overflow menu, opened
from the overflow button just left of the customize button, which always
stays visible. The toolbar shows commands only: `set_items` with anything
other than a command button throws `std::invalid_argument`.

Parameters
----------
stable_id
    Identity of the toolbar, unique within its window.

Properties
----------
commands
    The declared commands, in toolbar order, as given to `set_commands`.
    Each has its `Command`, an optional image (a command with an image shows
    only its icon, one without its text) and whether the toolbar shows it
    before the user customizes. Setting them shows those marked shown and
    replaces any customization; a missing command, a repeated command ID or
    one that breaks the `StableId` segment rule (its toolbar item and
    customize row are named after it), or more than 20 commands throws
    `std::invalid_argument`.
shown_command_ids
    The IDs of the commands the toolbar shows, in toolbar order.
    `set_shown_command_ids` shows exactly the given IDs in the given order,
    as when restoring saved state, and reports no customization. An ID that
    names no declared command is kept in place and shows nothing, so state
    saved by a later version survives a round trip. An empty, repeated or
    invalid ID, or more than 20, throws `std::invalid_argument`.
placement : default ``QuickAccessToolbarPlacement::above_ribbon``
    Whether the toolbar sits on the ribbon's tab row or below the ribbon.
    The toolbar only records the choice and reports it; the ribbon that
    hosts it applies it. `set_placement` raises `placement_changed` when the
    value changes and throws `std::invalid_argument` for a value outside the
    enumeration.
customize_menu_open : default ``false``
    Whether the customize menu is showing.
customize_menu_items
    The customize menu's rows: a check row per declared command (ID
    ``<toolbar>.customize.<command>``), a separator, and the placement row
    (ID ``<toolbar>.customize.placement``), whose text offers the other
    placement.
parts
    The toolbar's published parts, as dot members.

Events
------
customized
    Raised when a declared command is shown or hidden, from the customize
    menu or `set_command_shown`, with the toolbar's and command's IDs and
    whether the command is now shown.
placement_changed
    Raised when the placement changes, from the customize menu or
    `set_placement`, with the toolbar's ID and the new placement.

Methods
-------
command_shown
    Whether a declared command is shown. An undeclared ID throws
    `std::out_of_range`.
set_command_shown
    `set_command_shown(command_id, shown)` shows a declared command at the
    toolbar's end, or hides it, and raises `customized` when that changes
    anything. An undeclared ID throws `std::out_of_range`.
show_customize_menu
    Opens the customize menu below the customize button; returns false when
    the toolbar is not in a window.
close_customize_menu
    Closes the customize menu.

See Also
--------
ToolStrip : The strip a Quick Access Toolbar is built from.
RibbonToolTip : Enhanced tooltips for the toolbar's buttons.

Notes
-----
The toolbar is a `ToolStrip` rather than a control that holds one, so it
keeps ToolStrip's items, overflow menu, keyboard model and ``item`` item
system, and is one toolbar to accessibility. It publishes ToolStrip's parts
and ``customize_button`` (an actuator) and ``customize_glyph`` (the
`customize` mark), with ``hot`` and ``open`` states. Its buttons paint as
ToolStrip's flat buttons: a face only under the pointer or when pressed.

Keyboard: the arrow away from the strip (Down) opens the customize menu with
its first row selected; Left and Right move along the buttons as on a
ToolStrip. To accessibility the toolbar is a `toolbar` named "Quick Access
Toolbar" unless it has an accessible name, with a button per shown command
and a `menu_button` with an overlay for the customize button, which press and
expand open and collapse closes.

Use `capture_ribbon_state` and `apply_ribbon_state` to save and restore the
shown commands and placement with `serialize_ribbon_state` and
`parse_ribbon_state`.

Examples
--------
Save, Undo and Redo shown, Quick Print offered in the customize menu:

.. code-block:: cpp

   std::shared_ptr<gf::Command> save = std::make_shared<gf::Command>("file.save", "Save");
   std::shared_ptr<gf::Command> undo = std::make_shared<gf::Command>("edit.undo", "Undo");
   std::shared_ptr<gf::Command> redo = std::make_shared<gf::Command>("edit.redo", "Redo");
   std::shared_ptr<gf::Command> quick_print =
       std::make_shared<gf::Command>("file.quick-print", "Quick Print");
   std::shared_ptr<gf::QuickAccessToolbar> quick_access =
       gf::make_control<gf::QuickAccessToolbar>(gf::StableId("editor.quick-access"));
   (*quick_access).set_commands({
       {.command = save},
       {.command = undo},
       {.command = redo},
       {.command = quick_print, .shown = false},
   });
   gf::Window window(quick_access, {240.0, 25.0});
   // The customize menu now lists all four with checks; choosing Quick
   // Print shows it at the toolbar's end and raises customized.
   (*quick_access).set_command_shown("file.quick-print", true);
   // shown_command_ids() is {"file.save", "edit.undo", "edit.redo", "file.quick-print"}.
