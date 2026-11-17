# Presentation

GUI.Forms paints every window on the CPU into its own raster. *Presentation*
is the last step: getting the damaged pixels of that raster onto the screen.
The window systems differ here more than anywhere else, so the hosts share
one contract for it, the presentation shim (`src/core/host/presentation/`).
It has a safe path that always works and an optional faster one, and it
falls back from the second to the first without the user seeing a
difference.

## Two paths

**The copy path** is the safe fallback, and every host has one. The host
copies the frame's damaged rectangles into the window system's own
storage:

| Host | Copy path |
|---|---|
| macOS | a CoreGraphics draw of the raster into AppKit's backing store, clipped to the rectangles being drawn |
| Linux X11 | conversion into an `XImage`, sent with `XShmPutImage` where MIT-SHM works and `XPutImage` otherwise |
| Windows | a DIB blit (unchanged; its shim backend is the Windows side's, see below) |
| The window manager | none: its surface is its only path |

**The direct path** exists where the window system gives the program a
buffer to write into and presents that buffer itself:

| Host | Direct path |
|---|---|
| macOS | IOSurfaces as the contents of a layer over the view, locked for CPU writing |
| Linux X11 | MIT-SHM pixmaps presented with the Present extension (`XPresentPixmap`) |
| Linux Wayland | `wl_shm` buffers attached to the surface with `wl_surface_damage_buffer` (written; GUI.Forms has no Wayland host yet) |
| The window manager | the surface its host supplies (the reference) |
| Windows | to be decided by the Windows side's study |

A direct buffer may live in GPU memory. GUI.Forms treats it as a *blind
interface*: it locks or maps the buffer, writes CPU pixels, unlocks it and
asks the window system to present it. GUI.Forms never draws on the GPU,
compiles a shader, encodes or submits GPU work, or creates a rendering
context, on any path.

Some platforms hand out such buffers only through a graphics API. Those
*buffer routes* are allowed as enumerated host capabilities (owner,
2026-11-16): the API is used only to obtain, map or lock, and present the
buffer, inside the host adapter, behind its own build option, reported by
its own capability bit, with the copy path as the fallback on any refusal
or loss. Nothing of them appears in the portable contract, the public
headers or the C ABI beyond the capability bits.

## The contract

A host implements one or both of two interfaces
(`presentation_shim.hpp`):

- `PresentationCopyPath`: `copy(source, rects)` copies rectangles of the
  frame to the window; `scroll(plan)` moves pixels it already shows, where
  `scrolls()` says it can.
- `PresentationDirectPath`: `acquire(extent)` grants a CPU-writable buffer
  or refuses; `present(damage)` hands the written buffer to the window
  system with what changed on screen; `release()` gives every buffer back
  and stops showing them.

A `PresentationShim` drives them. For each frame the host gives it the
complete raster, the damage, and the scrolls that moved the raster's pixels
(`PresentFrame`). The shim then:

1. **Acquires** a direct buffer, when the direct path is usable.
2. **Scrolls**: moves the pixels already presented, where it can (below).
3. **Writes** the damage into the buffer. A buffer reports its *age*: 1
   when it holds the frame presented last, *n* when it was presented *n*
   presents ago, 0 when its contents are unknown. The shim keeps the
   damage of the last four frames (`PresentDamageHistory`) and writes
   exactly what the buffer is missing: the frame's damage for age 1, plus
   the frames since for an older one, the whole frame for age 0.
4. **Presents** with the damage that changed on screen, whatever the age.
5. **Releases** the buffers when it falls back, so that the copy path's
   pixels show.

Without a usable direct path, steps 1 to 4 run on the copy path: scroll,
then copy the damage, then report.

### Refusal, loss and the fallback

A direct path may refuse a buffer (`DirectRefusal`) or lose its buffers,
at a present or between frames (`notify_lost`). Either way the frame is
presented by the copy path at once, with the same pixels: after the direct
path has shown anything, the copy path writes the whole frame, so nothing
the direct path showed survives.

| Refusal or event | This frame | Later frames |
|---|---|---|
| `unsupported` (a remote display, an unwritable visual, no facility) | copy | copy for the window's life |
| `extent` (the size is declined) | copy | copy until the size changes, then direct again |
| `resources`, `busy` | copy | copy for 120 frames, then direct again |
| lost (at a present, while acquiring, or between frames) | copy, whole frame | copy for 120 frames, then direct again; after three losses in a row, copy for the window's life |
| a failed direct present | copy, whole frame | copy for 120 frames, then direct again |

While a present is still being read by the window system (`pending`,
ended by `notify_released`), the shim writes nothing and the host keeps its
damage. An `inactive` or `failed` outcome also leaves the damage with the
host, which presents it again.

### Host scroll

When a scroll only moves a viewport's children by whole device pixels, the
engine records a *scroll copy* instead of damaging the viewport
(`SurfaceRoot::request_scroll_copy`): the host moves the raster's pixels
and only the exposed band is repainted. Host scroll carries the same saving
to the screen: the window system moves the pixels it already shows, and
only the exposed band is copied to it.

A scroll's arithmetic is `plan_present_scroll`: the area clipped to the
buffer, the source and destination of the moved pixels, and at most two
exposed bands (a full-width band on the side the rows left and, beside it,
one on the side the columns left). The destination and the bands partition
the area. An offset as large as the area moves nothing and exposes all of
it.

Pixels are moved only while what is shown is exactly the frame before:

- on the copy path, after a presented copy, on the same path, until the
  host reports that the window's contents were lost (`notify_copy_contents_lost`,
  an expose without backing store);
