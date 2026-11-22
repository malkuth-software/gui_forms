The three-pane help viewer: a toolbar, the navigation tabs (Contents,
Index, Search and Bookmarks) and a `HelpBook`.

The toolbar holds Hide and Show, Back, Forward, Home, Print, Show in
contents, and the way back to the program's built-in language. The viewer
can be placed in any window or dock panel; `HelpBookWindow` presents it in
a window of its own. It shows one compiled language's help section, never opens a file
or a network resource, and passes external links on to the application.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
section
    The `HelpBookSection` shown (shared, read-only). `set_section` fills the
    contents and the index and shows the home page; null shows nothing.
home_page
    The home page's stable ID: the one `set_home` named, else the contents'
    first page.
selected_tab
    The navigation tab selected (`HelpNavigationTab`).
can_go_back
    Whether history has a page before the current one; `can_go_forward`,
    whether it has one after.
current_page
    The stable ID of the page shown.
navigation_visible
    Whether the navigation pane shows; `set_navigation_visible` changes it,
    as Hide and Show do.
navigation_width
    The navigation pane's width in logical units, at most 60% of the
    viewer; `set_navigation_width` changes it, and so does dragging the gap
    between the pane and the book.
labels
    The words the viewer shows (`HelpBookViewerLabels`), translated with the
    program's text; its ``built_in_language`` is the name of the program's
    built-in language, in that language, and empty hides the way back.
    `set_labels` changes them.
bookmarks
    The bookmarks, as stable IDs; the application keeps them in its
    settings. `set_bookmarks` replaces them.
index_row_count
    The Index tab's rows: one per keyword and subkeyword, in index order.
chooser_records
    The records the chooser offers (stable IDs) when an index row names
    several; empty while it is closed.
search_results
    The last search's hits as stable IDs, best first.
book
    The viewer's `HelpBook`. `contents_tree`, `index_field` and
    `search_field` are its other parts.
snapshot
    The viewer's counters (`HelpBookViewerSnapshot`): history size and
    position, navigations, searches, the last search's status and hit
    count.

Events
------
navigated
    Raised when a page is shown, with its stable ID.
external_link
    Raised when an external link is followed; the application decides what
    to do with it.
popup_requested
    Raised when a popup link is followed; the help system shows the popup.
print_requested
    Raised when printing is asked for, with a `HelpPrintRequest`: the page
    shown, or the contents book under it. The help system's handler prints.
bookmarks_changed
    Raised when the bookmarks change, with the new list.
built_in_language_requested
    Raised when the user chooses the way back to the built-in language; the
    help system switches help to that language and gives the viewer the new
    section.

Methods
-------
initialize_control_tree
    Makes the viewer's panes; `make_control` calls it once, after
    construction.
set_home
    Names the home page by its stable ID; empty takes the contents' first
    page.
show
    Opens a record at an anchor, adding it to history and selecting it in
    the contents; false, changing nothing, for a record the section does not
    hold.
show_contents
    Selects the Contents tab.
show_index
    Selects the Index tab with a keyword typed.
show_search
    Selects the Search tab and searches for a text.
select_tab
    Selects a navigation tab.
back
    Moves back in history; `forward` moves forward and `home` shows the home
    page. Each returns false when it cannot.
locate
    Selects the page shown in the contents.
set_images
    Sets the PNG resources the pages may show (`HelpBookImage`).
add_bookmark
    Adds the page shown to the bookmarks; `remove_selected_bookmark` removes
    the selected one.
open_index_row
    Opens an index row: its record, or the chooser when it names several;
    false when the row is out of range. `open_chooser_entry` opens one of
    the chooser's records.
open_search_result
    Opens one of the last search's hits.
set_titles_only
    Whether the search looks at titles only.

See Also
--------
HelpBook : The topic pane.
HelpBookSystem : A program's help, docked and opened by F1.

Notes
-----
History keeps at most `maximum_history` pages; the oldest goes first. Alt+C,
Alt+N, Alt+S and Alt+I select Contents, Index, Search and Bookmarks; F6
moves between the navigation pane and the book; Alt+Left and Alt+Right move
in history and Alt+Home goes home; Ctrl+P prints.
