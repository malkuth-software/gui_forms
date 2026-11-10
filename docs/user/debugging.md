# Debugging redraws

When a window shows stale pixels, flickers, or redraws far more than it
should, the first question is what the window actually presented again, and
why. The invalidation overlay answers it on screen: every area a window
presents again is outlined for 300 ms, in a style that names the cause, with
a legend in the bottom-right corner.

## Turning it on

The overlay is switchable while the program runs; nothing is rebuilt.

| Where | How |
|---|---|
| Environment | Set `GUI_FORMS_DEBUG_INVALIDATION=1` before the program starts. Any value other than empty or `0` turns it on. |
| Whole program (C++) | `Application::set_invalidation_overlay(true)`. Every window and popup window that has no setting of its own shows it, including windows opened later, until it is turned off or the program ends. The free function `set_invalidation_overlay_default(bool)` is the same switch without an `Application`. |
| One window (C++) | `Window::set_invalidation_overlay(true)`, `false`, or `std::nullopt` to follow the program's switch again. A window's own setting wins over the program's. |
| C ABI | `gf_diagnostics_table.set_invalidation_overlay(1)` (since ABI 1.1); `invalidation_overlay(&enabled)` reads it. Any thread. |
| Gallery | The diagnostics panel's **Show invalidation** check box, for the Gallery window. |

A window applies a change from its next damage or paint. While the overlay is
off it costs one relaxed atomic load per damage record and per paint, and
the window keeps no overlay storage.

## Reading the outlines

Each outline marks a rectangle the window presented again, in window
coordinates, and fades out over `invalidation_outline_lifetime` (300 ms).
Where the cause is a control, its stable id is drawn in a small label inside
the outline's top-left corner.

| Cause | Style | Meaning |
|---|---|---|
| control | red, solid | A control invalidated its paint or structure: a property write, a state change, an added or removed child. |
| layout | orange, dashed | A layout change moved or resized something, including a window resize. |
| expose | blue, dotted | The host asked for pixels nothing in the program damaged: an uncovered area, a restore, a theme or scale change at the host. |
| scroll copy | green, long dashes | A scroll moved pixels by copying instead of repainting them; the outline is the copied clip. |
| live surface | magenta, double line | A live surface or sub-surface presented a new frame. |
| external | cyan, double dashed line | External double-buffered content was presented through a compatibility paint endpoint (child-window or GDI drawing, such as the Windows Forms swap-in's shim). |

Typical findings:

- A red outline much larger than the control that changed means its
  `visual_outsets` or its parent's paint dependency is too wide.
- Red outlines that keep appearing while nothing changes mean a control
  invalidates itself every frame (an animation that never stops, or a
  property written with the same value through a path that does not compare).
- Blue outlines on every frame mean the host keeps exposing: the window is
  being covered, or a native child invalidates its parent.
- Glitches inside an area that has no outline mean pixels that should have
  been presented were not: the damage never reached the window.

## How it works

The window records which cause asked for each damaged rectangle. When a
paint succeeds, the painted rectangles are attributed to those causes; a
painted rectangle no damage asked for is a host expose. Live surfaces and
compatibility paint endpoints report their presentations to the window
directly.

The overlay never draws into the window's raster. The host composites it at
present time onto a copy of the presented frame and presents only the areas
it draws on, so the overlay causes no damage of its own and its fading
repaints are never reported. When an outline fades, the host presents that
area again from the raster, so nothing is left behind. The fade is driven at
about 30 frames a second (a timer on Windows and macOS, the event loop's wait
on Linux) and stops with the last outline. On macOS a fade step asks AppKit
to draw the outlined areas; with nothing owed that draw copies the raster and
paints nothing.
