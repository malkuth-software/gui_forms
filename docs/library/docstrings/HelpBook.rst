The help topic pane: one help page on paper, with its title over a rule,
collapsible sections under topic bars, callouts, links and PNG images,
tables, definition lists, code coloured by token, signatures and linked
cards, with find in page.

The book shows a `HelpPageModel` and scrolls vertically. It never opens a
link itself: it raises `link_activated`, and its owner decides; a
`HelpBookViewer` handles topic and popup links and offers external ones to
the application. A `HelpBookSystem` docks a book beside a program's window.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
page
    The page shown.
close_visible
    Whether the close mark shows at the upper left, as in a docked book.
    `set_close_visible` changes it.
labels
    The words the book shows for its callouts and its close mark
    (`HelpBookLabels`: ``Note``, ``Tip``, ``Caution`` and ``Close help`` by
    default), translated with the program's text. `set_labels` changes
    them.
focused_stop
    The link or topic bar keyboard focus is on, as an index into the book's
    focus stops (links in reading order, bars among them), or none.
content_height
    The page's content height when laid out at the current width.
content_width
    The page's content width: the wrap width, or more where a code block's
    lines are wider, which the book scrolls sideways.
find_count
    How many matches the current find has.
find_position
    The current match, counted from 1, or 0 when there is none.
layout_passes
    How many times the screen layout has been made, for tests that prove a
    pointer move, a hover or a re-arrange lays nothing out.

Events
------
link_activated
    Raised when a link is activated (a click, Enter, or the accessibility
    press), with a `HelpLinkActivation`: the page, the link's kind, target
    and fragment, and its box in window coordinates, where a popup anchors.
    A handler that acts sets ``handled``.
close_requested
    Raised when the user presses the close mark.
section_toggled
    Raised when a collapsible section is opened or closed, with a
    `HelpSectionToggle`.
link_hovered
    Raised when the pointer moves onto a link or off every link, with a
    `HelpLinkHover`: the page, the link's index or none, and its kind,
    target and fragment, for an owner's status bar. It costs no layout.

Methods
-------
show_page
    Shows a page, scrolled to an anchor (a heading's anchor or a listed
    control's stable ID) or to the top. Collapsible sections take their
    initial state, except one holding the anchor, which opens.
show_record
    Shows a record of a `HelpBookSection` by its stable ID, through
    `build_help_page`; false, changing nothing, when the section has no such
    record.
scroll_to_anchor
    Scrolls so the anchored block is at the top, opening the sections around
    it; false when no block has the anchor.
set_section_open
    Opens or closes a collapsible section; `section_open` says whether one
    is open.
set_images
    Sets the PNG resources a body may show (``![alt](name.png)``), as
    `HelpBookImage` records: the name in the body, the registered image and
    its size.
set_highlight
    Sets the words to mark with the search highlight, folded with
    `fold_help_text`.
set_visited
    Sets the topic targets shown as visited links; `mark_visited` adds one
    and `visited` asks about one.
move_focus
    Moves keyboard focus to the next or previous stop; false past the end.
activate_link
    Follows a link of the page, by its index, as a click would.
block_top
    A block's top in content coordinates, or none when it is hidden.
link_bounds
    A link's box in local coordinates, scroll applied, or none when it is
    not laid out. `topic_bar_bounds` gives a section's topic bar.
layout_for_print
    Lays the page out for printing at a width, with every section open, no
    close mark and no paper ground, and links followed by their target's
    title in brackets; returns the content height. The print layout is kept
    apart from the screen's.
print_break_positions
    Fills a list with the places the print layout may break pages: the top
    of every line, in content coordinates, ascending.
paint_print_band
    Paints a band of the print layout, from a top to a height in content
    coordinates, at an origin on a painter, clipped to the band.
find
    Marks every match of a query in the page's text (ASCII case folded
    unless matching case), makes the first the current one and scrolls it
    into view, opening the sections around it; returns the number of
    matches. `find_next` and `find_previous` move the current match, wrapping
    at the ends, and `clear_find` drops the marks.

See Also
--------
HelpBookViewer : A book with navigation, search and popups.
HelpBookSystem : A program's help, docked and opened by F1.
MarkdownDocument : Markdown text parsed into blocks.

Notes
-----
Tab and Shift+Tab move through the links and topic bars (past the last,
focus leaves the book), Enter and Space follow a link or toggle a section,
and the arrows, Page Up, Page Down, Home and End scroll.

The book lays its page out again only when the page, the open sections,
the font, the text metrics, the device scale or the wrap width change;
hover, presses and scrolling repaint. Lines are one and a half times the
body size apart, every run on a line shares one baseline, and inline code
is set in Portsmouth Mono, sized to the body's x-height, on a chip.

The paper's look is the theme's: the parts ``paper``, ``title``, ``rule``,
``body`` and ``close``; the content parts ``code_span``, ``code_block``,
``signature``, ``signature_bar``, ``table_header``, ``table_cell``,
``card`` (hot under the pointer) and ``find_match`` (the current match in
the current state); the syntax parts ``keyword``, ``type``, ``function``,
``string``, ``number``, ``comment`` and ``directive``; and the topic
items' ``topic_bar`` and ``expander``. Until a theme gives rules for them,
they are drawn with the tool-tip family (the paper), the menu-item family
(topic bars), the card family (callouts, cells and cards), the panel family
(header cells and code chips), the selection family (find marks) and the
link families.
