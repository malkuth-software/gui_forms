# Painting and damage

GUI.Forms paints on the CPU in response to damage or explicit frame requests.
There is no GPU path and no perpetual redraw loop. This page explains how a
mutation becomes a damaged rectangle, how a frame is painted and presented
coherently, and how to read the counters that prove it.

## From mutation to damage

A setter marks `Dirty::paint` on its control. When the outermost update scope
closes, the flush converts each paint mark into a damage rectangle in window
coordinates: the control's arranged bounds plus its `visual_outsets` (focus
rings and shadows that extend beyond layout without affecting it), plus the
outsets it had when last painted, clipped to each ancestor's client area.
Rectangles accumulate in a `DamageRegion` per paint plane. The region
merges rectangles when their union fills its bounding box, and collapses to a
bounding box when its fixed capacity is exceeded; `damage_region_compactions` and
`damage_region_collapses` count both events, and `maximum_damage_rectangles`
records the peak count held.

Paint planes let a control choose where it paints: the ordinary content
plane, or the overlay paint plane (`PaintPlane::overlay`), which overlays,
tooltips and drag images use. Damage is tracked separately and can be consumed
with `take_damage(plane)`, but this does not guarantee separate native surfaces
or eliminate repainting beneath an overlay.

`Window::paint` takes either one rectangle or a set of rectangles. Given a
set, one paint pass paints each rectangle under its own clip, so pixels
between distant changes are left alone; rectangles that overlap or lie
within two logical units of each other are merged first, and an empty set
paints the accumulated damage rectangles. Given one rectangle, or none, it
paints that rectangle or the bounding rectangle of accumulated damage, as
before. The Windows host invalidates each damage rectangle separately, reads
the update region rectangle by rectangle, paints that set and copies each
rectangle to the window. The macOS and X11 hosts paint their pending damage
rectangles the same way; macOS also clips its raster to them.

## Display chunks

Each control's paint output is a retained display chunk: a recorded list of
`Painter` commands with their resolved geometry, text runs and rasterized
glyphs. Painting replays valid chunks. For controls intersecting the paint
bounds, it rebuilds a chunk when paint is dirty, the chunk is missing, or its
paint plane or local bounds changed; rebuilding calls `on_paint`.
`display_chunks_rebuilt`, `display_chunks_reused` and
`display_commands_replayed` show the split; a scroll that rebuilds every chunk
is invalidating text or style when it only moved geometry.

Glyph bitmaps live with the chunk that drew them and are released with it.
There is no global glyph cache and no eviction policy; memory follows the
retained tree.

## The Painter

Controls draw through the renderer-neutral `Painter` vocabulary: fills,
strokes, paths, rounded rectangles, images by `ImageId`, text runs by font
role, clips and transforms. The `Painter` is implemented by the private
Skia-based CPU adapter on macOS and Linux and by the DIB compositor on
Windows. No Skia type is visible to a control, and the renderer can be
replaced without touching one.

Layout gives controls unsnapped logical boxes, identical at every display
scale. The painters snap at paint time: a filled rectangle, a stroked
rectangle, a gradient fill and the bounds of a rounded rectangle, filled or
stroked, have each edge rounded to the nearest device pixel, halves upward,
so a rounded border sits exactly on its fill. The extent is not
rounded. Two boxes that share a logical edge therefore share a device edge,
with no seam between them and no pixel covered twice, at 1.25 or 1.5 as
at 1 or 2. Rounded corners, paths, lines, text and images keep their
antialiasing. Under a rotated or skewed transform nothing snaps. A box
narrower than a device pixel can round to nothing.

PNG is the supported encoded image format. The resource registry validates
the PNG structure and retains its encoded bytes; the renderer decodes and
caches the pixels. Controls refer to resources by `ImageId`. The registry
also accepts raw pixels through `load_bgra32_premultiplied`.

## Coherent frames

A paint pass holds one exclusive UI-thread paint lease. Within the pass,
owner callbacks rebuild chunks into a transaction recorder; no command
reaches the host painter until every callback has succeeded. If a callback
throws, the pass restores the previous chunks, abandons the candidate frame
and republishes the damage so a later pass can retry. A mutation made during
paint increments `content_revision` and survives as one later pass rather
than reentering the current one.

