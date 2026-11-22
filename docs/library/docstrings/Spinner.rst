An indeterminate busy indicator: a ring of twelve dots, three of them lit,
turning while `active`.

A spinner shows that work is under way, not how much; a `ProgressRing` or a
`ProgressBar` shows an amount. An inactive spinner draws nothing. It takes
no input and no focus.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
active : default ``true``
    Whether the spinner shows and turns. An inactive spinner draws nothing
    and reports no busy state.
phase
    The first lit dot now, counted from twelve o'clock clockwise; 0 while
    still.
moving
    Whether the ring is turning now, on the window's motion clock.

See Also
--------
ProgressRing : Progress in a ring, with or without an amount.
SearchField : A search box with its own busy strip.

Notes
-----
The ring turns once every 1.2 seconds, a frame per dot step, only while the
spinner is active and drawn. With ambient motion suppressed (reduced motion,
ambient motion turned off, or an occluded window) the lit dots stand still
at twelve o'clock.

To assistive technology an active spinner is a busy progress bar without a
value, named by its accessible name; an inactive one is not exposed. A theme
styles the dots as the progress family's ``track`` and, lit, its
``activity`` part in the selected state.
