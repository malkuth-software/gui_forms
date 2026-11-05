A container that lays its children out as a CSS grid container: items placed in rows and columns of sized tracks, by line, by area name or automatically.

A grid layout panel places its visible children in a grid of columns and
rows, as CSS Grid Layout does. The template lists size the explicit tracks:
fixed lengths, percentages, content keywords, `minmax()`, `fit-content()`
and flexible `fr` fractions, with `repeat(auto-fill | auto-fit, ...)`
repeating fixed tracks as often as they fit. Each child is a grid item:
its item layout places it by line numbers, spans or a named area, or leaves
it to automatic placement, which fills the next free cells in row or column
order, sparse or dense. Items outside the explicit grid add implicit
tracks, sized by the automatic track sizes. The panel sizes the columns
from the items' widths, then the rows from their heights at those widths,
aligns the tracks in the panel and each item in its area. Use it for forms,
dashboards and card layouts whose items line up on both axes; use a
`FlexPanel` for one-dimensional rows or columns. `TableLayoutPanelCompat`
is the Windows Forms table, for migrating Windows Forms designs.

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
template_columns : default no tracks
    grid-template-columns, a `GridTrackList`: the explicit columns'
    `GridTrackSize` values, at most 10,000, and at most one automatic
    repeat inserted before `auto_repeat_index`. A list with an automatic
    repeat holds only fixed tracks (a length at one end at least), and
    the repeat is counted against a definite content width (once
    otherwise). With `auto_fit`, repetitions no item occupies collapse to
    nothing, and their gaps with them.
template_rows : default no tracks
    grid-template-rows, with the rules of `template_columns` against the
    content height.
template_areas : default none
    grid-template-areas: the template's rows and columns in cells and its
    named rectangles. Names must be distinct and each area must lie within
    the template; its rows and columns add explicit tracks where the
    template lists are shorter.
auto_flow : default ``GridAutoFlow::row``
    grid-auto-flow: automatic placement fills rows (`row`) or columns
    (`column`), and with `row_dense` or `column_dense` goes back to fill
    earlier holes.
auto_columns : default one ``auto``
    grid-auto-columns: the sizes of implicit columns, one to 10,000 of
    them, repeated in order (and backwards for implicit columns before
    the explicit grid).
auto_rows : default one ``auto``
    grid-auto-rows, with the rules of `auto_columns`.
justify_content : default ``LayoutAlignment::normal``
    How the columns share the content width left over: `start`, `end`,
    `flex_start`, `flex_end`, `center`, `space_between`, `space_around`
    or `space_evenly`. `normal` and `stretch` grow the `auto` columns to
    fill it. When the tracks overflow, the three distributions start at
    the start.
align_content : default ``LayoutAlignment::normal``
    How the rows share the content height left over, with the values of
    `justify_content`.
justify_items : default ``LayoutAlignment::normal``
    How each item sits across its area's width, unless its item layout
    sets `justify_self`: `start`, `end`, `flex_start`, `flex_end` or
    `center`; `baseline` behaves as `start`; `normal` and `stretch`
    stretch an item whose width is `auto` to its area minus its margins.
align_items : default ``LayoutAlignment::normal``
    How each item sits across its area's height, unless its item layout
    sets `align_self`, with the values of `justify_items`; `baseline`
    aligns the bottoms of the baseline-aligned items that start in the
    same row.
row_gap : default ``0``
    Space between rows. A percentage is of the panel's content height, and
    zero when that height is indefinite. It must be finite and
    nonnegative.
column_gap : default ``0``
    Space between columns, with the same rules as `row_gap` against the
    content width.
layout_snapshot : read-only
    The last arrange's resolution: the columns and rows with their sizing
    functions, base sizes, growth limits, sizes and positions; how many
    implicit tracks precede the explicit grid; and for each laid-out child
    its area and bounds. It borrows the panel and changes at the next
    arrange.
laid_out_children : read-only
    The runtime identities of the children in `layout_snapshot`, in the
    same order.
layout_outcome : read-only
    How the last arrange ended: `GridSolveOutcome::solved`, or the limit
    that refused it, `refused_too_many_items` (more than 65,536 children)
    or `refused_too_many_cells` (an implicit grid of more than 1,048,576
    cells). A refused arrange lays no child out.

Methods
-------
set_item_layout
    `set_item_layout(child, layout)` sets a direct child's
    `GridItemLayout`: its `placement` (`GridItemStyle`'s four
    `GridLine` values: `automatic`, a line number other than zero, negative
    from the end, `span` of at least one, or a named area's edge);
    `justify_self` and `align_self`; its `width`, `height`, minimums and
    maximums; and which margins are `auto`. The child must be a live
    direct child, line numbers and spans at most 10,000, sizes lengths,
    percentages of the item's grid area or `auto` (`none` for the
    maximums).
item_layout
    The child's item layout, or the default one for a child without its
    own record or a control that is not a direct child.

