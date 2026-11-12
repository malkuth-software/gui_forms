A diagnostic panel that shows the window's runtime counters.

A metrics view draws a dark card with a title and one or two lines read from
the window's `MetricsSnapshot`: the number of controls, paint passes, reused
display chunks, input events and focus scopes, and the area
painted. It is meant for development builds, test windows and diagnostics
panels, where it shows whether an interaction repaints more than it should or
leaves the window busy. It ignores the pointer. Read `Window::metrics_snapshot`
directly when the numbers are needed in code.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
initial_title
    The first `title`, "Runtime metrics" by default.

Properties
----------
title : default ``"Runtime metrics"``
    Heading drawn on the card and published as the accessible name.
style
    The `BasicControlStyle` colours: the card is filled with `text`, the
    accent bar uses `accent`, the title `face_light` and the counters `face`.
accent_width : default ``5.0``
    Width of the accent bar at the left edge, in layout units. It must be
    finite and from 1 to 32, or `std::invalid_argument` is thrown.

Notes
-----
The view reads the counters when it paints, but it does not repaint by
itself when they change. Refresh it explicitly with `invalidate`, for example
from a `Timer`, as the second example does. Its accessible value is the text
it last painted, so an assistive client reads what the card shows; a paint
that changes it advances the window's semantic generation, and before the
first paint the value is empty.

The view publishes the accessibility role `group`, named by `title`, with the
counters as its value. While it is not attached to a window, it shows
`Detached`.

Examples
--------
Add a metrics card to a window and read what it shows:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("app"));
   std::shared_ptr<gf::MetricsView> metrics =
       gf::make_control<gf::MetricsView>(gf::StableId("app.metrics"), "Window counters");
   (*metrics).set_requested_bounds({0.0, 0.0, 360.0, 84.0});
   (*root).add_child(metrics);
   gf::Window window(root, gf::Size{360.0, 84.0});
   window.perform_layout();
   gf::SemanticDescriptor semantics = (*metrics).semantic_descriptor();
   // semantics.value lists the counters, starting with the number of controls

Refresh the card once a second with an owned timer:

.. code-block:: cpp

   struct MetricsRefresh final : gf::Component {
       std::shared_ptr<gf::MetricsView> view;
       gf::Timer timer;

       MetricsRefresh(gf::Window& window, std::shared_ptr<gf::MetricsView> target)
           : view(std::move(target)), timer(window, std::chrono::seconds(1)) {
           gf::on(timer.tick(), *this, &MetricsRefresh::on_tick);
           timer.start();
       }
       void on_tick() { (*view).invalidate(gf::Dirty::paint | gf::Dirty::semantics); }
   };

   void example() {
       std::shared_ptr<gf::MetricsView> metrics =
           gf::make_control<gf::MetricsView>(gf::StableId("app.metrics"));
       gf::Window window(metrics, gf::Size{360.0, 84.0});
       MetricsRefresh refresh(window, metrics);
   }
