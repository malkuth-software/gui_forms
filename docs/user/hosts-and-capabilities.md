# Hosts and capabilities

The retained engine knows nothing about AppKit, Win32 or X11. A host adapter
translates one platform's windows, events and services into the portable host
protocol, and the engine asks the adapter what it can do rather than
assuming. Host service requests report `HostServiceStatus`, including
`unsupported` where a service is unavailable. Capability flags describe the
advertised protocol surface; some native integrations are not advertised yet.

## The host protocol

`HostCapabilities` carries `protocol_version` (currently 10; one row per version in
`host_protocol_revisions`), a `platform`
string, a bit set of `HostCapability` flags and the host's keyboard-command
defaults (`key_map`, below). `Application::capabilities()`
returns the set for the host your program was linked against, before any
window exists. `supports(flag)` answers one question; `to_json()` dumps the
whole record for logs.

| Capability | Meaning | macOS | Windows | Linux X11 |
|---|---|:-:|:-:|:-:|
| `lifecycle` | attach, activation, close, shutdown events | yes | yes | yes |
| `scale_notifications` | logical scale changes at run time | yes | yes | no |
| `monitor_geometry` | monitor and work-area records and change events | yes | yes | yes |
| `occlusion` | window hidden/exposed notifications that gate painting | yes | no | yes |
| `scheduled_wake` | timed wake for frames and timers | yes | yes | yes |
| `pointer_input`, `keyboard_input` | translated pointer and key events | yes | yes | yes |
| `text_composition` | input-method composition bridged to the window | yes | yes | no |
| `pointer_capture` | synchronous capture routed to one control | yes | yes | yes |
| `cursor` | portable cursor kinds applied natively | yes | yes | yes |
| `clipboard`, `clipboard_images` | bounded UTF-8 text and premultiplied BGRA8 images | yes | yes | yes |
| `typed_drag_destination` | inbound text, file-list and media-typed drops | yes | no | yes |
| `dialogs` | the five portable common-dialog request/result families | yes | yes | yes |
| `sound_cues` | system alert sounds | yes | yes | yes |
| `frame_capture` | encode the presented framebuffer as PNG | yes | yes | yes |
| `notification_area` | notification-area (tray) icons with click, double-click and context events (`NotifyIcon`) | yes (status item) | yes | when a system tray owns `_NET_SYSTEM_TRAY_Sn` at startup |
| `notification_area_messages` | notification text from a tray icon | no | yes | no |
| `notification_area_menus` | a native menu shown from a tray icon | yes | yes | no |
| `menus` | native menu-bar publication | no | no | no |
| `font_discovery` | host font enumeration | no, by design | no, by design | no, by design |
| `accessibility` | native accessibility publication of the semantic tree, verified by client tests | yes | yes | yes |
| `typed_drag_source` | outbound drag initiation | no | no | no |
| `runtime_windows` | owned windows opened and destroyed while the application runs | yes | yes | yes |
| `window_frames` | window frames read and requested in desktop logical units, with a report for every move and resize | yes | yes | yes (the window manager decides) |
| `screen_pointer` | the pointer's desktop position | yes | yes | yes |
| `window_at_point` | the application's topmost window at a desktop point | yes | yes | yes |
| `window_ordering` | bring a window to the front of the application's own windows | yes | yes | yes (the window manager may decline) |
| `transparent_windows` | a window presented with per-pixel alpha when the compositor allows it | yes | yes | yes (while a compositing manager runs) |
| `window_backdrops` | a system material behind a transparent window | yes | yes (Windows 11 22H2 and later) | no |
| `compositor_notifications` | compositor and preference changes reported at run time | yes | yes | yes |
| `presentation_preferences` | the host's high contrast and reduced transparency fed to each window; its light or dark preference and accent and highlight colours reported (Windows only so far) | yes | yes | no |

The table reflects the bits each adapter currently advertises; read
`Application::capabilities()` rather than this page in code.

## Compositor cooperation

