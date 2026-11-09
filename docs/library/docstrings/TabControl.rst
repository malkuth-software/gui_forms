A set of pages shown one at a time, chosen from a row of tabs.

A tab control holds `TabPage` controls and shows the selected one in its
display area, with a tab for every page along one edge. The user selects a
page by clicking its tab, with the arrow keys, Home and End while the
control has focus, or with Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageDown and
Ctrl+PageUp from anywhere inside the control. The tabs are a `PageStrip`:
they are as wide as their captions, and when they do not fit the strip's
scroll and list buttons appear. Use `ExpandableSections` when several parts should be visible
at once, and a `SplitContainer` to show two panes side by side.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Each tab is
    published to accessibility as ``<page stable id>.tab``.

Properties
----------
pages
    The pages in tab order. Only pages that are still children of the
    control are listed.
page_count
    The number of pages.
selected_index
    The index of the selected page, or empty when there are no pages.
    Assigning an index hides the old page, shows the new one and raises
    `selected_index_changed`; an index outside the pages throws
    `std::out_of_range` and an empty value `std::invalid_argument`.
    Selecting the current page does nothing.
selected_tab
    The selected page, or null. Assigning a page of this control selects it;
    any other page, or null, throws `std::invalid_argument`.
alignment : default ``TabAlignment::top``
    The edge the tabs run along: top or bottom with tabs side by side, left
    or right with tabs stacked. The display area takes the rest.
appearance : default ``TabAppearance::normal``
    How tabs are drawn, through the theme's recipes until the tab stack has
    its own look: `TabAppearance::normal` flat at rest with a plate when hot
    or selected, the selected tab joined to the page; `TabAppearance::buttons`
    a button's plate on every tab; `TabAppearance::flat_buttons` a command
    button's lighter plate on every tab.
size_mode : default ``TabSizeMode::normal``
    How wide each tab is: `TabSizeMode::normal` its caption plus padding,
    `TabSizeMode::fill_to_right` the row shared equally, `TabSizeMode::fixed`
    `item_size` whatever the caption.
item_size : default ``{120.0, 30.0}``
    In logical units before the window's text scale. Its height is the
    thickness of a top or bottom tab row, its width the thickness of a left
    or right column, in every size mode. Under `TabSizeMode::fixed` it is
    every tab's size. Smaller than 24 by 18, larger than 1024 by 1024, or
    not finite, throws `std::invalid_argument`.
style
    Kept for compatibility: the control paints by role from the window's
    theme and no longer reads it.
image_list : default null
    The images the pages' `image_key` and `image_index` name. A tab shows its
    image before its caption, painted by the strip as the tab's icon part.
    Throws `std::invalid_argument` for a disposed list or one that belongs to
    another window, and keeps the old list. Changes to the list move the
    image an index names.
overflowing
    Whether the tabs do not all fit, so the scroll and list buttons show.
draw_mode : default ``ItemDrawMode::normal``
    With ``ItemDrawMode::owner_draw`` the application draws the tabs'
    content (Windows Forms' `DrawMode.OwnerDrawFixed`): see
    `draw_item_content`. Throws `std::invalid_argument` for an unknown mode.
display_bounds
    The area given to the selected page, in the control's coordinates. With
    the default alignment it starts one pixel above the bottom of the tab
    strip.

Events
------
draw_item_content
    Raised while the tabs paint in the owner-draw mode, once per painted
    tab, with an `ItemContentPaint` whose index is the tab's and whose
    stable ID is its page's (Windows Forms' `DrawItem`). The painter is
    clipped to the tab's icon and label area; the tab's plate and focus cue
    are painted by role. Set `draw_default` to add the default content. A
    handler paints only; one that changes the tabs ends that paint, and the
    tabs are painted again.
selected_index_changed
    Raised after the selection changes, by the user or by a setter, with a
    `TabSelectionChange` holding the old and new index (either may be
    empty). By then the old page is hidden and the new page shown, and
    after `set_selected_index` or user input, focus has been moved as
    described in the Notes. Adding the first page raises it with no old
    index.
    Removing a page raises it when the selected page or its index changes,
    so removing a page before the selected one raises it although the same
    page stays selected.
