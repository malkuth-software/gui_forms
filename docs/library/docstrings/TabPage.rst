One page of a tab control, with the caption shown on its tab.

A tab page is a `Panel` that a `TabControl` shows in its display area while
the page's tab is selected. Add the page's controls to it with `add_child`;
they are placed, docked, anchored and scrolled as in any panel. The tab
control owns the page's position, size and visibility, and the selection.

Parameters
----------
stable_id
    Identity of the page, unique within its window. The page's tab is
    published to accessibility as ``<stable_id>.tab``.
initial_text
    The caption on the page's tab at construction.

Properties
----------
text
    The caption on the page's tab, drawn exactly as written, and the page's
    accessible name unless you set one. Setting it repaints the tab strip.
image_key : default empty
    The key of the tab's image in the tab control's `image_list`. Setting it
    clears `image_index`. An empty key, or one the list does not have, shows
    no image. Throws `std::invalid_argument` for a key that is not valid
    UTF-8 or is longer than 256 bytes.
image_index : default ``-1``
    The index of the tab's image in the tab control's `image_list`, used
    while `image_key` is empty; setting it clears `image_key`. ``-1``, or an
    index past the list's end, shows no image. Throws `std::out_of_range`
    below ``-1``.

See Also
--------
TabControl : The control that holds and selects pages.
Panel : The container behaviour a page inherits.

Notes
-----
A new page has a white background and a one-pixel line border, set through
the inherited `set_background` and `border_style`; change them as on any
panel.

Add the page to a tab control with `TabControl::add_page`, not `add_child`,
and leave its bounds and `visible` to the tab control. While it is not
selected the page and its controls are hidden: they are not painted,
hit-tested, focused or published to accessibility.

To scroll a page's content, turn on `auto_scroll` on the page rather than on
the tab control.

The page is always published to accessibility as a `group`, named by its
accessible name or else by its caption.

Examples
--------
A page whose content scrolls when the tab control is too small for it:

.. code-block:: cpp

   std::shared_ptr<gf::TabControl> tabs =
       gf::make_control<gf::TabControl>(gf::StableId("prefs"));
   std::shared_ptr<gf::TabPage> fonts =
       gf::make_control<gf::TabPage>(gf::StableId("prefs.fonts"), "Fonts");
   std::shared_ptr<gf::Label> sample =
       gf::make_control<gf::Label>(gf::StableId("prefs.fonts.sample"), "The quick brown fox");
   (*sample).set_requested_bounds({12.0, 12.0, 400.0, 600.0});
   (*fonts).add_child(sample);
   (*fonts).auto_scroll = true;   // scroll the page, not the tab control
   (*tabs).add_page(fonts);
   (*fonts).text = "Fonts and colours";   // the tab shows the new caption
