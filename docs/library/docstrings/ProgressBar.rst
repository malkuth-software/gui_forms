A bar that shows how far an operation has progressed.

The bar fills in proportion to its `value` between `minimum` and `maximum`,
which it inherits from `RangeControl` (0 to 100 by default). Update it with
`set_value` or `increment`, or bind it to a `Value<double>` with `bind`. When
the amount of work is unknown, choose `ProgressBarVisualStyle::marquee`, which
sweeps a band across the bar again and again regardless of the value. The bar
is display only: it takes no focus, keyboard or pointer input. Use a
`TrackBar` for a value the user adjusts.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
visual_style : default ``ProgressBarVisualStyle::continuous``
    How the bar is drawn: `blocks` (separate segments), `continuous`,
    `marquee` (a moving band for unknown progress), `pulse` (a moving
    highlight over the fill), `marching_stripes` or `laser_etch`. All but
    `blocks` and `continuous` are animated.
overlay_style : default ``ProgressBarOverlayStyle::none``
    `moving_stripes` adds animated diagonal stripes over the fill of any
    style except `marching_stripes`.
stripe_width : default ``7.0``
    Width of the stripes, between 2 and 32; other values throw
    `std::invalid_argument`.
animation_appearance : default ``ProgressBarAnimationAppearance{}``
    Colours and extents of the pulse, stripe and laser effects. Out-of-range
    extents throw `std::invalid_argument`.
animation_enabled : default ``true``
    Whether animated styles move. When false they are drawn still and the
    bar schedules no frames.
motion_policy : default ``MotionPolicy{}``
    The bar's own motion policy, a capability: `enabled`, `paused` and
    `reduced`. Paused stops the animation and keeps its current phase, so it
    resumes where it stopped; a policy that is not enabled stops it the same
    way. Reduced asks for gentler animation: at most ten frames a second,
    about a third of the speed and half the travel. The bar also reduces
    motion when the window's presentation settings ask for it. To change one
    flag, read the policy, change it and assign it back.
effective_motion_policy
    `motion_policy` combined with the window's reduced-motion setting; this
    is what the bar animates with.
animation_period : default ``std::chrono::milliseconds(1400)``
    Time for one animation cycle. Periods under 100 ms throw
    `std::invalid_argument`.
animation_phase
    Position in the current animation cycle, from 0 up to 1.

See Also
--------
RangeControl : Minimum, maximum, value and `bind`, shared with `TrackBar`.
TrackBar : A slider the user drags to choose a value.
Value : Application state that a progress bar can follow.

Notes
-----
`set_value` throws `std::out_of_range` for a value outside the range, while
`increment` clamps to it. Changing the range clamps the value and raises
`range_changed` and then, if the value moved, `value_changed`. The inherited
`scroll` event is never raised, because the user cannot move the bar.

A vertical bar, set with `set_orientation`, fills from the bottom. Without a
requested size the bar prefers 160 by 20 horizontally and 20 by 160
vertically.

An animated style runs an `AnimationTimer`, only while it is in a window,
shown, enabled for animation and not paused; a static style schedules nothing.
The timer is paced to the motion: about one device pixel of travel of the
fastest moving part a tick, at most one tick a display frame (a sweeping band
ticks every frame; marching stripes about 20 times a second at the default
period). Each tick repaints only what moved: the activity band, the pulse band
or the fill. A bar that is hidden, or not painted since the last tick, stops
its timer at that tick and starts it again when it is next painted. The bar paints from the theme's progress
colours; the inherited `RangeControl` `style` does not change its look.

The control publishes the accessibility role `progress_bar`, with the value
as text and as a number together with the minimum and maximum. It reports busy
while it is animating. Give it a name by writing `accessible_name` that says
what is in progress.

Examples
--------
Follow application state. The bar takes the value's state when the binding
starts, and then tracks every change:

.. code-block:: cpp

   gf::Value<double> copied(0.0);
   std::shared_ptr<gf::ProgressBar> bar =
       gf::make_control<gf::ProgressBar>(gf::StableId("copy.progress"));
   (*bar).accessible_name = "Copying files";
   (*bar).set_range(0.0, 250.0);   // 250 files
   (*bar).bind(copied);
   copied.set(125.0);              // the bar is now half full

Show activity of unknown length, and pause it while the work waits:

.. code-block:: cpp

   std::shared_ptr<gf::ProgressBar> busy =
       gf::make_control<gf::ProgressBar>(gf::StableId("sync.busy"));
   (*busy).accessible_name = "Synchronizing";
   (*busy).visual_style = gf::ProgressBarVisualStyle::marquee;
   gf::Window window(busy, gf::Size{160.0, 20.0});   // the band now animates
   gf::MotionPolicy policy = (*busy).motion_policy.read();
   policy.paused = true;
   (*busy).motion_policy = policy;                   // it stops, keeping its phase
