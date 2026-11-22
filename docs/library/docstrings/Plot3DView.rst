A 3D plot: surfaces, lines and scatters in a box with back panes, a grid and
ticks, turned, zoomed and panned by the user.

The plot shows a `plots::Plot3DScene`, drawn on the CPU into one picture,
with its tick labels, axis titles and the hovered point's readout drawn as
themed text over it. The picture is kept: it is rendered again only when the
view, the size, the scale, the items or the theme's colours change, once at
one sample per pixel while a gesture moves it and again at 2 by 2 samples
when the gesture ends. Use a `PlotView` for two-dimensional data.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
show_panes : default ``true``
    Whether the three back panes of the box are filled.
show_grid : default ``true``
    Whether the grid is drawn on the back panes at the ticks.
show_axes : default ``true``
    Whether the axis lines, ticks, tick labels and axis titles are drawn.
wheel_zoom : default ``true``
    Whether the wheel zooms about the pointer.
labels_along_axis : default ``false``
    Whether a steep axis's title is laid along its edge as projected, turned
    at that angle but kept reading left to right; otherwise the titles face
    the screen.
scene
    The scene as drawn last: its items, limits, labels and statistics.
camera
    The camera (`plots::Plot3DCamera`). `set_camera` takes a new one under
    `plots::Plot3DScene`'s rules, returns whether it was taken, and raises
    `camera_changed` when it changed.
hover
    The point under the pointer as last hovered (`plots::Plot3DHit`).
readout
    The hovered point's coordinates as shown at the plot's lower left, or
    the angles while a drag turns the box.
plot_bounds
    The plot rectangle, in local coordinates.
picture_renders
    How many pictures have been rendered; a repaint that changes nothing
    renders none.

Events
------
hovered
    Raised when the point under the pointer changes, with what lies there;
    a hit of none when the pointer leaves the data.
picked
    Raised on a click that did not drag, with what lies under it.
camera_changed
    Raised once per change of the camera by a gesture, a key, `set_camera`
    or `reset_view`.

Methods
-------
add_surface
    Copies a surface (`plots::Plot3DSurfaceSpec`) into the scene and returns
    its item ID, under `plots::Plot3DScene`'s rules and failures.
    `add_line` and `add_scatter` add a line and a scatter the same way.
remove_item
    Removes an item by its ID; false for an unknown one.
clear_items
    Removes every item.
reset_view
    Returns to the default angles, zoom and target; the projection stays.
set_axis_title
    Sets an axis's title, drawn beside its ticks; empty draws none. An axis
    outside `plots::Plot3DAxis` throws `std::invalid_argument`.
axis_title
    An axis's title; empty for an axis outside `plots::Plot3DAxis`.
set_box_aspect
    Sets the box's side ratios, 4:4:3 by default. A ratio that is not
    positive and finite throws `std::invalid_argument`.
hit_at
    What lies under a local point (from the control's top left) in the kept
    picture.

See Also
--------
PlotView : A two-dimensional plot.

Notes
-----
With the pointer, a left drag turns the box (azimuth across, elevation up
and down), a right drag zooms, a middle drag or Shift with a left drag pans,
the wheel zooms about the pointer (while `wheel_zoom` is set), and a double
click resets the view. The plot is a tab stop with a focus ring: the arrows
turn the box 5 degrees as a drag that way does, Shift with the arrows pans,
``+`` and ``-`` zoom, Home resets, and P switches between perspective and
orthographic projection. The events are raised on the UI thread after the
change they report.

The point under the pointer is looked up per pixel in the kept picture,
exactly on interpolated surfaces, and shown as the readout. Data items have
no tooltips.

To assistive technology the plot is one figure, named by its accessible
name, described by its items and view, with the readout as its value. Its
parts are ``plot``, ``picture`` (drawn as the image role's raster),
``x_axis``, ``y_axis``, ``z_axis``, ``readout`` and ``focus``; the labels and
readout are text roles.
