The retained model of one window: a tree of controls with its layout, focus, input, painting and UI thread.

A `Window` owns a root control and everything that belongs to the window as
a whole: which control has focus, which holds the pointer, what needs to be
laid out and repainted, the queue of work posted to the UI thread, overlays,
keyboard accelerators and the instrumentation counters. It is a complete
model on its own; a native host presents it, and `Application::run` is the
usual way to do that. Without a host the same object runs in tests and
headless tools: call `perform_layout`, feed it input with `dispatch_pointer`
and `dispatch_key`, run posted work with `drain_posted_work`, and read the
results.

A window made on the thread of the current application (see `Application`)
joins it. It shares the application's resources unless it was given its own,
starts from the application's presentation defaults, follows its tooltip
switch, and leaves the application when destroyed. With no current
application, as in tests, the window makes its own resources with the
built-in theme.

Everything but the native-host connection is declared on its base,
`SurfaceRoot`: the control tree, layout, focus, input, painting, the
posted-work queue, overlays, accelerators, tooltips and the counters. `Window`
adds what binds that root to a native window: resizing and scale, the
cursor, the presentation declaration, and the live-surface presentation
queue.

Parameters
----------
root
    The root control. The window holds it for its whole life and attaches
    the tree under it. It must be alive and must not have a parent: a null
    root throws `std::invalid_argument`, a root with a parent or a disposed
    root throws `std::logic_error`, and so does a stable ID used twice in the
    tree.
application
    The application to join explicitly, for a program that wants the
    dependency visible. Joining an application that is closed, or from
    another thread than the application's, throws `std::logic_error`.
client_size
    The logical size of the client area, `{0, 0}` by default. A native host
    keeps it in step with the native window through `resize`.
image_limits
    Quotas for the window's image registry: the window has resources of its
    own, with the joined application's theme. The other constructors use the
    default quotas.
resources
    An `ApplicationResources` to share with other windows: one image
    registry and one theme for all of them, so an `ImageId`, an `ImageList`
    and the theme hold in each and a control subtree moved between them
    keeps its images. The window takes the resources' current theme. Null
    resources throw `std::invalid_argument`; resources made on another
    thread throw `std::logic_error`. The window still joins the current
    application. Without `resources` or `image_limits`, a joined window
    shares the application's resources, and a window that joined none makes
    its own.

Properties
----------
application
    The application the window joined, or null: none was current when it
    was made, or the application was destroyed first.

Methods
-------
resize
    Changes the client size and marks the whole tree for layout, which runs
    at the next flush or geometry read. A negative size throws
    `std::invalid_argument`.

See Also
--------
SurfaceRoot : The base that declares the control tree and its work.
Control : The elements of the tree a window owns.
UpdateScope : Batches changes into one layout flush.
Application : Presents windows on the native host.
MetricsSnapshot : The counters a window records.
DispatchOperation : The handle returned by `begin_invoke`.

Notes
-----
The thread that constructs a window is its UI thread, and every control in
its tree belongs to that thread. Mutations, layout, input dispatch and
painting from any other thread throw `std::logic_error` without changing
anything, and count in `MetricsSnapshot::rejected_wrong_thread_operations`.
Only `begin_invoke`, `invoke`, `check_access`, `invoke_required` and
`dispatcher_snapshot` are meant to be called from other threads. Destroy the
window on its UI thread too: its destructor detaches the tree, and doing that
from another thread terminates the program.

Destroying a window cancels its queued work and removes its accelerators,
overlays, focus scopes and frame requests, then detaches the root. The root
and its children are not disposed; dispose the root yourself if nothing else
should use it.

