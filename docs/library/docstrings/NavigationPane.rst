An Outlook-style navigation pane: the selected page's content above a stack of large page buttons.

A navigation pane owns a `PageSet`. Its header shows the selected page's
title with a collapse button; below it the selected page's control fills the
content area; then a grip, one large button per page (icon and title) and,
at the bottom, a strip of icons for the pages that do not fit as large
buttons, ending in a configure button. Dragging the grip up or down shows
more or fewer large buttons. Collapsed, the pane is a narrow bar with an
expand button, the selected page's title read from bottom to top and the
large buttons as icons; clicking the title opens the content in an overlay
beside the bar. Use it for a program's top-level areas (mail, calendar,
contacts) where one area is shown at a time.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The content panel is
    created as ``<stable_id>.content``, the overlay's root as
    ``<stable_id>.overlay`` and the configure menu as ``<stable_id>.menu``.

Properties
----------
pages
    The pane's set (no pinning; after the selected page leaves, the next page
    is selected, else the previous): add, remove, rename, move and select
    pages through it. A page's ``image_key`` names an image in `image_list`.
collapsed : default ``false``
    Whether the pane is the narrow bar. Collapsing closes the configure menu;
    expanding closes the overlay.
button_count : default ``4``
    The number of large buttons the user chose. A count above
    ``PageSetLimits::maximum_capacity`` throws `std::out_of_range`.
visible_button_count
    The large buttons shown after layout: `button_count`, limited by the
    pages not hidden and by the pane's height. Zero while collapsed.
hidden_pages
    The pages the user hid from the buttons and icons, in page order.
    `set_page_hidden` hides or shows one and `page_hidden` asks.
expanded_width : default ``220``
    The width the pane asks for while expanded, and the overlay's width, from
    80 to 1024; a width outside that range throws `std::out_of_range`.
image_list : default none
    The images the pages' ``image_key`` values name, drawn at the list's
    image size (at most 32). A disposed list, or one bound to another
    window, throws `std::invalid_argument`.
focus_region : default ``true``
    Whether the pane registers itself as an F6 region of its window while
    attached. A pane nested in another region is not registered.
overlay_open
    Whether the collapsed pane's overlay is open.
configure_menu_open
    Whether the configure menu is open.
state
    The part of the pane's state a program saves with its settings: whether
    it is collapsed, the button count, the hidden pages and the page order.

Events
------
options_requested
    Raised when "Navigation pane options…" is chosen from the configure
    menu. The pane has no options dialog of its own; the program shows one.
state_changed
    Raised after any part of `state` changed: collapsing or expanding, the
    button count, a page hidden or shown, or the page order.
overlay_open_changed
    Raised after the overlay opened (``true``) or closed (``false``).

Methods
-------
initialize_control_tree
    Creates the content panel. `make_control` calls it; do not call it
    yourself.
show_more_buttons
    Shows one more large button than are shown, as the grip's Up key and
    the configure menu do; false when no more fit or every page has one.
show_fewer_buttons
    Shows one fewer; false when none are shown.
set_page_hidden
    Hides a page from the buttons and icons, or shows it again. False,
    changing nothing, for a page not in the set or already in that state.
open_overlay
    Opens the content overlay of a collapsed, attached pane; false otherwise.
close_overlay
    Closes the overlay.
show_configure_menu
    Opens the configure menu of an expanded, attached pane; false otherwise.
    An unknown opening mode throws `std::invalid_argument`.
close_configure_menu
    Closes the configure menu.
apply_state
    Restores a saved state: the order is applied to the set in one batch,
    identities the set does not hold are skipped and counted in the result,
    and one `state_changed` is raised. Apply it after adding the pages. A
    repeated identity throws `std::invalid_argument` and a count above the
    set's capacity `std::out_of_range`, both changing nothing.

See Also
--------
PageView : Pages under a row of tabs.
PageSet : The pages, their order and their selection.

Notes
-----
A page's content is a control held by a `PageControlContent`, the content
record of the page view family, so a page taken from a `PageView`'s set can
be put into the pane's with its control. The pane shows only the selected
page's control, as the single child of its content panel, and removes it
from the tree when the selection moves; a control that already has a parent
is not shown. A page without content shows an empty content area.

