One axis of a `PlotView`, declared as a child of the plot: its direction,
scale, initial range, title, fitting and following.

The first horizontal axis is drawn below the plot and a second above it;
the first vertical axis left of the plot and a second right of it; further
axes are ignored. A plot without a horizontal or a vertical axis item uses a
linear one over 0 to 1. An item draws nothing, takes no input, no focus and
no space, and publishes no accessibility node: the plot reads it.

Parameters
----------
stable_id
    Identity of the item, unique within its window.

Properties
----------
direction : default ``PlotAxisDirection::horizontal``
    Which way the axis runs: horizontal (an x axis) or vertical (a y axis).
scale : default ``PlotAxisScale::linear``
    How the axis maps values: linear, log10, symlog, time (UTC seconds) or
    category.
minimum : default ``0``
    The smallest value shown until a gesture or a fit moves the view. A
    value that is not finite throws `std::invalid_argument`.
maximum : default ``1``
    The largest value shown until a gesture or a fit moves the view. A value
    that is not finite throws `std::invalid_argument`.
label : default empty
    The axis title, drawn beside its tick labels.
auto_fit : default ``true``
    Whether the axis fits the data when the series change, until a gesture
    moves the view.
follow_span : default ``0``
    A horizontal axis's following window: above zero the axis shows the
    newest x and this span before it, as a live plot's tail; zero turns
    following off. A negative or non-finite value throws
    `std::invalid_argument`.
revision
    A count every property write raises; the plot compares it with the one
    it last took the item's range at.

Methods
-------
axis_description
    The axis as the plot's primitives describe it (`plots::Axis`), on a
    given side.

See Also
--------
PlotView : The plot that reads the axis.
PlotSeriesItem : A series drawn against the axes.

Notes
-----
Changing a property tells the owning plot, which lays out and repaints. The
plot takes the item's range again only until a gesture moves its view;
`PlotView::reset_view` takes it again.
