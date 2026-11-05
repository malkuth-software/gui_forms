A plain surface that holds and arranges child controls.

A panel groups controls on a themed background. Its children are placed by
their requested bounds, `dock` and `anchor`, and with `auto_scroll` on the
panel scrolls them when they do not fit. The user does not interact with the
panel itself: it takes no focus and has no input of its own beyond its
scroll bars. Use a `GroupBox` when the group needs a visible caption, a
`FlexPanel` or `GridPanel` to arrange children automatically,
and a `MaterialPanel` to paint a custom gradient, image or shadow.

Parameters
----------
stable_id
    Identity of the panel, unique within its window. Logs, automation and
    the accessibility tree address the panel by this ID.

Properties
----------
border_style : default ``BorderStyle::none``
    The edge drawn around the panel: none, a one-pixel `BorderStyle::line`,
    or a two-tone `BorderStyle::sunken` or `BorderStyle::raised` bevel. An
    unknown value throws `std::invalid_argument`.
background
    The fill colour. Until you call `set_background` it comes from the
    theme's surface for `visual_role`. `clear_background` returns to the
    theme.
has_background_override : default ``false``
    Whether `set_background` has replaced the theme's background.
style
    The colours used for the border: your `set_style` value, or else the
    theme's basic style.
has_style_override : default ``false``
    Whether `set_style` has replaced the theme's colours.
visual_role : default ``ControlVisualRole::panel``
    Which themed surface the panel paints: `ControlVisualRole::panel`,
    `ControlVisualRole::window` or `ControlVisualRole::card`. Any other role
    throws `std::invalid_argument`.

Methods
-------
clear_background
    Removes the `set_background` override so the theme's surface is painted
    again.
clear_style
    Removes the `set_style` override.
paint_panel
    For subclasses. Paints the panel's background and border into a
    rectangle, so an `on_paint` override can draw the standard surface
    first.

See Also
--------
GroupBox : A panel with a caption and a frame.
MaterialPanel : A panel that paints a `SurfaceMaterial` of your own.
FlexPanel : Arranges children in wrapping rows or columns.
GridPanel : Arranges children in a grid of tracks and areas.
ScrollableControl : The scrolling behaviour every panel inherits.

Notes
-----
Children are added with `add_child`, owned by the panel, and painted in the
order they were added, so the last one added is drawn on top. Each child
sits at its requested bounds, given in the panel's coordinates. A child with
a `dock` style takes an edge of the panel instead; docked children claim
their edges in the order they were added, so the first docked child gets the
outermost strip and a `DockStyle::fill` child added last takes what remains.
A child's `anchor` (top and left by default) keeps its distance to the
anchored edges when the panel is resized. The panel's padding insets the
area that docked children fill and that anchored children keep their
distances to. Hidden children take no space.

With no override the panel paints the theme's material for `visual_role`,
then the border. Setting a background or a style replaces that material
with a flat fill of `background`.

The panel is never focused: clicking an empty part of it leaves focus where
it was. Children are painted and hit-tested only inside the panel's
viewport, which excludes any visible scroll bars. See `ScrollableControl`
for scrolling, the mouse wheel and the scroll bars.

An unnamed panel is left out of the accessibility tree and its children are
published in its place. Give it an accessible name or description and it is
published as a named `group` containing them.

`set_style(BasicControlStyle)` hides `Control`'s
``set_style(ControlStyles, bool)``; call that one as
``(*panel).Control::set_style(...)``.

Examples
--------
Build a window layout from docked panels. Each docked child takes an edge in
the order it was added, and the fill child added last takes the rest:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> shell = gf::make_control<gf::Panel>(gf::StableId("shell"));
   std::shared_ptr<gf::Panel> toolbar = gf::make_control<gf::Panel>(gf::StableId("shell.toolbar"));
   std::shared_ptr<gf::Panel> status = gf::make_control<gf::Panel>(gf::StableId("shell.status"));
   std::shared_ptr<gf::Panel> content = gf::make_control<gf::Panel>(gf::StableId("shell.content"));
   (*toolbar).set_requested_bounds({0.0, 0.0, 0.0, 32.0});   // only the height is used
   (*toolbar).dock = gf::DockStyle::top;
   (*status).set_requested_bounds({0.0, 0.0, 0.0, 22.0});
   (*status).dock = gf::DockStyle::bottom;
   (*content).dock = gf::DockStyle::fill;
   (*shell).add_child(toolbar);
   (*shell).add_child(status);
   (*shell).add_child(content);

   gf::Window window(shell, {400.0, 300.0});
   window.perform_layout();
   // toolbar {0, 0, 400, 32}, status {0, 278, 400, 22}, content {0, 32, 400, 246}

Give a panel a fixed colour and a border, and a name so that assistive
technology announces it as a group:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> swatch = gf::make_control<gf::Panel>(gf::StableId("swatch"));
   (*swatch).set_requested_bounds({12.0, 12.0, 120.0, 80.0});
   (*swatch).set_background(gf::Color::rgba(250, 240, 200));
   (*swatch).set_border_style(gf::BorderStyle::line);
   (*swatch).accessible_name = "Highlight colour";