The posted-work queue holds at most 4096 callbacks; `begin_invoke` throws
`std::length_error` beyond that, `std::invalid_argument` for an empty
callback and `std::logic_error` after `shutdown_dispatcher`. One dispatch
turn runs at most 1024 callbacks, and work posted from a callback waits for
the next turn. A callback that throws keeps its exception on its
`DispatchOperation`; later callbacks still run. Dropping the operation does
not cancel the work; call `DispatchOperation::cancel`. `invoke` from another
thread throws `std::logic_error` when no host is running, and
`DispatchCancelledError` if the work is cancelled before it runs.

A pointer event goes first to `Control::on_pointer_preview` on each control
from the root down to the target; a handler that sets `handled` stops it
there. A primary press then focuses the target if it is focusable; if that
focus change is refused, for example by validation, the press goes no
further. Otherwise the target captures the pointer. The target's mouse
raiser runs next (`Control::on_mouse_down`, `on_mouse_up`, `on_mouse_move`
or `on_mouse_wheel`: the control's own handling, then the event's
subscribers), then `Control::on_pointer_bubble` on its ancestors up to the
root, unless the raiser set `handled`.
Releasing a button over the control it was pressed on then raises Click and
MouseClick (or DoubleClick and MouseDoubleClick) on that control, and for
the primary button `Control::on_activate`, which is how a button raises
`clicked`.
The target is the control holding capture, or else the result of `hit_test`.
Pointer moves raise enter and leave events on the control under the pointer.

A key event goes first to accelerators registered to run before the focused
control (when no focus scope is open), then through the focused control's
route: `Control::on_key_preview` from the root down, `Control::on_key_down`
or `Control::on_key_up` on the focused control, and `Control::on_key_bubble`
back up. If nothing handled
it, the window tries the other accelerators; then Alt with a letter or digit
activates the matching mnemonic, Enter the `accept_button` and Escape the
`cancel_button`; finally Tab and Shift+Tab move focus.

Input dispatched while the window is painting is queued, in order, and
delivered after the paint; the dispatch functions then return true if the
event was queued.

An open `UpdateScope` defers only layout. Every change still records its
paint damage immediately, and `take_damage` and `paint` inside a scope work
with the last committed geometry.

Examples
--------
Click a button by dispatching pointer input, without a native host:

.. code-block:: cpp

   struct Dialog final : gf::Component {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("dialog"));
       std::shared_ptr<gf::Button> ok = gf::make_control<gf::Button>(gf::StableId("dialog.ok"), "OK");
       int clicks = 0;

       Dialog() {
           (*ok).set_bounds({20.0, 20.0, 80.0, 28.0});
           (*root).add_child(ok);
           gf::on((*ok).clicked(), *this, &Dialog::on_ok);
       }
       void on_ok() { ++clicks; }
   };

   void example() {
       Dialog dialog;
       gf::Window window(dialog.root, gf::Size{240.0, 120.0});
       window.perform_layout();
       gf::PointerEvent down{gf::PointerAction::down, gf::PointerButton::primary, {40.0, 30.0}};
       gf::PointerEvent up{gf::PointerAction::up, gf::PointerButton::primary, {40.0, 30.0}};
       window.dispatch_pointer(down);   // focuses and captures the button
       window.dispatch_pointer(up);     // released over it: dialog.clicks == 1
   }

Post work to the UI thread and run it. On a worker you would only call
`begin_invoke`; the host runs the turn. Here the example runs it itself:

.. code-block:: cpp

   struct ShowDone final {
       std::shared_ptr<gf::Label> status;
       void operator()() const { (*status).text = "Done"; }
   };

   void example() {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("root"));
       std::shared_ptr<gf::Label> status =
           gf::make_control<gf::Label>(gf::StableId("status"), "Working");
       (*root).add_child(status);
       gf::Window window(root, gf::Size{320.0, 40.0});

       gf::DispatchOperation posted = window.begin_invoke(ShowDone{status});
       gf::DispatchDrainResult turn = window.drain_posted_work();
       // turn.invoked == 1, and the label now reads "Done"
       static_cast<void>(posted);
       static_cast<void>(turn);
   }
