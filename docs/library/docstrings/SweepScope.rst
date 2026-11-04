A read-only scope that draws a level as a trace on a graticule.

The scope shows its `value` between `minimum` and `maximum`, which it
inherits from `RangeControl` (0 to 100 by default): a trace rises from the
lower left of the plot to a peak above the level's place and falls to the
middle of the right edge, while a sweep line crosses the plot once each
`sweep_period`. A caption above the plot reads the caption and the level's
percentage, such as "LEVEL 42%". Update the level with `set_value`, or bind
it to a `Value<double>` with `bind`. The scope is display only: it takes no
focus, keyboard or pointer input. It belongs to the instruments package.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
caption : default ``"LEVEL"``
    The words drawn before the level's percentage. An unnamed scope is
    announced by its caption.
sweep_period : default ``std::chrono::microseconds(1'234'568)``
    The time the sweep takes to cross the plot once, from 0.1 to 60 seconds;
    other periods throw `std::out_of_range`. As a property it is a number of
    seconds.
effect_instance
    The sweep's effect instance: its counters and state checksum, for
    diagnostics and tests.

See Also
--------
RangeControl : Minimum, maximum, value and `bind`, shared with `ProgressBar`.
ProgressBar : A bar that shows how far an operation has progressed.
EffectRegistry : The registry the scope's effect, `scope_sweep`, is listed in.

Notes
-----
The trace and the sweep are one effect program, `scope_sweep`, bounded and
verified by the gui_forms plugin's effect pass and listed in the built-in
effect registry with the scope as its parent. The scope draws its region,
plot, graticule and caption by part role through the theme; the trace takes
the theme's teal and amber accents and the sweep the teal at a little over
half opacity.

The sweep runs on the window's motion clock at 30 frames a second, only
while the scope is in a window and drawn: a scope not painted between two
frames is dropped from the clock and enlisted again by its next paint, and
each frame repaints only the plot. Under reduced motion, or with the
window's ambient motion switched off, the sweep rests in the middle of the
plot and the scope schedules no frames.

Without a requested size the scope prefers 220 by 120. The caption takes an
18-unit band across the top; the plot fills the rest, inset 8 units from the
sides and the bottom, with four graticule lines across it.