A window can ask the window system to show what lies behind it (stage T5,
[`planning/stages/T5-compositor/PLAN.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/T5-compositor/PLAN.md)). The request names a purpose, not
a platform material: `WindowBackdrop::opaque` (every window today),
`transparent` (clear pixels show the desktop, unblurred),
`window_material` and `transient_material` (the system's materials for a
main window and for popups). GUI.Forms never reads or blurs the desktop
itself.

- **Declare it.** Set `ApplicationWindowOptions::non_opaque` (or call
  `Window::declare_non_opaque` before a host attaches). A window not
  declared is always opaque, because a host fixes what a native window can
  show when it creates it.
- **Ask for it.** `Window::request_backdrop(WindowBackdropRequest)` takes
  the preferred backdrop and up to two fallbacks
  (`WindowBackdropRequest::with_fallback`); `opaque` always follows them,
  and `transparent` is a fallback only when you list it. It returns the
  `WindowBackdropGrant` in force: the backdrop given and, for the
  preferred one, the reason (`granted`, `not_declared`, `host_unsupported`,
  `os_version`, `no_compositor`, `high_contrast`, `reduced_transparency`,
  `transparency_disabled`, `backend_failure`). An unavailable backdrop is
  never an error: the window is opaque, drawn by GUI.Forms, and the grant
  says why.
- **Follow it.** The request stands. Each host reports its
  `CompositorState` (whether a compositor runs, what each backdrop needs,
  the user's transparency, reduced-transparency and high-contrast
  settings) after attach and whenever it changes; the window resolves its
  grant again and raises `Window::backdrop_changed` when the backdrop or
  its reason changes. A compositor stopping turns the window opaque at the
  next frame.
- **Accessibility settings win.** High contrast or reduced transparency,
  the host's (`presentation_preferences`) or set by the application in
  `PresentationSettings`, withdraw every backdrop.

**macOS.** A window declared non-opaque is made inside a container view
holding an `NSVisualEffectView` behind the window's own view, so that a
backdrop can change without moving that view. `transparent` makes the
window non-opaque on a clear background; `window_material` shows the
effect view at `underWindowBackground` and `transient_material` at
`popover`, blending behind the window and following its active state.
The window server always composites. The accessibility option "Reduce
transparency" is reduced transparency and is followed as it changes;
with it on, AppKit draws every material as a solid colour. "Increase
contrast" is not reported as high contrast. Clear pixels still receive
the pointer: there is no click-through. The measurements behind this are
in [`planning/stages/T5-compositor/MEASUREMENTS.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/T5-compositor/MEASUREMENTS.md).

**Windows.** The client's premultiplied alpha is shown through
`DwmEnableBlurBehindWindow` with an empty region, which keeps the native
caption. `window_material` is Mica and `transient_material` is Acrylic,
through `DWMWA_SYSTEMBACKDROP_TYPE`, where Windows offers it (Windows 11
22H2 and later); elsewhere they are refused with `os_version`, so list
`transparent` as a fallback if you want it. While a window is inactive,
Windows draws its own solid colour in place of Mica or Acrylic. Turning
transparency effects off in Windows settings counts as reduced
transparency. High contrast is read from the system. Both are followed
as they change. Clear pixels still receive the pointer: there is no
click-through. The measurements behind this are in
[`planning/stages/T5-compositor/MEASUREMENTS.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/T5-compositor/MEASUREMENTS.md).

**X11.** A window declared non-opaque is made with the screen's 32-bit
TrueColor visual, whose spare byte XRender names as alpha; while
`transparent` is granted its presented pixels carry the frame's
premultiplied alpha, and under `opaque` they are presented at alpha
255. Whether a compositing manager runs is the owner of
`_NET_WM_CM_S<screen>`, followed
through XFixes as managers start and stop: without one the X server shows
clear pixels as black, so the window is granted `opaque` with the reason
`no_compositor`, and `transparent` again once a manager starts. X11 has
no system material (`window_material` and `transient_material` are
`host_unsupported`) and no high-contrast or reduced-transparency setting
to report, so `PresentationSettings` stay the application's. Clear pixels
still receive the pointer. A server without the 32-bit visual leaves the
window opaque (`host_unsupported`). The measurements behind this are in
[`planning/stages/T5-compositor/MEASUREMENTS.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/T5-compositor/MEASUREMENTS.md).

**Owned and popup windows.** What a host can grant a window owned by
another (a floating panel, and in time a native popup, menu or tooltip)
is reported in that window's own `CompositorState`, as for any window:

| Owned or popup window | macOS | Windows | X11 |
|---|---|---|---|
| per-pixel alpha (`transparent`) | yes | yes | while a compositing manager runs |
| system material (blur) | `NSVisualEffectView` in the owned window or panel | DWM system backdrop on the owned window (Windows 11 22H2 and later) | none; any blur is the compositing manager's own |
| saturation of the material | no | no | no |

Popups, menus and tooltips are still drawn inside their owner's
surface, so for now they take only a backdrop chain's tint
([Transparency, glass and reflections](transparency.md), "Window
backdrops").

The headless reference host simulates these capabilities
(`HeadlessApplication::simulate_compositor_state`).

## Input settings

Every host reports the platform's double-click interval after a window
attaches, and again when the system setting changes (`HostInputSettingsEvent`,
protocol version 10). The window keeps it in its `PresentationSettings`, the
record controls already read, as `double_click_milliseconds`. A control that
must tell a slow second click from a double-click, such as a list starting a
label edit, compares against it. `double_click_origin` says where the value
came from: `SettingOrigin::host` for the platform's own setting, or
`SettingOrigin::default_value` for GUI.Forms' stated default of 400 ms
(`default_double_click_milliseconds`). The interval is a perceptual duration,
between 1 and 5000 ms. A report changes only these two fields, so the
application's own presentation settings stay as they are.

| Host | Source | Refreshed |
|---|---|---|
| Windows | `GetDoubleClickTime` | on `WM_SETTINGCHANGE`, when the value moved |
| macOS | `NSEvent.doubleClickInterval` | on each activation, when the value moved (AppKit posts no change notification, and the setting is changed in System Settings) |
| Linux X11 | the XSETTINGS manager's `Net/DoubleClickTime` | on the manager's property change, a new manager, or the manager's exit; without a manager, the 400 ms default, reported as a default |
| Headless | a fixed test value, 500 ms (`host::headless_double_click_milliseconds`) | never |

The X11 host also counts its own clicks with this interval.

## Keyboard-command defaults

Some keys differ by platform convention: closing a document is Ctrl+F4 on
Windows and X11 desktops and Cmd+W on macOS. The core names such keys as
keyboard commands (`KeyCommand`: `close_document`, `next_tab`,
`previous_tab`, `move_tab_later`, `move_tab_earlier`, `cycle_forward`,
`cycle_backward`, `context_menu`, and docking's `next_document`,
`previous_document`, `next_tool_panel`, `previous_tool_panel`,
`position_menu`, `return_to_document`, `hide_tool_panel`,
`maximize_document_area`, `move_panel`), and each host reports the keys its convention
gives them in `HostCapabilities::key_map`; `to_json` lists them as HID usage
and modifier bits. An application has one set of keys for all its windows
(`KeyBindings`, `Application::key_bindings`, owner decision DK-44): the
first host session seeds it with its host's map, and controls ask their
window (`Control::effective_key_map`) rather than naming keys themselves.
An application rebinds a command with `Window::rebind_key_command` from any
of its windows, and the rebinding holds in all of them, over whatever host
attaches.

| Command | Windows, X11, headless (`pc_key_map`) | macOS |
|---|---|---|
| `close_document` | Ctrl+F4 | Cmd+W |
| `next_tab`, `previous_tab` | Ctrl+PageDown, Ctrl+PageUp | the same |
| `move_tab_later`, `move_tab_earlier` | Ctrl+Shift+PageDown, Ctrl+Shift+PageUp | the same |
| `cycle_forward`, `cycle_backward` | Ctrl+Tab, Ctrl+Shift+Tab | the same |
| `context_menu` | Shift+F10, or the context-menu key | the same |
| docking's commands | Ctrl+F6, Ctrl+Shift+F6, Alt+F7, Alt+Shift+F7, Alt+Hyphen, Escape, Shift+Escape, Ctrl+Shift+F12, Ctrl+Alt+Shift+M (move) | the same |

The context-menu key is `PhysicalKey::context_menu` (HID "Keyboard
Application"): `VK_APPS` on Windows and the `Menu` keysym on X11. Apple
keyboards have no such key; the macOS host maps the virtual key code a PC
keyboard's key reports (0x6E), which has not been tested on a Mac.
`PageStrip` and `PageView` use the tab commands, close and the context
menu; `TabControl` uses the tab and cycle commands and leaves close and the
context menu to the controls around it; `DockSite` uses docking's
commands, the cycle commands (its navigator) and the context menu (a
panel's position menu).

## The three native hosts and the headless one

macOS (AppKit)
: `src/host/macos/`, the only place Objective-C++ is allowed. One `NSWindow`
  owns a custom view that presents the CPU raster; a display link drives
  scheduled frames; NSTextInputClient bridges IME composition; Retina scale
  arrives as a scale notification. Pointer capture maps onto the native
  implicit drag sequence rather than a global event tap. Fonts are read from
  the bundle.

Windows (Win32)
: `src/host/windows/`, the only place Win32 and COM types are allowed. An
  ordinary resizable top-level window presents a top-down 32-bit DIB with
  software alpha blending. The process asks for per-monitor DPI awareness
  (`SetProcessDpiAwarenessContext`, resolved at run time, falling back to
  `SetProcessDPIAware`); the scale is the window DPI divided by 96, never
  below 1, and `WM_DPICHANGED` delivers a scale notification and applies the
  suggested window rectangle. The PNG resources the core has already
  validated are decoded by the shared owned PNG decoder, as on every host
  (no WIC). Text comes from the shared engine, not GDI, using the
  bundled faces in `fonts\` beside the executable, or in the directory
  `GUI_FORMS_FONT_DIR` names; with an incomplete font pack the renderer name
  says so and no text metrics provider is installed. An opt-in `WM_COPYDATA`
  automation channel, off unless the host options set `automation_enabled`
  (the gallery sets it from `--automation`), addresses controls by stable
  ID; it accepts commands of at most 4096 bytes with the prefix
  `GUI.Forms.Automation/1`.
  MSAA publication is implemented through `IAccessible`, and UI Automation
  through providers for `UiaRootObjectId` (AutomationId is the stable ID;
  Invoke, Toggle, Value, RangeValue, ExpandCollapse, SelectionItem and
  Selection patterns, and TextPattern and TextPattern2 for a text box's
  text, units, selection, caret and bounds). The adapter advertises `text_composition`, but
  its current input path forwards committed `WM_CHAR` text rather than a TSF
  preedit session. OLE drag/drop and native menu publication are not implemented.

Linux (X11)
: `src/host/linux/`, built with `-DGUI_FORMS_ENABLE_LINUX_HOST=ON`. Xlib
  window and event handling, Xcursor cursors, X11 clipboard selections,
  XDND drag destinations, and native ATK/AT-SPI accessibility publication.
  The publication is checked from an AT-SPI client's side on CI:
  `gui_forms_linux_accessibility_tests` drives a small fixture, and
  `gui_forms_linux_accessibility_gallery_tests` compares every Gallery page
  with the semantic tree (role, name, states, actions, values, children)
  and performs one default action per control type
  ([`results/2026-10-20-a-l2-linux-gallery`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-20-a-l2-linux-gallery)). A text box's text is published
  whole; editable text ranges are not implemented.
  The CPU raster is presented through X11 images. There is no Wayland adapter;
  Wayland sessions run the X11 host through XWayland.

Headless
: `src/host/headless/`, built everywhere and linked by the tests. It emits
  attach, drains one initialization turn, then delivers configured synthetic
  activation and input, producing a byte-stable trace. It is the reference
  ordering every native adapter must match. This adapter is a private build
  target, not an installed SDK component. SDK consumers can link
  `GUIForms::Controls` to test retained models without a native host.

Every native adapter advertises `accessibility`: it publishes the semantic
tree, including editable text ranges, through the platform's own API
(NSAccessibility, MSAA and UI Automation, ATK/AT-SPI), and a client test on
each host reads it back through that API (stage A). The headless adapter
publishes nothing and does not advertise it.

## What stays portable

Event order, focus and capture semantics, the close sequence, dialog request
and result shapes, clipboard payload limits, the scheduler's wake rules and
coordinate conversion up to window client space are defined once in the core.
No adapter may change them. Platform truths that differ (whether scale can
change at run time, whether composition exists, which dialogs are native) are
exposed as capability bits and `HostServiceStatus` results, not as hidden
behaviour differences.

Two consequences are worth knowing. Inside a window, coordinates are the
window's client coordinates; across windows, the windowing seam (below) uses
desktop logical units. And the core never encodes one
platform's assumption about dialogs, paths or menus; a request a host cannot
honour comes back `unsupported` immediately.

## Rendering adapters

The host presents pixels; it does not paint them. Painting goes through the
`Painter` implemented by a private renderer:

- A CPU-only Skia build (no GPU backend, no font backend, PNG as the only
  codec) on macOS and Linux, confined to `src/render/skia/`. PNG decoding
  and encoding on every host use the shared owned codec in
  `src/core/image/` (on stx_zlib), not Skia's or the platform's.
- A DIB compositor on Windows that implements the same blending arithmetic
  byte for byte.

`MetricsSnapshot::renderer_name` names the active one and `cpu_only` is
always true. No Skia type crosses into a public header, a control or a test
of the renderer-neutral core; a boundary test enforces that.

## Multiple windows

`Application::run(std::vector<ApplicationWindow>)` presents up to 64 windows
in one host run, each with its own `Window` model, stable ID, options and an
optional owner ID for tool windows. The `ready` callback hands you an
`ApplicationWindowHandle`, a weak, thread-affine handle for `show`, `hide`,
`toggle_full_screen`, `set_title`, `request_client_size` and `request_close`;
requests after close return `after_shutdown`. `request_client_size` asks the
native window for a client size; the model follows when the host's resize
event arrives, as for a user resize. `Window::resize` only reports a size the
host already has, so calling it from application code leaves the model and
the screen disagreeing. One window's handlers may mutate another window's
model and have it present on the next wake.

### Runtime windows and the desktop

`ApplicationWindowHandle::windows()` returns the run's `ApplicationWindows`
(stage DK decisions 6 and 7). `open_window` opens an owned or tool window
while the application runs and `destroy_window` destroys one without asking
its closing handler; at most 64 windows are live at once, and identities
(`HostWindowId`) are never reused within a run. `ApplicationWindowHandle::
frame` and `request_frame` read and place a window's client rectangle,
`frame_changed` reports every move and resize, `pointer_position` and
`window_at` answer where the pointer is and which of the application's
windows is under a point, and `bring_to_front` orders a window above the
application's other windows without activating it and never above other
applications' windows. Each is a capability bit above; a host that cannot
do one reports `unsupported`.

These requests use desktop logical units: origin at the top left of the
primary monitor, y downwards. macOS points are such units already. Windows
(per-monitor DPI aware) and X11 address their desktops in device pixels;
there each monitor keeps its device origin and its extent is divided by its
scale (`gui_forms/host/windowing/desktop_coordinates.hpp`), and
`ApplicationWindows::monitors` returns the monitors in those units, while
`HostServices::query_monitors` keeps reporting device units. On X11 a frame
is a request to the window manager, which may adjust it; whether XWayland
honours client positions is measured per compositor, not assumed.

The portable rules (identities, bounds, ownership, validation, thread and
shutdown checks) live in the `HostWindowing` base class, so every adapter
applies them the same way; the headless reference host
(`src/host/headless/application/`) implements the same seam on a simulated
desktop for tests.

### Images and theme across windows

Each `Window` has an image registry and a theme. Windows constructed with
the same `ApplicationResources` (stage DK decision 8) share both: an
`ImageId` or an `ImageList` from one is valid in the others, setting the
theme on one sets it on all, and a control subtree moved from one window to
another keeps its images. Build a window opened during the run from the main
window's resources, `Window(root, size, main_window.resources())`. A window
constructed without resources makes its own, so a single-window program, or
windows meant to stay separate, behave as before (a `Button` still refuses
an `ImageList` from a window that does not share its registry). The shared
registry lives until its last window, or other holder, releases it.

### Changing several windows at once, and dragging a window

`WorkspaceTransaction` makes one coherent change across windows (stage DK
decision 9): `include` holds an update scope on each window it changes,
`open_window` creates windows hidden, `destroy_window`, `show_window` and
`defer_notification` queue their work, and `commit` (or the destructor)
lays out each window once, shows the new windows, then destroys the emptied
ones, then runs the notifications once, in order. A panel moving between
two windows is never invisible and observers see only the finished state.

`WindowFrameDrag` is the framework's own capture drag of a window by a drawn
caption (decision 12): every pointer move reads the desktop pointer and
requests the window's frame, so other windows can follow the pointer, and
`cancel` puts the window back. `WindowFrameDrag::mode_for` gives
`native_move_loop` on a host without window frames, the desktop pointer or
pointer capture; the caller then starts the platform's move loop from a
drag region. The drag's statistics say whether the host kept up; per-host
measurements belong in [`results/2026-10-18-dk-w0-capture-drag/`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-18-dk-w0-capture-drag).

### Floating windows

`ApplicationWindow::floating` makes a window a floating tool panel
(`HostWindowFloating::tool`) or a document torn out into its own window
(`HostWindowFloating::document`), owned by the primary window (stage DK
decisions 1, 10 and 11). One rule on every host, decided by the
`HostWindowing` base class from `host_window_kind_traits`:

- no taskbar button, minimized and restored with the primary window, above
  the primary window only and never above other applications' windows;
- a floating tool hides while the application is inactive and returns when
  it is activated (the Mac palette convention); a floating document stays
  visible;
- both move with the primary window, keeping their offset from it, and are
  clamped to the work area of the screen they land on; the offset is
  remembered through clamping, so moving the primary window back restores
  it, and only the user (or a frame request) moving the floating window
  changes it;
- in macOS full screen they join the primary window's space.

`show` and `hide` on a floating window's handle say whether the program
wants it; the policy decides when it is on screen. At most 32 floating
windows are live at once.

| Behaviour | Windows | macOS | X11 |
|---|---|---|---|
| Native object | owned window (`WS_EX_TOOLWINDOW` for a tool) | child window of the primary (`NSPanel` for a tool, with `hidesOnDeactivate` off: the policy hides it) | transient for the primary, `_NET_WM_WINDOW_TYPE_UTILITY`, `_NET_WM_STATE_SKIP_TASKBAR` |
| Activation | `WM_ACTIVATEAPP` | application did become or resign active | any window holding the focus after a turn of events (reported once a focus has been seen) |
| Minimized | `IsIconic` on move and size | did miniaturize and deminiaturize | `_NET_WM_STATE_HIDDEN` |
| Full-screen space | none | `NSWindowCollectionBehaviorFullScreenAuxiliary` | none |

An existing `tool_window` keeps each host's behaviour: on macOS it is still
an `NSPanel` that AppKit hides while the application is inactive, and on
Windows and X11 it does not hide. Decision 11 does apply to it on X11: it is
now typed as a utility window and kept off the taskbar.

## Choosing a host at build time

Enable exactly one native host per build (`GUI_FORMS_ENABLE_MACOS_HOST`,
`GUI_FORMS_ENABLE_WINDOWS_HOST`, `GUI_FORMS_ENABLE_LINUX_HOST`). The installed
`GUIForms::Application` target is that host; a consumer that needs another
platform installs the SDK built there. Renderer-free consumers link
`GUIForms::Controls` and are host independent.
