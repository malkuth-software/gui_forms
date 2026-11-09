# Layout

Layout in GUI.Forms is a two-phase retained pass, measure then arrange, that
runs for dirty controls when layout is flushed, including when an update scope
closes, a read barrier demands it, or a paint needs current geometry. Three
containers lay their children out by the rules of CSS: `FlexPanel` (Flexbox),
`GridPanel` (Grid) and `BlockPanel` (block and inline flow). Each one holds a
subset of the CSS properties and is checked against Chromium. The other
containers keep the Windows Forms model of absolute bounds, Dock and Anchor,
and every container shares one base `Control` policy.

## Coordinates and units

All layout runs in logical pixels. The window has a `scale` (1.0, 2.0, and
fractional values on Windows and Linux) that converts logical pixels to device
pixels at paint time. Controls never see device pixels except through the
`Painter`.

Layout is identical at every scale. Arranged bounds are unsnapped logical
doubles. The painter snaps each edge to a device pixel, so neighbours share
an edge with no gap or overlap.

Bounds are `Rect{x, y, width, height}` in the parent's client space:
- `requested_bounds` is what you set;
- `arranged_bounds` is what the last layout produced;
- `absolute_bounds` is in window client coordinates.

Reading arranged or absolute geometry may trigger the layout read barrier.

## Base policy on every control

Every `Control` carries the inputs the containers consume:

