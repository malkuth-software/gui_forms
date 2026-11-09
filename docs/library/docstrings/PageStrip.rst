The tab headers of a page set in one row, on the top, bottom, left or right edge of their content.

A page strip shows the pages of a `PageSet` as tabs, in the set's order, and
shows and changes the set's selection. It has no content area of its own:
`PageView` puts one under a strip, and a docking workspace uses a strip alone
for its auto-hide edges. Tabs are sized to their labels (or fixed, or sharing
the row). When they do not fit, a strip shows scroll buttons and a list
button at its end; a document strip instead shrinks its tabs towards a
minimum width and then hides the last ones behind a list button that shows
how many are hidden. The arrow keys move between tabs and select as they
move, unless the strip is set to manual selection. Document strips also show
close buttons, a dot for modified pages and icon-only pinned tabs.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Tabs are published to
    accessibility as ``<stable_id>.tab.<page id>``.
pages
    The set whose pages the strip shows. The strip borrows it: the set must
    stay alive while the strip is bound to it, and its owner calls
    `release_page_set` first if the strip may outlive it.

Properties
----------
page_set
    The bound set, or null after `release_page_set`.
placement : default ``PageStripPlacement::top``
    The edge of the content the strip sits on: top, bottom, left or right.
    The tabs touch that edge, and the selected tab's seam lies along it. A
    side strip stacks its tabs from the top down, its overflow buttons at
    the bottom, and turns their labels: on the left edge they read from
    bottom to top, on the right edge from top to bottom. Each tab's icon,
    label and close button follow the reading direction, so on the left edge
    the icon is at the bottom and the close button at the top. Up and Down
    move between the tabs of a side strip.
item_sizing : default ``PageItemSizing::content``
    ``content`` sizes each tab to its icon and label plus padding, within the
    minimum and maximum widths; ``fixed`` gives every tab the fixed width;
    ``fill`` shares the row's width equally, within the minimum and maximum.
item_widths : default minimum ``40``, maximum ``240``, fixed ``120``
    The tab widths in logical units at text scale 1. `set_item_widths`
    throws `std::invalid_argument` unless 0 < minimum <= maximum <= 1024 and
    0 < fixed <= 1024, and keeps the old widths.
activation : default ``PageStripActivation::automatic``
    Whether the arrow keys, Home and End select the tab they move to
    (automatic) or only move the keyboard's position, Space or Enter then
    selecting it (manual, for pages that are slow to show).
overflow : default ``PageStripOverflow::scroll``
    What the strip does when its tabs do not fit. ``scroll``: scroll back,
    scroll forward and list buttons at the end, the tabs keeping their
    widths. ``shrink_then_list``: the ordinary tabs shrink towards the
    minimum width, the widest first; when even that does not fit, the tabs
    from the end of the order that do not fit are hidden behind a list
    button showing their count and a down arrow. Pinned tabs never shrink
    and are never hidden.
close_buttons : default ``PageCloseButtons::none``
    Which closable tabs show their close button: ``none``,
    ``selected_and_hot`` (the selected tab and the tab under the pointer) or
    ``all``. Unless ``none``, space for the button is reserved after the
    label of every closable tab, so widths do not change when it appears.
reorder_enabled : default ``false``
    Whether an unclaimed drag of a tab reorders it live within its group:
    the dragged tab follows the pointer and the tabs it passes slide aside.
    `PageView`'s document preset turns it on. Turning it off ends a drag
    under way with nothing moved.
mnemonics_enabled : default ``true``
    Whether ``&`` in a title marks a mnemonic: Alt and the letter select the
    page and focus its tab, and the marker is not shown. When off, titles are
    shown exactly as written.
font
    The tab label font.
image_list
    The images that pages' image keys name; a tab whose key is in the list
    shows the image before its label. `set_image_list` throws
    `std::invalid_argument` for a disposed list or one that belongs to
    another window.
draw_mode : default ``ItemDrawMode::normal``
    With ``ItemDrawMode::owner_draw`` the application draws the tabs'
    content: see `draw_item_content`. Throws `std::invalid_argument` for an
    unknown mode.
overflowing
    Whether the tabs do not all fit, so the scroll and list buttons (under
    ``shrink_then_list``, the list button) show.
visible_pages
    The pages whose tabs are laid out, in the order they are shown.
hidden_count
    How many pages have no tab laid out.
widths_frozen
    Whether tab widths are frozen after a close by the pointer.
dragged_page
    The page whose tab is being slid in a live reorder, or none.
insertion_mark
    The gap the insertion mark shows, or none.
focused_page
    The page the keyboard is on while the strip has focus, or none.
page_list_open
    Whether the page list is open.
page_list_items
    The list's rows while it is open, one per page in order; the rows of
    pages without a tab are italic.

