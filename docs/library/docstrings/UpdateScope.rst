A batch of changes to a window that is laid out once, when the batch ends.

`Window::begin_update` opens a scope and returns this token. While any scope
is open, the window keeps recording what each change invalidates but does
not run layout to answer geometry reads, so ten property changes cost one
layout pass instead of ten. Closing the outermost scope, by calling `close`
or by letting the token go out of scope, runs one bounded layout flush.
Scopes nest; closing an inner one only lowers the depth. Use a scope around
any group of related changes, such as filling a form or applying a theme.

Parameters
----------
root
    The surface root (usually a `Window`) whose scope the token closes. Do not construct a token this
    way: it does not open a scope, so closing it would close one that
    `Window::begin_update` opened elsewhere. Always obtain tokens from
    `Window::begin_update`.
other
    A token to move from. The moved-from token no longer closes anything.

Methods
-------
perform_layout
    Lays out pending changes now, while keeping the scope open, so that the
    code inside the scope can read up-to-date geometry. It is the same as
    calling `Window::perform_layout`.
close
    Closes the scope now rather than at the end of the block. If it was the
    outermost scope, pending layout runs before `close` returns, and an
    exception from a control's layout code propagates from here. Calling it
    again, or destroying the token afterwards, does nothing.

See Also
--------
Window : Opens scopes with `begin_update` and owns the layout they defer.
Control : `suspend_layout` and `resume_layout` defer layout for one subtree.

Notes
-----
Inside a scope, `Control::arranged_bounds`, `Control::absolute_bounds`,
`Window::hit_test` and the other geometry reads return the last committed
geometry instead of laying out first. Explicit layout requests still run:
`perform_layout` on the token, the window or a control lays out immediately.
A scope defers layout only. Paint damage is still recorded as each change is
made, and the host is woken to paint as usual.

The token is move-only. Moving it transfers the duty to close; assigning a
token over another closes the scope the target held first.

A token must be closed on the window's UI thread, and the window must still
exist. Prefer an explicit `close` when the final layout might throw: a
destructor cannot report an exception, so a layout failure, or closing on
the wrong thread, while the token is being destroyed terminates the program.

The counters `MetricsSnapshot::update_scopes_started`,
`MetricsSnapshot::update_scope_depth` and
`MetricsSnapshot::maximum_update_scope_depth` record scope use, and
`MetricsSnapshot::flush_count` shows how many layout flushes ran.

Examples
--------
Several changes, one layout pass when the block ends:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("root"));
   std::shared_ptr<gf::Label> status = gf::make_control<gf::Label>(gf::StableId("status"), "Ready");
   std::shared_ptr<gf::Button> cancel = gf::make_control<gf::Button>(gf::StableId("cancel"), "Cancel");
   (*root).add_child(status);
   (*root).add_child(cancel);
   gf::Window window(root, gf::Size{320.0, 80.0});
   window.perform_layout();
   window.reset_activity_metrics();
   {
       gf::UpdateScope scope = window.begin_update();
       (*status).set_bounds({8.0, 8.0, 200.0, 24.0});
       (*status).text = "Working";
       (*cancel).set_bounds({216.0, 6.0, 96.0, 28.0});
   }   // one layout flush here
   gf::MetricsSnapshot metrics = window.metrics_snapshot();   // metrics.flush_count == 1
   static_cast<void>(metrics);

Lay out early to read geometry in the middle of a batch:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("root"));
   std::shared_ptr<gf::Label> caption = gf::make_control<gf::Label>(gf::StableId("caption"), "Name");
   (*root).add_child(caption);
   gf::Window window(root, gf::Size{320.0, 80.0});
   gf::UpdateScope scope = window.begin_update();
   (*caption).set_bounds({8.0, 8.0, 120.0, 24.0});
   scope.perform_layout();
   gf::Rect placed = (*caption).arranged_bounds();   // {8, 8, 120, 24}
   scope.close();
   static_cast<void>(placed);