| Input | Meaning |
|---|---|
| `requested_bounds`, `set_bounds(rect, BoundsSpecified)` | Authored position and size; partial masks update only x, y, width or height atomically |
| `minimum_size`, `maximum_size` | Hard clamps applied after measure |
| `margin`, `padding` | Outside and inside spacing in logical pixels, finite and nonnegative |
| `auto_size`, `auto_size_mode` | Grow-only or grow-and-shrink to the preferred size |
| `dock`, `anchor` | Edge-relative placement inside the parent |
| `layout_direction` | Inherited, left to right, or right to left (see [Right to left](#right-to-left)) |
| `visible` | Hidden children take no space in any container's layout |

A parent assigns a child a private layout slot; it never overwrites the
child's authored `requested_bounds`. That is why repeated layout and resize
are stable: the authored input is still there to measure from.

## The measure contract

A container asks a child for its size with a `LayoutConstraint`: one
`AvailableExtent` per axis, which is one of four kinds:
- `definite`: exactly this extent;
- `at_most`: no more than this extent;
- `min_content`: the narrowest the child can be without overflowing;
- `max_content`: the width the child takes with no limit.

`Control::measure_for_layout(constraint)` is the entry the containers use.
It keeps four answers per control: one min-content width, one max-content
width and the two most recent other queries. Each answer is stamped with
the control's measure generation, and any measure invalidation of the
control advances the generation, so an answer never outlives the change
that made it stale. A clean control is asked the same question again at
no cost.

A miss calls the protected virtual `measure_intrinsic(constraint)`. The
base answers with the control's preferred size, for both content kinds,
because most controls cannot rewrap. `Label` overrides it:
- its min-content width is its widest word;
- its max-content width is its widest unwrapped paragraph.

Two predicates bound how far a change travels. Both are `true` by default,
which keeps every control's propagation as it was:
- `size_depends_on_content()`;
- `sizes_children_from_content()`.

A measure invalidation stops at the first ancestor for which neither
holds. That ancestor is marked for arrange only.

## The CSS containers

The three CSS containers take the property names and the initial values of
CSS. The typed records are in `gui_forms/layout/style/layout_style.hpp`.
Each container has container properties set with setters, and one item
record per child, set with `set_item_layout(child, record)` and dropped
when the child leaves. A child's own `margin` is its CSS margin, and its
`minimum_size` and `maximum_size` still bound it. Lengths are logical pixels.
Percentages are of the container's content box, and `calc()` of a length
and a percentage is exact.

`FlexPanel`
: CSS Flexbox, as in Flexbox section 9.
  - Container properties: `flex_direction`, `flex_wrap`, `justify_content`,
    `align_content`, `align_items`, `row_gap` and `column_gap`.
  - Item record: a `FlexItemLayout`, holding `flex` (grow, shrink, basis
    and `order`), `align_self`, width, height and their minimums and
    maximums, and `auto` margins.
  - An item's automatic minimum size is its content's minimum.
  - Items align on their first baseline with `align-items: baseline`.

`GridPanel`
: CSS Grid.
  - Container properties: template columns, rows and areas; automatic
    columns and rows; `auto_flow` (row or column, sparse or dense);
    `justify_items`, `align_items`, `justify_content`, `align_content` and
    the two gaps.
  - Tracks: fixed sizes, `auto`, `min-content`, `max-content`, `fit-content()`,
    `minmax()`, `fr`, and `repeat()`, including `auto-fill` and `auto-fit`.
  - Item record: a `GridItemLayout`, which places the item by line numbers,
    spans or a named area, and sets its alignment and sizes.
  - Auto-placement follows Grid section 8.5. The track sizing algorithm
    follows sections 12.4 to 12.8, including spanning and flexible items
    and first-baseline alignment across a row.

`BlockPanel`
: CSS block and inline flow, without floats.
  - Block items stack with their vertical margins collapsed (CSS 2.1
    section 8.3.1):
    - between siblings;
    - through empty blocks;
    - through a nested `BlockPanel` that is not a formatting-context root.
  - Item record: a `BlockFlowItemLayout`, whose `inline_block` level marks
    an atomic inline item. Runs of inline items break into line boxes and
    align by `text_align`, with `vertical_align` of `baseline`, `middle`,
    `top` or `bottom` against the container's `line_strut`.
  - `auto` horizontal margins share the free space.

```cpp
std::shared_ptr<gf::FlexPanel> row =
    gf::make_control<gf::FlexPanel>(gf::StableId("row"));
// ... add three children ...
gf::FlexItemLayout one_share{};
one_share.flex.grow = gf::FlexFactor{1.0};
gf::FlexItemLayout two_shares{};
two_shares.flex.grow = gf::FlexFactor{2.0};
(*row).set_item_layout(*narrow, one_share);
(*row).set_item_layout(*wide, two_shares);
// In a 100 px row, a fixed 20 px child keeps its width, and the other two
// share the remaining 80 px one to two from their 10 px bases.
```

Each CSS container provides three inspection calls:
- `layout_snapshot()` returns the solver's records: a flex item's base,
  hypothetical and final sizes and its lines, the grid tracks with their
  base sizes, growth limits and positions, and the block line boxes with
  their baselines;
- `layout_outcome()` (`GridPanel` and `BlockPanel`) reports a layout that
  was refused;
- `item_positioning(child)` reads a child's positioning.

Each panel's reference page lists every property, with examples.

### Grid spans and placement

A `GridPanel` owns its **spans**: regions that fuse cells, each a
`GridSpan` (`gui_forms/layout/grid_span/grid_span.hpp`) with an optional
name, an anchor cell (row first, zero-based) and its rows and columns. A
named span is a named area; `grid-template-areas` is another spelling of the
named spans. Each child's **placement** is one of three forms: automatic,
into a span by name, or at a cell. Two child properties write it, and
writing either replaces the other:

```cpp
gf::GridSpan footer{};
footer.name = "footer";
footer.anchor = gf::GridCell{.row = 2U, .column = 0U};
footer.columns = 3U;
const gf::GridSpanOutcome defined = (*table).define_span(footer);   // applied, or why not

(*table).area[*ok] = "footer";                                     // into the span named footer
(*table).cell[*title] = gf::GridCell{.row = 0U, .column = 0U};      // a single cell
const gf::GridCell at = (*table).cell[*ok].read();                 // the anchor of the region it occupies
(*table).area[*ok] = "";                                           // automatic again
```

- **Span edits validate first.** `define_span`, `remove_span` (by name or by
  anchor) and `set_spans` check the whole result and, on a refusal, change
  nothing and return the reason as a `GridSpanOutcome`: `overlaps` (two
  spans share a cell), `name_repeated`, `extent_invalid` (no rows or columns,
  or past 10,000 lines), `covers_placed_child` (a span would cover a cell a
  child is placed at, other than its anchor), `in_use` (a named span with
  children placed into it by name) and `not_found`. A span may lie past the
  explicit tracks; implicit tracks grow to hold it, as in CSS Grid.
- **Placement is checked when written.** The child must be a live direct
  child, an area must name a span, and a cell must not lie inside a span
  other than at its anchor. A child placed at an anonymous span's anchor
  occupies the whole region.
- **Several children in one region** stack in child order, as in CSS Grid.
  Unplaced children are auto-placed by `auto_flow`, a span counting as one
  region.
- **Alignment inside the region** is the child's own: `margin`,
  `minimum_size`, `maximum_size`, and the Control properties `justify_self`
  and `align_self`.
- **The item record's lines** are a third spelling: a `GridItemLayout` with
  any line that is not automatic makes the area and cell placement
  automatic, and writing `area` or `cell` makes the lines automatic.

In a form document, spans are `span` blocks inside the grid, before its
children, and the placement is the child's `area` or `cell`:

```text
grid-panel app.layout {
  style: "grid-template-columns: 1fr 1fr auto; grid-template-rows: auto 1fr auto";
  span footer { anchor: 2 0; columns: 3; }   /* named */
  span { anchor: 0 2; rows: 2; }             /* anonymous: placed by its anchor */
  label app.title { cell: 0 0; text: "Title"; }
  list-box app.items { cell: 0 2; }          /* the anonymous span */
  button app.ok { area: "footer"; text: "OK"; }
  text-box app.note { }                      /* auto-placed */
}
```

A span's name belongs to its grid, not to the document's stable IDs. Spans
that overlap, or a cell inside a span, are refused with E814
(`form_span_conflict`). `TableLayoutPanelCompat` keeps its own Windows Forms
algorithm (see below).

### Positioning

Every item record carries `positioning`: CSS's `position` (`PositionKind`
`static_position`, `relative` or `absolute`) and the four insets.
- A relative item is laid out in the flow, then offset by its insets
  without moving its neighbours.
- An absolute item leaves the flow. It takes no space, no grid cell and no
  place on a line, and it does not separate collapsing margins. It is
  placed afterwards by its insets, size and margins in its containing
  block. That block is the padding box of the nearest positioned box: the
  container itself when its parent positions it, otherwise a positioned
  ancestor, or the window's client area.

When an absolute item's containing block is not its parent, the window
records a `containing_block_not_parent` warning in
`Window::layout_diagnostics()`, once for each change of the block. When an
ancestor that moves the block is arranged, the item is arranged again in
the same pass.

### Limits

Every limit is checked before any storage is touched:
- a container holds at most 65,536 children;
- a grid axis has at most 10,000 tracks;
- grid auto-placement covers at most 1,048,576 cells.

A refused layout publishes no child bounds. `GridPanel` and `BlockPanel`
report the refusal through `layout_outcome()`.

Each pass is linear in the number of items, plus a sort when an `order` is
nonzero. The containers reuse their storage from pass to pass and do not
allocate per item.

At 10,000 children:
- changing one item's text costs one measure callback;
- resizing the container costs none for items whose constraint did not
  change;
- arranging again allocates nothing.

## The Windows Forms containers

`Panel`, `GroupBox`
: Absolute placement by `requested_bounds`, plus Dock and Anchor.
  - Dock consumes children in reverse z-order, backmost first, so that the
    topmost child receives the remaining fill area.
  - Paint stays back-to-front, and hit testing front-to-back.
  - Both inherit retained scrolling from `ScrollableControl`.

`FlowLayoutPanelCompat`
: For Windows Forms migration: an imported `FlowLayoutPanel` becomes it.
  Use `FlexPanel` for new work.
  - It orders visible children left to right, right to left, top down or
    bottom up.
  - It honours child margins, container padding, optional wrapping and
    per-child flow breaks (`set_flow_break`).
  - Hidden children leave no gap.
  - It is Windows Forms' flow, not flexbox.

`TableLayoutPanelCompat`
: For Windows Forms migration: an imported `TableLayoutPanel` becomes it.
  Use `GridPanel` for new work.
  - Tracks are bounded rows and columns, sized as absolute, auto-size or
    weighted percent.
  - Cells are placed explicitly or by row-major automatic placement, with
    row and column spans and fixed, add-rows or add-columns growth.
  - It draws cell borders and reports its resolved tracks and any overflow.
  - Inside its cell, a child is placed by Dock, or by Anchor when its Dock
    is `DockStyle::none`. Anchor sets edge alignment, centring and
    stretching after margins.

`SplitContainer`, `MasterDetailView`
: Two panes with a draggable seam and stable panel and seam IDs. The seam is
  a real control and participates in focus and keyboard resizing.

`ScaledPanel`, `ScaledGroupBox`
: Children are placed in a caller-declared design coordinate space, and each
  slot is scaled proportionally into the arranged bounds. This suits fixed
  compositions that must survive resizing without per-child anchors.

`ResponsiveTrackPanel`
: Nested fixed, content-measured and weighted tracks with priority-authored
  collapse, for toolbars and shells that must degrade deterministically at
  small sizes.

## Right to left

`layout_direction` is a registered property: `inherit` (the default),
`left_to_right` or `right_to_left`. The window's root is left to right. A
form document or the property grid can set it.

- The CSS containers follow CSS's start and end model. A row starts at the
  right, grid column lines run from the right, and `start` and `end`
  alignment and the left and right insets resolve as CSS resolves them.
  Padding and margins stay physical.
- Dock, Anchor, requested bounds and the two compatibility containers
  mirror fully, as Windows Forms' `RightToLeftLayout` does:
  - the layout is computed left to right, then mirrored once, about the
    parent's child viewport;
  - `DockStyle::left` docks at the right;
  - an anchor to the left keeps its distance to the right edge;
  - a requested x is measured from the right;
  - padding and margins mirror;
  - flow and table panels start at the right.
- Left to right changes nothing.

Bidirectional text inside a text control is the text engine's own.

## Scrolling

`ScrollableControl` is the shared base for `Panel` and `ContainerControl`.
- `auto_scroll`, `auto_scroll_margin`, `auto_scroll_min_size` and
  `auto_scroll_position` determine a two-axis content extent.
- Child requested bounds stay in content coordinates and never drift as the
  view scrolls.
- Scroll bars are an overlay outside the child viewport, so descendants
  cannot paint or receive hits through them.
- Scroll operations that notify report their type, orientation and old and
  new values. Programmatic `scroll_to` and `scroll_by` default to
  `notify = false`.
- `scroll_control_into_view` honours each target's `auto_scroll_offset`.
- A scrolled right-to-left parent moves its content toward the left as it
  scrolls.
- Multiline `TextBox` reports its document extent through the same base, so
  a text box gets real scroll bars rather than private offsets.

The three CSS containers scroll with `auto_scroll` and size to fit their
content with `auto_size`.

## Custom layout

A custom control overrides two hooks:

```cpp
gf::Size measure(gf::Size available) override;        // report preferred size
void arrange(gf::Rect final_bounds) override;         // place children
```

To give a container different answers for min-content and max-content, a
control can also override `measure_intrinsic(const gf::LayoutConstraint&)`.

Keep `measure` free of retained mutations: read children's preferred sizes
and your own properties to compute a size. Mutations can request another
pass. A layout that keeps invalidating itself eventually hits the bounded
pass limit and reports `bounded_pass_limit_hits`.

In `arrange`, call `arrange_self(final_bounds)` to commit your own geometry,
then `set_child_layout(child, rect)` to assign each child's layout slot.
Calling the child's `set_bounds` changes its authored `requested_bounds`,
even during arrangement; it is not a layout-slot assignment.

Declare the layout effects of your properties with `invalidate_declared`
(see [Update scopes and dirtiness](update-scopes-and-dirtiness)), so that
changing a property re-measures only when it has to.

## Instrumentation

`MetricsSnapshot` reports these counters:
- `measure_passes`, `arrange_passes`, `controls_measured`,
  `controls_arranged`, `measure_nodes_visited` and `arrange_nodes_visited`;
- for the measure cache: `measure_cache_hits`, `measure_cache_misses`,
  `maximum_measures_per_control_pass`, `layout_boundary_stops` and
  `layout_measure_clamps`.

Two patterns point to wasted work:
- a layout change that visits more nodes than it measures is traversing
  clean subtrees;
- a change whose arranged count equals the control count is laying out
  everything again, and is probably invalidating too widely.

## Verified against Chromium

The CSS containers are checked against a pinned Chromium, which runs only
when the reference boxes are captured, never inside GUI.Forms. The corpus
is in `tests/layout_corpus/`.
- It holds 211 fixtures: flex, grid, block, positioning, the two
  compatibility containers, and 30 text fixtures whose leaves are
  `TextBlock`s laid out by the bundled text engine and faces.
- Every fixture runs at four viewports and two device scales.
- Every fixture whose layout depends on direction also runs right to left.
- Every text fixture also runs at a text scale of 150 %.
- `gui_forms_layout_corpus_tests` compares every box edge with Chromium's
  on every host:
  - box fixtures within 0.05 logical px;
  - text fixtures within 0.51 px per edge, and each text leaf's first
    baseline within 1.0 px.
- A known difference is recorded on its node with its reason, and the
  report lists it. The text fixtures have one: the line breaker follows
  UAX #14, which keeps `10-20` whole, where Chromium breaks after the
  hyphen.

## Not provided

- **Baselines of nested containers.** A `FlexPanel`, `GridPanel` or
  `BlockPanel` inside another container does not answer its first item's
  baseline; it is aligned by the bottom of its border box, as a box with
  no text is.
- **CSS properties outside the subset.** These are not provided:
  - floats;
  - `display: contents`, inline flex and inline grid;
  - `subgrid` and masonry;
  - named grid lines;
  - `position: fixed` and `sticky`;
  - `min()`, `max()` and `clamp()`;
  - units other than `px` and `fr` (an imported `em` is resolved to px).

  Reading a style refuses each of these by name, with a diagnostic.
- **Virtualizing containers.** A container lays out every child. Data
  collections of 100,000 items or more realize only their visible window,
  through item systems such as `ObjectView`.
- **Per-monitor DPI policy inside the core.** The scale is a host fact the
  window is told.
- **Window resizes and the measure cache.** Resizing the window still
  measures the whole tree again, even when the root is a CSS container.
- **Flow layout and Dock and Anchor.** `FlowLayoutPanelCompat` uses its own
  packing policy rather than the base Dock and Anchor rules. Table cells do
  support Dock and Anchor.
