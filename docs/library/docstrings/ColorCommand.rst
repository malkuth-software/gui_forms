The value command of a colour picker: a shared Command, a colour and the recent colours.

A colour command is a ribbon value command (owner decision RB-13). It holds a
shared `Command`, whose identity and state it shares, and adds a typed
colour, or none for Automatic (the program's default colour). Committed
colours become the recent colours, most recent first, which every
`RibbonColorPicker` bound to the command shows.

Parameters
----------
command
    The shared `Command`. A null command throws `std::invalid_argument`.

Properties
----------
command
    The shared `Command`.
stable_id
    The command's stable ID.
state
    The command's state.
value : default none
    The colour, or none for Automatic. `set_value` is the program's change:
    it raises `value_changed` when the value differs and invokes nothing.
recent_colors
    Recently committed colours, most recent first, without repeats, at most
    `ColorCommand::maximum_recent_colors` (10).

Events
------
value_changed
    Raised after the value changes, by `set_value` or `commit`.

Methods
-------
commit
    The user's choice: sets the value (raising `value_changed` when it
    differs), moves a colour to the front of the recent colours, then
    executes the command with the given source. Returns false, changing
    nothing, while the command is disabled. Automatic is a value but not a
    recent colour.
clear_recent_colors
    Forgets the recent colours.

See Also
--------
RibbonColorPicker : The picker that shows and commits it.
FontCommand : The font control's value command.

Notes
-----
Events arrive on the UI thread; on a commit `value_changed` comes first and
then the command's `invoked`. There is no live preview.

Examples
--------
A text colour command:

.. code-block:: cpp

   std::shared_ptr<gf::Command> colour = std::make_shared<gf::Command>("doc.colour", "Font colour");
   std::shared_ptr<gf::ColorCommand> command = std::make_shared<gf::ColorCommand>(colour);
   (*command).commit(stxlib::Optional<gf::Color>::of(gf::Color::rgba(192, 0, 0)), "doc.picker");   // dark red, now the first recent colour
