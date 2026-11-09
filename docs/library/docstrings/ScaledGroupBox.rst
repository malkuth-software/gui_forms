A group box whose children keep their proportions as it is resized.

A scaled group box draws the same caption and frame as a `GroupBox`, but
places each child in a design coordinate space that you declare, and scales
every child's slot to the box's current size. A child placed at half the
design width always covers half the box, whatever size the box is given.
Use it for fixed compositions, such as a control panel drawn to a design,
that must survive resizing without per-child anchors. Use a `GroupBox` when
children should keep their own sizes, and a `ScaledPanel` for the same
behaviour without a caption.

Parameters
----------
stable_id
    Identity of the group box, unique within its window.
initial_text
    The caption, with `&` marking the mnemonic as in `GroupBox`.
initial_design_size
    The width and height of the design space. Both must be finite and
    positive, or the constructor throws `std::invalid_argument`. The default
    unit square lets you give design bounds as fractions of the box.

Properties
----------
design_size
    The size of the design space that child slots are given in. Setting it
    relays out every child; a size that is not finite and positive throws
    `std::invalid_argument`.

Methods
-------
add_at
    ``add_at(child, design_bounds)`` adds a child and gives it a slot in
    design coordinates. A null child, or bounds that are not finite or have
    a negative size, throw `std::invalid_argument`; the errors of
    `add_child` also apply.
set_design_bounds
    Moves an existing child to a new slot. A child of another parent throws
    `std::logic_error`; invalid bounds throw `std::invalid_argument`.
design_bounds
    The child's slot in design coordinates, or empty if it has none.
arrange
    Scales each slot by the box's arranged width over the design width and
    its arranged height over the design height, and gives the result to the
    child.

See Also
--------
ScaledPanel : The same proportional layout without a caption.
GroupBox : A captioned group whose children keep their requested bounds.

Notes
-----
Slots cover the whole box, from its top-left corner: neither the caption
nor the padding is reserved, so leave room for the caption in the design.
A child's own requested bounds, `dock` and `anchor` are not used and are
left unchanged.

Only children added with `add_at`, or given a slot with
`set_design_bounds`, are arranged; a child added with `add_child` alone gets
no slot. Removing a child discards its slot at the next layout.

The box does not scroll; leave `auto_scroll` off. Input, the caption
mnemonic and accessibility are those of `GroupBox`.

Examples
--------
Place a button in a 100 by 100 design space. In a 200 by 160 box its slot
is scaled by 2 horizontally and 1.6 vertically:

.. code-block:: cpp

   std::shared_ptr<gf::ScaledGroupBox> pad = gf::make_control<gf::ScaledGroupBox>(
       gf::StableId("mixer.pad"), "&Pad", gf::Size{100.0, 100.0});
   std::shared_ptr<gf::Button> play =
       gf::make_control<gf::Button>(gf::StableId("mixer.pad.play"), "Play");
   (*pad).add_at(play, {10.0, 30.0, 80.0, 30.0});

   gf::Window window(pad, {200.0, 160.0});
   window.perform_layout();
   // play is arranged at {20, 48, 160, 48}

   (*pad).set_design_bounds(*play, {10.0, 60.0, 80.0, 30.0});
   window.perform_layout();
   // play moves down to {20, 96, 160, 48}
