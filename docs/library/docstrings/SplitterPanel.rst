One of the two panes of a `SplitContainer`, which holds that pane's content.

A splitter panel is created by its `SplitContainer` and reached through
`first_panel` or `second_panel` on the container; you do not create one
yourself. It covers exactly the space the container gives the pane, and you
add the pane's content to it with `add_child`. It places its children like a
`Panel`, by their requested bounds, Dock and Anchor, and can scroll them
with `auto_scroll`.

Parameters
----------
stable_id
    Identity of the panel. A `SplitContainer` names its panels
    ``<container id>.panel1`` and ``<container id>.panel2``.

Properties
----------
background : default ``Color::rgba(0, 0, 0, 0)``
    The color that fills the pane. The default is fully transparent, so
    nothing is drawn and whatever lies behind the container shows through.
    Because the fill follows the pane's current bounds, the colored area
    tracks the seam while the user drags it. Changing it only repaints.

See Also
--------
SplitContainer : The container that owns two splitter panels and the seam.
Panel : A general container with the same child placement.

Notes
-----
The container collapses a pane by setting its panel's `visible` property to
false and restores it by setting it back, so leave the panels' visibility to
the container and use `set_first_collapsed` or `set_second_collapsed` on the
`SplitContainer` instead.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description.

Examples
--------
Give the left pane of a split a tinted background and fill it with content:

.. code-block:: cpp

   std::shared_ptr<gf::SplitContainer> split =
       gf::make_control<gf::SplitContainer>(gf::StableId("library"));
   std::shared_ptr<gf::SplitterPanel> sources = (*split).first_panel();
   (*sources).set_background(gf::Color::rgba(225, 235, 243));

   std::shared_ptr<gf::Panel> content =
       gf::make_control<gf::Panel>(gf::StableId("library.sources"));
   (*content).dock = gf::DockStyle::fill;
   (*sources).add_child(content);
