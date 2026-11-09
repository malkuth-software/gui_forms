A zoomable, pannable view of a bitmap that the application owns and edits.

A raster canvas presents a `gui_drawing::Bitmap`, such as a paint document, a
sprite or a captured image. The canvas does the presentation: it uploads the
bitmap to the window's image registry, maps between client and bitmap
coordinates at any zoom and pan, and after an edit uploads only the changed
pixels and repaints only the area they cover. Tools, selection and undo stay
in the application. Derive from `RasterCanvas` to add pointer tools by
overriding `on_pointer`, or to draw a selection over the image by overriding
`on_paint_overlay`. Use a `PictureBox` to show an image that does not change,
and a `DrawingSurface` for vector drawing.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
bitmap : default ``nullptr``
    The bitmap shown, shared with the application. Setting a different bitmap
    releases the old one's uploaded tiles and, while the canvas is attached to
    a window, uploads the new one at once. Setting the same pointer again does
    nothing; setting a null pointer is the same as `clear_bitmap`.
zoom : default ``1.0``
    Client units per bitmap pixel: at 4.0, one pixel covers four by four
    units. It must lie in [1/64, 256]; any other value throws
    `std::invalid_argument`.
view_origin : default ``(0, 0)``
    The bitmap coordinate shown at the canvas's top-left corner, in bitmap
    pixels. Both coordinates must be finite and no larger than 1,000,000,000
    in magnitude, or `std::invalid_argument` is thrown.
sampling : default ``ImageSampling::nearest``
    How pixels are resampled when zoomed. `ImageSampling::nearest` keeps hard
    pixel edges and `ImageSampling::linear` blends them. With linear
    sampling, the repaint after an edit grows by one pixel on each side to
    cover the blended edge.
transparency_grid : default ``true``
    Whether a checkerboard is drawn under the visible part of the bitmap, so
    that transparent pixels can be seen. Its two colours are set with
    `set_transparency_colors`; the defaults are `Color::rgba(246, 247, 249)`
    and `Color::rgba(211, 215, 220)`.
canvas_background : default ``Color::rgba(112, 118, 126)``
    Fill for the whole canvas, visible around the bitmap when the bitmap is
    smaller than the view or panned away.
transparency_cell_size : default ``0.0``
    Side of one checkerboard cell in client units. Zero picks a size that
    grows with the visible area and is never below 8. Any other value must
    lie in [2, 128], or `std::invalid_argument` is thrown.
presented_generation : default ``0``
    The bitmap `generation()` most recently uploaded to the window, or 0 when
    nothing is uploaded. When it differs from the bitmap's current
    generation, `synchronize_bitmap` has work to do.
last_resource_error : default ``ImageResourceError::none``
    Why the most recent upload failed, for example
    `ImageResourceError::registry_encoded_limit_exceeded` when the window's
    image quota is too small for the bitmap. A successful upload sets it back
    to `ImageResourceError::none`.
visible_bitmap_bounds
    The part of the bitmap visible in the canvas, in bitmap pixels and
    clamped to the bitmap. It is empty when there is no bitmap.

Methods
-------
clear_bitmap
    Removes the bitmap and releases its uploaded tiles from the window.
set_view
    `set_view(zoom, origin)` sets `zoom` and `view_origin` together, with one
    repaint. `set_zoom` and `set_view_origin` each call it with the other
    value unchanged.
set_transparency_colors
    `set_transparency_colors(first, second)` sets the two checkerboard
    colours.
synchronize_bitmap
    Brings the window's copy of the bitmap up to date after the application
    has edited it. Only the rectangles that changed since
    `presented_generation` are uploaded, and only the client area they cover
    is damaged. If any uploaded tile has been removed from the window's
    registry, the whole bitmap is uploaded again. Returns true when the
    window's copy is current, including when there is no bitmap, and false
    when the canvas is not attached to a window or an upload failed; see
    `last_resource_error`.
bitmap_to_client
    Maps a rectangle of bitmap pixels to client coordinates under the current
    `zoom` and `view_origin`. Use it to draw a selection over the image.
client_to_bitmap
    Maps a point in client coordinates to a fractional bitmap coordinate. The
    pixel under the point is the integer part of each coordinate.

See Also
--------
DrawingSurface : Vector drawing through a paint callback.
PictureBox : A control that shows a registered image.
LiveSurface : Frames produced continuously, for video or emulation.

Notes
-----
Editing the bitmap does not repaint the canvas. After each edit, made with
`set_pixel` or a `begin_edit` and `commit_edit` pair, call
`synchronize_bitmap`. An edit that is cancelled with `cancel_edit` leaves
nothing to upload.

