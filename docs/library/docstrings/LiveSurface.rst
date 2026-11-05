A thread-safe exchange that hands the newest frame from a pixel producer to the window.

A live surface carries pixels produced continuously outside the retained tree:
a video decoder, an emulator screen, an audio visualizer. The producer
acquires a write lease on one buffer of a small pool, fills it and publishes
it; the window samples only the newest complete frame. A slow presenter skips
frames instead of queueing them, and the producer never waits. A live surface
is not a control: show it through a `DrawingSurface` whose paint callback
calls `Painter::draw_live_surface`. Use a `RasterCanvas` for a bitmap that
the user edits, and `Window::load_bgra32_premultiplied` for an image that
rarely changes.

Properties
----------
snapshot
    The surface's state in one `LiveSurfaceSnapshot`: its description,
    `epoch`, `published_generation`, the `publishes`, `dropped_acquires` and
    `read_acquires` counts, the `last_read_generation` sampled by a renderer,
    and whether a write lease is active, a frame exists and the previous
    epoch's frame is being carried.

Methods
-------
acquire_latest
    The newest published frame, as an immutable `LiveSurfaceFrame` read
    lease, or an empty frame before the first publication. While a frame is
    held, its buffer is not reused, so release it promptly. Between
    `reconfigure` and the first publication at the new size it returns the
    last frame of the previous epoch, at its old size, with whole-surface
    damage. Otherwise the frame's `damage()` is the rectangle of the latest
    publication only.
create
    `create(LiveSurfaceDescription)` allocates the whole buffer pool and
    returns the new surface, or null when the description is invalid or
    allocation fails. Width and height must each be 1 to 32768, with at most
    268,435,456 pixels in all, and `buffer_count` must be 2 to 8; the
    default is 3. Every buffer starts zeroed.
reconfigure
    `reconfigure(LiveSurfaceDescription)` replaces the pool with buffers for
    a new description and starts a new epoch, whose generations count from 1
    again. It returns false and changes nothing when the description is
    invalid, allocation fails or a write lease is held. Until the first
    publication after it, readers still receive the last frame of the old
    epoch, which a presenter scales to its destination. The presentation
    wakes are called afterwards.
try_acquire_write
    `try_acquire_write(preserve_published_contents = false)` returns an
    exclusive `LiveSurfaceWriteLease`. It returns an empty lease, and counts
    the attempt in `dropped_acquires`, when another lease is active or when
    every other buffer is held by a reader; it never blocks, and the
    producer should skip that frame. The buffer still holds what was last
    written into it: an older frame, or zeros. Pass true to start from a copy
    of the newest publication, as you must when you publish only a damaged
    rectangle.
acquire_for_presentation
    Used by the window's presenter, not by applications. It acquires the
    newest frame and takes the damage accumulated since that presenter's
    previous call, which `acquire_latest` never consumes.
connect_presentation_wake
    `connect_presentation_wake(std::function<void()>)` registers a callback
    that runs after every publication and every reconfiguration, on the thread
    that made it, once the surface's lock is released. It may only signal,
    for example post work to a dispatcher; it must not paint or touch
    controls. Exceptions it throws are ignored. The returned
    `LiveSurfaceWakeConnection` disconnects when destroyed; an empty callback
    returns an unconnected one.

See Also
--------
DrawingSurface : The control that presents a live surface.
RasterCanvas : A zoomable view of an application-owned bitmap.
Worker : A background thread for a producer.

Notes
-----
A `LiveSurfaceWriteLease` gives `pixels()`, `height()` rows of `row_bytes()`
bytes, four bytes per pixel in the surface's format. `publish(damage)` makes
the buffer the newest frame and returns its generation, 1 for the first
publication of each epoch, or 0 for an empty lease. Empty or out-of-bounds
damage means the whole surface. Damage is a promise that every pixel outside
it equals the previous publication, which lets a presenter copy only the
damage. Destroying a lease without publishing abandons the frame, as does
`abandon()`.

Pixels are premultiplied sRGB BGRA8 (blue, green, red, alpha in memory), the
one format of every surface and raster on every host. Setting `opaque` in the
description promises that every pixel has alpha 255, so the presenter may copy
instead of blend and skip painting what lies beneath.
`native_live_surface_pixel_format()` returns that format.

Every member may be called from any thread; an internal lock serializes
them. The usual split is a producer on a `Worker` that acquires and
publishes, and the UI thread presenting. `Window::queue_live_surface_presentation`
registers the surface with the window's presenter for as long as the control
stays attached; call it on the UI thread.

Frames and leases keep their buffers alive, even after the surface itself is
released.

Examples
--------
Create an opaque surface in the host's pixel order, publish one grey frame and
read it back:

.. code-block:: cpp

   gf::LiveSurfaceDescription description;
   description.width = 320U;
   description.height = 180U;
   description.pixel_format = gf::native_live_surface_pixel_format();
   description.opaque = true;
   std::shared_ptr<gf::LiveSurface> surface = gf::LiveSurface::create(description);

   gf::LiveSurfaceWriteLease lease = (*surface).try_acquire_write();
   if (lease) {   // empty when every buffer is busy: skip this frame
       std::span<std::byte> pixels = lease.pixels();
       for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
           pixels[offset] = std::byte{32};
           pixels[offset + 1U] = std::byte{32};
           pixels[offset + 2U] = std::byte{32};
           pixels[offset + 3U] = std::byte{255};
       }
       static_cast<void>(lease.publish());   // generation 1, whole surface
   }
   gf::LiveSurfaceFrame newest = (*surface).acquire_latest();   // generation 1

Present the surface in a window through a `DrawingSurface`, as the gallery's
live-surfaces page does: register it once, and invalidate the view after a
publication so that its paint replays the newest frame:

.. code-block:: cpp

   struct PresentLiveSurface final {
       std::shared_ptr<gf::LiveSurface> surface;
       void operator()(gf::Painter& painter, gf::Rect bounds, gf::Rect) const {
           painter.draw_live_surface(surface, bounds, 1.0);
       }
   };

   void example() {
       std::shared_ptr<gf::LiveSurface> surface = gf::LiveSurface::create({160U, 90U});
       std::shared_ptr<gf::DrawingSurface> view =
           gf::make_control<gf::DrawingSurface>(gf::StableId("player.video"));
       (*view).set_requested_bounds({0.0, 0.0, 320.0, 180.0});
       (*view).accessible_name = "Video";
       (*view).set_paint_callback(PresentLiveSurface{surface});
       gf::Window window(view, gf::Size{320.0, 180.0});
       window.perform_layout();

       gf::LiveSurfaceWriteLease lease = (*surface).try_acquire_write();
       static_cast<void>(lease.publish());
       static_cast<void>(window.queue_live_surface_presentation(view, surface));
       (*view).invalidate(gf::Dirty::paint);
   }
