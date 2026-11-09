# Styled text

A multiline `TextBox` can draw parts of its text in other styles: syntax
colours, error squiggles, search matches. The styles are **decorations**
kept beside the text. They never change the text, they follow every edit,
and the box composes them into runs only to lay out and paint the rows on
screen.

## Layers

Each producer owns one **layer**, a `TextDecorationLayer` with a number of
its own choosing: a syntax highlighter one layer, a diagnostics pass
another, a search a third. A producer replaces its layer's contents whenever
its answer changes, so producers never see each other. Layers compose in
ascending order: where two layers set the same field, the later one's value
stands.

A `TextDecoration` gives a byte range (`Utf8Range`, UTF-8 offsets into the
text) a `TextDecorationStyle` and an optional producer `tag`. Each field the
style leaves unset is drawn as the layers under it, or the box itself, draw
it:

| Field | What it does |
|---|---|
| `ink` | a theme ink (`TextInkRole`: `text`, `muted`, `link`, `visited_link`, `notice`), so the theme and high contrast style it |
| `foreground` | an authored colour, standing over the ink except in high contrast |
| `weight`, `italic` | change the font (weight 1 to 1000). Decorated text is measured in its own font, so the caret, hit testing and the accessibility layout follow the glyphs |
| `underline`, `underline_color` | `TextUnderline::single` or `squiggle`, in its own colour or the text's ink |
| `background` | a flat ground under the text, under the selection band. It is left out in high contrast |

```cpp
#include <gui_forms/controls/panel/text_box/text_box.hpp>

#include <cstddef>
#include <span>

namespace gf = gui_forms;

constexpr gf::TextDecorationLayer syntax_layer{1};

// Colours the bytes [start, end) of `box` as a keyword.
void mark_keyword(gf::TextBox& box, std::size_t start, std::size_t end) {
    gf::TextDecorationStyle keyword{};
    keyword.ink = gf::TextInkRole::link;
    keyword.weight = 700;
    const gf::TextDecoration decoration{
        .range = gf::Utf8Range{gf::Utf8Offset(start), gf::Utf8Offset(end)}, .style = keyword, .tag = 0U};
    box.set_decorations(syntax_layer, std::span<const gf::TextDecoration>(&decoration, 1U));
}
```

## Changing decorations

| Member | What it does |
|---|---|
| `set_decorations(layer, decorations)` | replaces the layer's decorations with a copy |
| `replace_decorations(layer, range, decorations)` | replaces the layer's decorations within `range` only, for an incremental highlighter |
| `clear_decorations(layer)`, `clear_all_decorations()` | remove one layer or all of them |
| `decorations()` | the `TextDecorations` collection, read only |
| `styled_runs(range, runs)` | fills `runs` with the composed runs over `range`, as the box draws them |

A range past the text throws `std::out_of_range`, and a range that ends
before it starts or a weight outside 1 to 1000 throws
`std::invalid_argument`. Either way, nothing changes. Empty ranges are
dropped.

## Edits and repainting

The box records every change of its text (typing, undo and redo,
assignment) and shifts the decorations with it, so a highlighter only has to
replace the range it re-reads. A change of colour, underline or ground
repaints only the rows of its range. A change of weight or italics lays the
text out again. The selection band and its ink stay over decorations;
underlines show through it. A single-line box keeps decorations but draws
its text in one style.

## From C

The C ABI (since 1.1, additively) has `gf_text_decoration` and five slots in
`GF_TABLE_TEXT_EDITING`: `set_decorations`, `replace_decorations`,
`clear_decorations`, `decoration_count` and `decoration_at`. See the C ABI
header `gui_forms/c/gf_abi.h`.
