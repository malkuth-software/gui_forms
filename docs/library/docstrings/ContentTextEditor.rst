Editable text a consumer lays out and renders itself, edited by GUI.Forms.

The content editing mode: the consumer shapes and draws the text with its
own engine and fonts, and GUI.Forms keeps the editing logic. The editor
holds the text and edits it through the same engine as `TextBox`: the caret
and selection, the editing keys and input methods, undo and redo, the
clipboard, and the publication of the text, selection, caret and text layout
to assistive technology. The consumer hands back the positions of its lines,
clusters and, where they do not stand at cluster edges, its carets
(`ContentTextLayout`), and publishes the pixels through the
`ContentFrameSink` that `open_frames` returns, from any thread.

It is an integrity boundary, not a security boundary. Every layout is
validated against the text before use and every frame before a byte is
read; a refusal changes nothing. Text the consumer cannot lay out is still
edited, undone and published by GUI.Forms.

Properties
----------
text
    A copy of the text (UTF-8). The setter `set_text(text)` replaces it
    without undo history, putting the caret at the end, and throws
    `std::invalid_argument` for text that is not valid UTF-8.
text_revision
    Grows with every change of the text; a layout names the revision it was
    made for.
read_only
    Whether the text refuses edits (`set_read_only`).
multiline
    Whether Enter inserts a line break and Up, Down, Home and End move by the
    layout's lines (`set_multiline`); false by default.
draws_selection
    Whether the editor draws the selection highlight and the caret over the
    consumer's frame (`set_draws_selection`); true by default. A consumer that
    draws them itself reads `caret_rectangle` and `selection_rectangles`.
layout_current
    Whether the adopted layout is for the current revision.
live_surface
    The `LiveSurface` the frames go into; null before `open_frames`.
caret_rectangle
    The caret's bar in local coordinates, from the current layout; none
    without one.
selection_rectangles
    The selection's boxes in local coordinates, one for each part of a
    cluster it covers.
can_redo
    Whether redo would change the text.

Events
------
layout_requested
    Raised with the new revision when the text changed or the region's size
    did: lay out `text()` and call `submit_layout`.
text_changed
    Raised with the new text after every change.
selection_changed
    Raised with the new selection.

Methods
-------
submit_layout
    `submit_layout(layout)` validates a layout and adopts it, returning
    `ContentLayoutResult::accepted`, or why it was refused (a stale revision,
    too many entries, a range or boundary that does not fit the text, a
    cluster that does not cover its line, a box that is not finite, caret
    stops out of order).
open_frames
    `open_frames(opaque)` opens the channel the consumer publishes the
    editor's frames through, revoking any earlier one.
position_at
    `position_at(point)` returns the text offset nearest a local point by the
    current layout (the end of the text without one).
redo
    Redoes the last undone edit; returns whether the text changed.

See Also
--------
ContentSurface : A region whose frames a consumer renders, without editing.
TextBox : The editor whose engine this one shares, with GUI.Forms' own layout.

Notes
-----
The protocol runs on the UI thread. Each change of the text raises
`layout_requested`; the consumer lays out the text and submits the layout for
that revision, then publishes a frame. Until a current layout arrives, keys
that move by grapheme or word, editing, undo and the clipboard work as ever;
the caret and selection are not drawn, Up and Down do nothing, the pointer
places the caret at the end, and the published text layout holds the
graphemes with empty boxes. A layout made for an earlier text is never
applied to a later one.

Lines and clusters are in text order. Each line with text must be covered by
its clusters from its start to its end, without gaps; a line break belongs
to no line. A caret inside a cluster of several graphemes (a ligature)
divides it by its bytes unless a caret stop names that offset.

Examples
--------
Supply a one-line layout with a fixed advance:

.. code-block:: cpp

   std::shared_ptr<gf::ContentTextEditor> editor =
       gf::make_control<gf::ContentTextEditor>(gf::StableId("notes"), "hi");
   gf::ContentTextLayout layout;
   layout.text_revision = (*editor).text_revision();
   layout.lines.push_back({.range = {gf::Utf8Offset(0U), gf::Utf8Offset(2U)}, .bounds = {0.0, 0.0, 20.0, 16.0}});
   layout.clusters.push_back({.range = {gf::Utf8Offset(0U), gf::Utf8Offset(1U)}, .line = 0U, .bounds = {0.0, 0.0, 10.0, 16.0}});
   layout.clusters.push_back({.range = {gf::Utf8Offset(1U), gf::Utf8Offset(2U)}, .line = 0U, .bounds = {10.0, 0.0, 10.0, 16.0}});
   const gf::ContentLayoutResult result = (*editor).submit_layout(layout);
   if (result != gf::ContentLayoutResult::accepted) return;   // refused: the editor keeps no layout