Events
------
draw_item_content
    Raised while the strip paints in the owner-draw mode (owner decision
    W-Q6), once per painted tab that has an icon or a label, with an
    `ItemContentPaint`: the tab's position, its page's ID, and its icon and
    label area as the content rectangle, to which the painter is clipped.
    The tab's plate, close button, pin, modified mark and focus cue stay
    painted by role. Set `draw_default` to add the default content. A
    handler paints only; one that changes the strip's pages, settings or
    scroll position, or disposes it, ends that paint, and the strip is
    painted again.
page_invoked
    Raised when a tab is clicked, chosen with the keyboard, by its mnemonic,
    from the page list or through accessibility, after its page is selected
    (it may have been selected already). It is not raised for a programmatic
    selection, nor when a handler of the set's ``selecting`` cancelled.
page_close_requested
    Raised once when a closable page's close is asked for: by its close
    button (cause ``pointer``), a middle click released over the tab it
    pressed (``middle_button``), Ctrl+F4 (Cmd+W on macOS) on the selected
    page (``keyboard``) or the accessibility ``close`` action
    (``semantic``). The strip never
    removes the page: the set's owner decides, and sets ``handled`` when it
    acts or refuses. A `PageView` removes a page nobody handled unless its
    close policy is ``request_only``.
page_pin_requested
    Raised when a pinned tab's pin is clicked, asking to unpin the page. When
    no handler sets ``handled`` the strip unpins it in the set.
page_drag_started
    Raised once when a press on a tab moves 4 logical units or more along
    either axis, with the page, the press point and the pointer (window
    coordinates). An owner that takes the drag over, such as a docking
    workspace, sets ``claimed``: the strip then releases the pointer capture
    (unless the owner has already taken it) and does nothing more with the
    press; it never reorders.
page_context_menu_requested
    Raised on the host's context request: a pointer request over a tab asks
    for that tab's menu (at the pointer, selecting nothing), and a keyboard
    request for the keyboard's tab's (at the tab's corner on the content's
    side where reading starts).
    The strip shows no menu of its own; a handler that shows one sets
    ``handled``.
page_double_clicked
    Raised on the second press of a double click on a tab's body, after the
    press selected the page; never for its close button or pin.

Methods
-------
release_page_set
    Unbinds the set. The strip closes the page list, shows no tabs, and its
    page operations do nothing from then on.
ensure_visible
    Scrolls so a page's tab is shown; false for a page not in the set.
scroll_backward
    Scrolls the row one tab towards the start; false at the start.
scroll_forward
    Scrolls the row one tab towards the end; false when the last tab already
    ends the row.
page_at
    The page whose tab is under a point in window coordinates, or none.
show_page_list
    Opens the list of every page, with the selected one checked; false when
    the strip is not overflowing or not in a window.
close_page_list
    Closes the page list.
select_relative
    Selects the next (+1) or previous (-1) page, wrapping, and raises
    `page_invoked`.
move_selected
    Moves the selected page one place later (+1) or earlier (-1) within its
    group, the pinned pages or the ordinary ones; false at the group's end.
    Ctrl+Shift+PageDown and Ctrl+Shift+PageUp call it.
cancel_drag
    Ends a live reorder with nothing moved, as Escape does; false when none
    is under way.
insertion_position_at
    The gap an ordinary page dropped at a point would take, as a position in
    the current order: before the first laid-out tab whose middle lies after
    the point, else after the last; never inside the pinned group; none
    outside the strip.
show_insertion_mark
    Shows the insertion mark at a gap until it is cleared or the set's pages
    or order change; false, showing nothing, for a gap beyond the set or not
    between laid-out tabs.
clear_insertion_mark
    Removes the insertion mark.
request_close
    Raises `page_close_requested` for a closable page with the given cause;
    true when the request was handled (by a handler, or by the owning view's
    removal). A page that is not in the set or not closable raises nothing.
tab_identity
    The accessibility identity of a page's tab.

See Also
--------
PageSet : The pages, their order and their selection.
PageView : A strip with the selected page's content below it.
TabControl : The Windows Forms tab control.

