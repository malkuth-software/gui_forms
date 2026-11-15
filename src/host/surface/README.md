# The single-surface window manager

GUI.Forms' own window manager for hosts with one surface (owner decision,
2026-11-15). Users:
- the browser host (`src/host/web/`), now;
- STXlib's FreeBSD framebuffer appliance, later;
- any later host with one surface and no window manager of its own.

## What a host supplies

`manager/surface_host.hpp` is the whole contract:
- one surface, premultiplied BGRA8, that the manager writes;
- a present call for its damaged device rectangles;
- an input queue of portable records (`input/surface_input.hpp`);
- small services: a title, a cursor (or none: the manager then draws one),
  pointer capture, clipboard writes, the text-input caret, full screen, and
  its own file, folder and colour dialogs if it has them.

`run_surface_application(host, windows, options)` does the rest.

## What the manager does

- **Layers** (`desk/surface_desk.hpp`). Every window of the run is a layer:
  - the primary window fills the surface;
  - owned, tool and floating windows have a frame;
  - popups, menus and tooltips are placed as the program puts them.
- **Frames.** A title bar with minimise, maximise and close boxes, an edge,
  and invisible resize grips on the edges and corners. Windows move by
  dragging the title bar; a double click on it maximises or restores.
- **States.** Normal, minimised and maximised. A minimised window, and the
  windows it owns, leave the drawing; it shows as a tab along the bottom
  edge, and a click on the tab restores it.
- **Focus and stacking.** The active window, raising with owned windows,
  and modal blocking of every window a modal one does not own.
- **The frame theme** (`SurfaceFrameTheme`). The manager's own theme
  specification:
  - the title bar's thickness;
  - an edge or none;
  - where the invisible grips sit, outside and inside the frame;
  - the controls' width and end (the owner's standard is the upper right);
  - the colours.
  A window may also have no title bar at all, when its program draws its
  window controls into its own content.
- **Compositing** (`compose/surface_compose.hpp`). Each window paints into
  its own CPU Skia raster. The manager composes on the CPU, back to front:
  - each frame's edge;
  - the title strip (a small GUI.Forms window, `FrameChrome`);
  - the client raster;
  - the tabs;
  - the drawn cursor.
- **Efficiency.** The manager composes only damage:
  - a window's painted rectangles;
  - the old and new frames of a moved, resized, restacked or restyled layer;
  - the cursor's old and new places.
  Nothing is composed while nothing changed. Display frames are asked for
  only while a live surface animates.

The window manager's further feature, the window capsule, is added on top
of this desk (see the capsule's own files).

## Tests

- `tests/surface_desk_tests.cpp`: the desk, the input queue and rules, the
  compose kernel, device rectangles and cursor sprites.
- `tests/surface_window_manager_tests.cpp`: the whole manager on a test
  host (resize, minimise, maximise and restore, close, focus order, modal
  blocking, damage limits).
