Markdown source parsed for presentation and editing.

GUI.Forms' rich text is markdown in the stx_rtf2md dialect: CommonMark
0.31.2 with ``~~strikethrough~~``, attribute spans
(``[text]{u color=#rrggbb size=+1}``) and block attribute lines
(``{align=center}``). `parse_markdown` reads a source into a document of
blocks (paragraphs, headings, list items, code blocks and rules), each
holding styled spans. Every span records the source bytes it comes from, so
an editor can style the source in place: the bytes no span covers are
markup, such as emphasis delimiters, list and quote markers, link
destinations and attribute lists. Tables and task-list markers are read as
text, raw HTML is text, and an image is its alternative text. Parsing never
opens a link or an image, and the document keeps no borrow of the source.

See Also
--------
RtfConversionResult : RTF in and out of the dialect.

Notes
-----
The reader is STXlib's CommonMark 0.31.2 reader (stxlib::markdown) with the
dialect's attribute spans and block attribute lines added. stx_rtf2md's own reader stays the normative reader
for conversion; over the dialect's canonical documents the two give the
same blocks and spans, which the tests check over stx_rtf2md's corpus.
`MarkdownLimits` bounds the source, the tables and the text; a failed parse
leaves the document empty and names the error in `MarkdownParseResult`.

Examples
--------
Reading the spans of a heading and its plain text:

.. code-block:: cpp

   gf::MarkdownDocument document;
   const gf::MarkdownParseResult parsed = gf::parse_markdown("# Plan for **May**\n", document);
   const gf::MarkdownSpan& month = document.spans[1];   // "May", bold, source bytes 13 to 16
   std::string plain;
   gf::markdown_plain_text(document, plain);   // "Plan for May"
