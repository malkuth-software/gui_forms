# Mouse events and context requests

A control raises the Windows Forms mouse events under their Forms names:
`MouseDown`, `Click`, `MouseClick`, `DoubleClick`, `MouseDoubleClick`,
`MouseUp`, `MouseMove`, `MouseWheel`, `MouseEnter`, `MouseHover`,
`MouseLeave` and `MouseCaptureChanged`. It also raises
`ContextMenuRequested`. The window raises every one of them on its UI thread
from the pointer events the host sends (`Window::dispatch_pointer`), so a test
drives them with no host attached.

The core never counts clicks. Every press carries the click count the host
reported, and that count decides between a click and a double-click. The
[per-host table](#what-each-host-offers) lists what each platform reports and
what the Linux host emulates.

## Subscribing

Each event is an accessor on `Control` that returns a typed `Event`.
Subscribe to it as you would to any other event. An owning `Component` keeps
the connection, and disposing that component disconnects it.

| Accessor | Arguments | Forms event |
|---|---|---|
| `mouse_down()` | `const MouseEventArgs&` | `MouseDown` |
| `mouse_up()` | `const MouseEventArgs&` | `MouseUp` |
| `mouse_move()` | `const MouseEventArgs&` | `MouseMove` |
| `mouse_wheel()` | `const MouseEventArgs&` | `MouseWheel` |
| `click()` | none | `Click` |
| `mouse_click()` | `const MouseEventArgs&` | `MouseClick` |
| `double_click()` | none | `DoubleClick` |
| `mouse_double_click()` | `const MouseEventArgs&` | `MouseDoubleClick` |
| `mouse_enter()`, `mouse_hover()`, `mouse_leave()` | none | `MouseEnter`, `MouseHover`, `MouseLeave` |
| `mouse_capture_changed()` | none | `MouseCaptureChanged` |
| `context_menu_requested()` | `ContextMenuRequest&` | none; see [Context requests](#context-requests) |

`MouseEventArgs` holds the same values as Forms' `MouseEventArgs`, with
these fields:

- `button`: the button, or `none` for a move or wheel without one.
- `clicks`: the host's click count for the press. It is 2 for the second
  press of a double-click, and 0 for a move or a wheel. A release carries the
  count of the press it ends.
- `location`: the position in the control's own coordinates.
- `window_position`: the position in the window's coordinates.
- `wheel_delta`: in logical pixels, positive away from the user and to the
  right.
- `modifiers`: the modifier keys held.

A control that has no subscribers stores no event record. The record is made
the first time any of the accessors is called.

Buttons keep their own `clicked()` event. It is raised by the activation that
follows a primary-button `Click`, by `perform_click()`, and by the keyboard.
Subscribe to `clicked()` when you mean "the button was pressed", whatever
caused it. Subscribe to `click()` when you mean the mouse gesture.

## Order

The window raises the events in Forms' order. The tables show what one
control sees for each gesture, with a subscriber on every event.

A single click with the primary button:

| Step | Events |
|---|---|
| press | `MouseDown` (clicks 1) |
| release over the same control | `Click`, `MouseClick`, the activation (`on_activate`, a button's `clicked`), `MouseUp`, `MouseCaptureChanged` |

A double-click on a control with `ControlStyles::standard_double_click`:

| Step | Events |
|---|---|
| first press and release | as a single click |
| second press (clicks 2) | `MouseDown` only |
| second release | `DoubleClick`, `MouseDoubleClick`, `MouseUp`, `MouseCaptureChanged` |

`DoubleClick` arrives on the second release, never on the press, and a
double-click is not an activation. A press whose count is higher than 2 (a
triple click on macOS or Linux) is also a double-click's press.

Other gestures:

- **The release lands elsewhere.** A release that is no longer over the
  pressed control raises only `MouseUp` and `MouseCaptureChanged`. This also
  holds for the second press of a double-click. A press that wanders and comes
  back still clicks.
- **The secondary or middle button.** These buttons click as in Forms:
  `MouseDown`, `Click`, `MouseClick`, `MouseUp`. No capture is taken and no
  activation follows.
- **A button.** `ButtonBase` raises `Click` for the primary button only
  (`clicks_primary_button_only`). It clears `standard_double_click`, so a fast
  second click on a button is a second `Click`.

`MouseEnter` comes before the first `MouseMove` over a control, and
`MouseLeave` comes when the pointer moves to another control or out of the
window. `MouseHover` is raised once per entry, after the pointer rests within
the hover rectangle for the hover time. `MouseCaptureChanged` goes to the
control that loses the capture, after its `MouseUp`.

A handler may close and destroy the window, as a dialog's OK button does.
The window then raises nothing more for that gesture.

## Control styles

Two styles decide the click events, as `ControlStyles.StandardClick` and
`StandardDoubleClick` do in Forms. Set them with `set_style` and read them with
`has_style`.

| `standard_click` | `standard_double_click` | Single click | Second press of a double-click |
|---|---|---|---|
| on | on | `Click`, `MouseClick` | `DoubleClick`, `MouseDoubleClick` |
| on | off | `Click`, `MouseClick` | `Click`, `MouseClick` (clicks 2) |
| off | off | none: only `MouseDown` and `MouseUp` | none |

Without `standard_click` there is also no activation. `MouseDown`, `MouseUp`,
the movement events and the capture event do not depend on either style.

## Context requests

A context request asks the control for its context menu. It comes from the
host only (`HostContextRequestEvent`), and a test sends one with
`Window::dispatch_context_request(origin, position)`. It has an `origin`:

- **A pointer request** goes to the control under the position, or to the
  captured control if one exists.
- **A keyboard request** goes to the focused control. It has no pointer
  position. `location` is the control's own origin, so the control picks its
  anchor, such as its selection or its keyboard item.

The window offers the `ContextMenuRequest` to that control first, then to each
of its ancestors in turn. For each control it calls
`on_context_menu_requested` and then the `context_menu_requested()`
subscribers. The first one that sets `handled` stops the request. The call
returns whether any control handled it.

The core has no key route of its own for context requests. A host whose
platform offers no keyboard request sends none.

## What each host offers

| | Windows | macOS | Linux (X11) | Headless |
|---|---|---|---|---|
| Click count | 2 from `WM_*BUTTONDBLCLK` (`CS_DBLCLKS`), otherwise 1; a third press counts 1 | `NSEvent.clickCount` (1, 2, 3 and more) | **emulated**: counted by the host from each press's time and position, against `Net/DoubleClickTime` and `Net/DoubleClickDistance` (2 pixels when the desktop offers none) | the count in the `PointerEvent` |
| Double-click time | `GetDoubleClickTime` | `NSEvent.doubleClickInterval` | XSETTINGS `Net/DoubleClickTime`, or the 400 ms default | 500 ms |
| `MouseHover` | the platform's `WM_MOUSEHOVER`, using its hover time and rectangle | the window's timer, 400 ms by default | the window's timer, 400 ms by default | the window's timer |
| Pointer context request | `WM_CONTEXTMENU`, after the right button's release | `menuForEvent:` (the right button's press, or Control-click) | **emulated**: the right button's press, reported after the press itself | `dispatch_context_request` |
| Keyboard context request | `WM_CONTEXTMENU` (Shift+F10, the Menu key) | the accessibility show-menu action only (VoiceOver VO+Shift+M) | **emulated**: the Menu key or Shift+F10, when no control handled the key | `dispatch_context_request` |

The X11 protocol has no click count and no context-menu event, so the Linux
host follows the desktop's conventions itself. Because of this, the moment of
a pointer context request differs: Windows sends it after the release, while
macOS and Linux send it on the press. A control that shows its menu from
`ContextMenuRequested`, rather than from `MouseUp`, behaves correctly on all
three. [Hosts and capabilities](hosts-and-capabilities.md#mouse-events-and-context-requests)
describes the settings records behind the table.

## The input state

`Window::interaction_snapshot()` returns a copy of the window's input state:

- the held keys and the modifiers;
- the pointer position, and whether the pointer is inside the window;
- which buttons are down;
- the host's last click count;
- the focused, captured, pressed and hovered controls;
- the hover timing.

The state is updated as each event arrives, before any control sees it, so a
handler reads the state as of the event it handles. `hovered_control()` and
`pressed_control()` return the controls themselves.

## Rules and the mouse events

A window's [rules](rules.md) can trigger on these events
(`trigger::mouse_down`, `trigger::click`, `trigger::context_menu_requested`
and the rest), and their conditions read the input state above
(`condition::hovering`, `condition::pressed` and others).

Rules on `MouseDown`, `MouseUp`, `Click`, `MouseClick`, `DoubleClick` and
`MouseDoubleClick` run before the control's own subscribers, whenever the
rules were added. A consuming rule keeps the event from those subscribers. A
consumed `Click` also keeps a button from activating. The other events
(`MouseUp` after a consumed `Click`, for example) are still raised. A
consuming `ContextMenuRequested` rule marks the request handled, so the
request reaches no ancestor.

## Example

`examples/reference/mouse_events.cpp` builds a window with no host. It
subscribes an owning component to a button's mouse events and to a panel's
context request, then drives a click, a double-click and a keyboard context
request. It also adds a consuming rule and shows that the rule withholds the
click.

```cpp
class Journal final : public gf::Component {
public:
    Journal(gf::Button& ok, gf::Panel& surface) {
        gf::on(ok.mouse_down(), *this, &Journal::down);
        gf::on(ok.click(), *this, &Journal::clicked);
        gf::on(ok.mouse_up(), *this, &Journal::up);
        gf::on(surface.double_click(), *this, &Journal::doubled);
        gf::on(surface.context_menu_requested(), *this, &Journal::menu);
    }
    // ...
};
```
