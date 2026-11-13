A row of tabs over the selected page's content, in the strip or the document preset.

A page view owns a `PageSet` and shows it through a child `PageStrip`, with
the selected page's control in the content area beside it: below a strip at
the top, above one at the bottom, and to the right or left of a strip on
the left or right edge. Each page's content is an ordinary control the
application supplies, either when it adds the page or later, the first time
the page is selected. The view remembers which control had focus in each
page, and Ctrl+PageDown and Ctrl+PageUp move between pages from anywhere
inside it. Use it where a `TabControl` would serve but pages are named by
identity, created lazily, nested, or moved between views. The document
preset is for open documents: close buttons, a dot for unsaved changes,
optional pinned tabs, tabs that shrink and then overflow into a list, and
Ctrl+F4 to close.

Parameters
----------
stable_id
    Identity of the control, unique within its window. The strip is created
    as ``<stable_id>.strip`` and each page's panel as
    ``<stable_id>.panel.<page id>``.
options
    Either the options of the view's set (the strip preset: the selection
    fallback after the selected page leaves, the next page, else the previous,
    by default; whether pages may be pinned; the capacity), or a
    `PageViewOptions` naming the preset, whether pages may be pinned and the
    capacity; the preset then chooses the fallback (strip: neighbour;
    document: the most recently active page). An invalid preset throws
    `std::invalid_argument`.

Properties
----------
pages
    The view's set: add, remove, move, select, take and put pages through it.
strip
    The child strip that shows the tabs; its placement, sizing, activation,
    overflow, close buttons and mnemonics are set on it. Hiding it (as a
    docking tool stack with one panel does) gives the whole view to the
    content.
preset
    ``PageViewPreset::strip`` or ``document``, fixed when the view is made.
    The document preset sets the strip's close buttons to
    ``selected_and_hot``, its overflow to ``shrink_then_list``, turns its
    mnemonics off (document titles are data) and its live reorder on; these
    may be changed on the strip afterwards.
close_policy : default ``PageViewClosePolicy::remove_when_unhandled``
    What the view does with a close request that no handler of the strip's
    ``page_close_requested`` handled: remove the page, or, under
    ``request_only`` (for an owner that decides, such as a docking
    workspace), nothing.
ctrl_tab : default ``PageViewCtrlTab::select_next``
    What Ctrl+Tab and Ctrl+Shift+Tab do while focus is inside the view:
    select the next or previous page, or pass the key on, as a view inside a
    docking workspace does so that the workspace's navigator receives it.
    Ctrl+PageDown and Ctrl+PageUp select in every view.
focus_region : default ``false``
    Whether the view registers itself as an F6 region of its window. A view
    nested in another region is not registered.

Events
------
content_requested
    Raised when a page without content is selected for the first time,
    before the application's handlers of the set's ``selected_page_changed``
    run. A handler supplies the content with `set_page_content`; a page left
    without content shows an empty content area and is not asked again.

Methods
-------
initialize_control_tree
    Creates the strip. `make_control` calls it; do not call it yourself.
add_page
    Adds a page with its control, or with none for late content, at the end
    of its group or at a position. The set's refusals are returned and leave
    the view unchanged.
set_page_content
    Attaches a control to a page that has none.
page_content
    The control of a page, or null when it has none or is not in the set.
panel_identity
    The accessibility identity of a page's panel, or empty when the page has
    no content.

See Also
--------
PageStrip : The tabs alone.
PageSet : The pages, their order and their selection.
TabControl : The Windows Forms tab control.

Notes
-----
The view owns its set and the strip borrows it. A page's control is held by
the set's `PageControlContent` and placed in a panel the view owns while the
page is in the view's set: taking the page out (``PageSet::take``) takes the
control out of the view with its state, putting it into another view's set
shows it there, and removing the page releases it.

