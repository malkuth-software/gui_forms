# Overlays, Popups and Splash

Some content shows apart from a window's ordinary layout: a menu, a
drop-down list, a tooltip, an alert, the splashscreen. GUI.Forms has three
mechanisms for it, named by what they ask of the user and by how they are
made:

| Kind | What it is | It asks the user | Examples |
|---|---|---|---|
| **Overlay** | a control subtree composited into its owner's surface, in the window's own pixels; it cannot extend past the window's edges | depends on the content | anchored overlay panels, toasts, a dialog's modal layer, and every menu, drop-down and tooltip where no Popup or Splash window can open |
| **Popup** | its own undecorated native window, owned by the window that opens it, with per-pixel alpha and a shadow GUI.Forms paints | a decision: it says hello, you answer it, it goes away | a `TaskDialog` or `FontDialog` panel, menus, drop-down lists and pickers |
| **Splash** | a headless, lightweight window with no chrome and no input of its own, kept warm so that showing it is cheap | nothing | tooltips; the splashscreen while a program starts |

The content is the same in each case. A menu's rows, a tooltip's bubble and
a dialog's panel are retained controls in their owner's tree, styled by the
owner's theme. Only the presentation differs: composited into the owner, or
handed to a Popup or Splash window. That is why the overlay is also every
Popup's and Splash's fallback, and not a second implementation.

## Overlays

`SurfaceRoot::open_overlay(owner, overlay, options)` attaches `overlay`, a
live control with no parent and no window, as a separate retained root of
the owner's window. It joins the window's lifetime, focus, semantics and
dispatch, but no layout container places it. The returned `OverlayToken`
keeps it open:

```cpp
#include <gui_forms/gui_forms.hpp>

namespace gf = gui_forms;

class ColourPicker final : public gf::Component {
public:
    explicit ColourPicker(gf::Window& window) : window_(window) {}

    void open(const gf::Control::Ptr& anchor, const gf::Control::Ptr& panel) {
        overlay_ = window_.open_overlay(anchor, panel);   // panel: a live, detached root
    }

    void close() noexcept { overlay_.disconnect(); }

private:
    gf::Window& window_;
    gf::OverlayToken overlay_{};   // destroying the token closes the overlay
};
```

- **The owner** must be a live, visible control of the window, and enabled
  unless `OverlayOptions::require_enabled_owner` is false (a passive
  overlay, such as a tooltip). The overlay closes when its owner goes.
- **Closing** the overlay detaches its subtree again, so the caller may open
  it later. `OverlayToken::closed_event()` reports the close.
- **Focus.** An interactive overlay opens a focus scope
  (`begin_focus_scope`, [Input and focus](input-and-focus)); Escape and a
  click outside close the stock menus and pickers and give focus back to
  the scope that opened them.
- **Toasts** are overlays too: `Window::show_toast(owner, content,
  duration)` shows one at a time, above the bottom of the client area.
- **Sub-surfaces** keep their overlays inside their own region.

`AnchoredOverlayLayer` is a stock control for panels anchored to another
control, and `ContextMenu::show(owner, window_position, mode)` opens a menu.

## Popups

A Popup forces a decision. GUI.Forms opens one in three ways:

- **Owned dialogs.** `TaskDialog` and `FontDialog` take
  `set_surface(gf::DialogSurface::native_popup)`. Their panel then opens in
  a Popup window centred over the owner's client area, while the modal
  layer that blocks the rest of the owner stays in the owner's window. The
  default, `DialogSurface::overlay`, keeps the panel in the window.
- **Menus, drop-downs and pickers.** These open in warm Popup windows when
  the run can, so a menu or a drop-down list may reach past the window onto
  the desktop. A warm Popup window never takes the focus: keys still reach
  the owner's window, and the control that opened the Popup forwards them
  to it. The program does nothing to choose this.
- **Your own content.** `Window::open_native_popup(owner, content, bounds,
  options)` opens a live detached root in a Popup window at `bounds` in the
  window's client area, kept inside its monitor's work area:

```cpp
gf::NativePopupToken show_details(gf::Window& window, const gf::Control::Ptr& anchor,
                                  const gf::Control::Ptr& details) {
    gf::NativePopupOptions options{};
    options.focus = gf::HostPopupFocus::takes_focus;       // declared per popup
    options.fallback = gf::NativePopupFallback::overlay;   // the default
    gf::NativePopupToken popup =
        window.open_native_popup(anchor, details, gf::Rect{40.0, 60.0, 320.0, 200.0}, options);
    if (popup.surface() == gf::NativePopupSurface::overlay) {
        // No Popup window: popup.refusal() says why (no_run, unsupported or
        // host_refused), and the content opened as an overlay instead.
    }
    return popup;   // destroying the token closes the popup
}
```

- **Fallback.** Outside `Application::run`, on a host without Popup windows,
  or when the host refuses one, the content opens as an overlay at the same
  bounds, or nothing opens when `fallback` is `refuse`.
- **Only a window** opens Popups. A sub-surface is not a window.
- **Backdrop.** `NativePopupOptions::backdrop` gives the Popup window its own
  backdrop chain ([Transparency, glass and reflections](transparency)).

## Splash

A Splash forces no decision. It has no surface root, layout pass or theme
state of its own: the owner measures and paints the content, and the Splash
window only presents it. Its kinds are the menu, the tooltip and the
splashscreen; in 0.5, tooltips and the splashscreen are Splash windows, and
menus use the warm Popup windows above.

### Tooltips

A tooltip is plain, translatable text that belongs to its control:

```cpp
(*save).tool_tip_text = "Save the document";
```

Each window shows tooltips through one presenter, with the timing every
`ToolTip` component of the window shares. Where the host can, the bubble is
shown in a warm Splash window beside the window, with the library's shadow
and fade; elsewhere it is a passive overlay. It takes no input either way.
`Window::set_tool_tips_shown` and `Application::set_tool_tips_shown` turn
tooltips off.

A tooltip's colours are the theme's `tool-tip` family: every stock theme
draws black text on a light plate. The bubble is not a child of the control
it describes, so a `fore_color` or `back_color` set on the window, a panel or
the control does not reach it (as a Windows Forms `ToolTip` inherits no
`ForeColor`). Restyle tooltips in the theme, with a `:family(tool-tip)` rule;
no `!important` is needed to keep the text black under an authored
`ForeColor`.

### The splashscreen

`Splashscreen::show` opens a borderless, centred window that takes no input,
before the application exists. It shows the program's artwork (a PNG whose
pixel size is the window's size, 160 to 1600 on each side), one status line
and an optional progress bar, in fixed built-in styling. It never waits for
the theme compile:

```cpp
#include <gui_forms/application.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace gf = gui_forms;

std::vector<std::byte> splash_artwork();   // the program's own PNG bytes

int start() {
    gf::SplashscreenOptions splash_options{};
    splash_options.artwork = splash_artwork();
    splash_options.status = "Starting";
    gf::SplashscreenResult splash = gf::Splashscreen::show(std::move(splash_options));
    // splash is empty when it was refused: invalid_argument, already_shown
    // or unsupported (no display). The program goes on without one.

    if (splash) {
        (*splash.splashscreen).set_status("Compiling the theme");
    }
    gf::ApplicationConfig config{};
    gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
    if (!created) return 1;
    // ... make the first window and run it: the splashscreen closes when that
    // window is ready, or earlier through close() or its destruction.
    return 0;
}
```

There is one splashscreen at a time, on the thread that later runs the
application.

## Where each one opens

Each host reports what it can present as the `transient_surfaces`
capability ([Hosts and capabilities](hosts-and-capabilities)). The rules that
decide placement, validity and animation are the same on every host; only
the presentation target differs.

| Where | Menus, drop-downs, pickers | Tooltips | Splashscreen |
|---|---|---|---|
| macOS, Windows | Popup windows | Splash windows | yes |
| X11 with a compositing manager | Popup windows | Splash windows | yes |
| X11 without one | opaque Popup windows, with no shadow or fade (`opaque_transient_surfaces`) | opaque Splash windows | yes |
| outside a run, headless hosts, sub-surfaces, tests | overlays | overlays | none |

`Window::popup_windows_available()` and `popup_windows_translucent()` say
which applies to a window during a run.

## Styling

A menu, a tooltip or a dialog panel is the owner's control subtree, so the
owner's theme styles it with ordinary control rules and the `tool-tip` and
`menu-item` families ([The theme language](theme-language.md#selectors)).
The theme language's `popup` surface selector is compiled and held for
Popup windows' backdrops; see its section for what applies today.