`Window::paint(painter, requested_damage)` returns `std::optional<PaintReceipt>`:
there is no receipt when no frame is produced. A receipt is returned only after
the backend has replayed every command and the window has re-validated the
surface epoch. The host presents that exact receipt and then calls
`notify_presented(receipt, duration)`. Duplicate, backward, forged-future and
stale-epoch receipts are rejected and counted. A resize or scale change
increments the surface epoch, so a frame rendered for the old size can never
be presented into the new surface.

`PaintLeaseSnapshot` exposes `content_revision`, `rendered_revision`,
`presented_revision`, `surface_epoch`, the lease state (`dirty_queued`,
`rendering`, `rendering_dirty`, `ready`) and the number of requests queued or
merged. Intermediate revisions are never stored as render jobs; a burst of
mutations produces one frame.

## Input during paint

Pointer, key, text, drag and semantic events that arrive while a paint lease
is held are queued, not dispatched. Consecutive moves from one pointer
compact to the latest; downs, ups, wheels, keys and text keep causal order.
The queue holds 1,024 entries; overflow is rejected and reported. One drain
runs after the outermost lease releases. `DeferredInputSnapshot` reports
pending, delivered, compacted and rejected counts.

## Waking the host

Model-originated damage does not paint by itself; it arms one wake through
`set_paint_wake_handler`. The host consumes all damage, presents, and the wake
is rearmed for the next independent mutation. Occlusion suppresses the wake
without discarding dirtiness, and exposure requests one wake. This is why a
second window controlled by the first can mutate it and have it present
without any input passing through the changed window.

Animations request frames explicitly (ADR-016). A control that animates owns
an `AnimationTimer`: each tick damages what it changed, and the timer waits
for the next paint before it ticks again, so a hidden control or a covered
window holds no wake. Work that is not drawing, such as a press-and-hold
repeat, runs on `Window::schedule_ui_timer`, which the control stops itself.
`schedule_paint` requests one later repaint. Deadlines coalesce, callback
faults are isolated per request, and nothing repaints while the window is
idle: `Window::next_wake` is empty in a resting window.

## Live surfaces

A live surface (`LiveSurfaceDescription`, `LiveSurfaceFrame`) lets a producer (an audio visualizer, a video decoder, a
game board) write pixels directly into a pooled frame and have the window
composite it. Frames are premultiplied BGRA8, the format of every raster; the surface can promise
`opaque`, which lets the presenter copy instead of blend and skip painting
what lies beneath. On macOS a live-only redraw can omit retained painting
entirely when the existing raster is valid and opaque clips cover every
damaged device pixel. Presents copy that format to the screen as it is.

## Sub-surfaces

A `SubSurfaceHost` puts a control subtree on a thread of its own. The host
hands its `SubSurfaceContent` to a `SubSurfaceThread`, which builds the tree
there in a `SubSurface` (a surface root like the window, with its own dirty
structure, layout, paint and dispatcher) and publishes each paint into a
live surface the window composes over the host's region. The window's tree
sees only the host: the sub-surface's paints never damage it.

The sub-surface is sized in device pixels (the region's size times the
window scale, rounded outward), so the window copies its frames and never
scales them. It produces nothing before the first size, and each change of
device size or scale opens a new content epoch: the host sends a new
`SubSurfaceConfiguration`, asks the window to pair the region with a frame of
that epoch, and schedules a present itself. Configurations coalesce (the
newest wins). Work for the sub-surface is posted through
`SubSurfaceHost::dispatcher`; its controls see no `Window`. When the host
leaves its window the thread is stopped and joined, and the tree is released
on its own thread.

A sub-surface shows the window's environment: the application theme, the
presentation settings (with reduced motion) and a layered user theme. Its
themed parts, motion values and ambient effects run on its own thread, and
ambient frames tick on the display time its pacer names. An effect reading
the backdrop samples the sub-surface's own backplane, never the window's.

### Pacing producers

