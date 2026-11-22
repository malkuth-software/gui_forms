A plain-text label of a `PlotView`, anchored at a data point and declared as
a child of the plot.

The plot places the label beside its anchor, clear of the annotations
before it and inside the plot (`plots::layout_annotations`). The item draws
nothing itself, takes no input, no focus and no space, and publishes no
accessibility node: the plot reads it.

Parameters
----------
stable_id
    Identity of the item, unique within its window.

Properties
----------
text : default empty
    The annotation's words, as plain text.
x : default ``0``
    The anchor's x, in the units of the plot's first horizontal axis. A
    value that is not finite throws `std::invalid_argument`.
y : default ``0``
    The anchor's y, in the units of the plot's first vertical axis. A value
    that is not finite throws `std::invalid_argument`.

See Also
--------
PlotView : The plot that draws the label.
PlotSeriesItem : A series the label may point at.
