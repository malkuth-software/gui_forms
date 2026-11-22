A retained, declarative plot: axes, series and annotations declared as item
children, with panning, zooming, picking and a legend.

A plot is declared, in a form or in C++, as a `PlotView` whose children are
items: `PlotAxisItem` (its axes), `PlotSeriesItem` (what it draws) and
`PlotAnnotationItem` (labels at data points). Other children are ignored.
The plot reads its items on each layout and paint, and an item's change
tells the plot, which lays out and repaints, so a form's rules reach a plot
through its items' properties and its own. Use a `Sparkline` for a small
trend without axes or interaction, and a `Plot3DView` for a surface or
points in three dimensions.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
title : default empty
    The words drawn above the plot, and its accessible name unless an
    accessible name is set.
live : default ``false``
    Whether the plot draws on its own sub-surface, paced by that
    sub-surface's frame pacer and fed by its series' ring channels (see
    Notes). Off, it draws on the window's thread.
show_legend : default ``true``
    Whether the legend is drawn, when a series has a label.
show_grid : default ``true``
    Whether the major and minor grid lines are drawn.
crosshair : default ``false``
    Whether a crosshair follows the pointer over the plot, with its
    coordinates.
handles
    The handles the user drags across the plot, as `PlotHandle` records:
    points, and vertical or horizontal lines, in the data coordinates of the
    first axes. `set_handles` replaces them, cancelling a running handle
    drag first; it throws `std::invalid_argument` and changes nothing for a
    non-finite position, a repeated ``id`` or more than
    `maximum_plot_handles`.
dragged_handle
    The ``id`` of the handle being dragged, or none.
series_items
    The series items, in child order, as the plot read them last.
query
    The query rectangle, if one is kept.
cursor
    The keyboard point cursor's point, or none when it is on no point.
    `set_cursor` places it on an element of a series (false when there is
    none) and raises `point_picked`.
summary
    The figure's summary: its series, their kinds and point counts, and the
    visible ranges. It is the plot's accessible description.
table_text
    The plot's data as tab-separated rows (series, x, y), at most 1000 rows
    per series; `table_row_count` counts them.
plot_bounds
    The plot rectangle, in local coordinates.
live_host
    The `SubSurfaceHost` the plot draws on while `live`; null otherwise.

Events
------
view_changed
    Raised once per change of the first axes' ranges by a gesture, a key,
    `set_x_range`, `set_y_range` or `fit_view`, with the new ranges.
    Automatic fitting and following, which run inside layout and paint,
    raise nothing.
selection_changed
    Raised when the query rectangle is made or cleared.
point_picked
    Raised when a click or the keyboard cursor reaches a point, with the
    series, the element and its coordinates.
legend_toggled
    Raised after a click on a legend entry, or `toggle_series`, changed a
    series' ``hidden``; it carries the series' position.
handle_moved
    Raised at each move of a dragged handle, and when a drag is cancelled
    (by Escape, a lost capture or the window's deactivation) with the handle
    back where it was.
handle_committed
    Raised when the user releases a dragged handle, with its final position.

Methods
-------
set_handle_position
    Moves a handle; false for an unknown ``id`` or a non-finite position.
    It raises nothing.
handle_point
    A handle's centre in local coordinates on the first axes, or none while
    the plot is not laid out.
x_range
    The visible range of the x axis at an index: the axis item's range until
    a gesture, a fit or following moves it. `y_range` gives a y axis's.
set_x_range
    Sets the visible range of an x axis; false, changing nothing, when the
    axis does not exist or the range cannot be shown. `set_y_range` sets a
    y axis's. Each raises `view_changed` for the first axes.
fit_view
    Fits the view to the visible series' data, and raises `view_changed`
    when it moved.
reset_view
    Takes the axis items' ranges again, and resumes automatic fitting.
toggle_series
    Hides or shows the series at a position (its item's ``hidden``) and
    raises `legend_toggled`, as a click on its legend entry does.
clear_query
    Removes the query rectangle.

See Also
--------
PlotAxisItem : An axis of the plot.
PlotSeriesItem : A series the plot draws.
PlotAnnotationItem : A label at a data point.
PlotLinkGroup : Plots that share their view.
Sparkline : A small trend without axes.

Notes
-----
With the pointer, a left drag pans, the wheel zooms about the pointer, a
right drag zooms to a box, Ctrl with a right drag keeps a query rectangle,
and a double click fits; over an axis each acts on that axis only. A click
on a point picks it, and a click on a legend entry hides or shows its
series. With the keyboard, the arrows move the point cursor (left and right
along its series, ten points with Shift; up and down between series), Alt
with the arrows pans, Ctrl with the arrows zooms one axis, ``+`` and ``-``
zoom, Home fits, End resumes following, and Escape cancels a gesture or
clears the query. The events are raised on the UI thread after the change
they report.

A primary press within `handle_pick_radius` of an enabled handle starts a
handle drag once the pointer passes the drag threshold, ahead of panning.
The handle follows the pointer and stays inside the plot rectangle: a point
moves on both axes, a line across itself only. Positions are in the first
axes' data coordinates, so a handle on a log or time axis moves as the axis
reads. A live plot draws and drags no handles, and handles have no keyboard
drag yet.

While `live` is set, the plot draws on a `SubSurfaceHost` child covering it,
paced by the sub-surface and running its signal programs on that clock.
Each series' ring channel (`PlotSeriesItem`'s ``live_channel``) is consumed
there, its producer thread writing without touching the UI thread. The live
plot's gestures and cursor run on its thread and come back as the same
events, raised on the UI thread. Without `live`, signal programs and
channels run on the window's motion clock, 60 times a second while the plot
has either. A plot that is not painted (a hidden tab, a collapsed panel, a
plot scrolled out) runs no signal ticks and consumes no channel until it is
painted again; its signals then continue without catching up, and what a
channel's ring overwrote meanwhile is lost.

To assistive technology the plot is one figure, named by its accessible
name or else its title, described by `summary`, with the cursor's point as
its value; `table_text` offers its data as rows. The plot's parts are
``title``, ``plot``, ``x_axis``, ``y_axis``, ``legend``, ``crosshair`` and
``focus``.
