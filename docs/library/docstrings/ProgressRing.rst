A circular progress indicator: a ring of forty dots lit in proportion to the
progress, or a turning run of dots while the amount is unknown.

Determinate, the dots from twelve o'clock clockwise are lit in proportion to
`value` over `maximum`, clamped to the ring. Indeterminate, a run of ten lit
dots turns once every 1.2 seconds instead. A theme, or `ring_style`, can draw
the ring as one continuous arc instead of dots. It takes no input and no
focus. Use a `ProgressBar` where a bar fits better, and a `Spinner` for
activity that never has an amount.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
value : default ``0``
    The progress, from 0 to `maximum`; a value outside that range is drawn
    clamped to it. A value that is not finite throws
    `std::invalid_argument`.
maximum : default ``100``
    The value of a full ring. A value that is not finite or not above zero
    throws `std::invalid_argument`.
indeterminate : default ``false``
    Whether the ring shows activity without an amount: a turning run instead
    of the value.
ring_style : default ``ProgressRingStyle::theme``
    How the ring is drawn: as the theme says (every built-in theme draws
    dots), as forty dots, or as one continuous arc.
fraction
    `value` over `maximum`, clamped to 0 to 1.
lit_dots
    The dots a determinate ring lights: `fraction` of `dot_count`, rounded
    to the nearest dot; a positive fraction lights at least one.
phase
    The first dot of the turning run; 0 while still or determinate.
moving
    Whether the run is turning now, on the window's motion clock.
draws_arc
    Whether the ring is drawn as a continuous arc: `ring_style` is ``arc``,
    or ``theme`` and the theme's rules say ``ring-style: arc`` for the
    control.

See Also
--------
Spinner : Activity without an amount.
ProgressBar : Progress as a bar.

Notes
-----
The turning run moves on the window's motion clock only while the ring is
indeterminate and drawn. With ambient motion suppressed (reduced motion,
ambient motion turned off, or an occluded window) the run stands still at
twelve o'clock. A determinate ring is off the clock. Drawn as an arc, the
value arc runs clockwise from twelve o'clock over exactly `fraction` of the
turn, and an indeterminate arc of a quarter turn moves with the same steps
as the dots.

To assistive technology the ring is a progress bar named by its accessible
name: determinate, with its value, a minimum of 0 and its maximum, and the
value as text; indeterminate, with no value and the busy state.

A theme styles the dots as the progress family's ``track`` and, lit, its
``fill`` (determinate) or ``activity`` (indeterminate) part in the selected
state, as `ProgressBar`'s track and fill; the custom state
``indeterminate`` selects the turning ring. Drawn as an arc, the parts are
``arc_track``, a full circle, and ``value_arc`` in the selected state.
