# Custom controls

GUI.Forms is a retained toolkit, and so is every program built on it: the
window keeps the controls, their state and their layout, and works only when
something changed. A control of your own takes part in the same model. This
page is a short guide to writing one. Each section points to the page that
covers its subject in full.

The example used throughout is `LinkPage` in
[`examples/link_page`](https://github.com/malkuth-software/gui_forms/tree/main/examples/link_page):
a scrolling page of wrapped paragraphs with links, painted by the control
itself. Its headless test, `gui_forms_link_page_example_tests`, runs with
the GUI.Forms tests.

## Look for an existing control first

Most programs need no control of their own. Before you write one, search:

- [Controls by function](../controls/generated/by-function) and the
  [API reference](../api/index), by what you want to show or edit;
- the headers, `include/gui_forms/controls/`, for a name or a property
  (`rg -l "word_wrap" include/gui_forms/controls`);
- this guide's other pages, for a mechanism that already does the work
  ([Styled text](styled-text), [Content surfaces](content-surfaces),
  [Text engine](text-engine)).

Some controls that are easy to overlook:

| You want to show | Use |
|---|---|
| Pages of formatted text with links, sections, images and printing | [`HelpBook`](../api/generated/gui_forms.HelpBook.rst) |
| Styled text that wraps, editable or read only | [`RichTextBox`](../api/generated/gui_forms.RichTextBox.rst) |
| Plain text that wraps and scrolls | [`TextBox`](../api/generated/gui_forms.TextBox.rst) with `multiline` and `read_only` |
| A very large number of owner-drawn rows | [`VirtualRowsView`](controls.md#large-row-sets) |
| Drawing in one region, with no input of its own | [`DrawingSurface`](../api/generated/gui_forms.DrawingSurface.rst) |
| An application-owned bitmap with zoom and pan | [`RasterCanvas`](../api/generated/gui_forms.RasterCanvas.rst) |
| Stock controls composed into one unit | `UserControl` ([Extension objects](../dev/architecture.md#extension-objects)) |

Write a control when you can say why none of these fits. If one nearly
fits, extending it in GUI.Forms usually serves every program better than a
second control in yours.

## The model

A control has three layers:

1. **Inputs**: its properties and content, and what the window gives it:
   the size from `arrange`, the font, the text engine, the scale.
2. **Kept layout**: everything derived from the inputs (lines, positions,
   hit rectangles), computed when an input changes and kept until one
   changes again.
3. **Readers**: paint, hit testing, hover, tooltips and accessibility read
   the kept layout. They compute nothing that the layout already holds.

The window follows the same model one level up: a write marks what it
dirties, and the next flush measures, arranges and paints only that
([Retained tree](retained-tree.md#state-is-retained-events-report-change),
[Update scopes and dirtiness](update-scopes-and-dirtiness),
[Painting and damage](painting-and-damage)).

## Keep the layout, keyed by its inputs

Put every input the layout depends on into one key, compare it on entry,
and return when nothing changed. This function is the only place the
control flows its text:

```cpp
void LinkPage::ensure_layout(const double flow_width) {
    const LayoutKey key = current_key(flow_width);
    if (laid_out_ && key == layout_key_) return;
    ++flow_count_;
    // ... flow every paragraph into lines_, pieces_ and links_ ...
    layout_key_ = key;
    laid_out_ = true;
}
```

`LayoutKey` holds the content revision, the flow width, the effective font
(`text_font`), the window's text metrics provider and its scale. A setter
that changes the content increments the revision and invalidates, so the
next arrange flows again; nothing else does. `TextBox::ensure_multiline_layout`
(`src/controls/panel/text_box/text_box_scroll.cpp`) and
`RichTextBox::ensure_layout` (`src/controls/panel/rich_text_box/rich_text_box_layout.cpp`)
follow the same pattern.

Paint with the font the layout was made with (the key's), so the painted
text matches the measured text.

## What each hook may change

| Hook | Reads | May change |
|---|---|---|
| A setter or command | anything | the input it sets; it marks what it dirties (`invalidate`, or `invalidate_declared` for a property) |
| `measure` | properties, children's sizes | nothing retained |
| `arrange` | its bounds, the inputs | the kept layout (through the cache), child slots; it never invalidates itself, scrolls or writes a property |
| `on_paint` | the kept layout | nothing |
| Pointer moves and hover | the kept layout, `location` | interaction state (the hovered part), and then only on a real change; it repaints what changed |

A control that asks for layout from its own layout or paint never settles.
[Layout](layout.md#custom-layout) describes `measure` and `arrange`, and
[Retained layout](layout.md#retained-layout) what the window does with a
control that breaks these rules.

Repaint as little as you can. `invalidate(Rect)` repaints a rectangle,
`invalidate(Dirty::paint)` the control, and `Dirty::arrange` or
`Dirty::measure` also lay it out again. `LinkPage` repaints the two links a
hover change touches and nothing else.

## Coordinates

`MouseEventArgs::location` is the pointer in the control's own coordinates:
hit-test with it. `window_position` is the same point in window coordinates,
for work at window level such as placing a popup
([Mouse events](mouse-events)). A scrolling control converts `location` to
content coordinates with the viewport's origin and the scroll position:

```cpp
const gui_forms::Point origin{viewport.x - scrolled.x, viewport.y - scrolled.y};
const gui_forms::Point content{local.x - origin.x, local.y - origin.y};
```

`absolute_bounds`, `window_frame` and `point_from_window` read arranged
geometry, which can flush pending layout first
([Layout](layout.md#coordinates-and-units)). Keep them out of handlers that
run on every pointer move.

## Scrolling content you paint

Derive from `ScrollableControl` and set `auto_scroll`. Then:

- Return `true` from `paints_scrolled_content`, so a scroll repaints the
  viewport instead of moving children's pixels.
- Return the size of what you paint from `painted_content_extent`, read from
  the kept layout. The base `arrange` scrolls over it, shows the bars and
  keeps the scroll position across arranges; the control never sets the
  position from its own `arrange`.
- In `arrange`, bring the layout up to date first, then call
  `ScrollableControl::arrange`. Text that wraps flows at the viewport's
  width, which the vertical bar narrows: when the viewport width after the
  base arrange differs from the width you flowed at, flow once more and call
  the base again. `LinkPage::arrange` remembers the room the bar took, so a
  steady arrange flows nothing and a resize flows once.
- Paint only what the damage and the viewport cross. Keep lines in content
  order and find the first visible one with a binary search
  (`std::upper_bound`), as hit testing does.

For many separate rows rather than one document, use `VirtualRowsView`: it
measures and paints only the rows near the viewport
([Large row sets](controls.md#large-row-sets)).

## What costs what

- **Flowing text shapes it.** The cost grows with the text, so flow when an
  input changes and never in paint or a pointer handler.
  `lay_out_paragraph` wraps a paragraph at about two shapings a line into
  storage you own and reuse (`ParagraphBuffers`)
  ([Paragraph layout for owner-drawn rows](text-engine.md#paragraph-layout-for-owner-drawn-rows)).
- **Measuring text in paint shapes it again.** The renderer keeps a bounded
  set of recent text layouts (2,048), which a long page's text does not fit
  in. Measure once, in the layout, and keep the widths.
- **Geometry reads can lay out.** See [Coordinates](#coordinates).
- **Allocation.** Reserve storage for the largest input and reuse it; do not
  allocate per line or per paint.
- **Paint is cheap when it only draws kept geometry** for the visible part.

## Prove it with the counters

Claims about cost are checked with numbers, not by reading code
([Reading the counters](update-scopes-and-dirtiness.md#reading-the-counters)).
`Window::metrics_snapshot()` returns `measure_passes`, `arrange_passes`,
`controls_measured`, `controls_arranged` and `read_barrier_flushes`, among
others. Test the largest content your program ships, with four checks:

- **Pointer motion.** Scroll to the middle and move the pointer 100 times
  over the control. `measure_pointer_motion`
  (`gui_forms/layout/diagnostics/pointer_motion_probe.hpp`) does both and
  returns what the moves cost; a retained control answers
  `lays_out_nothing()`.
- **Scroll.** Wheel and `scroll_to` move the view and flow nothing.
- **Resize.** A new width flows once; a second layout at the same size, or a
  new height alone, flows nothing.
- **Hover and click.** They change the hovered part and repaint, and land on
  the right part with the control away from the window's origin.

Each check also asserts that `layout_diagnostics()` is empty. From
`gui_forms_link_page_example_tests`:

```cpp
(*scene).scroll_to_middle();
const std::uint64_t flows = page.flow_count();
const PointerMotionCost cost = measure_pointer_motion(*(*scene).window, (*scene).page_area(), 100U);
require(cost.lays_out_nothing(), "100 pointer moves over the scrolled page lay out nothing");
require(page.flow_count() == flows, "and flow no text");
```

## The example

`examples/link_page` holds the control (`link_page.hpp`, `link_page.cpp`), a
generated document (`sample_document.cpp`) and a program that opens it:

```sh
cmake -S examples/link_page -B build/link-page -DCMAKE_PREFIX_PATH="$PWD/build/install" \
  -DGUI_FORMS_EXAMPLE_NATIVE=ON
cmake --build build/link-page
build/link-page/link_page
```

The headless test, `tests/link_page_example_tests.cpp` in the tests
repository, runs the four checks above over 2,000 paragraphs. Programs made
from the template harness's app template get the same control under
`reference/` and the same checks in their own tests.
