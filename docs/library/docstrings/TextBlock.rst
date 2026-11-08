Read-only text laid out as CSS lays out the text of a block, for use inside the layout panels.

A text block shows its text in lines, the way a browser shows the text of a
`<div>`. Runs of spaces, tabs and line ends collapse to one space, and
leading and trailing white space is dropped, as with CSS's
`white-space: normal`. Lines break where Unicode's line-breaking rules
(UAX #14) allow, as the window's text engine reports them: after spaces,
after hyphens, and between ideographs. A word wider than the line is not
broken; it overflows on a line of its own. Each line is the font's
`line-height: normal` tall unless a line height is set, and the text has
no inset. The block publishes its first and last baselines, so a
`FlexPanel` or `GridPanel` can align it by its first line, and a
`BlockPanel` can sit it on a line by its last. Use it for text inside
the CSS layout panels; use a `Label` for Windows Forms layouts, which
keeps Windows Forms' inset and line spacing.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
initial_text
    Text to display, as UTF-8. Line ends are white space, not breaks.

Properties
----------
text : default ``""``
    The text. Setting a different value raises `text_changed`.
font : default empty
    The block's own font. While empty, the block uses the theme's field
    typography; `resolved_font` reports the font in use. It is scaled by the
    window's text scale. An invalid specification throws
    `std::invalid_argument`; writing `std::nullopt` returns to the theme's
    font.
resolved_font
    The font in use: `font` when set, or else the theme's field typography,
    before the window's text scale.
foreground
    The text colour in use: `Control`'s `fore_color` cell when set, or else
    the theme's text colour. Writing `std::nullopt` to `fore_color` returns
    to the theme's colour.
text_align : default ``TextAlignment::start``
    How each line sits across the block, as CSS's `text-align`: `start`,
    `end`, `left`, `right` or `center`. `start` and `end` follow the
    layout direction, so `start` is right in a right-to-left window. A value
    outside the enumeration throws `std::invalid_argument`.
line_height : default none
    Each line's height in logical pixels at a text scale of 1, as CSS's
    `line-height: <length>`; it scales with the text. None is
    `line-height: normal`: the font's ascent, descent and line gap, each
    rounded to a whole pixel. The extra space (or the shortfall) goes half
    above the text, rounded down, and the rest below. A value that is not
    finite and positive throws `std::invalid_argument`; writing
    `std::nullopt` returns to normal.

Events
------
text_changed
    Raised after `text` changes. It carries the new text.

Methods
-------
first_baseline
    The baseline of the first line, from the top of the block, or none when
    the block has no text.
last_baseline
    The baseline of the last line when the block is laid out at the given
    width, or none when the block has no text.

See Also
--------
Label : Windows Forms' label, with its inset and line spacing.
FlexPanel : Rows and columns that align text blocks by their baselines.
BlockPanel : Blocks and lines of text and inline items.

Notes
-----
Its min-content width is the widest run of text between two break
opportunities, and its max-content width is the whole text on one line;
both include the padding. Given a width, it reports the height of its text
wrapped at that width.

Without the bundled text engine (a window with no text metrics provider,
or a provider that does not report break opportunities) lines break at
white space only. That answer is labelled estimated, as other text
measurements without the engine are.

The block does not take focus or pointer input. It publishes the
accessibility role `static_text`, named by its accessible name or else its
text; a block with an empty name is not exposed.

Examples
--------
A heading and a wrapped paragraph in a column:

.. code-block:: cpp

   std::shared_ptr<gf::FlexPanel> column =
       gf::make_control<gf::FlexPanel>(gf::StableId("article"));
   (*column).flex_direction = gf::FlexDirection::column;
   std::shared_ptr<gf::TextBlock> heading = gf::make_control<gf::TextBlock>(
       gf::StableId("article.heading"), "Export settings");
   (*heading).font = gf::FontSpec{gf::FontRole::content, 19.5, 700, false, 0.0};
   std::shared_ptr<gf::TextBlock> body = gf::make_control<gf::TextBlock>(
       gf::StableId("article.body"),
       "Exported files keep their names; existing files are never overwritten.");
   (*column).add_child(heading);
   (*column).add_child(body);

   gf::Window window(column, {240.0, 120.0});
   window.perform_layout();
   // the paragraph wraps to the column's 240 px width

Align a caption with a larger value by their baselines:

.. code-block:: cpp

   std::shared_ptr<gf::FlexPanel> row =
       gf::make_control<gf::FlexPanel>(gf::StableId("rate"));
   (*row).align_items = gf::LayoutAlignment::baseline;
   std::shared_ptr<gf::TextBlock> caption =
       gf::make_control<gf::TextBlock>(gf::StableId("rate.caption"), "Rate");
   std::shared_ptr<gf::TextBlock> value =
       gf::make_control<gf::TextBlock>(gf::StableId("rate.value"), "42 MB/s");
   (*value).font = gf::FontSpec{gf::FontRole::content, 26.0, 400, false, 0.0};
   (*row).add_child(caption);
   (*row).add_child(value);

   gf::Window window(row, {240.0, 60.0});
   window.perform_layout();
   // the caption's first line sits on the value's baseline
