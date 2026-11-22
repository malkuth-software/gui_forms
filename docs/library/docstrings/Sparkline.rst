A word-sized trend line: the newest values drawn as one line across the
control, with a dot on the newest.

A sparkline keeps the newest `capacity` values in a ring and draws them
across its bounds, the newest at the right, scaled to their own minimum and
maximum, with a dot on the newest value and, with `fill`, the band under the
line shaded. Any number of values draws at about four points per device
column. It is read-only: it takes no input and no focus. Use a `PlotView` for
axes, several series or interaction. To draw the same trend inside a grid
cell, a list item or an owner-drawn control without a `Sparkline`, call
`paint_sparkline`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
capacity : default ``120``
    How many of the newest values the line keeps, from 2 to 100000; outside
    that range throws `std::invalid_argument`. A smaller capacity drops the
    oldest values.
fill : default ``false``
    Whether the band under the line is shaded.
values
    The values kept now, oldest first.
latest
    The newest finite value, or none.
extent
    The finite extent of the values kept now; an invalid range when there
    are none.
plot_bounds
    The rectangle the line is drawn in, in local coordinates.

Methods
-------
push
    Appends a value, dropping the oldest past the capacity. NaN is a gap in
    the line; an infinity throws `std::invalid_argument` and changes
    nothing. `push_values` appends several at once, under the same rules.
clear_values
    Removes every value.

See Also
--------
PlotView : A full plot with axes and interaction.
SevenSegmentDisplay : A single reading.

Notes
-----
To assistive technology a sparkline is a figure, named by its accessible
name, with the newest value as its value and a description such as "Trend
of 120 values from 3.5 to 9.25", unless an accessible description is set.
Numbers are written with a point, whatever the locale. Its parts are
``plot``, ``series`` and ``marker`` (the newest dot).

`paint_sparkline` draws values (oldest first; NaN is a gap) the way the
control draws its ring, inside a rectangle, across a number of slots with
the newest at the right. The slots must be at least 2 and at least the
number of values, or nothing is drawn. It takes a `SparklineStyle` and a
`SparklineWorkspace` that it reuses from paint to paint and grows only for
a wider plot.