- on the direct path, inside a buffer of age 1. An older buffer has the
  scroll's area written from the frame instead (which costs what copying
  would; the window system is still told the whole area changed).

Otherwise the scroll is *refused* and its area joins the damage. That is
always correct, because the raster holds the whole frame.

| Host | Host scroll |
|---|---|
| Linux X11, copy path | `XCopyArea` within the window, while a compositing manager keeps the window's contents |
| Linux X11, Present path | in place when the buffer's age is 1 |
| Windows, copy path | `ScrollWindowEx` within the window, no GPU API; a move whose uncovered region reaches past the exposed bands (part of the area clipped, covered or off screen) is refused, as is any move under a child window or while the invalidation overlay is drawn |
| macOS | none yet: AppKit's backing store is not moved, and the view names AppKit's drawn rectangles as the damage, which already include the copies' clips, so it passes no scrolls |
| Wayland | in place when the buffer's age is 1, once a host passes its scrolls |
| The window manager | moves its surface's pixels for a window whose layer is opaque and uncovered over the scroll |

### Event order

Every step is recorded in the shim's trace (`PresentTrace`, a fixed ring,
off until enabled) and every path's trace follows one order. Per frame:

```text
frame       := [resume] [direct | refused] [copy] [between]
direct      := acquire scroll-step* damage present
               [lost [release] fallback copy]   after a lost outcome
               [fallback copy]                  may follow a failed one
refused     := refuse [release] [fallback copy]
copy        := scroll-step* damage present
scroll-step := scroll | scroll_refused
between     := lost | release | released_by_host
```

`check_present_trace` checks a trace against this order, and
`present_traces_equivalent` compares two traces of one scenario (the same
steps, outcomes and refusals; buffer ages and rectangle counts may differ,
since platforms keep different numbers of buffers and merge damage
differently).

GUI.Forms' own window manager (`src/host/surface/`) is the **reference**:
it implements the direct path in-process over its host's surface, and its
traces define the behaviour that the platform paths' traces are compared
against.

### One execution context

A shim, its paths and its trace belong to the thread that paints the
window: the host's UI thread. Every call is made there. The window
system's loss and release notices arrive in the host's event loop and are
forwarded from there; nothing in the shim is synchronized, because nothing
in it is shared. The frame is borrowed for one call, and a direct buffer is
borrowed between acquire and present.

## Capabilities

Three host capability bits describe presentation (`HostCapability`; in the
C ABI `GF_HOST_CAPABILITY_DIRECT_BUFFERS`, `_HOST_SCROLL` and
`_REFERENCE_PRESENTATION`):

- `direct_buffers`: the host presents through the direct path, with the
  copy path as its fallback;
- `host_scroll`: the host moves presented pixels for scroll copies;
- `reference_presentation`: the host is GUI.Forms' own window manager.

Each is reported only where the host implements it and its presentation
policy turns it on. While each platform's direct path and host scroll are
being verified, both are off by default and the hosts present exactly as
before. They are turned on per process with the environment variable
`GUI_FORMS_PRESENT`, a comma-separated list of `direct` and `scroll`:

```sh
GUI_FORMS_PRESENT=direct,scroll ./my_program
```

The X11 host passes its scroll copies, in device pixels, only while every
copy moved the raster; its present then names the screen without the
copies' clips, and the shim adds back the area of any scroll it refuses.

## Platform paths

| Path | Buffers | Refusals |
|---|---|---|
| macOS IOSurface (`src/surface/macos/view/macos_iosurface_present.mm`) | up to three BGRA IOSurfaces, `IOSurfaceLock` for writing, set as the contents of a layer beneath the compositor's sublayers | busy while every surface is in use (`IOSurfaceIsInUse`) or shown; resources when one cannot be made; lost when one cannot be locked |
| X11 Present (`src/surface/linux/application/x11_present_direct.cpp`) | up to three MIT-SHM pixmaps, presented with `XPresentPixmap` and an update region; reused after `PresentIdleNotify` | unsupported without libXpresent, MIT-SHM pixmaps or Present, on a remote display, on a visual not in the raster's layout, and on a 32-bit window presenting opaque; busy while every pixmap is read; resources when a segment cannot be made |
| Wayland `wl_shm` (`src/surface/linux/wayland/wayland_shm_present.cpp`, built with `GUI_FORMS_ENABLE_WAYLAND_PRESENT`) | up to three memfd buffers, ARGB8888 or XRGB8888, attached with `wl_surface_damage_buffer`; reused after `wl_buffer.release` | busy while every buffer is held; resources when one cannot be made; lost when the display is in error or a flush fails |