The pane never saves anything itself (owner decision DK-33): the program
reads `state`, stores it with its settings and passes it to `apply_state`
when it next builds the pane.

The configure menu lists "Show more buttons" and "Show fewer buttons", then
the pages that fit neither as a large button nor as an icon, then the pages
the user hid under a disabled "Hidden pages" row, then "Navigation pane
options…". Choosing a page selects it; choosing a hidden page also shows it
again. Hidden pages appear nowhere else.

The overlay is a window overlay beside the bar (on its right, unless only the
left has room), as tall as the pane and `expanded_width` wide, holding the
content panel while it is open. Focus moves to its first Tab stop and comes
back when it closes; the bar stays usable meanwhile. A press anywhere
outside the pane and the overlay closes it and then goes on to the control
under the pointer (owner decision DK-27). Escape closes it when the focused
control inside leaves the key unhandled (a text box takes Escape for
itself); so does clicking the title again or expanding the pane.

Keyboard: Tab moves through the collapse button, the grip, the tab list and
the configure button (collapsed: the expand button, the title and the tab
list) and then leaves the pane. In the tab list the arrows move across the
large buttons and icons, wrapping, and Home and End go to the ends, without
selecting; Enter or Space selects. On the grip, Up shows one more large
button and Down one fewer.

To accessibility the pane is a group (named by its accessible name, or
"Navigation pane"). The large buttons and icons are the tabs of one vertical
tab list; the selected tab controls the content panel, a ``tab_panel``
named by the page. Tabs publish select and focus, not press. The grip is a
``split_pane`` whose value is the number of large buttons, with increment,
decrement and set_value; the configure button is a ``menu_button``; the
collapse button and the collapsed title are buttons with expand and
collapse.

Every part is drawn by the by-role paint path. The pane's parts are
``region``, ``header``, ``header_icon``, ``header_label``,
``collapse_button`` and ``collapse_glyph`` (an ``arrow_left`` mark, or
``arrow_right`` when collapsed), ``content``, ``grip_bar``, ``grip`` (the
``grip_dots``), ``button_area``, ``overflow_area``, ``configure_button`` and
``configure_glyph`` (the ``customize`` mark), ``collapsed_bar``,
``collapsed_label`` (turned text), the overlay's ``overlay_shadow``,
``overlay_face``, ``overlay_header``, ``overlay_label`` and ``overlay_content`` (in
the pane's coordinates, present while it is open) and ``focus``. The large
buttons are the ``nav_page`` item system (``nav_page_face``,
``nav_page_icon``, ``nav_page_label``) and the icons the ``nav_overflow``
system (``nav_overflow_face``, ``nav_overflow_icon``, and
``nav_overflow_label``, the title's first character for a page without an
image), each with hot, pressed, selected and focus_visible. The buttons use
the ``menu_item`` recipe family until the default theme draws the design
language's tab material.

Examples
--------
A pane with three areas, restored from saved settings:

.. code-block:: cpp

   std::shared_ptr<gf::NavigationPane> pane =
       gf::make_control<gf::NavigationPane>(gf::StableId("areas"));
   constexpr std::array<std::string_view, 3> areas{"mail", "calendar", "contacts"};
   for (const std::string_view area : areas) {
       gf::PageRecord record{};
       record.id = gf::PageId{std::string(area)};
       record.title = std::string(area);
       static_cast<void>((*pane).pages().insert(record, gf::PagePlacement::end_of_group()));
       std::unique_ptr<gf::PageContent> content = std::make_unique<gf::PageControlContent>(
           gf::make_control<gf::Label>(gf::StableId(std::string(area) + ".view"), std::string(area)));
       static_cast<void>((*pane).pages().set_content(record.id, content));
   }
   gf::NavigationPaneState saved{};
   saved.button_count = 2U;
   saved.page_order = {gf::PageId{"calendar"}, gf::PageId{"mail"}, gf::PageId{"contacts"}};
   const gf::NavigationPaneRestore restored = (*pane).apply_state(saved);
   // Calendar now comes first; nothing in the saved state was unknown.
   static_cast<void>(restored.unknown_pages);
