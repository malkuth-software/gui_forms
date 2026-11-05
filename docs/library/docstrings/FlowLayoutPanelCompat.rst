For Windows Forms migration: Windows Forms' FlowLayoutPanel, laying its children out one after another in wrapping lines.

`FlowLayoutPanelCompat` exists so that a Windows Forms design keeps its
layout: an imported `FlowLayoutPanel` becomes this container, with its own
algorithm and its Windows Forms behaviours. For new work use a `FlexPanel`
(CSS flexbox: wrapping rows or columns, grow and shrink, gaps and alignment)
or a `GridPanel`.

The panel places its visible children in child order along a main axis:
left to right, right to left, top down or bottom up. When a child does not
fit in the rest of the line, it starts a new line, unless `wrap_contents` is
off.

In a right-to-left `layout_direction` the flow is laid out left to right and
mirrored: a `FlowDirection::left_to_right` flow starts at the right edge, a
vertical flow's columns start at the right, and padding and margins mirror
with it. A scrolled panel then scrolls its content toward the left.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
flow_direction : default ``FlowDirection::left_to_right``
    The main axis and the end it starts from. `FlowDirection::right_to_left`
    and `FlowDirection::bottom_up` mirror the placement of each line without
    reordering the children.
wrap_contents : default ``true``
    Whether a child that would cross the end of the line starts a new line.
    When false, only flow breaks start new lines, and a line may extend past
    the panel.
item_spacing : default ``{0.0, 0.0}``
    Extra space added between neighbouring items and between lines, on top of
    the children's own margins. For a horizontal flow, `width` separates
    items and `height` separates lines; for a vertical flow the roles swap.
    Each value must be finite and between 0 and 256.
main_alignment : default ``FlowMainAlignment::start``
    How each line distributes its free space along the main axis: at the
    start, centered, at the end, or spread between, around or evenly among
    the items. A line that holds a child with a nonzero `flex_grow` has no
    free space left to distribute, so alignment does not move it.
cross_alignment : default ``FlowCrossAlignment::start``
    How each child sits across the line: at the start, centered, at the end,
    or stretched to the line's thickness minus its margins. A line is as
    thick as its thickest child; when the panel holds a single line, the line
    fills the panel's client area inside its padding.
flow_break : child property, default ``false``
    Whether a child ends its line, a property the panel provides to its
    direct children and stores for each: `(*flow).flow_break[*child] =
    true;` makes the next child start a new line even when it would fit,
    and `(*flow).flow_break[*child].read()` reads it. A `.form` child
    writes `flow-break: true;`. A control that is not a live direct child
    is refused with `std::invalid_argument`; the value is dropped when the
    child leaves.

Methods
-------
set_flex_grow
    Gives `child` a share of the free space on its line, in proportion to its
    grow value among the line's children. A value of 0, the default, keeps
    the child at its preferred size. The value must be finite and between 0
    and 1024, and the child must be a live direct child.
flex_grow
    The grow value of `child`, or 0 for a control that is not a direct child.

See Also
--------
TableLayoutPanelCompat : Children in rows and columns with sized tracks and spans.
ResponsiveTrackPanel : Tracks that shrink and collapse in an authored order.
CommandOverflowPanel : A command row whose groups move into an overflow menu.

Notes
-----
Each child takes its preferred size plus its margins; the default `Control`
margin is 3 on every side, so set `margin` to zero for flush items. Hidden
children take no space and leave no gap. Dock and Anchor have no effect
inside a flow. A parent assigns each child a layout slot; the child's own
`requested_bounds` are never rewritten.

With `auto_size` on, the panel measures to the size of its packed content,
limited by the space available and by `minimum_size` and `maximum_size`; with
`AutoSizeMode::grow_only`, the default, it does not measure smaller than its
requested size unless the space available is smaller. Otherwise it takes its
requested size, or all available space when a requested dimension is zero.
Content that extends past the client area scrolls when `auto_scroll` is on.

The flow and break settings take effect at the next layout pass, and each
setter does nothing when the value is unchanged. Out-of-range values and
non-children throw `std::invalid_argument` without changing the panel.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description; otherwise it is left out of
the accessibility tree and its children are published in its place.

Examples
--------
Wrap three buttons with explicit spacing. The third button does not fit
after the second, so it starts the second line:

.. code-block:: cpp

   std::shared_ptr<gf::FlowLayoutPanelCompat> tools =
       gf::make_control<gf::FlowLayoutPanelCompat>(gf::StableId("tools"));
   (*tools).set_item_spacing({7.0, 9.0});
   std::shared_ptr<gf::Button> cut =
       gf::make_control<gf::Button>(gf::StableId("tools.cut"), "Cut");
   std::shared_ptr<gf::Button> copy =
       gf::make_control<gf::Button>(gf::StableId("tools.copy"), "Copy");
   std::shared_ptr<gf::Button> paste =
       gf::make_control<gf::Button>(gf::StableId("tools.paste"), "Paste");
   (*cut).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
   (*copy).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
   (*paste).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
   (*cut).margin = gf::Insets{};
   (*copy).margin = gf::Insets{};
   (*paste).margin = gf::Insets{};
   (*tools).add_child(cut);
   (*tools).add_child(copy);
   (*tools).add_child(paste);

   gf::Window window(tools, {75.0, 80.0});
   window.perform_layout();
   // cut at {0, 0}, copy at {37, 0}, paste on the next line at {0, 29}

Let two children share the free space of a line in the ratio 1 to 2:

.. code-block:: cpp

   std::shared_ptr<gf::FlowLayoutPanelCompat> row =
       gf::make_control<gf::FlowLayoutPanelCompat>(gf::StableId("row"));
   (*row).set_wrap_contents(false);
   std::shared_ptr<gf::Button> fixed =
       gf::make_control<gf::Button>(gf::StableId("row.fixed"), "Fixed");
   std::shared_ptr<gf::Button> narrow =
       gf::make_control<gf::Button>(gf::StableId("row.narrow"), "Narrow");
   std::shared_ptr<gf::Button> wide =
       gf::make_control<gf::Button>(gf::StableId("row.wide"), "Wide");
   (*fixed).set_requested_bounds({0.0, 0.0, 20.0, 10.0});
   (*narrow).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
   (*wide).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
   (*fixed).margin = gf::Insets{};
   (*narrow).margin = gf::Insets{};
   (*wide).margin = gf::Insets{};
   (*row).add_child(fixed);
   (*row).add_child(narrow);
   (*row).add_child(wide);
   (*row).set_flex_grow(*narrow, 1.0);
   (*row).set_flex_grow(*wide, 2.0);

   gf::Window window(row, {100.0, 20.0});
   window.perform_layout();
   // fixed stays 20 wide; narrow becomes 30 wide and wide 50 wide