A producer paces itself with a `LiveSurfacePacer`, never with a sleep loop.
After each present tick the window's host reports, per surface, the
generation it showed and the display timing of the tick
(`LiveSurface::notify_presented`, a `LiveDisplayTiming`). The pacer lets a
producer render its next frame once its last one was shown (or, if it
published nothing, once a tick passed), and names the display time that
frame will be shown for. If the window shows nothing for one and a half
refreshes (a busy UI thread) the pacer releases the frame anyway and counts
the refreshes missed, so a stalled window never stops a producer. A
sub-surface paces every frame this way; the Windows host reports the
compositor's vertical blank, or its 60 Hz cadence without one.

### Sub-surface input

A `SubSurfaceHost` is a tab stop. The pointer events that reach it and the
key and text events it receives while focused are forwarded, in order and
with growing sequence numbers, to the sub-surface's thread, which
dispatches them to its own tree; pointer positions arrive in the
sub-surface's coordinates. The window's accelerator pre-filter runs before
anything is forwarded. A key the sub-surface leaves unhandled comes back to
the window asynchronously and continues there as though the host had not
handled it: bubble from the host's parent, accelerators, dialog keys, and
Tab. The sub-surface's own Tab traversal does not wrap, so Tab at its last
stop comes back and moves focus past the host; Tab onto the host enters at
the first stop, Shift+Tab at the last, and a click or programmatic focus
restores the control focused before. The sub-surface is active only while
the host has focus in an active window. `SubSurfaceStatistics` counts the
inputs received and dispatched and the keys returned.

A secondary or middle press on the host holds the window's pointer capture
until its release, as the window already does for the primary button, so a
drag that leaves the region still reaches the sub-surface. For an input
method, `SurfaceRoot::text_input_caret` gives the caret box in window
coordinates: the focused control's `Control::text_input_caret`, or a narrow
box at its left edge. A sub-surface publishes its own caret at the end of
each turn and the host reports it at its offset, so the input method never
waits on the sub-surface's thread.

### Requests: tooltips beyond a control

