A region of a window drawn by a sub-surface: content with its own control
tree on its own thread, composed into the window frame by frame.

The host occupies the region, owns the sub-surface's thread, sizes and
reconfigures it, and composes its `LiveSurface` into the window. To the
window it is one control: the sub-surface's frames never mark the window's
tree. A live `PlotView` draws this way. The content starts on its own thread
the first time the host is attached to a window with a non-empty size, and
stops when the host leaves the window: the thread releases the content and
is joined. A host attached again shows nothing until it is given content
again. Everything on the host happens on its window's UI thread.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
publishes_semantics
    Whether the host publishes the sub-surface in the window's
    accessibility tree, as its own pane with the sub-surface's nodes as its
    virtual children; on by default. A control that publishes what the
    sub-surface draws itself turns it off with `set_publishes_semantics`;
    the host then publishes no node and performs no accessibility action.
opaque
    Whether the content covers its whole region with opaque pixels, so the
    window copies its frames instead of blending them.
started
    Whether the sub-surface's thread exists: started and not stopped.
state
    The thread's state (`SubSurfaceState`); stopped while there is none.
failure
    What the content threw, or why it built no root, while the state is
    failed; empty otherwise.
statistics
    The sub-surface's counters (`SubSurfaceStatistics`).
dispatcher
    A `WindowDispatcher` that posts work to the sub-surface's thread; empty
    while it is not running.
content_epoch
    The content epoch of the newest configuration sent; zero before the
    first.
live_surface
    The `LiveSurface` the sub-surface publishes into; null before it
    started.
returned_keys_discarded
    The keys the sub-surface returned unhandled after the host had lost
    keyboard focus or left its window; they are not replayed in the window.

Events
------
stopped
    Raised once for each thread that ends, with a `SubSurfaceStopped`: by
    the host (new content, leaving the window) or because the content
    failed, with what it threw.
pairing_missed
    Raised once for each content epoch whose required frame was not
    published in time, so the window presented an older frame; it carries
    the epoch.

Methods
-------
set_content
    Sets the content to show (`SubSurfaceContent`) and whether it is
    opaque. A running sub-surface is stopped and joined first; null content
    stops it and leaves the region empty.
adopt_content
    As `set_content`, taking the content only once nothing before the take
    can fail: a throw before it leaves the caller holding the content.
redraw_all
    Sends a configuration under a new content epoch, carrying an update
    (`SubSurfaceContentUpdate`, or null for none), so that the
    sub-surface's first frame at that epoch shows it and the window composes
    no older frame once it pairs. Call it in the same turn as the change of
    the window's own controls it must agree with, such as a scroll or a
    column width. It returns the epoch the update rides, or 0 while there is
    no content, no window or no size yet; the update then waits for the
    first configuration, and new content discards it.

See Also
--------
LiveSurface : The surface the frames are published into.
PlotView : A plot that can draw on a sub-surface.

Notes
-----
The host is a tab stop. Pointer events that reach it, and key and text
events while it has keyboard focus, are forwarded in order to the
sub-surface's thread, which dispatches them to its own tree; the window's
accelerator pre-filter has already run. Keys the sub-surface leaves
unhandled come back later and continue in the window as if the host had not
handled them: they bubble from the host's parent, reach accelerators and
dialog keys, and Tab moves focus past the host. Keyboard focus inside the
sub-surface follows the host's focus and the window's activation.

The sub-surface shows nothing beyond its region, so it asks the host for
its tooltips. The host offers each request to the window's tooltip
presenter, which applies the window's policies and placement, and revokes
the pointer's requests when the pointer leaves it and the keyboard's when
its keyboard focus leaves. `request_statistics` counts the requests.

`stopped` and `pairing_missed` are raised by tasks posted to the window's
thread, never inside the present or the stop that found them, so a handler
may set new content or call `redraw_all`. A host destroyed before its task
runs raises nothing.

To assistive technology the host is a pane named by its accessible name
("Sub-surface" without one), whose default action focuses it. The
sub-surface's nodes appear as its virtual children: each stable identity is
prefixed with the host's, bounds are moved into the window, and actions are
posted to the sub-surface's thread, which refuses them if the control has
gone. Editable text there publishes its value and selection but no text
layout, so hosts offer no text ranges inside a sub-surface. The parts are
``content``, the composed frame, and ``focus``.