deselecting
    Raised with a `TabControlCancelEvent` for the selected page before a
    change of page by the user (a click, a key, the strip's list) or by
    `set_selected_index` (Windows Forms' `Deselecting`). Set `cancel` to keep
    the page; nothing has changed yet.
selecting
    Raised after `deselecting`, for the page about to be chosen (Windows
    Forms' `Selecting`). Set `cancel` to keep the selected page. A handler
    that disposes the control or removes the page ends the change.
deselected
    Raised with a `TabControlEvent` for the page left, once the new page is
    shown (Windows Forms' `Deselected`).
selected
    Raised after `deselected` for the page chosen (Windows Forms'
    `Selected`), before `selected_index_changed`. The first page added, and
    the page that replaces a removed selected page, raise only
    `selected_index_changed`. Windows Forms raises `Deselected` before
    `Selecting`; here both cancellable events come first, so a cancelled
    change raises nothing after them.

Methods
-------
add_page
    Adds a page at the end, making it a child of the control. The first
    page added is selected; later pages are hidden until selected. Adding a
    page that is already here does nothing; a null page, or one whose stable
    ID another page of the control has, throws `std::invalid_argument`; a
    page beyond `TabControl::maximum_pages` (4096) throws
    `std::length_error`; the errors of `add_child` also apply. A refused page
    leaves the control unchanged.
remove_page
    Removes a page and returns it, or returns null when it is not a page of
    this control. When the selected page is removed, the page that takes its
    index, or else the last page, is selected and shown.
page_at
    The page at an index; an index outside the pages throws
    `std::out_of_range`.
tab_bounds
    The rectangle of the tab at an index, in the control's coordinates, or
    an empty rectangle while the tab is scrolled out of view; an index
    outside the pages throws `std::out_of_range`.

See Also
--------
TabPage : One page of a tab control.
TabAlignment : The edge the tabs run along.
TabAppearance : How the tabs are drawn.
TabSizeMode : How wide each tab is.
PageStrip : The tab strip a tab control is built on.
ExpandableSections : Several collapsible parts visible at once.

Notes
-----
Each page is arranged into `display_bounds`, and the control shows only the
selected page and hides the rest, so a hidden page's controls are neither
painted, hit-tested nor focusable. The control sets each page's `visible`
whenever it lays out its pages, so do not set it yourself. Children added
with `add_child`
instead of `add_page` are not arranged.

The tabs are an internal `PageStrip` child (``<stable id>.strip``) over one
record per page: its layout, its painting by role and its overflow serve
the control. When the tabs do not fit, they keep their widths and the strip
shows scroll buttons and a list button; the selected tab is always scrolled
into view. On the left and right the captions turn, reading bottom to top
on the left and top to bottom on the right. The strip takes no focus and
publishes nothing to accessibility; the control does both. Captions are
drawn as written; `&` has no mnemonic meaning here.

A primary press on a tab focuses the control and selects the tab
immediately. The control takes focus as one tab stop. While it has focus,
Left and Right (top and bottom alignment) or Up and Down (left and right
alignment) select the previous or next tab, wrapping around, and Home and
End select the first and last. Ctrl+Tab and Ctrl+Shift+Tab, and
Ctrl+PageDown and Ctrl+PageUp, cycle forwards and backwards, wrapping
around, while focus is anywhere inside the control and before the focused
control sees the key; when tab controls are nested, the innermost one
around the focus takes them. These are the window's named keyboard
commands (``cycle_forward``, ``cycle_backward``, ``next_tab``,
``previous_tab``), so `Window::rebind_key_command` changes them. The
commands a tab control does not offer (``close_document``, Ctrl+F4 or Cmd+W;
``context_menu``, Shift+F10 or the context-menu key; moving a tab) pass to
the controls around it, such as a `PageView` holding the tab control in a
document. When the selection changes while focus is inside the old page,
the control remembers that focused control and moves focus to the control
last focused in the new page, or else to the control itself. A click on a
tab moves focus to the control before it selects, so it does not remember
the old page's focus.

The control is published to accessibility as a `tab_group` whose value is
the selected page's caption. Each page has a virtual `tab` child named by
its caption, with the selected state, naming its page as the panel it
controls; a tab scrolled out of view has no bounds and is not visible. Its
focus and select actions (and press, which tabs no longer publish) focus the
control and select that tab. The selected page is published as a
``tab_panel`` named by its caption.

The control does not scroll; leave `auto_scroll` off and scroll the pages
instead.

Examples
--------
Build a two-page settings dialog and select its second page:

.. code-block:: cpp

   std::shared_ptr<gf::TabControl> tabs =
       gf::make_control<gf::TabControl>(gf::StableId("settings.tabs"));
   std::shared_ptr<gf::TabPage> general =
       gf::make_control<gf::TabPage>(gf::StableId("settings.general"), "General");
   std::shared_ptr<gf::TabPage> advanced =
       gf::make_control<gf::TabPage>(gf::StableId("settings.advanced"), "Advanced");
   std::shared_ptr<gf::CheckBox> wrap =
       gf::make_control<gf::CheckBox>(gf::StableId("settings.general.wrap"), "&Wrap lines");
   (*wrap).set_requested_bounds({12.0, 12.0, 160.0, 24.0});   // in the page's coordinates
   (*general).add_child(wrap);
   (*tabs).add_page(general);    // the first page is selected
   (*tabs).add_page(advanced);
   (*tabs).selected_tab = advanced;

Follow the selection with an owned handler, and change it from the keyboard:

.. code-block:: cpp

   struct Settings final : gf::Component {
       std::shared_ptr<gf::TabControl> tabs =
           gf::make_control<gf::TabControl>(gf::StableId("settings.tabs"));
       std::size_t shown = 0;

       Settings() {
           gf::on((*tabs).selected_index_changed(), *this, &Settings::on_page);
       }
       void on_page(const gf::TabSelectionChange& change) {
           if (change.new_index) {
               shown = *change.new_index;
           }
       }
   };

   void example() {
       Settings settings;
       (*settings.tabs).add_page(
           gf::make_control<gf::TabPage>(gf::StableId("settings.general"), "General"));
       (*settings.tabs).add_page(
           gf::make_control<gf::TabPage>(gf::StableId("settings.advanced"), "Advanced"));

       gf::Window window(settings.tabs, {480.0, 300.0});
       window.perform_layout();
       window.request_focus(settings.tabs);
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::right});
       // the second page is selected: settings.shown is 1
   }
