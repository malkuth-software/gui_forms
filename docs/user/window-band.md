# The window band

A window band is a thin strip that a top-level window reserves along the edges of its client area. The window draws it above every paint plane, overlays included, and the band hits nothing. Its use is to show the user something about the whole window: focus, attention, or a status that an outside shim draws into the reserved space. It is part of the application's own drawing, so it cannot vouch for sandboxing. Only a frame drawn from outside the process can do that.

## Declaring it

```cpp
WindowBandOptions options{};
options.width = 2.0;                                  // every side, logical units
options.full_screen = WindowBandFullScreen::hides;    // or stays (the default)
window.declare_band(options);

// One width per edge instead; an edge of 0 reserves nothing there.
options.edges = WindowBandEdges{0.0, 6.0, 0.0, 2.0};  // left, top, right, bottom
window.declare_band(options);
```

The reservation is static while the band is declared:

- The root is measured against the client area less the band, and arranged inside it.
- Every coordinate derived from arranged bounds follows the inset. That includes absolute bounds, `point_to_window`, accessibility frames and hit tests.
- `Window::content_rect()` is the client area less the band.
- Only these changes lay the window out again:
  - declaring or undeclaring the band;
  - a theme or high-contrast change of its width;
  - a scale change that snaps it to another width;
  - entering or leaving full screen with `WindowBandFullScreen::hides`.

Each width is snapped to whole device pixels at the window's scale (at least one), so content inside the band stays on the device grid. A 1-unit band is one device pixel at 1.25 (0.8 units) and two at 1.5 (1.33 units). Widths run from 0 to `maximum_band_width` (32).

## What it shows

`Window::set_band_appearance` changes only paint: the four strips repaint, and nothing is laid out.

| Field | Meaning |
|---|---|
| `state` | `rest` (the rest colour, transparent by default), `lit`, `flashing` (alternates lit and rest), `cycling` (blends between them along a cosine, starting lit) |
| `rest_colour`, `lit_colour` | the application's colours. Each one that is set replaces the theme's. |
| `lit_role` | the theme colour the band lights in when no lit colour is given: `theme` (the theme's `band-lit-color`, else the focus ring's), `focus`, `accent`, `accent_light`, `text`, `border` |
| `frequency` | cycles a second of a flash or a cycle. 0 holds the band lit. |
| `texture` | a procedural pattern or a surface finish drawn under the colour, one surface per strip. A finish may not be the glint. |

The engine's limiter is the one the control border uses:

- At most one cycle a second (two luminance transitions).
- A saturated red never flashes.
- The band rests lit under reduced motion, with ambient motion off, and in an occluded window.
- High contrast draws the palette's focus colour, solid and untextured, at least its focus width wide on each edge that has a width.

`Window::band_snapshot()` reports what the band is doing now: the reserved edges, the drawn colour, whether a flash or cycle runs and at what frequency, whether a texture is drawn, and the corners it follows.

## Corners and the host

`Window::set_corner_preference` asks the host for `system`, `square` or `round` corners. The band follows what the host reports (`band_snapshot().corners`), not the preference. `Window::shaping_report()` says what the attached host lets the band rely on:

- whether it reports corners;
- which shapes it may report;
- whether it reports full screen;
- whether it can draw a native border;
- the corners now, and whether they are as asked.

| Host | Corners | Full screen | Native border |
|---|---|---|---|
| Windows 11 | shaped and reported: round (8 px) or square on request; square when maximized, snapped or full screen | reported | not yet (contract below) |
| Windows 10 | square, reported as not complied for `round` | reported | no |
| macOS | reported only. AppKit has no public API to shape a window's corners or report their radius, so the report is square in full screen and for a borderless window, and unknown otherwise. `round` is never complied; `square` only where the corners are square. | reported | no |
| Linux (X11) | not reported. The window manager or compositor decides, and the band draws square. | reported | no |

A rounded band is one stroke along the window's outline. It is drawn only when every edge has the same width and there is no texture; otherwise the band draws square strips.

## The C ABI

The windows table (since 1.5) has these slots:

- `declare_band` (`gf_window_band_options`)
- `undeclare_band`
- `set_band_appearance` (`gf_window_band_appearance`)
- `band_report` (`gf_window_band_report`)
- `set_corner_preference`
- `content_rect`
- `shaping` (`gf_window_shaping`)

Each takes a window from `windows.create`, or any control in a window. Records are checked whole before anything changes. The host's corner shapes are in `gf_host_capabilities.window_corner_shapes`.

## Windows 11: the contract for the native border (hand-back)

The Windows host sets the corner preference only; it does not yet set the border colour. This is the contract its implementation follows:

1. **Corner preference (implemented).**
   - `DWMWA_WINDOW_CORNER_PREFERENCE` (33) is set from the window's preference: `system` gives `DWMWCP_DEFAULT`, `square` gives `DWMWCP_DONOTROUND`, `round` gives `DWMWCP_ROUND`.
   - The report is rounded with radius 8 (logical) for default and round, and square for do-not-round.
   - A maximized, snapped or full-screen window reports square.
   - Before build 22000 the attribute is refused: square, with `complied` false for `round`.
2. **Native border colour (to implement).**
   - A band whose drawn colour is a static solid may also be mirrored to `DWMWA_BORDER_COLOR` (34) as a COLORREF. DWM then draws it outside the client area, with no reservation.
   - When to mirror:
     - Only while the band is declared, reserved, `lit` or `rest` with an opaque colour, untextured, and not high contrast.
     - Never for `flashing` or `cycling`. Repeated attribute calls are not a supported animation path.
   - Turning it off: `DWMWA_COLOR_DEFAULT` (0xFFFFFFFF) when the band is undeclared or starts to move.
   - Reporting:
     - The host reports `WindowShapingReport::native_border` true only on build 22000 or later.
     - It adds a `native_border` capability only once this is implemented.
   - The native border never replaces the reserved band: content stays out of the reservation either way.
   - It is not a trust signal: the application sets the attribute itself.
3. **Border thickness (to report).**
   - `DWMWA_VISIBLE_FRAME_BORDER_THICKNESS` (37) reads DWM's own border width.
   - The host may report it beside the corners, so that a band can sit flush inside it.
   - It changes no layout.
4. **Order of calls.**
   - The corner preference is applied at attach and whenever the session asks again: on each `HostWindowStateEvent` and on each preference change.
   - The border colour would be applied when the band's drawn colour changes while it is static, from the window's band refresh, on the UI thread.
