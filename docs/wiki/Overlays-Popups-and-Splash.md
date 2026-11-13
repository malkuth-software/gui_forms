# Overlays, Popups and Splash

Content that shows apart from a window's layout takes one of three forms:

| Kind | What it is | Examples |
|---|---|---|
| **Overlay** | a control subtree composited into its owner, inside the window's edges | anchored panels, toasts, a dialog's modal layer; every menu and tooltip where no Popup or Splash window can open |
| **Popup** | an undecorated native window owned by a window; it forces a decision | `TaskDialog` and `FontDialog` panels, menus, drop-down lists and pickers |
| **Splash** | a headless, lightweight window that forces nothing | tooltips, the splashscreen |

The content is always the owner's retained controls, styled by the owner's
theme; only the presentation differs. The overlay is every Popup's and
Splash's fallback: outside a run, on a host without the
`transient_surfaces` capability, in sub-surfaces and in tests.

```cpp
gf::OverlayToken overlay = window.open_overlay(anchor, panel);      // destroying the token closes it

(*confirm).set_surface(gf::DialogSurface::native_popup);           // a TaskDialog's panel in a Popup window

gf::NativePopupOptions options{};
options.focus = gf::HostPopupFocus::takes_focus;
gf::NativePopupToken popup = window.open_native_popup(anchor, details, gf::Rect{40.0, 60.0, 320.0, 200.0}, options);

(*save).tool_tip_text = "Save the document";                       // shown by the window's tooltip presenter

gf::SplashscreenOptions splash_options{};
splash_options.artwork = splash_artwork();                         // PNG bytes; before Application::create
gf::SplashscreenResult splash = gf::Splashscreen::show(std::move(splash_options));
```

- Menus, drop-downs and pickers use warm Popup windows by themselves, which
  never take the keyboard focus from their owner.
- Tooltips take no input. Turn them off with `Window::set_tool_tips_shown`
  or `Application::set_tool_tips_shown`.
- A tooltip's colours are the theme's `tool-tip` family (black text on a
  light plate in every stock theme); an authored `fore_color` or
  `back_color` on the window or its panels does not reach it.
- The splashscreen shows the program's artwork, a status line and an
  optional progress bar, and closes when the first window of the run is
  ready.
- X11 without a compositing manager shows Popups and Splash windows opaque,
  without shadow or fade.

Full page: [Overlays, Popups and Splash](https://gui-forms.readthedocs.io/user/overlays-popups-and-splash.html).
