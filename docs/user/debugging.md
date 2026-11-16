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
| One window (C++) | `Window::set_invalidation_overlay(stxlib::Optional<bool>::of(true))`, `::of(false)`, or `stxlib::Optional<bool>::none()` to follow the program's switch again. A window's own setting wins over the program's. |
| C ABI | `gf_diagnostics_table.set_invalidation_overlay(1)` (since ABI 1.2); `invalidation_overlay(&enabled)` reads it. Any thread. |
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

# Native exceptions in a binding

An exception inside the library that reaches a C ABI function refuses the
call. Since ABI 1.2 the refusing thread also keeps a failure snapshot: the
exception's text, the function, and the shadow frames it was thrown under
(the library calls, events, posted callbacks, layout and paint passes it
unwound). `gf_diagnostics_table.failure_snapshot` copies it out after the
call has returned; `clear_failure_snapshot` forgets it.

A development build of the library also keeps the exception's C++ type and
the native return addresses at the throw (C ABI 1.4):
`gf_diagnostics_table.failure_detail` copies them out for the same failure.
In C#, `GuiFormsException.native_detail` carries them (`exception_type`,
`trace`), also as the `native_exception_type` and `native_trace` entries of
the exception's `Data`; a release build keeps neither, and the property is
null.

| Where | How |
|---|---|
| C ABI | `set_exception_reflection(1)` turns reflection on for the process (it starts off). An exception the library has no status for is then `GF_STATUS_NATIVE_EXCEPTION` instead of `GF_STATUS_INTERNAL`. `raise_native_exception(message)` throws one on purpose, so a binding can test its handling. |
| C# | `NativeException.reflection = true`. `Abi.check` then throws a `NativeException` whose `frames` are the snapshot's frames. It is thrown through one named method per frame (`NativeFrames.library_call`, `NativeFrames.posted_callback`, and so on), so the stack trace lists the library's frames between your call and the throw. |

A native callback entry, such as a window procedure or an AppKit method,
still ends the process when an exception reaches it, because its caller is
foreign code that takes no status. Its report on stderr now prints the
snapshot's frames before the native stack.

# Counting allocations

GUI.Forms counts the allocations of its own counted paths, so a program can
check that a frame, a scroll or a setup step allocates what it should. The
counters are in `gui_forms/allocation.hpp`.

What is counted is GUI.Forms' counted paths and nothing else, each named by
an `AllocationSubsystem`:

| Subsystem | What it counts |
|---|---|
| `image_resources` | the bytes an image registry holds for each image |
| `image_atlas` | an `ImageAtlas`'s tables and staging storage |
| `effects` | an effect instance's state storage |
| `display_chunks` | the command lists of retained display chunks and of the recording painter |
| `damage_lists` | damage rectangles and pending localized paint marks |
| `text_layout` | the paragraph layout's reused storage and the bundled engine's shaping workspace |

The library does not replace or hook the global `operator new`, so the
program's own allocations, the standard library's elsewhere in GUI.Forms,
and those of Skia, FreeType, HarfBuzz and the host toolkits are not counted.
A figure here is never the process total; read that from the platform.

```cpp
#include <gui_forms/allocation.hpp>
#include <gui_forms/window.hpp>

#include <cstdint>

namespace gf = gui_forms;

// The display-chunk allocations the calling thread made in one workload.
std::uint64_t chunk_allocations_during(gf::Window& window) {
    const gf::AllocationScope scope{};
    window.perform_layout();   // the workload under test
    const gf::AllocationStatistics counted = scope.counted();
    return counted.of(gf::AllocationSubsystem::display_chunks).allocations;
}
```

- `AllocationScope` counts what its constructing thread allocates and
  releases through the counted paths from its construction until
  `counted()` is read; scopes nest. `counted()` on another thread throws
  `std::logic_error`.
- `allocation_statistics()` reads the process-wide counters (every thread,
  since the process started) and `thread_allocation_statistics()` the
  calling thread's. Neither is ever reset.
- `AllocationCounts` holds allocations, releases, allocated bytes and
  released bytes. `AllocationStatistics::to_json()` names every subsystem,
  and its `scope` member says that the record covers the counted paths only.

