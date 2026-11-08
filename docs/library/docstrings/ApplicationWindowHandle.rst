A weak handle for asking the host to show, hide, retitle, resize or close a running window.

The `ready` callback in `ApplicationWindowOptions` hands you a handle for its
window; keep a copy wherever you need to act on the native window later, for
example to close it from a Quit button. A handle does not keep the window
alive. Each request returns a `HostServiceStatus` instead of throwing, so a
request that cannot be carried out is reported rather than lost. A
default-constructed handle refers to no window.

Properties
----------
active
    Whether the window is ready and not yet closed, as seen from the calling
    thread. It is false on any other thread than the one that called
    `Application::run`.
id
    The window's `HostWindowId` in this run: assigned in opening order and
    never reused, so it identifies the window in frame reports and in
    `ApplicationWindows::window_at`. Invalid before `ready`, after the
    window closes, and for a host without application windowing.
windows
    The run's `ApplicationWindows`, for opening and destroying windows
    while the application runs and for the pointer and the window under a
    desktop point. Inactive when this handle is inactive.
frame
    The window's frame in desktop logical units: the client area and, where
    the host measures it, the outer frame with its decorations. A host
    without window frames returns `HostServiceError::unsupported`.

Methods
-------
request_close
    Asks the host to close the window, as if the user had clicked its close
    button. The `closing` callback still runs and may cancel the close.
show
    Asks the host to show the window, for example after `hide` or when it
    started hidden.
hide
    Asks the host to hide the window without closing it. The primary window
    cannot be hidden; for it the request returns
    `HostServiceError::invalid_argument`.
toggle_full_screen
    Asks the host to enter or leave full-screen presentation. A host without
    full-screen support returns `HostServiceError::unsupported`.
set_title
    Changes the native title bar text. The empty string is allowed; text over
    65536 bytes, malformed UTF-8 or an embedded NUL returns
    `HostServiceError::invalid_argument` and is not sent to the host. The
    text is only borrowed for the call.
request_client_size
    Asks the host to make the window's client area a given size in logical
    units, as if the user had dragged its edge. Each side must be from 1 to
    32768 and at least the window's `minimum_size`, or the request returns
    `HostServiceError::invalid_argument`. A window that is full screen,
    maximized or minimized returns `HostServiceError::unsupported`. The model
    takes the new size only when the host's resize event arrives, and the
    platform or window manager may give a different size; read
    `Window::client_size` afterwards rather than assuming the request's size.
    `Window::resize` is the host's side of this and does not resize the native
    window.
request_frame
    Asks the host to place the window's client area at a rectangle in
    desktop logical units: the position and the size together. The size
    follows the rules of `request_client_size`; each origin coordinate must
    be within plus or minus 1000000. The new frame arrives through the
    `frame_changed` callback, on Windows before the request returns and on
    X11 with a later event, and a window manager may adjust it.
bring_to_front
    Orders the window above the run's other windows without activating it.
    It never moves the window above other applications' windows, and owned
    windows stay above their owners.

See Also
--------
ApplicationWindowOptions : Delivers the handle through its `ready` callback.
ApplicationWindows : The run's windowing, reached through `windows`.
Application : Runs the windows the handles refer to.
HostServiceStatus : The result of each request.

Notes
-----
Every request checks, in this order: a window that is gone, or a
default-constructed handle, returns `HostServiceError::after_shutdown`; a
call from another thread than the one running `Application::run` returns
`HostServiceError::wrong_thread`; a closed window returns
`HostServiceError::after_shutdown`; a window whose `ready` callback has not
run yet returns `HostServiceError::backend_failure`; a service the host does
not provide returns `HostServiceError::unsupported`. An exception inside the
native adapter is reported as `HostServiceError::backend_failure`.

An accepted request means the host received it. The native window may carry
it out later in the same event loop, so do not expect the window to be
hidden, resized or closed when the call returns.

Handles are cheap to copy. After the window closes, or after
`Application::run` returns, every request returns
`HostServiceError::after_shutdown`.

Examples
--------
Keep the handle from `ready` and close the window from a button. This needs a
native host, so it is compiled but not run:

.. code-block:: cpp

   // compile only
   struct Editor final : gf::Component {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("editor"));
       std::shared_ptr<gf::Button> quit =
           gf::make_control<gf::Button>(gf::StableId("editor.quit"), "&Quit");
       gf::ApplicationWindowHandle handle;

       Editor() {
           (*quit).set_bounds({16.0, 16.0, 96.0, 28.0});
           (*root).add_child(quit);
           gf::on((*quit).clicked(), *this, &Editor::on_quit);
       }
       void on_quit() {
           static_cast<void>(handle.request_close());   // closing may still cancel
       }
   };

   struct KeepHandle final {
       Editor* editor;
       void operator()(gf::Window&, gf::ApplicationWindowHandle window_handle) const {
           (*editor).handle = window_handle;
       }
   };

   void example() {
       gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
       if (!created) return;
       gf::Application& application = *created.application;
       Editor editor;
       gf::ApplicationWindowOptions options;
       options.title = "Editor";
       options.ready = KeepHandle{&editor};
       gf::ApplicationResult result = application.run(
           std::make_unique<gf::Window>(editor.root, gf::Size{640.0, 480.0}), std::move(options));
       static_cast<void>(result);
   }
