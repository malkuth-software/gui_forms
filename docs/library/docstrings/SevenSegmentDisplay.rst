A segmented numeric readout: a row of cells in which each character of
the reading lights the bars of a seven-segment display, as a meter, clock or
instrument panel shows a value.

The reading is right-aligned in `digit_count` cells, or in as many cells as
it needs when `digit_count` is zero. Digits, the hexadecimal letters, the
other letters a seven-segment cell can form, ``-``, ``_``, ``=``, the degree
sign and spaces each take a cell; ``.`` and ``,`` light the decimal point of
the cell before them, and ``:`` is a colon cell, half a digit wide with two
dots. A reading longer than the cells keeps its last digits, as an LCD
counter does, and `overflowed` reports it. Use a `Label` for text that is
not a reading, and a `LinearGauge` or `RadialGauge` to show a value against
a scale.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
text : default empty
    The reading the cells show. A character no cell can form shows as a
    blank cell. A point with no digit before it, or a second point after
    one, takes a blank cell of its own. Invalid UTF-8 throws
    `std::invalid_argument` and keeps the old reading.
digit_count : default ``0``
    The number of digit cells, at most 64; zero shows as many as the
    reading needs. A colon cell is not counted. A larger value throws
    `std::invalid_argument`.
ghost_segments : default ``true``
    Whether the unlit segments are drawn faintly, as on a real display.
cells
    The cells shown now, left to right, as `SevenSegmentCell` records (the
    lit segments, the point and whether the cell is a colon), padded on the
    left with blank cells to `digit_count` digits.
overflowed
    Whether the reading has more digits than `digit_count`, so that its
    first digits are not shown.
parts
    The control's parts and where they lie: ``plot``, the window around the
    cells; ``trace``, the box around the lit bars; and ``graticule``, the box
    around the cells while `ghost_segments` is set.

Events
------
text_changed
    Raised after `text` changes, with the new reading.

See Also
--------
LinearGauge : A value against a scale, with alarm limits.
IndicatorLamp : An off, on or error lamp.

Notes
-----
The cells grow and shrink with the control's height, and shrink further to
fit its width. The display takes no input or focus. To assistive technology
it is static text, read-only, named by its accessible name (or else by the
reading), with the reading as its value.

A theme styles the window as the ``plot`` part (the instruments' field,
drawn as a well), a lit bar as the ``trace`` part in the selected state and
an unlit one as the ``graticule`` part, both of the progress family:
``seven-segment-display::part(trace)`` and ``::part(graticule)``.

`seven_segment_pattern` gives the segments a character lights, as bits from
the top bar (bit 0) clockwise to the upper left (bit 5) and the middle bar
(bit 6); zero for a character no cell can form.
