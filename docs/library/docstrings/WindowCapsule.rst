A window's controls as one capsule in three sections, worked by gestures:
maximise and minimise on the left, help in the middle, move on the right,
and a close that takes three clicks.

A program without a title bar places a capsule in a corner of its own
content. The capsule then runs the gesture on its own pointer and key input
and raises `requested` for each command; the program carries the command
out on its window (`ApplicationWindowHandle`). A window manager that draws
title bars can show a capsule that is not `interactive`, run the gesture
itself (`WindowCapsuleGesture`) and give the capsule its `look`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
look
    The `WindowCapsuleLook` shown: the arming, the close intention, the
    minimise drag and the hovered, pressed and focused sections.
    `set_look` changes it; an interactive capsule's own gesture replaces it
    at its next input. The capsule paints again only when it changed.
colors
    The `WindowCapsuleColors` its fills and glyphs are drawn in, over its
    theme parts: by default a dark face, light glyphs, a green invitation
    and a red X. `set_colors` changes them.
help_width
    The question mark's width, at least the capsule's height; 20 by
    default. `set_help_width` changes it.
interactive
    Whether the capsule takes the pointer and, when focused, the keyboard,
    and raises `requested`; on by default. Not interactive, it only shows
    the look it is given and passes the pointer through. `set_interactive`
    changes it.

Events
------
requested
    Raised for each command of an interactive capsule's gesture, with a
    `WindowCapsuleRequest`: maximise or restore, minimise, a move begun,
    continued, finished (with its snap) or cancelled, a tile, a nudge, the
    help, or close. Points are in the capsule's client coordinates; a
    program moving its window reads the screen pointer and asks for the
    frame, and chooses any snap itself.

Methods
-------
set_measures
    Sets the gesture's measures (`WindowCapsuleMeasures`): the click slop,
    the minimise distance, the snap edge and release, the animation time and
    the keyboard nudge. Non-finite or negative values fall back to the
    defaults.
section_at
    The section at a client point; none outside the capsule.
section_bounds
    A section's rectangle in client coordinates.

See Also
--------
BusinessCard : The program's card the help can show.

Notes
-----
On the left half, a double click maximises or restores, and a drag down of
at least the minimise distance, released, minimises. A click on the
question mark opens the help (`window_capsule_help_text` is the capsule's
own); hovering it shows the window's title. Dragged past the slop, the right
half moves the window live. Closing takes three clicks: a click on one half
arms it (that half turns into half an X and the other lights green), a click
on the other half completes the X (the capsule turns red), and a click on
the X closes the window. Leaving the capsule, Escape, or the window losing
the capsule disarms it.

Focused, Left and Right choose a section and Enter or Space click it
(arming moves the focus to the other half, so Enter three times closes); Up
maximises or restores, Down minimises, Shift with Left or Right tiles to a
half, Shift with Down floats, Ctrl with an arrow moves the window by the
nudge, F1 opens the help, and Escape disarms, then leaves the capsule. Any
other key leaves the capsule and goes to the window. The arming colour and
the minimise drag's spring-back animate; with reduced motion they take their
targets at once. A press whose capture is taken away cancels the gesture.

To assistive technology the capsule is a group named "Window controls" (or
its accessible name) whose sections are buttons: "Maximise or minimise",
"Help" and "Move", and "Close" while the X shows. Its parts are ``face``,
``left_half``, ``help``, ``right_half``, ``glyph`` (the halves' marks and
the X), ``label`` (the question mark) and ``focus``.
