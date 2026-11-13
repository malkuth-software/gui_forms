A rich text field whose document is markdown, shown styled as it is edited.

A rich text box holds markdown source, the `stx_rtf2md` dialect that
`parse_markdown` reads, and shows it styled in place: headings large and
bold, emphasis, inline code and code blocks in their faces, links in the
link role. The markup itself (heading, list and quote markers, emphasis
delimiters, link destinations, fences) stays visible, dimmed, so what the
user sees is what the document holds. The source is edited with a multiline
`TextBox`'s editing engine: the same caret, selection, keys, undo, input
methods and clipboard. A control bar above the text formats the selection by
editing the markup. Use a `TextBox` for plain text.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The bar's buttons
    are ``<stable_id>.bar.<button>``.
markdown : default ``""``
    The initial source. Invalid UTF-8 throws `std::invalid_argument`; a
    source past `maximum_bytes` throws `std::length_error`. The caret starts
    at its end.

Properties
----------
markdown
    The source as edited. `set_markdown` replaces it, puts the caret at the
    end, clears the history and raises `content_changed`, `text_changed`
    (when the plain text changed) and `selection_changed`; it throws as the
    constructor does.
text
    The plain text: the document's blocks with the markup removed, joined by
    line feeds, as `markdown_plain_text` gives it. Assigning it replaces the
    document with markdown that reads back as the text: punctuation escaped
    and each line feed a hard line break. Leading white space on a line is
    kept as character references; trailing line feeds are dropped.
document
    The parsed source the box presents, for reading.
read_only : default ``false``
    Whether the user may only select, copy and follow links. Typing,
    deleting, pasting, cutting, the bar and its keys, undo and redo are
    refused, and the bar's buttons show disabled.
word_wrap : default ``true``
    Whether lines wrap at word boundaries to the box's width. Without
    wrapping a source line is one row and the box scrolls horizontally.
accepts_tab : default ``false``
    Whether Tab inserts a tab instead of moving focus.
control_bar_visible : default ``true``
    Whether the control bar is shown. Its keys work without it.
font : default content role, 12 points
    The body font. Headings scale it and are set bold; code is set in the
    monospace role at its size. An invalid `FontSpec` throws
    `std::invalid_argument`.
selection_format
    The formatting at the selection, as the bar shows it, each format on,
    off or mixed (`RichTextFormatState`): a span style is on when every
    selected content character has it, mixed when some do, and off when
    none does; an empty selection reads the character before its caret. A
    line format reads the non-blank lines the selection touches the same
    way; headings of different levels, or a heading beside body text, are
    mixed.

Events
------
text_changed
    Raised after the plain text changes, with the plain text. A change to
    the markup alone, such as making a word bold, leaves it silent.
content_changed
    Raised after any change to the source, before `text_changed`.
selection_changed
    Raised after the selection or caret moves, and after every change.
link_clicked
    Raised with the target when a click with Ctrl (Cmd on macOS) lands on a
    link's text. A plain click places the caret, so that the link can be
    edited.

Methods
-------
set_rtf
    Replaces the document with RTF converted to markdown (`import_rtf`) and
    clears the history. A failure, or markdown past `maximum_bytes`
    (``limit_exceeded``), leaves the box as it was.
rtf
    Writes the document as RTF (`export_rtf`) and returns the result, whose
    report counts what the conversion could not carry.
clear
    Empties the document and clears the history.
toggle_bold
    Wraps the selection's text on each line it covers in ``**``, or removes
    the delimiters that already wrap it; with an empty selection it inserts
    a pair with the caret between. One undoable step; false when read-only
    or nothing changed.
toggle_italic
    As `toggle_bold`, with ``*``.
toggle_code
    As `toggle_bold`, with a backtick.
set_heading_level
    Sets the heading marker of each line the selection touches: 0 removes
    it, 1 to 6 writes that many ``#``. Any other level throws
    `std::invalid_argument`.
toggle_bulleted_list
    Gives each non-blank line the selection touches a ``-`` marker, or
    removes the markers when every such line has one.
toggle_numbered_list
    As `toggle_bulleted_list`, numbering the lines ``1.``, ``2.`` and on.
insert_link
    Wraps the selection as a link's text and puts the caret where the
    destination goes; with an empty selection it writes an empty link with
    the caret in its brackets.
replace_selection
    Replaces the selection with source text as one undoable edit. It returns
    false when the box is read-only, the text is not valid UTF-8 or the
    source would pass `maximum_bytes`.
delete_selection
    Removes the selection as one undoable edit.
redo
    Reapplies the last undone step.
visual_line_count
    The number of displayed lines after wrapping.

See Also
--------
TextBox : Plain text in one line or many.
MarkdownDocument : The parsed source the box presents.
LinkLabel : A link in a label.

Notes
-----
Its `tool_tip_text` shows as grey hint text in the empty document while
it has focus, in place of a tooltip bubble.

Keyboard, while the box has focus: the keys of a multiline `TextBox`.
Ctrl or Cmd with B, I and K makes the selection bold, italic or a link.
Enter in a list item or a quote starts the next item with the same marker
(the next number in a numbered list); Enter on an empty item removes its
marker and ends the list. Shift+Enter is a plain line feed.

The control bar holds Bold, Italic, a Heading menu (body text and headings
1 to 3), bulleted and numbered lists, Link and inline Code. A button shows
checked when its format is on for the selection, and mixed, its checked
face at half strength, when the format holds for part of it; the item
states are ``checked`` and ``indeterminate``, and assistive technologies
read checked and mixed. The heading button shows open while its menu is.
Assistive technologies see the buttons as children of the box and press
them, or open the heading menu, as the pointer does.

Painting is by role: the text in the editor role, the markup in its muted
colour, a link in the link role underlined, the selection in the selection
role. A code block's rows are shaded and inline code has a tinted ground.

Windows Forms' run-model members are not provided: the document is the
markdown source, and its formatting is the markup. There is no selection
colour, size, alignment or font member, no embedded objects or OLE, and no
``SelectedRtf``, ``Rtf`` string property, ``RightMargin`` or
``BulletIndent``; `set_rtf` and `rtf` convert whole documents.

Examples
--------
A note field that loads markdown, bolds its first word and saves RTF:

.. code-block:: cpp

   std::shared_ptr<gf::RichTextBox> note =
       gf::make_control<gf::RichTextBox>(gf::StableId("note"), "Draft notes for [review](https://example.org)\n");
   (*note).select(gf::Utf8Offset(0U), gf::Utf8Offset(5U));
   (*note).toggle_bold();
   std::vector<std::byte> rtf;
   const gf::RtfConversionResult saved = (*note).rtf(rtf);
   // (*note).markdown() starts "**Draft** notes"; (*note).text.read() starts "Draft notes"
