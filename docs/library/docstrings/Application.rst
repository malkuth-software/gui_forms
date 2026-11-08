The application: what every window shares, and the entry point that presents
windows on the platform's native host.

A program makes one with `Application::create` before its windows. The result
owns the application, or refuses with nothing made. While it lives it is the
current application (`Application::current`). Every top-level `Window` made on
its thread joins it: the window shares its resources (the image registry and
the theme, compiled once by `create`), starts from its presentation defaults
and follows its tooltip switch. Popups, dialogs, menus and tooltips open from a
window and take that window's configuration. Without an application, which is
how tests and renderer-free programs work, a window makes its own resources
with the built-in theme.

`run` takes one or more retained `Window` models, gives each a native window,
pumps native events until the primary window closes and then returns an
`ApplicationResult`. It is the C++ counterpart of Windows Forms'
`Application.Run`. Link `GUIForms::Application`, which contains the one native
host the SDK was built with, for `run` and `capabilities`; the rest is part of
`GUIForms::Core`.

Properties
----------
closed
    Whether `close` has run.
window_count
    The windows that joined the application and are alive.
resources
    The resources every joined window shares, unless it was made with its
    own.
presentation_settings
    The presentation defaults (`ApplicationConfig::presentation`) that a
    joined window starts from, until the host reports its own.
key_bindings
    The application's one set of keyboard-command keys (owner decision
    DK-44). `run` gives it to every window it runs, so a rebinding in any
    window holds in all.
tool_tips_shown : default ``true``
    The program's tooltip switch (`set_tool_tips_shown`): whether tooltips
    show in every joined window whose own switch is unset
    (`Window::set_tool_tips_shown`). A tooltip that already shows stays
    until it closes. Any thread.

Methods
-------
create
    `create(ApplicationConfig)` compiles the configured theme into fresh
    resources and registers the application as current: first the base
    (`ApplicationThemeConfig::base`), then the program's packages in order
    (`compile_theme_packages`), with imports from
    `ApplicationThemeConfig::library` and the stock anatomies plus
    `ApplicationThemeConfig::controls`. With no packages and the default
    base, the application takes the process default theme
    (`default_compiled_theme`) without compiling it again. It returns an
    `ApplicationCreateResult` holding the application and the compile's
    warnings in `findings`. Refused, with nothing made: `already_exists`
    while another application is current; `theme_refused` when the compile
    refuses the theme (duplicate package names and more than
    `maximum_theme_packages` included), with its diagnostics in
    `findings`.
current
    The current application, or null. Any thread may ask, but use the
    application only on its own thread.
close
    Ends the application's time as current, before it is destroyed. While a
    window that joined it lives, `close` is refused with `windows_alive`
    and changes nothing. After it, `create` may make another application,
    and windows made from then on do not join this one. Calling it again
    does nothing. Destroying an application that still has windows reports
    them on stderr; they keep the resources they share and read nothing
    more from it.
run
    `run(std::unique_ptr<Window>, ApplicationWindowOptions)` presents one
    window, with the stable ID `"main"`. `run(std::vector<ApplicationWindow>)`
    presents up to `maximum_windows` (64) windows: exactly one primary window
    with no owner, plus owned and tool windows. It returns when the primary
    window has closed. The window models are consumed even when `run`
    fails.
validate
    Checks a set of windows against the rules `run` applies, without
    presenting anything, and returns the first problem as an
    `ApplicationResult`. It is part of `GUIForms::Core` and needs no host.
shutdown
    Ends the rendering library once per process: it frees stx_skia's caches
    and singletons. Call it after the last `run` has returned, when every
    window, surface and font the program made is gone; the window models
    `run` consumed are already destroyed. It is refused with
    `already_running` while a `run` is in progress (from a callback, for
    example) and with `wrong_thread` on macOS off the main thread. After it,
    `run` and `shutdown` return `after_shutdown`. A process may end without
    it, a crash included, and lose nothing: the library holds only process
    memory, which the operating system reclaims.
capabilities
    The capability record of the linked native host: its name and the
    services it offers, such as dialogs, clipboard and cursors. A build
    without a native host reports the name `"unsupported"` and no
    capabilities.

See Also
--------
ApplicationWindowOptions : Title, sizes and callbacks for one window.
ApplicationWindowHandle : Show, hide, retitle or close a running window.
ApplicationWindow : One entry of a multi-window run.
ApplicationResult : What `run`, `close`, `validate` and `shutdown` return.
ApplicationConfig : The theme, presentation defaults and image quotas an application is made with.
Window : The retained model each native window presents.

Notes
-----
`run` reports problems through `ApplicationResult::error` instead of
throwing:

- `ApplicationError::invalid_argument`: the windows break a rule below;
  `ApplicationResult::window_index` names the offending window.
- `ApplicationError::wrong_thread`: on macOS, `run` was not called on the
  main thread; or a `Window` was constructed on a different thread from the
  one calling `run`.
- `ApplicationError::already_running`: another `run` is in progress in this
  process.
- `ApplicationError::unsupported`: the library was built without a native
  host.
- `ApplicationError::callback_failure`: a callback in
  `ApplicationWindowOptions` threw. The first exception is kept in
  `ApplicationResult::callback_exception`, and the host closes the primary
  window to end the run.
- `ApplicationError::backend_failure`: the native host exited with a nonzero
  `ApplicationResult::native_exit_code`.
- `ApplicationError::after_shutdown`: `shutdown` has already run in this
  process.
- `ApplicationError::windows_alive`: from `close`, while a window that
  joined the application lives.

The rules `validate` checks: between 1 and 64 windows; every stable ID
nonempty, unique, at most 1024 bytes of valid UTF-8 without NUL; an owner ID,
when present, naming another window, with no ownership cycles; a title of at
most 65536 bytes of valid UTF-8 without NUL; initial and minimum sizes finite,
between 1 and 32768, with the minimum no larger than the initial size; and
exactly one primary window (no owner ID, not a tool window), which must be
initially visible and must not hide on close. `run` with a single window
makes that window the primary, so its options must keep
`initially_visible` true and `hide_on_close` false.

For each window the callbacks run in this order: `wake_ready`, then `ready`
once the native window and its services exist and before it is first shown,
then input and painting, then `closing` for each close request, and `closed`
once it has closed. All of them run on the thread that called `run`.

Examples
--------
Present one window and report how the run ended. `run` needs a native host,
so this example is compiled but not run:

.. code-block:: cpp

   // compile only
   struct Greeter final : gf::Component {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("root"));
       std::shared_ptr<gf::Button> greet =
           gf::make_control<gf::Button>(gf::StableId("greet"), "&Greet");

       Greeter() {
           (*greet).set_bounds({24.0, 24.0, 120.0, 36.0});
           (*root).add_child(greet);
       }
   };

   void example() {
       gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
       if (!created) return;
       gf::Application& application = *created.application;
       Greeter greeter;
       std::unique_ptr<gf::Window> window =
           std::make_unique<gf::Window>(greeter.root, gf::Size{480.0, 140.0});
       gf::ApplicationWindowOptions options;
       options.title = "Greeter";
       options.initial_size = {480.0, 140.0};
       options.minimum_size = {320.0, 140.0};
       gf::ApplicationResult result = application.run(std::move(window), std::move(options));
       if (!result.accepted()) {
           std::fprintf(stderr, "run failed: %d\n", static_cast<int>(result.error));
       }
       // Once, after the program's last run.
       gf::ApplicationResult ended = gf::Application::shutdown();
       if (!ended.accepted()) {
           std::fprintf(stderr, "shutdown failed: %d\n", static_cast<int>(ended.error));
       }
   }
