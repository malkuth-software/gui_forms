A paint-gated UI timer that drives one control's animation.

A control that animates owns an `AnimationTimer` as a member and starts it
with a tick target. The timer ticks only while the control is in a window and
shown, and no faster than the control is painted: a tick that finds the
control hidden, or not painted since the last tick that damaged it, stops the
timer, and the control's next paint starts it again. So an animation that is
off screen, covered or in a window that is not painting holds no wake. Each
tick damages what it changed with `Control::invalidate` and reports whether
it did.

Properties
----------
started
    Whether the timer is started and not stopped, ticking or waiting for a
    paint.
ticking
    Whether a tick is scheduled on the owner's window.
interval
    The interval given to the last `start`.

Methods
-------
start
    `start(Control& owner, FrameInterval interval, Tick tick)` starts, or
    restarts, ticking `tick` every `interval` for `owner`. The first tick
    falls on a multiple of the interval in `FrameClock` time, so that timers
    with the same interval tick in the same frame poll. When `owner` is not in
    a window or not shown, the timer waits for its next paint. An interval
    shorter than 1 ms, or an empty `tick`, throws `std::invalid_argument` and
    leaves the timer as it was.
stop
    Stops the timer. A stopped timer is not started by a paint.
note_painted
    Call it at the start of the owner's `on_paint`. It counts the paint, and
    starts a timer that was waiting for one.

See Also
--------
Timer : A component that ticks at a fixed interval whether or not anything is painted.
AnimationTimeline : Timeline sampling for animations.

Notes
-----
The tick target returns `AnimationTickResult::damaged` when it invalidated
what it changed, and `AnimationTickResult::unchanged` when nothing moved; an
unchanged tick expects no paint, so a calm or finished state stays scheduled.
The target is called on the window's UI thread and may call `start` or
`stop`.

Ticks keep their phase: when the UI thread is late by several intervals, the
timer ticks once and its next tick is the first deadline of its cadence that
lies in the future. Pace the interval to what the animation needs:
`animation_interval_for_travel` gives the interval at which a part moving at
a given speed in device pixels a second moves about one device pixel a tick,
never shorter than `display_frame_interval`.

The timer observes its owner and never keeps it alive. Destroying the timer
stops it.