A control that wants something shown beyond itself asks its root:
`SurfaceRoot::request_tool_tip(owner, ToolTipRequest, answer)` carries the
content as data (text, an optional semantic description, an anchor in the
root's coordinates, a duration) and returns an ID; the answers
(`SurfaceRequestOutcome`: shown, declined, closed, revoked or stale) are
posted to the `SurfaceRequestAnswer` target through the root's dispatcher,
and `withdraw_tool_tip_request` takes the request back. One request is
current per root; a newer one replaces it. The control never needs to know
which kind of root it lives in: a window's tooltip presenter shows the
request at once, as `show_tool_tip` does, and a sub-surface forwards it to
its parent, which decides.

Alerts take the same path. `SurfaceRoot::raise_alert(owner, AlertRequest,
answer)` carries a message, a caption, the buttons, an icon and the default
button, and returns at once; a window shows its alerts one at a time, in the
order raised, through the host's message dialog, and a sub-surface forwards
each to its parent, which raises it on the host. The one answer is closed
with the chosen button (`SurfaceRequestOutcome::choice`), declined when
there is no host or the host refuses, or revoked when `withdraw_alert` took
it back, or its owner left the root, before it showed.

### Sub-surface tooltips

A sub-surface borrows its region from its parent and never shows anything
beyond it itself: no tooltip bubble and no native popup (native popups are
opened from a window). Instead it asks its parent. When the thing under the
pointer, under keyboard focus, or named by `SurfaceRoot::show_tool_tip` or
`request_tool_tip` changes, the sub-surface's thread posts a
`SubSurfaceRequest` (the text, the thing's semantic description, and its
anchor in the sub-surface's coordinates) and revokes its request for the
previous thing; it does no timing of its own. The host receives the request
on the window's thread. It drops a request measured in an older content
epoch, or made before the pointer or focus left the host, as stale (the
sub-surface asks again once it has the new configuration); declines one
whose origin the window's tooltip switches turn off; and otherwise offers it
to the window's tooltip presenter as one of its tooltip parts
(`Control::tool_tip_items`), anchored at the host's origin. The presenter
applies its delays, reshow, auto-pop and placement as for any other tooltip;
a program request shows at once. The answer goes back asynchronously to the
sub-surface's thread, and an answer to `request_tool_tip` on to its target
there. The host
revokes the pointer's requests when the pointer leaves it and the keyboard's
when its focus leaves, and drops every request when its thread stops.
`SubSurfaceHost::request_statistics` and `SubSurfaceStatistics` count both
sides.

Overlays opened with `SurfaceRoot::open_overlay` inside a sub-surface stay
within its region.

### Sub-surface accessibility

Assistive technology sees one tree per window. At the end of each turn in
which its tree's semantic generation moved, a sub-surface publishes an
immutable `SemanticSnapshot` and tells the host through the window's
dispatcher; the host publishes that snapshot's nodes as its own virtual
children, so the Windows, macOS and Linux adapters read them from the
window's snapshot like any other node and never call into the sub-surface's
thread. Each node is translated into the window: its stable identity is
prefixed with the host's (`<host>.<node>`), its bounds move by the host's
position, its runtime identity is derived from the host and the node's own
runtime identity (so a replaced control, or a restarted sub-surface, gives
new nodes), and it is focused only while the sub-surface has keyboard
focus. An action on one of those nodes is posted to the sub-surface's thread
with the node's own runtime identity and performed or refused there
(`SubSurfaceStatistics` counts the snapshots and the actions); a focus
action also focuses the host. Editable text inside a sub-surface publishes
its value and selection but no text layout, so hosts offer no text ranges
there.

## Reading the counters

`MetricsSnapshot` carries `paint_passes`, `controls_painted`,
`paint_invalidations_consumed`, `requested_damage_area`,
`painted_damage_area`, `paint_bounding_area`, `damage_rectangles_painted`,
`full_window_paints`, `partial_paints`,
`frames_presented`, `present_duration_nanoseconds`,
`worst_present_duration_nanoseconds`, `renderer_name` and `cpu_only`. A
healthy interaction shows `partial_paints` climbing while
`full_window_paints` stays at the resize count, and `painted_damage_area`
close to `requested_damage_area`. A sparse frame shows `painted_damage_area`
well below `paint_bounding_area`, the summed bounding box of each pass's
painted rectangles. `cpu_only` is always true; the field exists so that a
snapshot states it.

What reaches the screen is counted apart, in device pixels (four bytes each
in today's rasters). `retained_pixels_presented` and
`retained_rectangles_presented` count what each retained present copied;
`live_frames_presented`, `live_pixels_presented`,
`live_rectangles_presented` and `live_present_duration_nanoseconds` count
live-surface frames: one per surface per new frame, with the fragments its
clip is cut into around overlays as its rectangles. A surface the window
pairs with content (`Window::require_live_surface_content`) counts
`live_pairing_waits` for each present tick it kept its previous frame while
its newest one was older than that content, and
`live_unpaired_presentations` for each such frame presented anyway at the
next tick. When one present copies both, the duration is split by the pixels each copied. The present durations
cover painting and composing as well as the copy; `screen_copy_nanoseconds`
counts only the copy to the screen (pixel conversion and the native copy),
retained and live together, so the copy itself compares across hosts. Divide by `frames_presented` or `live_frames_presented`
for the copy per frame. A host that presents its rectangles separately shows
`retained_rectangles_presented` above `frames_presented`; one that copies a
bounding box shows them equal. Both Windows profiles copy each damage
rectangle (the transactional one still swaps its whole candidate only after
every rectangle is copied); the X11 host copies each damage rectangle; the
macOS host copies the rectangles AppKit draws, with live frames counted inside
the same copy. AppKit merges the macOS host's display requests into their
union, so its count shows one rectangle where the host painted several; the
pixels outside the painted set are copied unchanged. A macOS draw with
nothing to paint and no live frame (a window shown again, activated or
restored, or an exposure while hidden) copies the raster as it stands and
reports nothing: it adds no present, no pixels and no
`screen_copy_nanoseconds`. Every host composes a new opaque live frame the same way: the
frame's changed region is drawn straight into the raster, and retained
painting covers only real damage, such as the share of an overlay above the
change, in the same frame. On X11 a frame that is not opaque is composed by
retained painting over what lies beneath it.

`Window::presentation_availability()` reports what the attached host offers:
the paint-contract version it meets (`paint_contract_version`, now 2; a host
that cannot meet it fails to attach), the optional extensions, and the live
paint lease. Since version 2 every host presents the rectangle set it paints,
never their union; `partial_present` is reported true for readers of
version 1. `to_json()` serializes it, and the Windows host includes it in its
host diagnostics.
