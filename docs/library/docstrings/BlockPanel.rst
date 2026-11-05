A container that lays its children out as a CSS block formatting context: blocks stacked with collapsing margins, and runs of inline items broken into lines.

A block flow panel lays its visible children out as CSS normal flow does.
Each child is a block item or an inline item, as its item layout says. A
block item takes its own line across the panel: with an `auto` width it
fills the panel less its margins, and its vertical margins collapse with
its neighbours' to the larger one (a positive and a negative margin add).
A run of consecutive inline items, each an atomic box such as an
`inline-block` element or a text control, is broken into line boxes at
the boundaries between items, aligned along the line by `text_align` and
on it by each item's `vertical_align`. A nested block flow panel laid out
as a block item shares its first and last children's margins with this
panel where no padding separates them, as nested CSS blocks do. Use it
for documents and forms that read top to bottom; use a `FlexPanel`
for rows or columns that grow and shrink, and a `GridPanel` for
items that line up on both axes.

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
text_align : default ``TextAlignment::start``
    How each line box's items sit along it: `start`, `end`, `left`,
    `right` or `center`. A line wider than the panel starts at its start
    edge whatever the alignment.
line_strut : default none
    The `LineStrut` every line box starts with: the ascent and descent the
    panel's font and line height give a line above and below its
    baseline, and the x-height `middle` aligns to. When none is set the
    panel uses its font's: the `line-height: normal` line box of `font`
    at the window's text scale, as CSS's strut is. With a zero strut a
    line is as tall as its items. Its values must be finite and
    nonnegative; `clear_line_strut` returns to the font's.
font
    The font the panel's inline content is set in, for its strut: the one
    given to `set_font`, or else the theme's field typography. An invalid
    specification throws `std::invalid_argument`; `clear_font` returns to
    the theme's font.
layout_snapshot : read-only
    The last arrange's resolution: for each laid-out child its bounds and
    its line, and the line boxes with their tops, heights, baselines and
    offsets; the content's height, and the margins that collapsed through
    the panel's top and bottom. It borrows the panel and changes at the
    next arrange.
laid_out_children : read-only
    The runtime identities of the children in `layout_snapshot`, in the
    same order.
layout_outcome : read-only
    How the last arrange ended: `BlockFlowSolveOutcome::solved`, or
    `refused_too_many_items` (more than 65,536 children), which lays no
    child out.

Methods
-------
set_item_layout
    `set_item_layout(child, layout)` sets a direct child's
    `BlockFlowItemLayout`: its `level` (`BlockFlowLevel::block` or
    `inline_level`); its `width`, `height`, minimums and maximums; which
    margins are `auto`; and its `vertical_align` (`baseline`, `middle`,
    `top` or `bottom`). Sizes are lengths, percentages of the panel's
    content box or `auto` (`none` for the maximums); a length may be
    negative inside a percentage, as `calc(100% - 24px)`.
item_layout
    The child's item layout, or the default one for a child without its
    own record or a control that is not a direct child.
shared_margins
    `shared_margins(width)` reports the margins this panel's content
    collapses through its top and bottom edges when it is laid out at that
    border-box width as a block item of another block flow panel, which
    reads them; for any other panel it reports none.

See Also
--------
FlexPanel : Rows or columns of items that grow, shrink and wrap.
GridPanel : Items placed in rows and columns of sized tracks.
FlowLayoutPanelCompat : The Windows Forms flow, for migrating Windows Forms designs.

Notes
-----
A block item's `auto` left and right margins share the space its width
leaves, centring it when both are `auto`; when nothing is left, or no
margin is `auto`, it keeps its start margin. A percentage height is of
the panel's content height when the panel's height is definite (it is not
sized from its content as a nested block), and `auto` otherwise. An
inline item with an `auto` width shrinks to fit its content, between its
min-content and max-content widths. Every item's baseline is the bottom
of its margin box. A block whose height is zero and that has no content,
or a nested panel with no line box or nonempty block, lets its own top and
bottom margins collapse together. A last child's bottom margin passes
through a nested panel only while that panel's height is its content's;
a minimum or maximum that changes it keeps the margin out of both.

The child's own `minimum_size` and `maximum_size` bound its used size.
Margins are the child's `margin`, which a control keeps nonnegative, and
the default `Control` margin is 3 on every side, so set it to zero for
flush items. Hidden children take no space. A panel with `auto_scroll` on
is a formatting-context root, as a CSS scroll container is: its
children's margins stay inside it.

The panel keeps one item record per child, dropped when the child leaves.
A parent assigns each child a layout slot; the child's own
`requested_bounds` are never rewritten. A block item is asked only its
height at its used width, and a clean item's measure is answered from its
cache, so a change of the panel's height costs no item a measure
callback. A right-to-left panel starts its lines at the right, and an
over-constrained block keeps its right margin.

With `auto_size` on, the panel measures to its content; otherwise it takes
its requested size, or all available space when a requested dimension is
zero. Content that extends past the client area scrolls when
`auto_scroll` is on.

Setters take effect at the next layout pass and do nothing when the value is
unchanged. Unknown or inadmissible values and non-children throw
`std::invalid_argument` without changing the panel.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description; otherwise it is left out of
the accessibility tree and its children are published in its place.

Examples
--------
Stack three buttons whose 10-unit margins collapse between them:

.. code-block:: cpp

   std::shared_ptr<gf::BlockPanel> stack =
       gf::make_control<gf::BlockPanel>(gf::StableId("stack"));
   std::vector<std::shared_ptr<gf::Button>> buttons;
   for (int index = 0; index < 3; ++index) {
       std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(
           gf::StableId("stack.button" + std::to_string(index)), "Button");
       (*button).set_requested_bounds({0.0, 0.0, 10.0, 20.0});
       (*button).margin = {0.0, 10.0, 0.0, 10.0};
       (*stack).add_child(button);
       buttons.push_back(button);
   }

   gf::Window window(stack, {200.0, 100.0});
   window.perform_layout();
   // each button is 200 wide and 20 high, at y 10, 40 and 70

Centre a row of 80-wide inline buttons, wrapping the third:

.. code-block:: cpp

   std::shared_ptr<gf::BlockPanel> row =
       gf::make_control<gf::BlockPanel>(gf::StableId("row"));
   (*row).set_text_align(gf::TextAlignment::center);
   (*row).set_line_strut(gf::LineStrut{});   // lines as tall as their items
   gf::BlockFlowItemLayout atomic{};
   atomic.level = gf::BlockFlowLevel::inline_level;
   atomic.width = gf::SizeValue{gf::SizeKind::length_percentage,
                                gf::LengthPercentage{gf::LogicalLength{80.0}, 0.0}};
   atomic.height = gf::SizeValue{gf::SizeKind::length_percentage,
                                 gf::LengthPercentage{gf::LogicalLength{20.0}, 0.0}};
   std::vector<std::shared_ptr<gf::Button>> buttons;
   for (int index = 0; index < 3; ++index) {
       std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(
           gf::StableId("row.button" + std::to_string(index)), "Button");
       (*button).margin = gf::Insets{};
       (*row).add_child(button);
       (*row).set_item_layout(*button, atomic);
       buttons.push_back(button);
   }

   gf::Window window(row, {200.0, 60.0});
   window.perform_layout();
   // buttons 0 and 1 at x 20 and 100 on the first line (y 0); button 2
   // centred at x 60 on the second line (y 20)