Notes
-----
Layout keeps the selected page visible: a selection, a change to the pages,
a resize or a change of setting scrolls the selected tab (under manual
selection, the keyboard's tab) into view. The scroll buttons and the mouse
wheel scroll by one whole tab and may move the selected tab out of view until
the next of those. Under ``scroll`` a page chosen from the list is selected and
scrolled into view; its place in the order does not change. Under
``shrink_then_list`` a hidden page chosen from the list takes the last
visible position: the strip selects it and moves it there in one
`PageSet` batch, so the set publishes one change of each kind, the pages
before that position keep their places and the pinned boundary holds. A
cancelled selection moves nothing. A hidden page selected any other way (a
key, a program) is shown in the last place without being moved.

Dragging. A press on a tab arms a drag; past the threshold the strip raises
`page_drag_started`. A claimed drag belongs to the claimant. Otherwise,
with `reorder_enabled`, the tab follows the pointer within the visible run
of its group (the pinned tabs or the ordinary ones), carrying the tab state
``captured``, and takes the place of the furthest tab of the group whose
middle its own middle has reached; the release moves the page there with
one `PageSet` move, so a drag never crosses the pinned boundary. Escape,
the strip losing focus, turning reordering off, or a change to the set's
pages or order ends the drag with nothing moved. Under ``shrink_then_list``
the selected page shown after the front run does not slide.

Document features. A closable tab shows its close button by the
`close_buttons` rule, with a plate while the pointer is on it; pressing it
selects nothing and the request is raised on the release over the same
button. A modified page shows a dot in the close button's place, replaced by
the close button while the tab is hot. After a close by the pointer (the
button or a middle click) the remaining tabs keep their widths until the
pointer leaves the strip, so the next tab's close button arrives under the
pointer; a resize or a setting also thaws them. A pinned page with an image
shows only its icon until the pointer is over it, then its full width with
its pin in the close button's place; a pinned page without an image keeps
its label. Ctrl+F4 (Cmd+W on macOS) asks to close the selected page; Delete
closes nothing.

A press on a tab selects first and then focuses the strip. The order of
events is the set's ``selecting``, its ``selected_page_changed``, then
`page_invoked`; a cancelled selection raises nothing more and leaves the
keyboard's tab where it was. Ctrl+PageDown and Ctrl+PageUp select the next
and previous page while the strip has focus, and Ctrl+Shift+PageDown and
Ctrl+Shift+PageUp move the selected page within its group, and Shift+F10 or
the context-menu key asks for the keyboard's tab's context menu. These keys
are the window's named keyboard commands (`KeyCommand`: ``close_document``,
``next_tab``, ``previous_tab``, ``move_tab_later``, ``move_tab_earlier``,
``context_menu``), so they follow the host's convention and an
application's `Window::rebind_key_command`; the arrows, Home, End, Space,
Enter and Escape are the strip's own.

Every page is published to accessibility as a tab of the strip's tab list
(role ``tab_group``, horizontal, or vertical on a side strip): those scrolled out of view are published
without bounds and without the visible state, so every tab can be reached.
Tabs carry their position in the set and offer ``select``, and ``close`` when
the page is closable; they never offer ``press``. Until accessibility gains
states for them, a tab's description reads ``pinned``, ``modified`` or
``pinned, modified``. While the strip overflows, its scroll buttons (under
``scroll``) and its list button (a ``menu_button`` with an overlay; under
``shrink_then_list`` its description gives the hidden count) follow the
tabs.

The tabs are the ``page`` item system: each tab has the parts ``tab``,
``tab_icon``, ``tab_label``, ``tab_modified`` (the dot, drawn with the
``radio_dot`` mark), ``tab_close`` and ``tab_close_glyph`` (states hot and
pressed), ``tab_pin`` and ``tab_pin_glyph`` (hot, pressed, and checked for
the pinned drawing), ``tab_seam`` (the selected tab only) and ``tab_focus``;
at most one of the dot, the close button and the pin is present. Tabs have
the states hot, pressed, captured (dragged), selected, disabled and
focus-visible, and the custom states ``pinned`` and ``modified``. The list
button's count is the part ``list_count``, and the gap bar shown by
`show_insertion_mark` the part ``insertion_mark`` (role ``drop``, state
drop-accepted). They are painted through the by-role paint path with
the menu-item family, flat at rest, until the default theme draws the tab
stack and the notched document tab. Painting reads a layout cached between
changes and allocates nothing per tab.

Examples
--------
A settings strip whose arrow keys only move the focus, Space selecting:

.. code-block:: cpp

   gf::PageSet pages{};
   const std::array<std::string_view, 3> titles{"&General", "&Advanced", "Ab&out"};
   for (std::size_t index = 0; index < titles.size(); ++index) {
       gf::PageRecord record{};
       record.id = gf::PageId{"settings." + std::to_string(index)};
       record.title = std::string(titles[index]);
       static_cast<void>(pages.insert(record, gf::PagePlacement::end_of_group()));
   }
   std::shared_ptr<gf::PageStrip> strip =
       gf::make_control<gf::PageStrip>(gf::StableId("settings.tabs"), pages);
   (*strip).activation = gf::PageStripActivation::manual;
   static_cast<void>((*strip).select_relative(1, gf::PageSelectionCause::programmatic));
   // pages.selected_page() is now "settings.1", titled "&Advanced".
