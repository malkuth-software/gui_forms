A pie or doughnut chart: one slice per value, each a share of the total,
with share labels and a legend.

A pie chart shows records described by `PieSlice`: a stable ID, a label, a
value and an optional colour. Slices run clockwise from `start_angle` in the
order given; a slice without a colour takes the theme's series colour for
its position, as `PlotView`'s series do, so the chart follows the theme. A
doughnut is a pie with a hole: set `doughnut_radius` to the hole's radius as
a percentage of the pie's. It is the plots package's counterpart of Windows
Forms' ``Chart`` with a ``Pie`` or ``Doughnut`` series.

Parameters
----------
stable_id
    Identity of the chart, unique within its window.

Properties
----------
slices
    The slices in the order given. `set_slices` replaces them, keeps the
    selection on its ID, and throws `std::invalid_argument` for a repeated
    ID or one that breaks the `StableId` segment rule, invalid UTF-8, a
    value that is negative or not finite, or more than `maximum_slices`
    slices; the old slices then stay.
total
    The sum of the values.
doughnut_radius : default ``0``
    The hole's radius as a percentage of the pie's, from 0 (a pie) to 90
    (Windows Forms' ``DoughnutRadius``).
start_angle : default ``0``
    Where the first slice starts, in degrees clockwise from the top, from 0
    to below 360 (Windows Forms' ``PieStartAngle``).
label_style : default ``PieChartLabelStyle::inside``
    Where each slice's share is written: on the slice, beyond its edge, or
    nowhere (Windows Forms' ``PieLabelStyle``).
legend_visible : default ``true``
    Whether the legend beside the pie lists the slices with their colours
    and shares.
font
    The font of the share labels and the legend.
selected_id
    The selected slice's ID, or none. `set_selected_id` selects one, or
    none, and throws `std::out_of_range` for an unknown slice.
realized_slices
    Every slice's parts and states, in the order given.

Events
------
selection_changed
    Raised after the selected slice changes: by a click on a slice or its
    legend entry, the arrow keys, the accessibility select or press, or
    `set_selected_id`, and when `set_slices` drops the selected slice.
slice_activated
    Raised on a double click, on Enter, and on the accessibility press of a
    slice.

Methods
-------
set_slice_value
    Sets one slice's value. Throws `std::out_of_range` for an unknown slice
    and `std::invalid_argument` for a value that is negative or not finite.
share
    A slice's share of the total, from 0 to 1.
index_of
    The position of the slice with an ID, or none.
slice_at
    The slice under a point in local coordinates, on the pie or its legend
    entry, or none.

See Also
--------
PlotView : A plot with axes, several series and interaction.

Notes
-----
To assistive technology a pie chart is a list whose items are the slices,
each named by its label with its value and share as its value. Its parts
are ``plot``, ``legend`` and ``focus``; each slice publishes ``slice``,
``slice_label``, ``legend_swatch`` and ``legend_label``, with the states
hot, selected and focus_visible. A selected slice moves out from the centre.
