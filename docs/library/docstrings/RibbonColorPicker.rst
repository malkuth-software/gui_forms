A ribbon drop-down colour picker: a split button over a palette overlay.

A colour picker (stage RB, phase 6) is a small split button. Its face shows
an icon over a bar of the current colour and applies that colour again; its
arrow opens a palette overlay below it holding Automatic, a grid of palette
colours ten to a row, and the recent colours. Bound to a `ColorCommand` the
picker shows and commits the command's value; without one it keeps its own.
A `Ribbon` hosts it as `RibbonCommandKind::color_picker` on one row.

Parameters
----------
stable_id
    Identity of the picker. Its palette's content is named
    ``<stable_id>.palette``.

Properties
----------
command
    The `ColorCommand` it shows and commits to, or null.
value
    The colour shown, or none for Automatic: the command's, or the picker's
    own. `set_value` sets it as the program does.
palette : default the ten standard colours
    The palette colours. `set_palette` throws `std::invalid_argument` for an
    empty palette, a repeated colour, or more than
    `RibbonColorPicker::maximum_palette` (60).
image
    The face's icon, over the colour bar.
text : default ``"Colour"``
    The picker's accessible name, with an optional mnemonic.
natural_size
    The size the picker measures at the current text scale (34 by 22 units
    at text scale 1).
palette_open
    Whether the palette is open.
palette_overlay
    The palette's content control, null before the first opening.
realized_entries
    The open palette's entries, their parts and states, in the palette's
    local coordinates.

Events
------
value_changed
    Raised after the value shown changes, by the user or the program.
palette_open_changed
    Raised when the palette opens or closes.

Methods
-------
open_palette
    Opens the palette below the picker; false while disabled, already open or
    not attached to a window. Opened by keyboard, the keyboard starts on the
    current colour.
close_palette
    Closes the palette.

See Also
--------
ColorCommand : The value command it commits to.
Ribbon : The ribbon that hosts it.

Notes
-----
A click on the face (or Enter, Space or the press action) applies the
current colour again. The arrow, Down, F4 or the show-menu action opens the
palette. In the palette the arrows move through the entries (Up and Down to
the nearest entry of the row above or below), Home and End go to the ends,
Enter, Space or a click commits and closes, and Escape or a click away
closes without a choice; focus returns to the picker.

The picker publishes ``region``, ``face``, ``icon``, ``color_bar`` (a well),
``color_sample`` (a fill inside it carrying the value as its content colour,
absent for Automatic), ``arrow``, ``arrow_glyph`` and ``focus``, and declares
the ``palette_entry`` item system (``entry_face``, ``entry_well``,
``entry_sample`` (a fill carrying the entry's colour; none for Automatic),
``entry_label``, ``entry_focus``; hot, selected, focus_visible) whose
entries the open palette shows. To accessibility it is a `split_button` whose value
is the colour (``"#C00000"``, or ``"Automatic"``); the palette's entries are
positioned buttons, the current one selected.

Every part paints by role. The colour samples are fills with a content
colour (owner decision W-7; design language §8.5): drawn exactly as given,
also in high contrast, where their wells keep them outlined, and while the
picker is disabled, when the wells, edges and labels show the state (T2
decision E2-1).

Not yet: theme colour shades, a "More colours" dialog and live preview.

Examples
--------
A text colour picker:

.. code-block:: cpp

   std::shared_ptr<gf::ColorCommand> command =
       std::make_shared<gf::ColorCommand>(std::make_shared<gf::Command>("doc.colour", "Font colour"));
   std::shared_ptr<gf::RibbonColorPicker> picker =
       gf::make_control<gf::RibbonColorPicker>(gf::StableId("doc.colour-picker"));
   (*picker).text = "Font &colour";
   (*picker).command = command;
