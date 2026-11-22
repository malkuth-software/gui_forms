A colour picker: a saturation and brightness field for the current hue, a
vertical hue track, an optional alpha track and a swatch of the value.

The swatch shows the value over a checkerboard, so transparency is visible.
`ColorDialog`'s hosts and `ColorValueEditor`'s drop-down use the picker; use
a `ColorDialog` to ask for a colour in a dialog of its own.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
initial_value
    The colour the picker starts with; opaque white by default.

Properties
----------
value : default opaque white
    The picked colour. Picking writes it; a program's write moves the thumbs
    (keeping the hue when the colour is grey, and the saturation when it is
    black).
alpha_enabled : default ``true``
    Whether the alpha track is shown and the alpha can be picked; hiding it
    leaves the value's alpha as it is.
hsv
    The thumbs' position as a `ColorPickerHsv`: hue in degrees from 0 up to
    360, saturation and brightness from 0 to 1. `set_hsv` moves the thumbs
    and writes the value, keeping its alpha; a component that is not finite
    or outside its range throws `std::invalid_argument` and changes nothing
    (a hue of 360 is taken as 0).
dragged_part
    The part a pointer press is dragging (`ColorPickerPart`), or none.

Events
------
value_changed
    Raised after `value` changes, with the new colour, during a drag too.
value_committed
    Raised when a pick finishes: a drag's release or a key edit. Not raised
    by a program's write of `value`.

Methods
-------
color_of
    The colour of an HSV position with a given alpha.
hsv_of
    The HSV position of a colour, keeping a previous position's hue for a
    grey and its saturation for black.

See Also
--------
ColorDialog : A colour asked for in a dialog.
ColorValueEditor : A colour field with the picker in its drop-down.

Notes
-----
A primary press on the field, the hue track or the alpha track picks there
and drags that part until the release; each move writes `value`. With the
keyboard, Left and Right move the saturation and Up and Down the brightness
by 1/100 (1/10 with Shift); Page Up and Page Down move the hue by 5 degrees
(30 with Shift); while `alpha_enabled`, Home and End move the alpha by 1/16
down or up, and Shift with Home or End sets it to 0 or 255. Each key edit
writes the value and raises `value_committed`.

The picker keeps its HSV position apart from the value, so a grey keeps the
hue it was picked at. Every area is a theme part: ``field``, ``hue_track``,
``alpha_track`` and ``swatch`` are wells carrying their content colours,
which the default and familiar themes draw as gradients over a
checkerboard, in forced high contrast too; a theme without those rules
draws plain wells. The thumbs are ``field_thumb``, ``hue_thumb`` and
``alpha_thumb``.
