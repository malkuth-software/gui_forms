A diagnostic view that draws a text run on its baseline and reports how its font resolved.

A typography specimen draws one run of text in a given `FontSpec`, with a
guide line along each baseline and, below it, a line of diagnostics from the
text engine: the resolution status, the face that was used, the number of
font runs, any missing glyph clusters, the ascent, descent and baseline, and
the display scale. It is for reviewing fonts, sizes and text scaling, and for
spotting text that the engine could not resolve exactly, which is drawn in a
warning red. It ignores the pointer and takes no focus; use a `Label` to show
text to users.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
initial_text
    The text to draw, empty by default.

Properties
----------
text
    The text drawn. Changing it remeasures the specimen.
font : default ``{FontRole::content, 12.0, 400, false}``
    The font requested for the text, before the window's text scale is
    applied. An invalid font, such as one with a size that is not positive,
    throws `std::invalid_argument`.
caption : default ``""``
    A small heading drawn above the text, such as the font's description.
    When set, it also prefixes the accessible name.
diagnostics_visible : default ``true``
    Whether the diagnostics line is drawn at the bottom.
wrap_text : default ``false``
    Whether the text wraps at spaces to fit the width. While wrapping, runs of
    white space, line breaks included, are drawn as single spaces.
resolved_layout
    How the text resolves through the window's text engine for the effective
    font: the `ResolvedTextLayout` with its status, primary face, runs,
    missing clusters and metrics. Without a native host there is no text
    engine, and the status is `TextResolutionStatus::estimated` with no
    face.

See Also
--------
Label : Shows text to users.
VisualInspectorView : Shows the resolved font of any control's text.
EasingPreview : Compares easing curves under the current motion policy.

Notes
-----
The specimen asks for at least 160 by 28 layout units, more for a caption,
the diagnostics line and wrapped lines, within the space available. It
measures with the window's text engine and draws with the painter's.

The specimen publishes the accessibility role `static_text`. Its name is the
text, preceded by the caption and a colon when there is one; it does not use
`accessible_name`. Its value is the resolution status and the face, such as
`exact; Portsmouth Rapids`, or `estimated; no resolved face` without a text
engine.

Examples
--------
Review a body text run with wrapping, and check how it resolved:

.. code-block:: cpp

   std::shared_ptr<gf::TypographySpecimen> specimen =
       gf::make_control<gf::TypographySpecimen>(
           gf::StableId("review.body"), "The quick brown fox jumps over the lazy dog");
   gf::FontSpec body;
   body.role = gf::FontRole::content;
   body.size = 14.0;
   (*specimen).font = body;
   (*specimen).caption = "Body: content, 14 / 400";
   (*specimen).wrap_text = true;
   gf::Window window(specimen, gf::Size{240.0, 120.0});

   gf::ResolvedTextLayout layout = (*specimen).resolved_layout();
   // without a native host, layout.status is TextResolutionStatus::estimated
