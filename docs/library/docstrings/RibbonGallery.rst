A ribbon gallery: a grid of image items in categories, in the ribbon or as a drop-down.

A ribbon gallery shows choices as pictures: styles, shapes, colours,
layouts. It comes in two forms. In the ribbon it shows a fixed number of rows
with scroll buttons and a "more" button that opens the full gallery as an overlay
over the ribbon (owner decision RB-23). As a drop-down it is that full gallery,
grouped by category under captions, shown as the overlay of a drop-down or split
button. The selection follows a `GalleryCommand`'s value; resting the pointer
or the keyboard on an item for 300 ms asks the command for a live preview
(owner decision RB-13), and a click or Enter commits it.

Parameters
----------
stable_id
    Identity of the gallery. Its overlay layer is ``<stable_id>.layer``, the
    full gallery its more button opens ``<stable_id>.more``, and category
    nodes ``<stable_id>.category.<id>``.
form : default in_ribbon
    `RibbonGalleryForm::in_ribbon` or `RibbonGalleryForm::drop_down`.

Properties
----------
form
    The form, fixed at construction.
categories
    The categories in display order.
items
    The items. `set_items` replaces the categories and items: items without a
    category show first, then category by category in category order. A
    repeated ID or one that breaks the `StableId` segment rule, an item
    naming an unknown category, or invalid UTF-8 throws
    `std::invalid_argument`.
command
    The `GalleryCommand` whose value the selection follows, which a click or
    Enter commits and which receives previews. `set_command` sets it; without
    one the gallery keeps its own selection and offers no preview.
selected_id
    The selected item's ID: the command's value, or the gallery's own.
    `set_selected_id` selects without committing; an unknown ID throws
    `std::invalid_argument`.
item_size : default 48 by 48
    Every item's image area, from 16 to 256 units a side (`std::out_of_range`
    otherwise).
show_labels : default ``false``
    Whether labels show under the images.
visible_rows : default ``3``
    In the ribbon: the rows shown, 1 to 8.
minimum_columns : default ``1``
    In the ribbon: the least columns. `set_column_range` sets both bounds,
    within 1 to 32, and keeps the column count inside them.
maximum_columns : default ``8``
    In the ribbon: the most columns.
columns : default ``8``
    In the ribbon: the current column count, which the ribbon's scaling
    lowers one column at a time (owner decision RB-8). `set_columns` outside
    the range throws `std::out_of_range`.
drop_down_columns : default ``6``
    Drop-down: the columns per row. `set_drop_down_size` sets it and the rows.
drop_down_rows : default ``5``
    Drop-down: the item rows shown before the gallery scrolls.
first_visible_line
    The first line shown: an item row, or a category caption in the drop-down
    form.
line_count
    The number of lines.
can_scroll_up
    Whether a line is hidden above.
can_scroll_down
    Whether a line is hidden below.
overlay_open
    Drop-down: whether the gallery is open as an overlay.
more_gallery
    In the ribbon: the full gallery the more button opens, made on first use.
focused_id
    The item keyboard focus is on, or none.

Events
------
selection_changed
    Raised after the selection changes, with the new item or none.
overlay_open_changed
    Drop-down: raised when the overlay opens or closes.

Methods
-------
scroll_to_line
    Shows a line first; layout keeps the last page full.
open_overlay
    Drop-down: shows the gallery in an overlay under an anchor (a button, or an
    in-ribbon gallery), with keyboard focus in it. Returns false for the
    in-ribbon form, when already open, or when the anchor is not attached to a
    window.
close_overlay
    Withdraws any preview, then closes the overlay and returns focus.
show_more
    In the ribbon: opens the full gallery under this one. Returns false for
    the drop-down form or when it cannot open.
width_for_columns
    In the ribbon: the width, in logical units at the current text scale, the
    gallery measures at a given column count. A `Ribbon` holding the gallery
    reads it for each column step of its reduction ladder.

See Also
--------
GalleryCommand : The value command a gallery commits and previews through.
AnchoredOverlayLayer : The overlay a drop-down gallery opens in.
ToolStrip : The strip the ribbon's commands follow.

Notes
-----
Preview and commit, in order. A pointer or keyboard focus resting on an
enabled item for 300 ms raises the command's `preview_requested`. Moving to
another item and resting there previews it in place of the first, without a
cancellation between them. Leaving the gallery, Escape, keyboard focus
leaving the gallery, the command becoming disabled, or the overlay closing
raises `preview_cancelled` (before the overlay's `overlay_open_changed`). A click
(press and release on one item), Enter or Space commits: the command's
`value_changed`, then its `invoked`, with no cancellation; in an overlay the
overlay then closes. A pointer that crosses items without resting previews
nothing.

Keys: the arrows move by item, Up and Down by row in the same column; Page Up
and Page Down move a page of rows; Home and End go to the first and last
item; Enter and Space commit; Escape withdraws a preview first and then
closes an overlay. The focused row is scrolled into view, a category's caption
with its first row.

Items are records, not controls: lines (item rows and captions) are laid out
from the display order, and only the lines in view are realized, as the
``gallery_item`` item system (``item_face``, ``item_selection``,
``item_icon``, ``item_label``, ``item_focus``; hot, pressed, selected,
current, disabled, focus_visible) and the ``gallery_category`` item system
(``category_caption``, ``category_divider``). The gallery's own parts are
``well``, ``item_viewport``, ``scroll_up`` and ``scroll_down`` with their
glyphs, ``more_button`` with the ``more`` mark, and ``focus``. Items are flat
at rest; the dithered button look under the pointer is the theme's (owner
decision RB-15).

The vocabulary has no grid role: the gallery publishes the accessibility role
`list`, each item a `list_item` with its position in the set and the set's
size (grouped under a `group` per category in the drop-down form), every item
published even when not in view, the scroll buttons as buttons and the more
button as a `menu_button` that has an overlay.

Not supported yet: a resize gripper on the drop-down, menu rows beneath the
items, and the ribbon's own placement of in-ribbon galleries (phase 1 and 2).

Examples
--------
A styles gallery in the ribbon form with a shared value command:

.. code-block:: cpp

   std::shared_ptr<gf::Command> styles = std::make_shared<gf::Command>("doc.styles", "Styles");
   std::shared_ptr<gf::GalleryCommand> command = std::make_shared<gf::GalleryCommand>(styles);
   std::shared_ptr<gf::RibbonGallery> gallery =
       gf::make_control<gf::RibbonGallery>(gf::StableId("doc.styles.gallery"));
   (*gallery).set_items({{"headings", "Headings"}},
                        {{"style.title", "Title", {}, "headings"},
                         {"style.heading", "Heading 1", {}, "headings"}});
   (*gallery).set_column_range(2U, 6U);
   (*gallery).columns = 4U;
   (*gallery).command = command;
