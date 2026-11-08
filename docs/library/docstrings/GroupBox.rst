A framed group of controls with a caption.

A group box draws a frame with a caption in its top-left corner around
related controls, such as a set of radio buttons. Children are added and
arranged exactly as in a `Panel`. Pressing the caption's mnemonic moves focus
into the group. Use a `Panel` when the group needs no caption, and a
`ScaledGroupBox` when the children should scale with the frame.

Parameters
----------
stable_id
    Identity of the group box, unique within its window.
initial_text
    The caption. `&` marks the next character as the mnemonic, so
    ``"&Mode"`` responds to Alt+M; write `&&` for a literal ampersand.

Properties
----------
text
    The caption drawn over the top edge of the frame, and the accessible
    name unless you set one. Setting a different value repaints the caption
    and raises `text_changed`.
font : default ``{FontRole::control, 12.0, 600, false, 0.24}``
    The caption font: the control face at 12 logical pixels, semibold, with
    0.24 pixels of letter spacing. An invalid `FontSpec` throws
    `std::invalid_argument`.
use_mnemonic : default ``true``
    Whether `&` in `text` marks a mnemonic. When false the caption is drawn
    exactly as written and Alt+key does nothing.

Events
------
text_changed
    Raised after `text` changes. It carries the new caption.

Methods
-------
on_paint
    Fills the box with `background`, draws the frame in the border colour of
    `style` with its top edge running behind the caption, and draws the
    caption in the text colour of `style`, or in its disabled text colour
    when the box or one of its parents is disabled.

See Also
--------
Panel : The same container without a caption or frame.
ScaledGroupBox : A group box whose children scale with it.
RadioButton : Radio buttons with the same parent and group name exclude each other.

Notes
-----
The caption area is not reserved: children are placed in the box's own
coordinates, starting at its top-left corner, and one at y = 0 overlaps the
caption. At the default font the caption fills roughly the top 17 logical
pixels, so place children below that, or give the box a top padding so that
docked children start below the caption.

The frame is always drawn; the inherited `border_style` is ignored. The
background is a flat fill of `background` rather than the theme's material.

Radio buttons exclude the other radio buttons with the same group name among
their siblings, so putting a set in its own group box keeps it independent of
radio buttons elsewhere.

When the user presses Alt and the mnemonic, focus moves to the first control
after the group box in tab order that can take focus, normally its first
focusable child. The box itself never takes focus. A hidden or disabled box
ignores its mnemonic.

The box is always published to accessibility as a `group`, named by its
accessible name or else by the caption without its mnemonic marker; the
character after the marker is its access key.

Examples
--------
Group two radio buttons under a caption. They are siblings in the box, so
checking one unchecks the other:

.. code-block:: cpp

   std::shared_ptr<gf::GroupBox> mode =
       gf::make_control<gf::GroupBox>(gf::StableId("export.mode"), "&Mode");
   (*mode).set_requested_bounds({12.0, 12.0, 260.0, 64.0});
   std::shared_ptr<gf::RadioButton> fast =
       gf::make_control<gf::RadioButton>(gf::StableId("export.mode.fast"), "Fast");
   std::shared_ptr<gf::RadioButton> exact =
       gf::make_control<gf::RadioButton>(gf::StableId("export.mode.exact"), "Exact");
   (*fast).set_requested_bounds({12.0, 24.0, 110.0, 24.0});    // below the caption
   (*exact).set_requested_bounds({130.0, 24.0, 110.0, 24.0});
   (*mode).add_child(fast);
   (*mode).add_child(exact);
   (*fast).checked = true;
   (*exact).checked = true;   // fast is now unchecked

Alt and the caption's mnemonic move focus into the group:

.. code-block:: cpp

   std::shared_ptr<gf::GroupBox> mode =
       gf::make_control<gf::GroupBox>(gf::StableId("export.mode"), "&Mode");
   std::shared_ptr<gf::RadioButton> fast =
       gf::make_control<gf::RadioButton>(gf::StableId("export.mode.fast"), "Fast");
   (*fast).set_requested_bounds({12.0, 24.0, 110.0, 24.0});
   (*mode).add_child(fast);

   gf::Window window(mode, {260.0, 64.0});
   window.perform_layout();
   window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::m, gf::Modifier::alt});
   // window.focused_control() is now fast
