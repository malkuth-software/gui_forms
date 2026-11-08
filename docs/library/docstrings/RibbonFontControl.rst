A ribbon font control: a family list and a size list choosing one font.

A font control (stage RB, phase 6) is two real `ComboBox` children side by
side, a family list and a size list, that show and choose one `FontChoice`.
Bound to a `FontCommand` it shows the command's value and a choice in
either field commits to it, keeping the other half of the value; without one
it keeps its own value. A `Ribbon` hosts it as
`RibbonCommandKind::font_control` on one row.

Parameters
----------
stable_id
    Identity of the control. Its fields are named ``<stable_id>.family`` and
    ``<stable_id>.size``.

Properties
----------
families
    The families offered, in order. `set_families` throws
    `std::invalid_argument` for an empty, repeated, multi-line or invalid
    name, or more than `RibbonFontControl::maximum_families` (1024).
sizes : default 8 to 72 points
    The sizes offered, ascending. `set_sizes` throws `std::invalid_argument`
    for sizes that do not ascend or lie outside `FontCommand`'s range, or more
    than `RibbonFontControl::maximum_sizes` (64).
family_width : default ``120``
    The family field's width in logical units at text scale 1.
    `set_field_widths` sets both widths and throws `std::invalid_argument`
    outside 16 to 512.
size_width : default ``44``
    The size field's width.
natural_size
    The size the control measures at the current text scale.
command
    The `FontCommand` it shows and commits to, or null.
value
    The value shown: the command's, or the control's own. `set_value` sets it
    as the program does (the command's `set_value` when there is one).
family_box
    The family `ComboBox`.
size_box
    The size `ComboBox`.

Events
------
value_changed
    Raised after the value shown changes, by the user or the program.

See Also
--------
FontCommand : The value command it commits to.
Ribbon : The ribbon that hosts it.
ComboBox : Its two fields.

Notes
-----
A field shows empty while its half of the value is unknown or not in its
list. A refused commit (the command disabled) leaves the fields showing the
command's value, and a disabled command disables both fields. The control
publishes ``region``, ``family_field`` and ``size_field``; its fields
describe themselves to accessibility inside a group named "Font".

Not yet: typing a family or size that is not listed, and drawing each family
in its own face in the list.

Examples
--------
A font control bound to a font command:

.. code-block:: cpp

   std::shared_ptr<gf::FontCommand> command =
       std::make_shared<gf::FontCommand>(std::make_shared<gf::Command>("doc.font", "Font"));
   std::shared_ptr<gf::RibbonFontControl> font =
       gf::make_control<gf::RibbonFontControl>(gf::StableId("doc.font-control"));
   (*font).set_families({"Portsmouth Rapids", "Carlito"});
   (*font).command = command;
