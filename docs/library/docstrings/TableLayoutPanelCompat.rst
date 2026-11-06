For Windows Forms migration: Windows Forms' TableLayoutPanel, arranging its children in a grid of sized rows and columns.

`TableLayoutPanelCompat` exists so that a Windows Forms design keeps its
layout: an imported `TableLayoutPanel` becomes this container, with its own
algorithm and its Windows Forms track sizing. For new work use a `GridPanel`
(CSS grid: tracks, areas, spans and automatic placement) or a `FlexPanel`.

The panel divides its client area into column and row tracks. Each track is
sized absolutely, to fit its content, or as a weighted share of the space
left over, and each child occupies one cell or a span of cells. Children
without an explicit cell fill the free cells row by row, and the table grows
by rows or columns when it runs out.

In a right-to-left `layout_direction` the table is laid out left to right
and mirrored: the first column is at the right edge, a child anchored to
the left of its cell keeps to the cell's right, and padding, margins and the
cell borders mirror with it. Cell positions stay logical.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
column_count : default ``1``
    The number of authored column tracks, from 1 through 64. Out-of-range
    values throw `std::out_of_range`. Growing the count adds columns with the
    default `TableSizeMode::auto_size` style; shrinking it drops the styles
    of the removed columns.
row_count : default ``1``
    The number of authored row tracks, from 1 through 64, with the same rules
    as `column_count`.
grow_style : default ``TableLayoutGrowStyle::add_rows``
    What happens when a child does not fit the authored grid.
    `TableLayoutGrowStyle::add_rows` and `TableLayoutGrowStyle::add_columns`
    add tracks for the current layout, up to 64 per axis;
    `TableLayoutGrowStyle::fixed_size` leaves the child out. The added
    tracks use the `auto_size` style and do not change `column_count` or
    `row_count`.
cell_border_style : default ``TableCellBorderStyle::none``
    Lines drawn around and between the cells: none, a single line, or a
    two-tone inset or outset line. Grid lines sit in the middle of the
    `track_spacing` gaps. Changing it does not affect layout.
track_spacing : default ``{0.0, 0.0}``
    Space left between neighbouring columns (`width`) and rows (`height`),
    but not outside the outer tracks. Each value must be finite and between
    0 and 256.
column_styles
    The size mode and size of each authored column, one per column. Set them
    with `set_column_style`.
row_styles
    The size mode and size of each authored row. Set them with
    `set_row_style`.
column_widths
    The column widths resolved by the last layout, including any columns the
    table added. Empty before the first layout.
row_heights
    The row heights resolved by the last layout, including any rows the table
    added. Empty before the first layout.
layout_overflowed : default ``false``
    Whether the last layout left a visible child out because it found no cell
    for it.
cell : child property, no default
    A child's explicit cell, row then column from 0, a property the panel
    provides to its direct children and stores for each:
    `(*table).cell[*child] = GridCell{.row = 1U, .column = 0U};`. A child
    without one is placed automatically, and reading it then gives row 0,
    column 0. A `.form` child writes `cell: 1 0;`. A row or column of 64 or
    more throws `std::out_of_range`; a cell inside another child's span, or
    a control that is not a live direct child, throws
    `std::invalid_argument`. Moving a child carries its span with it unless
    another child is anchored at the old cell; the value is dropped when the
    child leaves.
column_span : child property, default ``1``
    How many columns the child's cell spans, from 1 through 64:
    `(*table).column_span[*child] = 2U;` (`column-span: 2;` in a `.form`).
    The table holds the spans: each is an anonymous `GridSpan` anchored at a
    child's cell (`spans()`), and a span of 1 by 1 removes it. A child needs
    an explicit cell before it can span (`std::invalid_argument`); a span
    that ends past track 64 throws `std::out_of_range`, and one that would
    overlap another span or cover another child's cell throws
    `std::invalid_argument`.
row_span : child property, default ``1``
    How many rows the child's cell spans, with the same rules as
    `column_span`.
spans
    The anonymous spans, in the order they were made, each anchored at a
    placed child's cell.
Methods
-------
set_column_style
    `set_column_style(column, style)` sets how one column is sized.
    `TableSizeMode::absolute` makes it exactly `style.size` wide, whatever its
    content. `TableSizeMode::auto_size` makes it as wide as its widest child
    plus margins, and a child that spans several columns can widen the auto-
    sized columns it covers; `style.size` is ignored. `TableSizeMode::percent`
    gives it a share of the width left after the absolute and auto-sized
    columns and the spacing, in proportion to `style.size` among all percent
    columns, so sizes of 1 and 2 split that width one third to two thirds; its
    content does not widen it. The size must be finite and nonnegative, and
    positive for `percent`, or `std::invalid_argument` is thrown; a column
    index at or beyond `column_count` throws `std::out_of_range`.
set_row_style
    The same as `set_column_style`, for one row.
cell_position
    The cell the last layout placed `child` in: its explicit cell, or the
    one automatic placement found. When the last layout placed it nowhere,
    returns its explicit cell, or no value if there is none. Reading it
    brings the layout up to date first.