The bitmap is uploaded when the canvas is attached to a window, or by
`set_bitmap` on a canvas already attached, in tiles of at most 512 by 512
pixels, each with a one-pixel border shared with its neighbours. Detaching or
disposing the canvas, clearing or replacing its bitmap removes the tiles from
the window. When the upload on attachment fails, attaching throws
`std::runtime_error` naming the resource error. An upload started by
`set_bitmap` does not throw: the canvas stays empty and `last_resource_error`
says why.

Bitmaps are premultiplied BGRA8 (`gui_drawing::PixelFormat::bgra32_premultiplied`),
the window's own order, so their pixels are uploaded as they are; the bitmap
itself is never modified.

The canvas takes keyboard focus, is a tab stop and shows a crosshair cursor.
It has no tools of its own: pointer input reaches `on_pointer` in a derived
class and observers of `pointer_observed`. Pointer positions are in window
coordinates, so convert them with `point_from_window` before
`client_to_bitmap`. A primary-button press captures the pointer, so the
release reaches the canvas even outside it; `set_pointer_capture` captures
it explicitly, for other buttons.

The canvas publishes the accessibility role `image`. With a bitmap, its value
gives the bitmap size and the zoom as a percentage, for example
`64 x 64 at 400.000000%`. It is exposed when it has a bitmap, an accessible
name or an accessible description.

Every member that changes the canvas, `synchronize_bitmap` included, must be
called on the UI thread of its window, and throws `std::logic_error` from any
other thread. `synchronize_bitmap` reads the bitmap, so do not edit the bitmap
on a worker at the same time.

Examples
--------
Show a 64 by 64 document at 4x, edit one pixel and synchronize. Only that
pixel is uploaded, and only its four-by-four area is repainted:

.. code-block:: cpp

   std::shared_ptr<gui_drawing::Bitmap> document =
       std::make_shared<gui_drawing::Bitmap>(64U, 64U);
   std::shared_ptr<gf::RasterCanvas> canvas =
       gf::make_control<gf::RasterCanvas>(gf::StableId("paint.canvas"));
   (*canvas).set_requested_bounds({0.0, 0.0, 256.0, 256.0});
   (*canvas).zoom = 4.0;
   (*canvas).bitmap = document;
   gf::Window window(canvas, gf::Size{256.0, 256.0});   // uploads the bitmap
   window.perform_layout();

   (*document).set_pixel(10U, 12U, gui_drawing::Color::from_rgb(200, 30, 30));
   static_cast<void>((*canvas).synchronize_bitmap());
   gui_drawing::PointF under_pointer = (*canvas).client_to_bitmap(gf::Point{41.0, 49.0});
   // under_pointer is (10.25, 12.25), which lies in pixel (10, 12)

A pencil tool in a derived canvas. The pointer position is converted from
window to client coordinates, then to a bitmap pixel:

.. code-block:: cpp

   class PencilCanvas final : public gf::RasterCanvas {
   public:
       explicit PencilCanvas(gf::StableId id) : gf::RasterCanvas(std::move(id)) {}

       void on_pointer(gf::PointerEvent& event) override {
           if (event.action != gf::PointerAction::down || !bitmap.read()) return;
           const gui_drawing::PointF at =
               client_to_bitmap(point_from_window(event.position));
           if (at.x < 0.0 || at.y < 0.0 ||
               at.x >= static_cast<double>((*bitmap.read()).width()) ||
               at.y >= static_cast<double>((*bitmap.read()).height())) {
               return;
           }
           (*bitmap.read()).set_pixel(static_cast<std::uint32_t>(at.x),
                                 static_cast<std::uint32_t>(at.y),
                                 gui_drawing::Color::from_rgb(0, 0, 0));
           static_cast<void>(synchronize_bitmap());
           event.handled = true;
       }
   };

   void example() {
       std::shared_ptr<PencilCanvas> canvas =
           gf::make_control<PencilCanvas>(gf::StableId("sketch.canvas"));
       (*canvas).set_requested_bounds({0.0, 0.0, 128.0, 128.0});
       (*canvas).zoom = 4.0;
       (*canvas).bitmap = std::make_shared<gui_drawing::Bitmap>(32U, 32U);
       gf::Window window(canvas, gf::Size{128.0, 128.0});
       window.perform_layout();

       gf::PointerEvent press{gf::PointerAction::down, gf::PointerButton::primary,
                              {10.0, 10.0}};
       static_cast<void>(window.dispatch_pointer(press));   // blackens pixel (2, 2)
   }