See Also
--------
FlexPanel : Rows or columns of items that grow, shrink and wrap.
TableLayoutPanelCompat : The Windows Forms table, for migrating Windows Forms designs.
ResponsiveTrackPanel : Tracks that shrink and collapse in an authored order.

Notes
-----
An item's width contributions are its min-content and max-content
measures, and its height contribution its measure at the width its columns
give it; its automatic minimum is its min-content size where CSS Grid
applies one (in a track whose minimum is `auto`). A percentage width or
height is of the item's grid area. The child's own `minimum_size` and
`maximum_size` bound its contributions, and its used size where its own
minimum or maximum is a length or `none`. Margins are the child's
`margin`, and the default `Control` margin is 3 on every side, so set it
to zero for flush items. Hidden children take no space.

The panel keeps one item record per child, dropped when the child leaves.
A parent assigns each child a layout slot; the child's own
`requested_bounds` are never rewritten. The panel measures each item once
per change, and an item stretched over fixed columns only for its height:
a clean item's measure is answered from its cache, so resizing the panel
costs no item a measure callback when its column width did not change.
A right-to-left panel mirrors its columns.

With `auto_size` on, the panel measures to its content, as a CSS grid
container sized to fit its content; otherwise it takes its requested size,
or all available space when a requested dimension is zero. Content that
extends past the client area scrolls when `auto_scroll` is on.

Setters take effect at the next layout pass and do nothing when the value is
unchanged. Unknown or inadmissible values and non-children throw
`std::invalid_argument` without changing the panel.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description; otherwise it is left out of
the accessibility tree and its children are published in its place.

Examples
--------
Lay four buttons in a fixed 80-wide column and a flexible one:

.. code-block:: cpp

   std::shared_ptr<gf::GridPanel> form =
       gf::make_control<gf::GridPanel>(gf::StableId("form"));
   gf::GridTrackSize fixed{};
   fixed.minimum.kind = gf::TrackBreadthKind::length_percentage;
   fixed.minimum.length = gf::LengthPercentage{gf::LogicalLength{80.0}, 0.0};
   fixed.maximum = fixed.minimum;
   gf::GridTrackSize flexible{};
   flexible.maximum.kind = gf::TrackBreadthKind::fraction;
   flexible.maximum.fraction = gf::FractionFactor{1.0};
   gf::GridTrackList columns{};
   columns.tracks = {fixed, flexible};
   (*form).set_template_columns(columns);
   (*form).set_column_gap(gf::LengthPercentage{gf::LogicalLength{10.0}, 0.0});
   (*form).set_row_gap(gf::LengthPercentage{gf::LogicalLength{5.0}, 0.0});
   std::vector<std::shared_ptr<gf::Button>> buttons;
   for (int index = 0; index < 4; ++index) {
       std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(
           gf::StableId("form.button" + std::to_string(index)), "Button");
       (*button).set_requested_bounds({0.0, 0.0, 10.0, 20.0});
       (*button).margin = gf::Insets{};
       (*form).add_child(button);
       buttons.push_back(button);
   }

   gf::Window window(form, {200.0, 45.0});
   window.perform_layout();
   // buttons 0 and 2 are 80 wide at x 0, buttons 1 and 3 110 wide at x 90;
   // the rows are 20 high at y 0 and y 25

Repeat 100-wide columns as often as they fit, sharing what is left:

.. code-block:: cpp

   std::shared_ptr<gf::GridPanel> cards =
       gf::make_control<gf::GridPanel>(gf::StableId("cards"));
   gf::GridTrackSize card{};
   card.minimum.kind = gf::TrackBreadthKind::length_percentage;
   card.minimum.length = gf::LengthPercentage{gf::LogicalLength{100.0}, 0.0};
   card.maximum.kind = gf::TrackBreadthKind::fraction;
   card.maximum.fraction = gf::FractionFactor{1.0};
   gf::GridTrackList columns{};
   columns.auto_repeat = gf::AutoRepeatKind::auto_fill;
   columns.auto_repeat_tracks = {card};
   (*cards).set_template_columns(columns);
   (*cards).set_column_gap(gf::LengthPercentage{gf::LogicalLength{15.0}, 0.0});
   (*cards).set_row_gap(gf::LengthPercentage{gf::LogicalLength{10.0}, 0.0});
   std::vector<std::shared_ptr<gf::Button>> buttons;
   for (int index = 0; index < 5; ++index) {
       std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(
           gf::StableId("cards.button" + std::to_string(index)), "Card");
       (*button).set_requested_bounds({0.0, 0.0, 10.0, 20.0});
       (*button).margin = gf::Insets{};
       (*cards).add_child(button);
       buttons.push_back(button);
   }

   gf::Window window(cards, {330.0, 50.0});
   window.perform_layout();
   // three 100-wide columns at x 0, 115 and 230; buttons 0 to 2 in the
   // first row at y 0, buttons 3 and 4 in the second at y 30
