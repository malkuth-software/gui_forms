# The application and its windows

A GUI.Forms program has one **application** and one or more **windows**.
The application is made first and owns what every window shares; a window
is made next, usually from a form document; then the program attaches its
behaviour and runs the window:

```text
configure              construct                       set up                  run
Application::create -> MainWindow window(model)     -> handlers and rules   -> application.run(window)
  theme, presentation    tree and bindings, at once
```

Each step needs the one before it, and the types enforce it: the generated
window takes its model at construction, so it is bound when it exists, and
`run` takes a window that has been made.

## Making the application

```cpp
#include <gui_forms/application.hpp>

namespace gf = gui_forms;

gf::ApplicationConfig config{};
config.theme.base = gf::ThemeBase::gui_forms;            // the base sheet
config.theme.packages.push_back(my_app::my_app_theme()); // the program's packages, in order
gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
if (!created) {
    // created.error: already_exists or theme_refused; for theme_refused,
    // created.findings hold the compile's diagnostics. Nothing was made.
}
gf::Application& application = *created.application;     // owned by created.application
```

`ApplicationConfig` holds:

- `theme`: the base (`ThemeBase::gui_forms` or `windows_professional`), the
  program's packages in compile order, the installed library a package's
  `@import` may name, the program's own control types a selector may name,
  and the effect registry a sheet's `effect` may name ([Themes](themes));
- `presentation`: the presentation defaults (text scale, contrast, motion,
  colour scheme and the rest of `PresentationSettings`) every window starts
  from until the host reports its own;
- `image_limits`: the quotas of the shared image registry.

The theme compiles once, in `create`. A window holds a reference to the
compiled, immutable theme and never compiles it again, so making a window
costs no theme work ([Painting and damage](painting-and-damage) lists the
instrumentation).

## The current application

While it lives, the application is the **current application**, and there
is one at a time: a second `create` is refused with `already_exists`.

- **Windows join it.** A top-level window made on the application's thread
  joins the current application: it shares its resources (the image
  registry and the theme), starts from its presentation, reads its tooltip
  switch and uses its keyboard-command keys. Each generated window class also
  has a constructor that takes the application first
  (`MainWindow window(application, model);`), for a program that wants the
  dependency visible.
- **Owned surfaces never take it.** Popups, dialogs, menus and tooltips open
  from a window or a control and take that window's configuration.
- **Without an application**, as in a test, a window makes its own resources
  with the built-in theme and the default presentation.
- **Closing.** `application.close()` ends its time as current, and is
  refused with `windows_alive` while a joined window lives. Destroying the
  application while windows live reports them on standard error; they keep
  the theme and resources they share and read nothing more from it.
  `Application::current()` returns the current application, or null.

## Changing settings during a run

- `application.set_theme(theme)` replaces the theme of every joined window,
  in the order they joined.
- `application.set_presentation_settings(settings)` gives new presentation
  defaults to every joined window. Each window keeps its own overrides
  (`window.set_presentation_overrides(overrides)`, a `PresentationOverrides`
  of optional fields) and raises `presentation_changed` when its settings in
  force change.
- `application.set_tool_tips_shown(false)` turns tooltips off in every window
  whose own switch is unset (`window.set_tool_tips_shown`).
- `application.key_bindings()` is the one set of keyboard-command keys. A
  rebinding made in any window holds in all of them.

A window can also show a theme of its own with `window.set_theme(theme)`; a
control can override it with `set_theme_override` ([Themes](themes.md#which-theme-a-control-uses)).

## Windows

A `gui_forms::Window` is a complete model of a top-level window: its control
tree, its thread, its dispatcher, its focus, its rules and its dirtiness. It
runs with or without a native window. The window class a form document
generates is a `Window` subclass whose controls are typed members
([Forms](forms.md#the-window-is-the-form)); a window built in code passes its
root control to `Window(root, client_size)`.

The three kinds of window differ in what they ask of the user and in how they
are made:

| Kind | What it is |
|---|---|
| **Window** | a full top-level window, made by the program and run by the application |
| **Popup** | an undecorated window owned by a window, which forces a decision: an alert, a dialog panel, a menu or a drop-down |
| **Splash** | a headless, lightweight window that forces nothing: a tooltip or the splashscreen |

Popups and Splash windows are opened from their owner, never made by the
program as windows; see [Overlays, Popups and Splash](overlays-popups-and-splash).

## Running

`run` hands a window to the native host for your platform, presents it,
pumps events until it closes, and returns an `ApplicationResult`:

```cpp
my_app::Settings settings{};   // the model, declared before the window it binds
std::unique_ptr<my_app::MainWindow> window = std::make_unique<my_app::MainWindow>(application, settings);
gf::ApplicationWindowOptions options{};
options.title = std::string(my_app::MainWindow::window_title);
options.initial_size = {640.0, 420.0};
const gf::ApplicationResult result = application.run(std::move(window), options);
const int exit_code = result.accepted() ? 0 : 1;
```

- **Ownership.** `run` takes the window by `std::unique_ptr` and owns it for
  the run, even when it fails. Make the window on the thread that calls
  `run`.
- **Options.** `ApplicationWindowOptions` holds the title, the initial and
  minimum sizes, and the `ready`, `closing` and `closed` callbacks;
  `closing` receives a `HostCloseRequest` whose `cancel` keeps the window
  open. `print_metrics_on_close` prints the instrumentation snapshot as one
  JSON line when the window closes.
- **Several windows.** `run(std::vector<gf::ApplicationWindow>)` runs up to
  64 windows in one host. Each `ApplicationWindow` names its `stable_id`, its
  `model`, its options and, for an owned window, its `owner_id`, with
  `tool_window` or `floating` for the owned kinds. `ApplicationWindowHandle`
  (from `ready`) shows, hides and resizes a window during the run.
- **The result.** `accepted()` is true for an ordinary close. Otherwise
  `error` says why: `wrong_thread`, `already_running`, `unsupported`,
  `backend_failure`, `callback_failure` (the exception is in
  `callback_exception`) and the others of `ApplicationError`.
- **The end.** `Application::shutdown()` frees the rendering library's
  caches once per process, after the last `run` has returned and every
  window is gone. A process that ends without it leaks nothing.

## Tests and headless use

Construct the window without running it: `my_app::MainWindow window(settings);`.
It works without a native window, joins the current application if there is
one, and makes its own resources otherwise. Drive its members, send keys with
`window.dispatch_key`, read `window.interaction_snapshot()`, and read the
instrumentation. Every GUI.Forms test runs this way.

## The C ABI

The C ABI keeps the same order. `GF_TABLE_APPLICATION`'s `create` makes the
application from a `gf_application_config` (the theme base, the packages in
compile order and the presentation source) and reports the theme compile in
a `gf_theme_report`; `close` and `state` complete it. `GF_TABLE_THEMES` holds
the one theme every window shows, so a theme loaded before a run is each
window's from its first frame. A window is made with `windows.create(root)`.
The splashscreen calls of `GF_TABLE_RUNTIME` work before anything else, on
the thread that will be the UI thread. See the C ABI header
`gui_forms/c/gf_abi.h` and [The C ABI conformance kit](c-abi-conformance-kit).
