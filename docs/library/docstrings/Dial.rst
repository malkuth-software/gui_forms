A rotary knob: a value in a range, set by turning.

The scale runs 270 degrees clockwise from the lower left (the minimum) over
twelve o'clock to the lower right (the maximum). The knob's pointer faces
the value, and with `show_arc` the scale lights from the minimum to the
value. Its input follows `TrackBar`'s, so a dial and a track bar can be
swapped for the same setting. For a value that is only shown, use a
`RadialGauge`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
tick_frequency : default ``10``
    The distance in value units between scale marks; zero draws none. A
    negative or non-finite value throws `std::invalid_argument`.
show_arc : default ``true``
    Whether the scale lights from the minimum to the value.
knob_bounds
    The knob's square in local coordinates.
pointer_angle
    The pointer's angle in radians, clockwise from twelve o'clock: minus
    three quarters of pi at the minimum, plus three quarters of pi at the
    maximum.
parts
    The control's parts and where they lie: ``track``, the scale;
    ``value_arc``, the lit scale (absent without `show_arc` or at the
    minimum); ``graticule``, the scale marks; ``thumb``, the knob;
    ``needle``, the square inside the knob the needle turns in; and
    ``focus``, the focus cue. The arcs report the square their circle is
    inscribed in.

See Also
--------
TrackBar : The same setting on a straight track.
RadialGauge : A value shown on a dial, not set.

Notes
-----
A primary press turns the knob to face the pointer, and a drag keeps it
turning, clamped to the scale: a drag through the gap at the bottom stays at
the end it came from. A press whose capture is taken away stops turning.
The wheel and the arrow keys move the value by `small_change`, Page Up and
Page Down by `large_change`, and Home and End to the ends. Every change
raises `scroll` and `value_changed` with a `RangeAction`, as `TrackBar`
does.

To assistive technology the dial is a slider with increment, decrement and
set-value actions. A theme styles ``track`` (an arc of the progress family),
``value_arc`` (an arc in the selected state), ``graticule``, the knob
``thumb`` (round, of the button family) with its ``needle``, and the focus
cue on the knob; it sets the arcs' thickness and the needle's width and
shape (``stroke-width``, ``needle-shape``).
