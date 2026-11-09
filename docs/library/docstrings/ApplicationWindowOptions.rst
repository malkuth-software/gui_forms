The title, sizes, behaviour and callbacks of one native application window.

Pass an `ApplicationWindowOptions` to `Application::run`, or set one in each
`ApplicationWindow` of a multi-window run. It is a plain struct: set the
fields you need and leave the rest at their defaults. The callbacks are how
an application reacts to the native window's life: keeping a handle once it
exists, confirming a close, cleaning up when it has closed.

See Also
--------
Application : Presents the windows these options describe.
ApplicationWindowHandle : The handle the `ready` callback receives.
HostCloseRequest : What the `closing` callback can cancel.

Notes
-----
The fields, with their defaults:

`title`, default ``"GUI.Forms Application"``
    The native title bar text. At most 65536 bytes of valid UTF-8 without
    NUL; change it later with `ApplicationWindowHandle::set_title`.
`initial_size`, default ``{1120, 680}``
    The client size the window opens with, in logical units.
`minimum_size`, default ``{0, 0}``
    The smallest client size the user can resize to, as Windows Forms'
    `MinimumSize`: a side of 0 sets no minimum there beyond the host's own,
    so the default sets none. The initial size must be finite and between 1
    and 32768, the minimum finite and between 0 and 32768, and the initial
    size may not be below the minimum. A refused entry's
    `ApplicationResult::refusal` says why (`below_minimum_size`, with
    `ApplicationResult::minimum_size` and `initial_size` naming both sizes).
`initially_visible`, default ``true``
    Whether the window is shown when the run starts. The primary window must
    keep this true.
`hide_on_close`, default ``false``
    Whether an accepted close hides the window instead of closing it, so it
    can be shown again with `ApplicationWindowHandle::show`. Only owned and
    tool windows may set it; the primary window may not.
`minimizable`, default ``true``
    Whether the window offers a minimize control.
`print_metrics_on_close`, default ``false``
    Whether the host writes the window's `MetricsSnapshot` and its host
    session snapshot to standard output, as one JSON line, after the window
    has closed.
`ready`
    Called once, on the UI thread, when the window has a native host and its
    services, before it is first shown. It receives the `Window` and an
    `ApplicationWindowHandle`; keep the handle if you need it later. Other
    windows of the same run may not be ready yet.
`wake_ready`
    Called just before `ready` with a wake function. Your own worker threads
    may call that function while the window is alive to ask the host for a
    turn; stop calling it before `closed` runs. The wake only schedules a
    turn.
`dispatch_pending`
    Called on the UI thread in the turn that a call to that wake requests,
    to run your own queued work.
`closing`
    Called with a `HostCloseRequest` when the user, the system or
    `ApplicationWindowHandle::request_close` asks the window to close. Set
    `cancel` to keep it open. If the callback throws, the close goes ahead.
`closed`
    Called once after the window has closed.
`frame_changed`
    Called on the UI thread for every native move or resize of the window
    once it is ready, with its frame in desktop logical units (see
    `ApplicationWindowHandle::frame`). The client size also reaches the
    model through the host's resize event.

The empty callbacks are simply not called. If any callback throws, the run
ends with `ApplicationError::callback_failure`; see `Application`.

Examples
--------
Confirm the close of a document window and set its title once it is ready.
This needs a native host, so it is compiled but not run:

.. code-block:: cpp

   // compile only
   struct ConfirmClose final {
       bool* unsaved;
       void operator()(gf::HostCloseRequest& request) const { request.cancel = *unsaved; }
   };

   struct ShowDocumentTitle final {
       void operator()(gf::Window&, gf::ApplicationWindowHandle handle) const {
           static_cast<void>(handle.set_title("Untitled - Notes"));
       }
   };

   void example() {
       gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
       if (!created) return;
       gf::Application& application = *created.application;
       bool unsaved_changes = false;
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("notes"));
       gf::ApplicationWindowOptions options;
       options.title = "Notes";
       options.initial_size = {800.0, 600.0};
       options.ready = ShowDocumentTitle{};
       options.closing = ConfirmClose{&unsaved_changes};
       gf::ApplicationResult result = application.run(
           std::make_unique<gf::Window>(root, gf::Size{800.0, 600.0}), std::move(options));
       static_cast<void>(result);
   }
