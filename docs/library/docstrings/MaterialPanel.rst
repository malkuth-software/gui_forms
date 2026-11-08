A panel that paints a surface material of your choosing behind its children.

A material panel paints a `SurfaceMaterial`: up to eight fill layers (solid
colours, linear and radial gradients, stretched, tiled or nine-patch
images), shadows, a border or per-edge borders, keylines and a corner
radius. Its children are added and arranged as in any `Panel`, and are
drawn over the material. Use it for banners, cards with a custom look and
other decorative surfaces; use a plain `Panel` when the theme's surface is
enough.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
material : default ``SurfaceMaterial{}``
    The material painted over the whole panel. The default is a single
    opaque white fill. Setting a material that `valid_surface_material`
    rejects (no fill, more than eight fills, four shadows or eight keylines,
    gradient stops that do not rise strictly from 0 to 1, a negative corner
    radius, or a border together with per-edge borders, among others) throws
    `std::invalid_argument` and keeps the current material. While the
    panel is in a window, every image fill must name an image registered
    with that window, with its exact pixel size, or the setter throws
    `std::invalid_argument`. Setting an equal material does nothing.

Events
------
material_changed
    Raised after `material` changes, with the new material.

Methods
-------
on_paint
    Paints `material` over the panel's arranged bounds. It ignores the
    inherited `background`, `border_style`, `style` and `visual_role`.
visual_outsets
    How far the material's shadows reach beyond the panel. The panel paints
    into that area without changing its layout or hit testing.
on_attached_to_window
    Checks the image fills against the window's images when the panel is
    attached. A missing or resized image throws `std::invalid_argument`, and
    the attachment is rolled back.

See Also
--------
Panel : A surface painted from the theme.
Card : A themed header, body and footer composition.
SurfaceMaterial : The material description.

Notes
-----
Normalized gradient coordinates run from 0 to 1 across the panel's arranged
bounds, so a gradient stretches with the panel. Shadows draw outside the
panel's bounds without affecting layout or hit testing, but the panel's
parent still clips it to the parent's client area.

The panel takes no focus and has no input of its own. Like `Panel` it is
left out of the accessibility tree unless it has an accessible name or
description, when it is published as a `group`.

Examples
--------
A rounded panel with a vertical gradient, a border and a soft shadow:

.. code-block:: cpp

   gf::SurfaceMaterial banner;
   banner.fills = {gf::MaterialFillLayer::linear(
       {0.0, 0.0}, {0.0, 1.0},
       {{0.0, gf::Color::rgba(250, 252, 255)}, {1.0, gf::Color::rgba(222, 230, 240)}})};
   banner.border = gf::MaterialBorder{gf::Color::rgba(120, 140, 160), 1.0};
   banner.shadows.push_back(gf::MaterialShadow{});
   banner.corner_radius = 8.0;

   std::shared_ptr<gf::MaterialPanel> hero =
       gf::make_control<gf::MaterialPanel>(gf::StableId("welcome.hero"));
   (*hero).set_requested_bounds({16.0, 16.0, 320.0, 120.0});
   (*hero).material = banner;
   std::shared_ptr<gf::Label> title =
       gf::make_control<gf::Label>(gf::StableId("welcome.hero.title"), "Welcome");
   (*title).set_requested_bounds({16.0, 16.0, 200.0, 28.0});
   (*hero).add_child(title);   // drawn over the material
