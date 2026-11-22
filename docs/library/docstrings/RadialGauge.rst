A dial gauge: a 270-degree scale arc lit from the minimum to the value, with
a needle at the value, scale marks, alarm arcs and the value as text.

The arc runs clockwise from the lower left (the minimum) over twelve
o'clock to the lower right (the maximum). Scale marks lie outside it every
`tick_frequency`; the alarm ranges (the minimum to `low_alarm`, and
`high_alarm` to the maximum) are alarm segments on the arc; and the value as
text sits below the centre while `show_value` is set. `GaugeBase` states
the value, alarm and accessibility rules. For a dial the user turns, use a
`Dial`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
parts
    The control's parts and where they lie: ``plot``, the round face;
    ``track``, the whole scale; ``alarm_arc``, the alarm ranges (absent
    without alarm limits); ``value_arc``, the reading (absent at the
    minimum); ``graticule``, the scale marks; ``needle``; and ``caption``,
    the value text (absent while `show_value` is clear). The arcs and the
    needle report the square their circle is inscribed in.

See Also
--------
LinearGauge : The same value filling a straight bar.
Dial : A knob the user turns.

Notes
-----
Every part but the caption is of the progress family, over the round
``plot`` face: ``track`` is an arc, ``alarm_arc`` arcs in the invalid state,
``value_arc`` an arc in the reading's states (selected, and invalid in
alarm), ``graticule`` the marks and ``needle`` the pointer; the caption is
panel text. The theme sets the arcs' thickness and the needle's width and
shape (``stroke-width``, ``needle-shape``).