clear_cell
    Returns `child` to automatic placement, the reset of `cell`. A child that
    is the only one anchored at a span is refused with
    `std::invalid_argument`; set its spans back to 1 first.
has_cell
    Whether `child` has an explicit cell.
control_from_position
    The visible child whose cell or span covers the given column and row, or
    null. Reading it brings the layout up to date first.

See Also
--------
FlowLayoutPanelCompat : Items one after another with wrapping.
ResponsiveTrackPanel : Tracks that shrink and collapse in an authored order.
ScaledPanel : Children placed in a design space that scales with the panel.

Notes
-----
Each layout places children in two passes. First, children with an explicit
cell take it, in child order; a child whose cells are already taken, or lie
outside a grid that `grow_style` cannot extend, is left out. Then the other
children fill the first free cell, scanning row by row (an automatic child
takes one cell: a span needs an explicit cell); when none is free, the table adds a row or a column as `grow_style`
allows. A child that is left out gets an empty slot at the top-left corner
and `layout_overflowed` becomes true. Hidden children take no cell, and the
added tracks disappear again when they are no longer needed.

Inside its cell, after its margins, a child keeps its preferred size, but
never more than the cell. `DockStyle::fill` fills the cell; `top`, `bottom`,
`left` and `right` stretch it along that edge. With `DockStyle::none`,
Anchor decides: anchoring both opposite edges stretches the child, one edge
aligns it to that edge, and neither centers it. The default anchor, top and
left, puts the child in the cell's top-left corner.

With `auto_size` on, the panel measures to the size its tracks need,
limited by the space available. Otherwise it takes its requested size, or
all available space when a requested dimension is zero. The panel does not
scroll its cells.

A parent assigns each child a layout slot; the child's own
`requested_bounds` are never rewritten. Setters do nothing when the value is
unchanged, and an invalid value changes nothing.

The panel is not focusable. It publishes the accessibility role `group` only
when it has an accessible name or description.

Examples
--------
A two-column form: labels in a column that fits them, fields in a column that
takes the rest of the width:

.. code-block:: cpp

   std::shared_ptr<gf::TableLayoutPanelCompat> form =
       gf::make_control<gf::TableLayoutPanelCompat>(gf::StableId("form"));
   (*form).set_column_count(2U);
   (*form).set_row_count(2U);
   (*form).set_column_style(0U, {gf::TableSizeMode::auto_size, 0.0});
   (*form).set_column_style(1U, {gf::TableSizeMode::percent, 1.0});
   (*form).set_track_spacing({8.0, 4.0});

   std::shared_ptr<gf::Label> name_label =
       gf::make_control<gf::Label>(gf::StableId("form.name.label"), "Name:");
   std::shared_ptr<gf::TextBox> name =
       gf::make_control<gf::TextBox>(gf::StableId("form.name"));
   std::shared_ptr<gf::Label> email_label =
       gf::make_control<gf::Label>(gf::StableId("form.email.label"), "Email:");
   std::shared_ptr<gf::TextBox> email =
       gf::make_control<gf::TextBox>(gf::StableId("form.email"));
   (*name).anchor = gf::AnchorStyles::left | gf::AnchorStyles::right;
   (*email).anchor = gf::AnchorStyles::left | gf::AnchorStyles::right;

   // Automatic placement fills the cells row by row.
   (*form).add_child(name_label);
   (*form).add_child(name);
   (*form).add_child(email_label);
   (*form).add_child(email);

   gf::Window window(form, {320.0, 80.0});
   window.perform_layout();
   // Each field stretches across the second column, less its margins;
   // the column's width is (*form).column_widths()[1].

A heading that spans both columns, with automatic children below it. In a
fixed-size table, a child with no free cell is left out:

.. code-block:: cpp

   std::shared_ptr<gf::TableLayoutPanelCompat> grid =
       gf::make_control<gf::TableLayoutPanelCompat>(gf::StableId("grid"));
   (*grid).set_column_count(2U);
   (*grid).set_row_count(2U);
   (*grid).set_grow_style(gf::TableLayoutGrowStyle::fixed_size);

   std::shared_ptr<gf::Label> heading =
       gf::make_control<gf::Label>(gf::StableId("grid.heading"), "Totals");
   std::shared_ptr<gf::Button> left =
       gf::make_control<gf::Button>(gf::StableId("grid.left"), "Left");
   std::shared_ptr<gf::Button> right =
       gf::make_control<gf::Button>(gf::StableId("grid.right"), "Right");
   std::shared_ptr<gf::Button> extra =
       gf::make_control<gf::Button>(gf::StableId("grid.extra"), "Extra");
   (*grid).add_child(heading);
   (*grid).add_child(left);
   (*grid).add_child(right);
   (*grid).add_child(extra);
   (*grid).cell[*heading] = gf::GridCell{.row = 0U, .column = 0U};
   (*grid).column_span[*heading] = 2U;

   gf::Window window(grid, {200.0, 80.0});
   window.perform_layout();
   gf::Control::Ptr top_right = (*grid).control_from_position(1U, 0U);
   // top_right is heading; left and right fill row 1, and
   // (*grid).layout_overflowed() is true because extra found no cell.
