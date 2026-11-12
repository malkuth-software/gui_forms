A list of a very large number of owner-drawn rows of varying height.

The view keeps no row records. It keeps the row count and each row's height
in a height-indexed order-statistic tree, and asks the application to
measure, draw and describe the rows it shows, through the three targets of
`VirtualRowsCallbacks`. Every row starts at the estimated height, so a
million rows cost no measuring at startup; a row is measured once, when it
first comes near the viewport. While rows above the viewport are measured,
edited, inserted or erased, the row at the top keeps its place on screen.
It is a GUI.Forms control; Windows Forms has no equivalent.

Parameters
----------
stable_id
    Identity of the view, unique within its window. Its scroll bar is a
    child control named ``<stable_id>.vertical-scroll-bar``; its rows are
    published to accessibility as ``<stable_id>.row-<n>``, followed by a live
    region named ``<stable_id>.announcements``.

Properties
----------
row_callbacks
    The measure, paint and describe targets. Each is a `Delegate`: an
    object and a member function the application binds; the view does not
    own it, so a target must outlive the view or be replaced first. A target
    that changes the view's rows, callbacks or scroll is refused with
    `std::logic_error`.
estimated_row_height
    The height rows take until measured. Changing it forgets every measured
    height.
scroll_offset
    The viewport's top in content coordinates, in logical units.
content_height
    Every row's height, measured or estimated.
follow_tail
    Whether a view at the end of its content stays at the end as rows are
    appended. Off by default. Scrolling away from the end stops following
    until the view returns to the end.
following_tail
    Whether the view is following now: follow_tail is on and the viewport is
    at the end.
announces_appends
    Whether rows appended at the end are announced through the live region.
    On by default.
announcement
    The live region's current text, such as "6 new rows, 106 in all".
statistics
    Work counters: rows measured, painted and described, scroll copies and
    repaints, and the height index's runs, nodes and depth.

Events
------
selection_changed
    Raised after the selected row changes, by the user or the program, or
    when the selected row is erased. It carries the previous and current
    rows.
row_activated
    Raised on a double click, Enter or Space on the selected row, and on the
    accessibility press of a row.

Methods
-------
reset_rows
    Replaces every row with estimated rows, scrolls to the top and clears
    the selection.
insert_rows
    Inserts estimated rows; the selection and the anchor follow their rows.
erase_rows
    Erases rows; a selected row among them is unselected.
refresh_rows
    Forgets the measured heights of rows whose content changed.
scroll_to_offset
    Scrolls to an offset, measuring the rows the new viewport shows.
scroll_to_row
    Scrolls a row into view at the start, centre or end of the viewport, or
    by the least scroll that shows it.

Notes
-----
Only the rows meeting the viewport are painted and described, and only
those within half a viewport of it are measured. A change of the content
width forgets the measured heights, since wrapped rows change height with
it. Scrolling by whole device pixels moves the painted pixels instead of
repainting them when the view's background is opaque: the theme's ground
for the view's surface, or ``back_color``, which overrides it per control.

Rows appended between two layouts are announced once, at the next layout,
with their count and the new total; screen readers speak the live region
politely when its text changes. Rows inserted before the end and a reset
are not announced.
