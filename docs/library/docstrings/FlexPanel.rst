A container that lays its children out as a CSS flex container: lines of items that grow, shrink and align along a row or column.

A flex layout panel places its visible children along a main axis, a row or
a column, as CSS Flexbox does. Each child is a flex item: it starts at its
flex basis, then grows into the line's free space or shrinks when the line
overflows, in proportion to its grow and shrink factors, within its minimum
and maximum sizes. With wrapping on, items that do not fit start new lines.
The panel then aligns the items along each line, the lines across the
panel, and each item across its line. Use it for toolbars, form rows and
cards whose items share space by rules, and a `GridPanel` when items must
line up in columns. `FlowLayoutPanelCompat` is the Windows Forms flow, for
migrating Windows Forms designs.

A child's record can also position it, as CSS's `position` does. A
relative child is offset by its insets after layout without moving its
neighbours. An absolute child leaves the flow and is placed by its insets,
size and margins in the padding box of its nearest positioned box: this
panel when its own parent positions it, otherwise a positioned ancestor,
or the window's client area. When that box is not this panel, the window
records a `containing_block_not_parent` warning in
`Window::layout_diagnostics()`, once for each change of the box.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
flex_direction : default ``FlexDirection::row``
    The main axis and the end it starts from: `row`, `row_reverse`,
    `column` or `column_reverse`. A row follows the effective layout
    direction, so a right-to-left panel starts its rows at the right edge.
flex_wrap : default ``FlexWrap::nowrap``
    Whether items that overflow the main axis start new lines. With
    `nowrap` the panel holds one line and its items shrink to fit;
    `wrap_reverse` stacks the lines from the cross end.
justify_content : default ``LayoutAlignment::normal``
    How each line distributes its free space along the main axis: `start`,
    `end`, `flex_start`, `flex_end`, `center`, `space_between`,
    `space_around` or `space_evenly`. `normal` and `stretch` behave as
    `flex_start`. When the items overflow the line, the three
    distributions start at the start; a lone item is centred by
    `space_around` and `space_evenly`.
align_content : default ``LayoutAlignment::normal``
    How the lines share the panel's cross space, with the values of
    `justify_content`; `normal` and `stretch` grow the lines to fill it. A
    single-line panel's line always fills the cross space.
align_items : default ``LayoutAlignment::normal``
    How each item sits across its line, unless its item layout sets
    `align_self`: `start`, `end`, `flex_start`, `flex_end`, `center` or
    `baseline`; `normal` and `stretch` stretch an item whose cross size is
    `auto` to the line's thickness minus its margins.
row_gap : default ``0``
    Space between lines of a row, or between items of a column. A
    percentage is of the panel's content height, and zero when that height
    is indefinite. It must be finite and nonnegative.
column_gap : default ``0``
    Space between items of a row, or between lines of a column, with the
    same rules as `row_gap` against the content width.
layout_snapshot : read-only
    The last arrange's resolution: for each laid-out child its flex base
    size, hypothetical and final main sizes, cross size, bounds, line and
    the reason its size was frozen; and the lines with their extents. It
    borrows the panel and changes at the next arrange.
laid_out_children : read-only
    The runtime identities of the children in `layout_snapshot`, in the
    same order.

Methods
-------
set_item_layout
    `set_item_layout(child, layout)` sets a direct child's `FlexItemLayout`:
    its `flex` grow and shrink factors, basis and order; `align_self`; its
    `width`, `height`, minimums and maximums; and which margins are `auto`.
    The child must be a live direct child, the factors finite and
    nonnegative, and the maximums may not be `auto`.
item_layout
    The child's item layout, or the default one for a child without its
    own record or a control that is not a direct child.

See Also
--------
GridPanel : Children in rows and columns of CSS grid tracks, areas and spans.
FlowLayoutPanelCompat : The Windows Forms flow, for migrating Windows Forms designs.
ResponsiveTrackPanel : Tracks that shrink and collapse in an authored order.

Notes
-----
An item's content size is its max-content measure, and its automatic
minimum is its min-content size along the main axis, as in CSS; set
`min_width` or `min_height` to a length to let it shrink further. The
child's own `minimum_size` and `maximum_size` bound it after the item
layout's sizes. Margins are the child's `margin`, and the default `Control`
margin is 3 on every side, so set it to zero for flush items. Hidden
children take no space.

Items are laid out in `order`, then child order, and the panel keeps one
item record per child, dropped when the child leaves. A parent assigns each
child a layout slot; the child's own `requested_bounds` are never
rewritten. The panel measures each item once per change: a clean item's
measure is answered from its cache, so resizing the panel costs no item a
measure callback when its constraint did not change.

With `auto_size` on, the panel measures to its content, as a CSS flex
container sized to fit its content; otherwise it takes its requested size,
or all available space when a requested dimension is zero. Content that
extends past the client area scrolls when `auto_scroll` is on. A panel with
more than 65,536 children lays none of them out.

Setters take effect at the next layout pass and do nothing when the value is
unchanged. Unknown or inadmissible values and non-children throw
`std::invalid_argument` without changing the panel.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description; otherwise it is left out of
the accessibility tree and its children are published in its place.

Examples
--------
Let two of three buttons share a row's free space in the ratio 1 to 2:

.. code-block:: cpp

   std::shared_ptr<gf::FlexPanel> row =
       gf::make_control<gf::FlexPanel>(gf::StableId("row"));
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
   gf::FlexItemLayout one_share{};
   one_share.flex.grow = gf::FlexFactor{1.0};
   gf::FlexItemLayout two_shares{};
   two_shares.flex.grow = gf::FlexFactor{2.0};
   (*row).set_item_layout(*narrow, one_share);
   (*row).set_item_layout(*wide, two_shares);

   gf::Window window(row, {100.0, 20.0});
   window.perform_layout();
   // fixed stays 20 wide; narrow becomes 30 wide and wide 50 wide;
   // all three stretch to the row's 20 height

Push the last button to the end of the row with an automatic start margin:

.. code-block:: cpp

   std::shared_ptr<gf::FlexPanel> bar =
       gf::make_control<gf::FlexPanel>(gf::StableId("bar"));
   (*bar).set_align_items(gf::LayoutAlignment::center);
   std::shared_ptr<gf::Button> save =
       gf::make_control<gf::Button>(gf::StableId("bar.save"), "Save");
   std::shared_ptr<gf::Button> close =
       gf::make_control<gf::Button>(gf::StableId("bar.close"), "Close");
   (*save).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
   (*close).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
   (*save).margin = gf::Insets{};
   (*close).margin = gf::Insets{};
   (*bar).add_child(save);
   (*bar).add_child(close);
   gf::FlexItemLayout pushed{};
   pushed.auto_margin.left = true;
   (*bar).set_item_layout(*close, pushed);

   gf::Window window(bar, {200.0, 30.0});
   window.perform_layout();
   // save at {0, 5}, close pushed to {160, 5}, both 40 by 20