When the selection changes, the view does its work before the application
hears of the change: it remembers the control that had focus in the old
page, raises `content_requested` if the new page has no content and was
never asked, shows the new page's panel and hides the old one, and, if focus
was inside the old page, moves it to the control it last had in the new page
(or to the strip when there is none). A click on a tab focuses the strip
instead. A handler of the set's ``selecting`` that cancels leaves the
selection, the shown page and the focus as they were. Because the first
page added to an empty set is selected at once, subscribe to
`content_requested` before adding pages.

Ctrl+PageDown, Ctrl+PageUp and Ctrl+Tab are taken in the bubble phase, so
when views are nested the innermost view around the focus answers first; a
view whose `ctrl_tab` is ``pass`` leaves Ctrl+Tab to the views around it.
Ctrl+F4 (close the selected closable page; Cmd+W on macOS) and
Ctrl+Shift+PageDown and Ctrl+Shift+PageUp (move it within its group) are
taken the same way. All of these are the window's named keyboard commands
(`KeyCommand`), so a host's convention and an application's rebinding reach
the view.

Closing a page: the strip raises one ``page_close_requested`` per cause (the
close button, a middle click, Ctrl+F4, the accessibility action). A handler
that acts, or refuses (a document whose save prompt was cancelled), sets
``handled``; otherwise the close policy applies. When the selected page is
removed, the set selects another by its fallback: in the document preset
the most recently active page.

To accessibility the strip is the tab list and the selected page's panel is
its ``tab_panel``, named by the tab and linked from it (the tab's controlled
panel); panels of pages not selected are hidden and not published. The view
adds no node of its own unless it has an accessible name.

The view paints its content area as the ``content`` part, a ``well``,
through the by-role paint path, so a ``page-view::part(content)`` rule
reaches it; its parts are ``region``, ``strip`` (composed from
``page_strip``), ``content`` and ``focus``.

Examples
--------
Open documents, closed with Ctrl+F4 or a middle click:

.. code-block:: cpp

   gf::PageViewOptions options{};
   options.preset = gf::PageViewPreset::document;
   std::shared_ptr<gf::PageView> documents =
       gf::make_control<gf::PageView>(gf::StableId("documents"), options);
   const std::array<std::string_view, 3> names{"notes.txt", "plan.md", "main.cpp"};
   for (const std::string_view name : names) {
       gf::PageRecord record{};
       record.id = gf::PageId{std::string(name)};
       record.title = std::string(name);
       record.closable = true;
       static_cast<void>((*documents).add_page(record, gf::Control::Ptr{}));
   }
   static_cast<void>((*documents).pages().set_modified(gf::PageId{"plan.md"}, true));
   static_cast<void>((*(*documents).strip()).request_close(gf::PageId{"notes.txt"},
                                                         gf::PageCloseCause::keyboard));
   // Nobody handled the request, so the view removed notes.txt.

Settings pages, the last one built only when it is first shown:

.. code-block:: cpp

   struct Settings final : gf::Component {
       std::shared_ptr<gf::PageView> view =
           gf::make_control<gf::PageView>(gf::StableId("settings"));

       void build() {
           gf::on((*view).content_requested(), *this, &Settings::make_page);
           gf::PageRecord general{};
           general.id = gf::PageId{"general"};
           general.title = "&General";
           static_cast<void>((*view).add_page(
               general, gf::make_control<gf::Label>(gf::StableId("general.text"), "General")));
           gf::PageRecord advanced{};
           advanced.id = gf::PageId{"advanced"};
           advanced.title = "&Advanced";
           static_cast<void>((*view).add_page(advanced, gf::Control::Ptr{}));
       }

       void make_page(const gf::PageContentRequest& request) {
           static_cast<void>((*view).set_page_content(
               request.page, gf::make_control<gf::Label>(gf::StableId("advanced.text"), "Advanced")));
       }
   };

   void example() {
       Settings settings{};
       settings.build();
       static_cast<void>((*settings.view).pages().select(
           gf::PageId{"advanced"}, gf::PageSelectionCause::programmatic));
       // The request ran: the advanced page now has its label.
       static_cast<void>((*settings.view).page_content(gf::PageId{"advanced"}));
   }
