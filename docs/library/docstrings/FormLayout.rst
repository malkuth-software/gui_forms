A form of labelled rows: a caption column beside a column of controls.

Each row is as tall as its control (or its caption, if taller), or as the
height `add_row` gave it. The caption column is as wide as the widest
caption, or `caption_width` when set; the controls take the rest of the
width. The captions are `Label` children the layout makes and owns, and
each names its control to assistive technology: it becomes the control's
accessible name when the control has none of its own. Use a `GridPanel` for
a free grid, and a `PropertyGrid` to edit an object's properties.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
caption_width : default ``0``
    The caption column's width in logical units, or 0 for the widest
    caption's. A value that is not finite or outside 0 to 4096 throws
    `std::invalid_argument`.
caption_alignment : default ``HorizontalAlignment::start``
    Where each caption sits in the caption column: at its start, centre, or
    end (beside the control).
row_spacing : default ``6``
    The space between rows in logical units, from 0 to 256; outside that
    range or not finite throws `std::invalid_argument`.
column_spacing : default ``8``
    The space between the caption column and the controls in logical units,
    from 0 to 256; outside that range or not finite throws
    `std::invalid_argument`.
row_count
    The number of rows, empty rows included.
used_caption_width
    The caption column's width at the last layout.

Methods
-------
add_row
    Appends a row: a caption made here, with the stable ID
    ``<control's ID>.caption``, and the control, both added as children. A
    height of 0 (the default) takes the control's measured height. A null
    control, one with a parent, or a negative or non-finite height throws
    `std::invalid_argument` and changes nothing. Returns the caption.
remove_row
    Removes the row holding a control, with its caption and the control;
    false when no row holds it.
forget_empty_rows
    Removes every empty row and its caption, and returns how many.
caption_of
    The caption of the row holding a control, or null.

See Also
--------
GridPanel : Rows and columns of any controls.
PropertyGrid : An object's properties, edited in place.

Notes
-----
Children added with `add_child` rather than `add_row` are not rows: they
keep their own requested bounds. A row's control removed other than by
`remove_row` (by `remove_child`, or by its disposal) leaves an empty row,
which takes no space until `remove_row` of that control or
`forget_empty_rows` drops it. A row whose control is hidden takes no space
either, and comes back when the control is shown.
