A group of `PlotView` controls whose first x axes, first y axes or both
share one range, such as linked subplots or a plot and its overview.

When a member's view changes (by a gesture, a key, `PlotView::set_x_range`,
`PlotView::set_y_range` or `PlotView::fit_view` on it), the group sets the
changed range on every other member, in the order they joined. Each member
still raises its own ``view_changed``; the group ignores those as echoes of
its own sets, so a change goes round once and the member that moved is never
set again. A range a member refuses (one its scale cannot show) is left, and
that member keeps its own.

Parameters
----------
axes
    Which of the members' first axes are linked: `PlotLinkAxes` ``x``,
    ``y`` or ``both``.
x
    The linked x range the group starts with; it must be valid. The first
    member to join takes it.
y
    The linked y range the group starts with; it must be valid.

Properties
----------
axes
    Which axes are linked. `set_axes` changes it from then on; linking an
    axis that was not linked makes every member take that link's range.
x_link
    The linked x range (`plots::AxisLink`); `y_link` is the y range.
member_count
    The number of plots that have joined.
propagations
    The ranges the group has set on members.
echoes
    The member view changes the group ignored as echoes of its own sets.

Methods
-------
add
    Joins a plot, which takes the linked ranges at once. A null plot, or one
    that has already joined, throws `std::invalid_argument` and adds
    nothing.

See Also
--------
PlotView : The plots the group links.

Notes
-----
The group observes its members and does not own them: it holds weak
pointers and skips a member that has died. Its subscriptions end when the
group is disposed or destroyed, or with the member's events. The group and
its members share one UI thread.