### Buffer routes

| Route | Capability | Build option and policy | What the API is used for |
|---|---|---|---|
| macOS `CAMetalLayer` (`macos_metal_present.mm`) | `metal_drawables` | `GUI_FORMS_ENABLE_METAL_PRESENT`; `GUI_FORMS_PRESENT=direct,metal` | the system default device for the layer, `nextDrawable` (only while a drawable is free), the drawable texture's `iosurface` (locked for writing), `-[CAMetalDrawable present]`; no command buffer |
| X11 DRI3 (`x11_dri3_present.cpp`, `src/surface/linux/gbm/`) | `dri3_pixmaps` | `GUI_FORMS_ENABLE_X11_DRI3_PRESENT` (with libXpresent); `GUI_FORMS_PRESENT=direct,dri3` | `DRI3Open` for the server's device, linear GBM buffers mapped for writing (age 0) and exported, `DRI3PixmapFromBuffer`, then `XPresentPixmap` |
| Wayland linux-dmabuf (`wayland_dmabuf_present.cpp`) | `linux_dmabuf` (reserved) | `GUI_FORMS_ENABLE_WAYLAND_DMABUF_PRESENT`; `direct,dmabuf` for a future Wayland host | linear GBM buffers on the compositor's render node, mapped and exported, `zwp_linux_dmabuf_v1` `create_immed` |
| Windows DXGI flip model or DirectComposition | to be added by the Windows side | the Windows side's | the Windows side's study (below) |

Two rules hold for every route, as for every direct path:

- **A route never waits for a buffer.** "No buffer ready" is a refusal
  (`busy`) for that frame, never a block on the UI thread: the frame is
  presented by the copy path at once with the same pixels, and the shim's
  pause follows (the table above). The Metal route chose this over a
  separate acquiring context: `nextDrawable` blocks while every drawable
  of the layer's pool is held, so before calling it the route counts the
  drawables the window server still holds (each tracked present's
  `presentedTime`, which is 0 until it reaches the screen; a present
  followed by a later displayed one has been replaced and is forgotten:
  `count_present_holds`) and refuses busy, without calling it, when the
  pool is held. The layer's one-second `nextDrawable` timeout stays only as
  a backstop. There is no asynchronous work, so nothing is cancelled: a
  release or the window's destruction only drops the route's references,
  and the window server finishes showing what it has.
- **A route claims only the contents it knows.** A buffer's age is what the
  route can prove of its contents. GBM mappings (the DRI3 and linux-dmabuf
  routes) do not promise the buffer's earlier contents, since a driver may
  map a staging copy that is not read back, and GBM reports no age; so
  those routes map for writing only and report age 0, and the shim writes
  the whole frame from the raster each time (`gbm_buffer_known_age` is
  where a driver's own report would answer). Where an age is known, the
  shim writes the union of the damage since that buffer was last written
  (`PresentDamageHistory`); an age older than the history kept is
  unknown.

Each route reports `direct_buffers` with its own bit. Where a route is not
built, or the policy does not name it, the host uses its non-API direct
path (IOSurface, MIT-SHM with Present, `wl_shm`) under
`GUI_FORMS_PRESENT=direct`, or the copy path. Every refusal and loss of a
route takes the same fallback as any direct path, with the same pixels.

## What a new platform backend provides

A backend, the Windows one included, supplies:

- a copy path: its existing present, taking device rectangles of the
  frame, reporting `presented`, `inactive` or `failed`, and, if the window
  system can move its own pixels, `scroll`;
- optionally a direct path: buffers the window system presents, each
  writable by the CPU between acquire and present, with an honest age; a
  refusal named by kind; a loss detected at present or reported between
  frames; and `release` making the copy path's pixels visible again;
- the capability bits, only for what it implements;
- a graphics API only inside the host adapter, only to obtain, map or lock
  and present a buffer, behind its own build option.

The Windows backend (the Windows side's item 14) must report:

- `direct_buffers` only while a direct path is in use for the process
  (built, and named by `GUI_FORMS_PRESENT=direct`);
- one new capability bit per buffer route it adds (for example DXGI flip
  model or DirectComposition), reserved in fold order after
  `linux_dmabuf` with its `GF_HOST_CAPABILITY_*` constant, its manifest
  entry, its name in the capability matrix and a row here and in
  [Hosts and capabilities](hosts-and-capabilities.md), reported only where
  it is built and the policy names it;
- `host_scroll` only where its copy path or direct path moves presented
  pixels (for example `ScrollWindowEx` or `IDXGISwapChain1::Present1`'s
  scroll rectangle);
- its refusals by kind and its losses (device removal, occlusion, a lost
  swap chain), so that the shim's trace matches the reference's.

The Windows host's existing DIB path is its copy path. With
`GUI_FORMS_PRESENT=scroll` it presents through the shim and reports
`host_scroll` (`ScrollWindowEx`, `src/surface/windows/raster/window_scroll.hpp`);
it reports no direct path and no buffer route (owner, 2026-11-16: no GPU
surfaces).
