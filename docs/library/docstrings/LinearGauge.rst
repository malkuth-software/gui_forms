A bar gauge: the value filling a straight scale from the minimum, with
scale marks, alarm segments and the value as text.

The bar runs along the gauge's `orientation`: left to right, or bottom to
top. Scale marks lie beside it every `tick_frequency`; the alarm ranges (the
minimum to `low_alarm`, and `high_alarm` to the maximum) are alarm segments
of the field; and the value as text sits at the bar's end (to the right of a
horizontal bar, below a vertical one) while `show_value` is set. A
horizontal gauge in a right-to-left layout is mirrored: the minimum at the
right and the value text at the left. `GaugeBase` states the value, alarm
and accessibility rules. Use a `ProgressBar` for the progress of an
operation.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
parts
    The control's parts and where they lie: ``plot``, the bar's field;
    ``alarm_zone``, the alarm segments (absent without alarm limits inside
    the range); ``trace``, the filled reading; ``graticule``, the band of
    scale marks; and ``caption``, the value text (absent while `show_value`
    is clear).

See Also
--------
RadialGauge : The same value on a dial with a needle.
ProgressBar : The progress of an operation.

Notes
-----
A theme styles ``plot`` (drawn as a well), ``alarm_zone`` (a track in the
invalid state, over the field and under the reading), ``trace`` (the
reading in the selected state, invalid while in alarm) and ``graticule``,
all of the progress family, and ``caption`` as panel text. The alarm
segments are the straight counterpart of `RadialGauge`'s alarm arc.
