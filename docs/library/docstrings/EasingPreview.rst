A diagnostic view that animates markers along tracks to compare easing curves.

An easing preview runs one looping `AnimationTimeline` and draws a row per
track: a label and a marker that travels across the row and back, shaped by
the track's `EasingCurve`. It shows how linear, ease-in, ease-out, back-out,
bounce and elastic motion feel at a given duration, and how a `MotionPolicy`
or the system's reduced-motion setting calms them. It is a review instrument
for motion design, not an input control: it ignores the pointer and takes no
focus.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
title : default ``"Animation timeline and easing"``
    Heading drawn at the top left.
style
    The `BasicControlStyle` colours used for the paper, borders, labels and
    track rails.
specification
    The `AnimationSpec` of the timeline. The constructor sets a 2400 ms,
    infinitely repeating, linear timeline. Setting it restarts the timeline
    from the beginning, even with an equal value. The easing of the
    specification itself does not move the markers; each track's curve does.
tracks
    The rows, each an `EasingPreviewTrack` with a curve, a label and a marker
    colour. By default there are eight, one per `EasingCurve`. There must be
    1 to 32 tracks with nonempty labels, or `std::invalid_argument` is
    thrown.
motion_policy : default ``MotionPolicy{}``
    The authored policy. While it is `active()`, that is enabled and not
    paused, the preview runs its animation timer; otherwise it freezes at its current
    phase. With `reduced`, frames come every 100 ms instead of every 16 ms
    and the markers travel half as far.
effective_motion_policy
    `motion_policy`, with `reduced` also set when the window's presentation
    settings ask for reduced motion. This is the policy the preview obeys.
marker_size : default ``14.0``
    Side of each marker square in layout units. It must be finite and from 4
    to 48, or `std::invalid_argument` is thrown.
phase : default ``0.0``
    Progress through the current iteration of the timeline, from 0 to 1. The
    markers travel out during the first half and back during the second.

Methods
-------
on_frame
    Called on each tick of the preview's animation timer; it samples the
    timeline into `phase`.

See Also
--------
AnimationTimeline : The timeline the preview samples.
MetricsView : Shows the window's runtime counters.
TypographySpecimen : Shows how a text run resolves and sits on its baseline.

Notes
-----
The timeline starts when the preview is first attached to a window. While
detached it pauses at its phase and resumes on reattachment. While the
policy is active and the preview is shown, it runs an `AnimationTimer` at
the display rate, so it keeps a resting window awake; pause it with `motion_policy` when
it is not being watched. Each tick repaints only what moved: each marker's old
and new place and the readout. A preview that is hidden, or not painted since
the last tick, stops its timer at that tick and starts it again when it is
next painted. With a finite `specification`, it stops its timer when the
timeline finishes.

A readout at the top right shows the policy, `LIVE`, `PAUSED`, `REDUCED`,
`REDUCED + PAUSED` or `DISABLED`, and the presented phase as a percentage.

The preview publishes the accessibility role `image`, with the readout as
its value, the presented phase as a numeric value from 0 to 1, and the
`busy` state while it is animating. Its accessible name is "Animation easing
preview" until changed.

Examples
--------
Compare two curves at a 600 ms duration, then pause the preview:

.. code-block:: cpp

   std::shared_ptr<gf::EasingPreview> preview =
       gf::make_control<gf::EasingPreview>(gf::StableId("motion.preview"));
   (*preview).set_requested_bounds({0.0, 0.0, 480.0, 160.0});
   std::vector<gf::EasingPreviewTrack> tracks;
   tracks.push_back({gf::EasingCurve::ease_out, "Panel slide", gf::Color::rgba(38, 114, 185)});
   tracks.push_back({gf::EasingCurve::back_out, "Badge pop", gf::Color::rgba(211, 121, 42)});
   (*preview).set_tracks(tracks);
   gf::AnimationSpec specification;
   specification.duration = std::chrono::milliseconds(600);
   specification.infinite = true;
   (*preview).set_specification(specification);
   gf::Window window(preview, gf::Size{480.0, 160.0});   // starts the timeline

   gf::MotionPolicy paused;
   paused.paused = true;
   (*preview).set_motion_policy(paused);   // stops its animation timer
