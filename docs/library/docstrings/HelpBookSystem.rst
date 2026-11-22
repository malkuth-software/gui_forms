A program's help: it holds the help text of the program's language files,
compiles the help book from it and the program's windows, and shows it on
F1, in What's This mode, on window cards and from the ribbon.

The system holds the program's language-file catalog (`LocaleCatalog`: the
built-in locale file, the help fields the attached windows' controls hold in
code, a translation and the user's overrides) and its help view
(`HelpCatalog`). It compiles the help book section from that view, the
attached windows' structure and the declared help topics, and presents it:

- F1 shows the focused control's page, walking up to the window's page and
  then home when the control has none;
- Shift+F1 enters What's This mode: the pointer becomes the help cursor,
  the next click shows the clicked control's help in a paper popup, and
  Escape leaves the mode;
- a window card shows a control's help line and "More..." to its page;
- the ribbon's help button and a `RibbonToolTip`'s F1, and a
  `HelpProvider`'s requests, routed by the program, show help too.

Pages show in an embedded viewer when one is set (`set_viewer`), else in a
`HelpBookWindow` owned by the first attached window that has a handle. The
system never opens a browser, a file or a network resource: external links
and printing are raised to the program.

Parameters
----------
options
    The `HelpBookSystemOptions`: the prefix of every stable ID the system
    makes, the built-in language's tag, the help window's title and size,
    the home page, the viewer's and the book's labels, the "More..." text,
    the images, the compile options and the popup's width.

Properties
----------
locale
    The language files' text: every field the loaded files give, and the
    program's own values as of the last compile.
options
    The options the system was made with.
popup_open
    Whether a paper popup is open.
popup_book
    The popup's `HelpBook`.
help_mode_active
    Whether What's This mode is on.
active_viewer
    The viewer pages show in now: the embedded one, or the help window's.
snapshot
    The system's counters (`HelpBookSystemSnapshot`): compiles, rejections,
    requests, pages shown, popups, help-mode entries, external links,
    attached windows, and whether a popup or help mode is on.
bookmarks
    The bookmarks, as stable IDs; the program keeps them in its settings.
    `set_bookmarks` replaces them.

Events
------
external_link
    Raised when an external link is followed; the program decides what to
    do with it.
print_requested
    Raised when printing is asked for, with a `HelpPrintRequest`.
bookmarks_changed
    Raised when the bookmarks change, with the new list.

Methods
-------
catalog
    The help view of `locale`. Every load and the way back replace it from
    `locale`, so a direct edit lasts only until the next one.
load_built_in
    Reads the built-in locale file, which must be for the options' built-in
    language, into the built-in layer. A refused file changes nothing;
    rejected fields are left out and listed in the result.
    `load_translation` reads a translation the same way.
load_user_overrides
    Reads the user's own locale file for the language in force, which wins
    over the translation and the built-in text field by field. A file for
    another language is refused and changes nothing.
    `clear_user_overrides` drops them.
use_built_in_language
    Shows help in the built-in language only, whatever the translation and
    the user's file say: the way back the viewer's built-in-language link
    turns on.
export_template
    Exports the template translators work from: the declared topics, the
    built-in file and the attached windows' program values, with the
    built-in text as values and, for the translation's language, its
    translations over them; never the user's overrides. An empty language
    exports the built-in one.
resolve_field
    One field of a record as it shows now, with the layer it came from.
compile
    Compiles the section and shows it in the viewers. The result keeps the
    rejections; the section holds every record that compiled.
set_section
    Uses a section compiled elsewhere instead of compiling; `section`
    returns the section, compiled first when the catalog changed since.
attach
    Attaches a window: F1 and Shift+F1 become help keys in it, and its
    structure joins the contents. Its application handle and window ID let
    the help window be owned by it. Detach (`detach`) every attached window
    before it is destroyed.
set_viewer
    Shows pages in a given viewer, such as one in a dock panel, instead of
    the help window; null goes back to the window.
attach_ribbon_tool_tip
    Answers a `RibbonToolTip`'s F1 with help.
attach_ribbon
    Answers a ribbon's help button with the window card of the ribbon's
    window, beside the ribbon; the ribbon must sit in an attached window.
resolve
    Where help for a control is (`HelpTarget`): the page and the anchor on
    it, or the home page when nothing on the way up had help.
show
    Shows a record at an anchor. `show_help_for` shows a control's help,
    `show_contents`, `show_index` and `show_search` open the viewer's tabs.
show_popup
    Shows the page of a record in a paper popup beside an anchor control.
show_card
    Shows the window card beside an anchor control: its help line and
    "More...", which opens its page; it falls back to the page popup when
    the control has no line. `close_popup` closes either.
enter_help_mode
    Enters What's This mode in a window; `cancel_help_mode` leaves it.
on_help_requested
    Answers a `HelpProvider`'s request with its target's page, unless a
    handler took it. `on_ribbon_help` answers the ribbon's help or a
    `RibbonToolTip`'s F1, and `on_ribbon_button_help` the ribbon's help
    button.
on_help_key
    F1 in an attached window: shows the focused control's help, ending
    What's This mode when it is on; true when the key was used.
    `on_help_mode_key` is Shift+F1's, entering the mode.

See Also
--------
HelpBookViewer : The viewer that shows the pages.
HelpBook : The topic pane.
HelpProvider : Help text and topics for controls.

Notes
-----
The system runs on the UI thread of the attached windows. A
`HelpBookWindow` hides when the user closes it and shows again on the next
open, so its history, tab and pane width carry over; it is destroyed with
its owner window or by its ``close``. Without application windowing, as in a
headless run, it does not open.
