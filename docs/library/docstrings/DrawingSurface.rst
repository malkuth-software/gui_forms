A control that draws whatever its paint callback draws.

A drawing surface lets an application draw its own visuals, such as a chart, a
status badge or the frames of a `LiveSurface`, through the renderer-neutral
`Painter` without deriving a new control. When it paints, it fills its bounds
with `background` and then calls the paint callback. By default it ignores the
pointer and publishes the `image` accessibility role, which suits pictures.
Derive from `Control` instead when the drawing needs keyboard focus or input
handling of its own, and use a `RasterCanvas` to show a bitmap the user edits.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
has_paint_callback
    Whether a paint callback is set.
hit_test_visible : default ``false``
    Whether the surface receives pointer input. While false, the pointer
    passes through to whatever lies beneath, so the surface never becomes the
    target of a press or hover. Turn it on to observe `pointer_observed`.
semantic_role : default ``SemanticRole::image``
    The accessibility role the surface publishes. It is exposed to assistive
    technology only when it has an `accessible_name` or an
    `accessible_description`.
background : default ``Color::rgba(0, 0, 0, 0)``
    Fill drawn over the whole surface before the paint callback runs. The
    default is fully transparent.

Methods
-------
set_paint_callback
    Replaces the paint callback, a `std::function<void(Painter&, Rect, Rect)>`.
    The callback receives the painter, the surface's local bounds, whose origin
    is (0, 0), and the damaged part of those bounds, and draws in local
    coordinates. An empty function removes it. Every call repaints the
    surface, even when the new callback is the same as the old one.

See Also
--------
RasterCanvas : A zoomable view of an application-owned bitmap.
LiveSurface : Pixel frames published by a producer, drawn by a paint callback.
PictureBox : A control that shows a registered image.

Notes
-----
The callback is not called on every frame. What it draws is recorded and
replayed until the surface is invalidated or resized, so it must draw the complete
picture each time, not only the damaged part. When the data it draws changes,
call `invalidate` with `Dirty::paint`; for animation, own an `AnimationTimer`
whose ticks invalidate the surface, or request one later repaint with
`Window::schedule_paint`.

The callback runs on the UI thread during a paint pass. Do not change controls
from it: a change made during paint is applied in a later pass, not the
current one. If the callback throws, the exception leaves `Window::paint`
and the frame is abandoned; the window keeps the previous picture and the
damage, so a later pass paints again.

The setters must be called on the UI thread of the surface's window; from
another thread, and on a disposed surface, they throw `std::logic_error`.

Examples
--------
Draw a status badge with a named paint function object. Giving it an
accessible name exposes it as an image:

.. code-block:: cpp

   struct StatusBadge final {
       gf::Color fill;
       void operator()(gf::Painter& painter, gf::Rect bounds, gf::Rect) const {
           painter.fill_rounded_rect(bounds, bounds.height / 2.0, fill);
       }
   };

   void example() {
       std::shared_ptr<gf::DrawingSurface> badge =
           gf::make_control<gf::DrawingSurface>(gf::StableId("status.badge"));
       (*badge).set_requested_bounds({0.0, 0.0, 24.0, 24.0});
       (*badge).set_accessible_name("Connected");
       (*badge).set_paint_callback(StatusBadge{gf::Color::rgba(46, 160, 67)});
   }

Make the surface clickable by turning on `hit_test_visible`, and observe the
pointer with an owned handler:

.. code-block:: cpp

   struct Swatch final : gf::Component {
       std::shared_ptr<gf::DrawingSurface> surface =
           gf::make_control<gf::DrawingSurface>(gf::StableId("palette.red"));
       int presses = 0;

       Swatch() {
           (*surface).set_hit_test_visible(true);
           gf::on((*surface).pointer_observed(), *this, &Swatch::on_pointer);
       }
       void on_pointer(const gf::PointerEvent& event) {
           if (event.action == gf::PointerAction::down) ++presses;
       }
   };

   void example() {
       std::shared_ptr<gf::Control> root =
           gf::make_control<gf::Control>(gf::StableId("palette"));
       Swatch swatch;
       (*swatch.surface).set_requested_bounds({10.0, 10.0, 40.0, 40.0});
       (*root).add_child(swatch.surface);
       gf::Window window(root, gf::Size{100.0, 60.0});
       window.perform_layout();

       gf::PointerEvent press{gf::PointerAction::down, gf::PointerButton::primary,
                              {20.0, 20.0}};
       static_cast<void>(window.dispatch_pointer(press));   // swatch.presses is now 1
   }
