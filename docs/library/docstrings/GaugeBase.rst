The shared base of the read-only gauges: a value against a scale, with
units, a number of decimals and optional alarm limits.

`GaugeBase` adds to `RangeControl`'s value, minimum and maximum the scale
marks every `tick_frequency`, the value shown as text with its `units` and
`decimal_places`, and the alarm limits `low_alarm` and `high_alarm`. A gauge
whose value is below `low_alarm` or above `high_alarm` is in alarm: its
reading takes the invalid state, which every stock theme draws in its
warning colours, and assistive technology hears the alarm with the value.
`LinearGauge` and `RadialGauge` draw it; create one of them. For a value the
user sets, use a `Dial` or a `TrackBar`.

Properties
----------
tick_frequency : default ``10``
    The distance in value units between scale marks; zero draws none. A
    negative or non-finite value throws `std::invalid_argument`.
units : default empty
    The unit shown after the value, such as ``"kPa"``; empty for none. It is
    localizable. Invalid UTF-8 throws `std::invalid_argument`.
decimal_places : default ``0``
    The digits after the decimal point in the shown value, at most 6; more
    throws `std::invalid_argument`.
show_value : default ``true``
    Whether the value is shown as text beside the scale.
low_alarm : default none
    The value below which the gauge is in alarm, or none. A present value
    that is not finite throws `std::invalid_argument`.
high_alarm : default none
    The value above which the gauge is in alarm, or none. A present value
    that is not finite throws `std::invalid_argument`.
in_alarm
    Whether the value lies outside the alarm limits that are set.
value_text
    The value as shown: `decimal_places` digits after a point (always
    ``.``, whatever the locale), then the units after a space when there
    are any.
tick_ratios
    Where the scale marks lie, as ratios from 0 to 1: one every
    `tick_frequency` from the minimum, and the maximum; none when
    `tick_frequency` is zero. At most 101 marks: a finer frequency is drawn
    as 101 marks across the range.

Methods
-------
ratio_of
    Where a value lies in the range, from 0 at the minimum to 1 at the
    maximum, clamped; 0 for an empty range.

See Also
--------
LinearGauge : The value filling a straight bar.
RadialGauge : The value on a dial with a needle.
SevenSegmentDisplay : A reading without a scale.

Notes
-----
A gauge takes no focus, and a press on it does nothing; its tooltip still
shows. To assistive technology it is a meter (a level indicator, or a
progress bar on hosts without one), read-only, with its value, range and
value text; while in alarm the value text ends in ``(low alarm)`` or
``(high alarm)`` and the gauge is marked invalid.
