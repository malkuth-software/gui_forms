A region whose pixels a consumer renders itself, on its own thread, with its own engine and fonts.

Interface text uses GUI.Forms' bundled families only; content may use any
font, rendered by the consumer into its own surface. A content surface is
that surface: the consumer publishes finished frames through the
`ContentFrameSink` that `open_content` returns, from any thread, and the
window composes the newest finished frame without waiting for the next.

It is an integrity boundary, not a security boundary. Content that stalls,
fails or publishes nonsense cannot stop or corrupt the interface: every frame
is validated before a byte is read, a refused frame leaves the last good one
on screen, and a watchdog marks content that falls silent as stale. It does
not govern what the consumer's own code does in the process.

Properties
----------
status
    Where the content is in its life (`ContentSurfaceStatus`):
    `disconnected` without content, `waiting` before the first frame, `live`,
    `stale` while the watchdog sees no frame and no heartbeat, `terminating`,
    `ended`, `abandoned` (it did not stop within the bounded wait) and
    `failed`.
stale_interval
    How long content may publish nothing before it is stale; one second by
    default, zero disables the watchdog. Setting it restarts the interval.
content_semantics
    The text published for the surface: its accessible name, the text it
    shows (the node's value) and a description. The setter works on the UI
    thread and throws `std::invalid_argument` for text that is not valid
    UTF-8 or is longer than the limit; the consumer's threads publish it
    through the sink.
live_surface
    The `LiveSurface` the frames go into; null without content.
statistics
    The channel's counters: frames published, refused and dropped,
    heartbeats, stale transitions and recoveries, semantic publications and
    refusals, and the last refusal.
request
    The device size and scale the surface shows at, as the content sees it
    through `ContentFrameSink::request`.

Events
------
status_changed
    Raised on the UI thread with the new status.

Methods
-------
open_content
    `open_content(opaque)` opens a new channel and returns its sink, revoking
    any earlier one. The status becomes `waiting`.
close_content
    Revokes the channel and clears the region.
check_watchdog
    `check_watchdog(now)` runs one watchdog check, as the surface's own timer
    does.
set_status_caption
    `set_status_caption(status, caption)` sets the text the veil shows for a
    status.

See Also
--------
LiveSurface : The newest-frame exchange the frames go through.
SubSurfaceHost : A retained GUI.Forms tree on its own thread.

Notes
-----
A frame is refused, and the surface keeps showing what it showed, when an
axis is zero or above 16384, the pixels exceed 256 MiB, a row is shorter than
four bytes a pixel, the span holds less than the frame describes (every
product is checked for overflow first), the format is unknown or the damage
lies outside the frame. `ContentFrameSink::publish` returns the reason.

Content that publishes only when something changes calls `heartbeat()` from
its loop so the watchdog knows it is alive. The surface also publishes the
busy state while content is stale.

Examples
--------
Publish one frame from a consumer thread:

.. code-block:: cpp

   std::shared_ptr<gf::ContentSurface> surface =
       gf::make_control<gf::ContentSurface>(gf::StableId("scene"));
   gf::ContentFrameSink sink = (*surface).open_content(true);
   std::vector<std::byte> pixels(64U * 32U * 4U, std::byte{255});
   gf::ContentFrame frame;
   frame.width = 64U;
   frame.height = 32U;
   frame.row_bytes = 64U * 4U;
   frame.opaque = true;
   frame.pixels = std::span<const std::byte>(pixels.data(), pixels.size());
   const gf::ContentPublishResult result = sink.publish(frame);
   if (result != gf::ContentPublishResult::published) return;   // refused or dropped: the last frame stays
