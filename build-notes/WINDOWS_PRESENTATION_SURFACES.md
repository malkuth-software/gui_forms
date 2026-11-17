# Windows presentation surfaces (handoff item 14, study)

Written in `claude/windows-present-surfaces` (base `checkpoint/2026-11-15-cp32`,
3c40164eb) for `planning/` on the notes `gui_forms` branch, like
`BLENDING_RULE.md`. It answers handoff item 14 against the blind-buffer rule in
`desks/coordinator/PRESENTATION_AND_BLENDING_PLAN.md`: GUI.Forms writes CPU
pixels into a buffer the OS gives it and the OS presents it; it never issues GPU
commands, compiles shaders, or creates a GPU device or context of its own; every
direct path is capability-reported; refusal or loss falls back to the safe copy
path with the same pixels; any route that needs a GPU API symbol to obtain the
buffer is listed for the owner before it is written. The Mac side's
`claude/present-shim-1` was not on GitHub when this was written
(`git ls-remote ... 'refs/heads/claude/present*'` empty, 2026-11-16), so the
study does not map routes onto the shim's names.

Every route was run as a program, not only read about. The lab lives outside
gui_forms in `D:/gw_present_lab` (its own CMake project, pinned CLANG64 /
LLVM 22.1.8, static link). Its source was removed from gui_forms (owner,
2026-11-17), so no Direct3D/DXGI/DComp code is in the repository; its raw output
is kept as `build-notes/WINDOWS_PRESENTATION_SURFACES_results.txt`.

## 0. Summary

- **No documented Windows route hands GUI.Forms a CPU pointer into a buffer the
  compositor reads.** Measured on Windows 11 26200: every route copies at least
  once after the CPU raster. The routes differ in who makes that copy (GDI, the
  display driver through `WriteToSubresource`, or a GPU copy the program
  issues) and in whether a Direct3D 11 device must exist.
- **Without any GPU API symbol:** DIB section + BitBlt (today's baseline),
  StretchDIBits from plain memory (today's band path), UpdateLayeredWindow, and
  buffered paint. They all composed correctly here, including the
  damage-limited updates, and ScrollWindowEx host scroll worked.
- **The best "direct" candidates both need a D3D11 device:** composition
  textures (Windows 11 22H2+, with dirty rectangles from 24H2) and the
  Composition Swapchain (Windows 11 22000.194+, WDDM 2.0). Each accepted a
  default-usage texture that the CPU writes through `Map` +
  `ID3D11Device3::WriteToSubresource`, with no draw, shader or copy command
  (needs `MapOnDefaultTextures`, which this NVIDIA adapter reports). Both
  presented the pattern exactly. They still create a GPU device, so the rule
  as written forbids them; they are listed for the owner (section 8).
- **Recommendation:** keep DIB + BitBlt as the Windows safe path and the
  default; add ScrollWindowEx host scroll and a ULW route for alpha popups now,
  since neither needs a GPU symbol. Report direct buffers as unsupported on
  Windows unless the owner admits a presentation-only D3D11 device (option 8.2).
  The route behind that option is composition textures, with the Composition
  Swapchain as the alternative.

## 1. Method and environment

- One program, `present_lab.exe all`, runs every route in turn
  (`src/route_*.cpp`). Each route draws a test pattern: premultiplied BGRA, a
  function of position and phase, with half-transparent and clear quarters for
  the alpha routes. It then reads the window back with `PrintWindow(PW_CLIENTONLY
  | PW_RENDERFULLCONTENT)` and with BitBlt from the window DC, and compares 48
  opaque samples bit for bit. "48/48" below means the composed window held
  exactly the pixels the CPU wrote. Each damage-limited frame changes only the
  right half, and the samples sit there, so a match proves the partial update
  landed. Scroll frames compare against the pattern shifted 24 rows.
- **Where:** the private desktop (`run_on_desktop.py --desktop gw_present_lab`),
  never the owner's. Commands:
  - build: `msys2.cmd /d/gw_present_lab/locked_build.sh`, which runs
    `with_build_lock.sh windows-present-surfaces:claude/windows-present-surfaces /d/gw_present_lab/build /d/gw_present_lab/build.sh`;
    rc=0 on the second build (the first failed on a missing `<cstdio>` and a
    `CheckDeviceState` that `IDCompositionDevice2` lacks; both fixed);
  - run: `py -3 -B D:/malkuth/projects/tools/run_on_desktop.py --desktop gw_present_lab -- D:/gw_present_lab/build/present_lab.exe all`;
    exit 0, run twice, outputs identical apart from GPU pointer and atlas
    offset values.
- **Machine:** Windows 11 Home 10.0.26200; NVIDIA RTX 2000 Ada (vendor 0x10DE,
  15 GB dedicated); D3D feature level 11_1; `UnifiedMemoryArchitecture=0`,
  `MapOnDefaultTextures=1`; `DXGI_FEATURE_PRESENT_ALLOW_TEARING=1`; DWM
  composition on.
- **Session:** `SM_REMOTESESSION=0` and session 1 is the active console
  session. Windows 11 *Home* has no Remote Desktop host, so the owner's
  connection to Shadow is Shadow's own stream of the console session, not an
  RDP session. This machine cannot run an RDP session at all; section 3 says
  what follows from that.
- **Private-desktop facts:** the lab desktop is not the input desktop. DWM
  still renders its windows for `PrintWindow(PW_RENDERFULLCONTENT)` (every
  route read back 48/48). DXGI reports every `Present` there as
  `DXGI_STATUS_OCCLUDED` (0x087A0001), a success code, and it still presented.
  `GetFrameStatistics` fails with `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`
  (0x887A000B), its documented first-call result. BitBlt from the window DC of
  a flip-model or `WS_EX_NOREDIRECTIONBITMAP` window reads nothing useful
  (0/48, or error 6). No timing was taken: Shadow's numbers are not evidence.

## 2. Routes

The table lists each route's requirements; per-route notes follow. Copy count
means the copies after the CPU raster holds the frame, up to the buffer the
compositor reads. The compositor's own composition pass into the desktop is
not counted; it exists on every composed route.

| Route | Linked / created | GPU API symbol to get the buffer | Minimum Windows | Copies | Per-pixel alpha | Measured here |
|---|---|---|---|---|---|---|
| A. DIB section + BitBlt | gdi32, user32 | no | any (DWM composes from Vista) | 1 (BitBlt to the redirection surface) | through DWM blur-behind (today) | 48/48, partial 48/48, scroll 48/48 |
| B. StretchDIBits from plain memory | gdi32 | no | any | 1 | as A | 48/48 |
| C. UpdateLayeredWindowIndirect | user32 (WS_EX_LAYERED) | no | Vista (Indirect + prcDirty) | 1 (into DWM's layered surface) | yes, premultiplied | 48/48, prcDirty 48/48 |
| D. Buffered paint | uxtheme | no | Vista | 1, plus 1 more to keep a retained frame | as A | 48/48, partial 48/48 |
| E. DXGI flip, HWND, GDI-compatible | d3d11, dxgi | **yes** (D3D11 device) | Windows 8 | 1 (GDI BitBlt into the back buffer) | no (HWND chains refuse premultiplied) | 48/48, dirty 48/48, scroll 48/48 |
| F. DXGI flip, CPU upload | d3d11, dxgi | **yes**, and GPU copy commands | Windows 8 | 1 (GPU CopySubresourceRegion) | no | 48/48 |
| G. DXGI composition chain + DComp visual | d3d11, dxgi, dcomp | **yes**, and GPU copy commands | Windows 8 (surface handle 8.1) | 1 (GPU copy) | yes | 48/48 |
| H. DComp surface (BeginDraw) | d3d11, dxgi, dcomp | **yes** (null device refused) | Windows 8; Device2 8.1 | 1 (GDI GetDC or GPU copy) | yes | 48/48, partial, Scroll 48/48 |
| I. DComp composition texture (+ dynamic texture) | d3d11, dcomp | **yes** (device; no draw or copy command) | Windows 11 22H2 (texture), 24H2 (dynamic, dirty rects) | 1 (driver `WriteToSubresource`) | yes | 48/48, dirty 48/48 |
| J. Composition Swapchain (presentation manager) | d3d11, dcomp (`CreatePresentationFactory`) | **yes** (device; no draw or copy command) | Windows 11 22000.194, WDDM 2.0 | 1 (driver `WriteToSubresource`) | yes | 48/48 |
| K. Windows.UI.Composition from Win32 | coremessaging, runtimeobject (+ d3d11 to draw) | **yes** for drawing surfaces | Windows 10 (desktop interop: unconfirmed version) | as H | yes | interop reachable; null graphics device refused |
| L. DComp `CreateSurfaceFromHwnd` (lesser known) | dcomp | no | Windows 8.1 | as A (the redirection surface is the content) | as A | device and commit ok; not read back |

### A. DIB section + BitBlt (baseline)

- **How:** `CreateDIBSection` (top-down 32 bpp) gives a CPU pointer;
  `WM_PAINT` BitBlts `rcPaint` from a memory DC into the window. Today's host
  does exactly this: `DibFrameStore::present_surface` and the non-transactional
  `paint()` in `src/surface/windows/application/windows_host.cpp`.
- **Copies:** 1. The CPU raster is the DIB. BitBlt copies the damage into the
  window's redirection surface, and DWM composes that. Measured: first paint
  76,800 pixels, the partial update 38,400 (`rcPaint` = 160,0,320,240).
- **Damage:** `InvalidateRect` / `rcPaint`. Exact.
- **Scroll:** `ScrollWindowEx(SW_INVALIDATE)` moved the window's pixels and
  invalidated only the exposed 320 x 24 strip (7,680 pixels repainted); the
  readback matched the shifted pattern. Today's host scrolls inside the raster
  (`apply_scroll_copies`, `SkiaRaster::scroll_copy`) and then BitBlts each
  copy's whole clip. Host scroll through ScrollWindowEx would shrink that
  present to the exposed strip. This is available now with no GPU symbol
  (recommendation 8.a).
- **Alpha:** the window's own alpha shows only with DWM's help. Today that is
  `DwmEnableBlurBehindWindow` with an empty blur region
  (`windows_compositor.hpp`, stage T5 C2).
- **Loss/refusal:** no device to lose. BitBlt and GdiFlush return FALSE, and
  the host already recovers with one exposure pass
  (`transaction-frame-not-presented`).
- **Note on today's transactional build** (`GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB`,
  default OFF): `DibFrameStore::begin` zero-fills the whole candidate and then
  memcpys the whole front into it, every frame. That is two whole-frame CPU
  passes before painting, in that build only.

### B. StretchDIBits from plain memory

The same single copy, but GDI reads caller memory, so no DIB section is needed.
This is today's band path (`dib_band_present.hpp`). It read back 48/48. The
same damage and scroll story as A applies.

### C. UpdateLayeredWindowIndirect

- **How:** a `WS_EX_LAYERED` window plus `UpdateLayeredWindowIndirect` with
  `ULW_ALPHA` and `AC_SRC_ALPHA`, from a premultiplied DIB section.
- **Copies:** 1, into the surface that the system keeps for the layered window.
- **Damage:** `prcDirty`. Microsoft Learn says that when it is non-NULL "only
  the area in this rectangle is updated from the source DC"
  (UPDATELAYEREDWINDOWINFO, updated 2024-02-22). The `psize` / `hdcSrc` pair
  still describes the whole window, so the source must hold the whole frame.
  Measured: the full update and the `prcDirty` update both read back 48/48.
  The lab's last call passed a half-width `psize`. It succeeded and resized
  the window: ULW treats `psize` as the window's new size, not as a partial
  source.
- **Scroll:** none. ULW has no scroll primitive, so the raster moves the pixels
  and names the union dirty.
- **Alpha:** true per-pixel premultiplied alpha, without blur-behind.
- **Limits:** a layered window does not get `WM_PAINT` content, and child
  HWNDs are not part of the ULW image. Good for Popups, Splash and tooltips,
  not for main windows that host native children.
- **Loss/refusal:** BOOL FALSE with GetLastError. No device.

### D. Buffered paint

`BeginBufferedPaint(BPBF_TOPDOWNDIB)` + `GetBufferedPaintBits` gives a CPU
pointer (row 320 pixels here) into a uxtheme pool DIB, and `EndBufferedPaint`
BitBlts it. That is the same single copy as A. The pool buffer was the same
one on the second paint (`same_pool_buffer=1`), but nothing promises its
contents survive. A retained raster therefore keeps its own frame and copies
into the pool, making two copies. No gain over A.

### E. DXGI flip model on the HWND, GDI-compatible buffers

- **How:** `D3D11CreateDevice`, then `IDXGIFactory2::CreateSwapChainForHwnd`
  (FLIP_SEQUENTIAL, 2 buffers, B8G8R8A8, `DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE`),
  then `IDXGISurface1::GetDC` and a BitBlt from the CPU DIB, then
  `ReleaseDC(dirty)` and `Present1`.
- **Refusals measured:**
  - premultiplied alpha on an HWND chain: `DXGI_ERROR_INVALID_CALL` 0x887A0001;
  - `DXGI_CPU_ACCESS_DYNAMIC` or `_READ_WRITE` in `BufferUsage`: 0x887A0001;
  - `Map` on a back buffer: `E_INVALIDARG`;
  - a bitblt-model chain with CPU access: `E_INVALIDARG`.

  So a swap chain never yields a CPU pointer.
- **Copies:** 1 by the program (GDI into the GPU back buffer). Flip model then
  hands the buffer to DWM with no redirection copy (Microsoft Learn,
  "DXGI flip model", updated 2025-04-15: "DWM can compose straight from those
  back buffers without any additional copy operations").
- **What the runtime copies for us (measured):** after a frame that wrote only
  its dirty right half, the next back buffer still held the frame before it,
  so nothing was copied forward at present time. Then a frame that wrote only
  the 24-row exposed strip, with `pScrollRect` = 0,0,320,216 and
  `pScrollOffset` = (0,-24), read back as the correctly scrolled previous
  frame. The runtime therefore copies the scrolled and the undamaged area from
  the previous frame into the presented buffer itself. Microsoft Learn,
  DXGI_PRESENT_PARAMETERS (updated 2024-02-22): the scrolled rectangle is
  where "the runtime bit-block transfers (bitblts) content". That copy is GPU
  work the runtime issues, not the program.
- **Damage:** `Present1` dirty rectangles. Microsoft Learn says the program
  "must update every single pixel in each rectangle" and "must not update any
  pixel outside" them.
- **Scroll:** the `Present1` scroll rectangle (measured above). GDI's
  ScrollWindowEx must not be used with flip model (the "DXGI flip model"
  page).
- **Tearing:** composed windowed presents do not tear. `ALLOW_TEARING` only
  matters for independent flip and full screen.
- **Loss/refusal:** `Present` returns `DXGI_ERROR_DEVICE_REMOVED` / `_RESET`;
  `DXGI_STATUS_OCCLUDED` is a success code that must not be treated as a
  failure. The fallback is expensive: once an HWND has a flip chain, "only
  Direct3D content in flip model swap chains ... are visible. The runtime
  ignores all other bitblt model Direct3D or GDI content updates" (the same
  page). A second chain on the same window was refused while the first lived
  (`E_ACCESSDENIED`), and the window-DC readback of a flip window was 0/48.
  Falling back to GDI therefore means destroying the chain and, to be safe,
  the window. That conflicts with "refusal falls back to the safe path with
  the same pixels".

### F. DXGI flip with a CPU-written upload texture

A `D3D11_USAGE_DYNAMIC` texture (`Map(WRITE_DISCARD)`, CPU pointer), then
`CopySubresourceRegion` into the back buffer, then `Present1`. It read back
48/48. The copy is a GPU command the program issues, which the blind-buffer
rule forbids outright.

### G. DXGI composition swap chain in a DComp visual

`CreateSwapChainForComposition` (premultiplied; GDI-compatible was also
accepted) or, from Windows 8.1, `DCompositionCreateSurfaceHandle` +
`IDXGIFactoryMedia::CreateSwapChainForCompositionSurfaceHandle`. Either goes
into a `WS_EX_NOREDIRECTIONBITMAP` window through `CreateTargetForHwnd` /
`SetContent` / `Commit`. Both read back 48/48 with per-pixel alpha.
`IDCompositionDevice::CheckDeviceState` reported valid. Copies, damage and
scroll work as in E/F (the same `Present1`). Per-pixel alpha comes without
DWM blur-behind.

### H. DirectComposition surfaces

- `DCompositionCreateDevice2(nullptr)` works, but `CreateSurface` on it fails
  with `E_UNEXPECTED` (0x8000000E): surfaces need a rendering device.
- On a D3D11 device:
  - `BeginDraw(IID_IDXGISurface1)` succeeds, with offset 1,1 inside a
    1024 x 1024 atlas;
  - `GetDC` on it succeeds, so GDI can BitBlt the CPU raster into the surface;
  - `Map` fails (`E_INVALIDARG`);
  - `BeginDraw(IID_ID3D11Texture2D)` returns a default-usage atlas texture
    (bind 0x20, misc 0x8200, no CPU access).
- Content written by a GPU copy read back 48/48. So did the partial
  `BeginDraw` over the right half and `IDCompositionSurface::Scroll` followed
  by a strip `BeginDraw`.
- Microsoft Learn (BeginDraw, updated 2024-02-22): the returned rectangle
  "does not contain the previous contents". Only the update rectangle is
  rewritten, and the rest of the surface keeps its pixels. That is
  damage-limited in the right way, and `Scroll` is real host scroll inside the
  surface.
- Virtual surfaces (65536 x 65536) were created.
- **Copies:** 1. Either GDI into the atlas texture (no D3D command by the
  program, but the device is the program's) or a GPU copy.

### I. Composition textures and dynamic textures (lesser known)

- **Source:** Windows SDK 10.0.28000.0, `um/dcomp.h`: `IDCompositionTexture`
  and `IDCompositionDevice4` (lines 2424-2465, under
  `NTDDI_VERSION >= NTDDI_WIN10_NI`, Windows 11 22H2), and
  `IDCompositionDynamicTexture` / `IDCompositionDevice5` (lines 2477-2518,
  under `NTDDI_WIN11_GE`, 24H2). mingw-w64 lacks them; the lab declares them.
  The header comment says the dynamic texture's dirty rectangles let DWM
  "optimize the redrawing", and that the caller must include every changed
  pixel. Microsoft Learn marks the API prerelease (searched 2026-11-16).
- **Measured:**
  - `CheckCompositionTextureSupport` = 1;
  - `CreateCompositionTexture` refuses textures without
    `SHARED | SHARED_NTHANDLE` (`E_INVALIDARG`), and accepts them shared,
    shared + displayable, and, the important case, default usage with
    `D3D11_CPU_ACCESS_WRITE` and shared + NT handle;
  - that texture was written with `Map(D3D11_MAP_WRITE, no pointer)` +
    `ID3D11Device3::WriteToSubresource` from the CPU raster (no draw, shader
    or copy command) and read back 48/48;
  - a dynamic texture took a whole set, and then a second CPU-written texture
    with a dirty rectangle, which read back 48/48;
  - CPU write with `D3D11_TEXTURE_LAYOUT_ROW_MAJOR` was refused at creation,
    and so was CPU write + displayable (`E_INVALIDARG`).
- **What that means:**
  - There is no CPU pointer: `WriteToSubresource` copies from the raster.
  - CPU-written textures cannot be displayable, so they never reach direct
    scanout or independent flip; DWM composes them.
  - A texture must not be written while DWM shows it. That needs two or more
    textures and `GetAvailableFence`, and its fence is an `ID3D11Fence`
    (another GPU API symbol).
  - A dirty update therefore writes the union of this frame's and the previous
    frame's damage (buffer age 2). There is no scroll primitive.
- **Vtable caution:** the dynamic texture's two `SetTexture` overloads are
  ordered by MSVC's reverse-declaration rule. The lab only called the slot
  that is safe under either order. The dirty-rectangle call reached the
  three-argument form under the assumed order, which is unconfirmed beyond
  the result. A GUI.Forms version would need the MSVC layout pinned by a test.

### J. Composition Swapchain (presentation manager)

- **Source:** Microsoft Learn, "Composition swapchain programming guide"
  (2021-09-10, updated 2025-03-25): it requires "WDDM ... 2.0, and Windows 11
  (Build 10.0.22000.194) or greater"; independent flip needs WDDM 3.0 and
  displayable surfaces; at most 31 buffers.
- **Measured:**
  - `CreatePresentationFactory` is exported by dcomp.dll; with a null device
    it returns `E_POINTER`;
  - with the D3D11 device, `IsPresentationSupported` = 1 and
    `IsPresentationSupportedWithIndependentFlip` = 1;
  - `AddBufferFromResource` refuses unshared textures and accepts shared + NT
    handle, displayable, and CPU-writable shared + NT handle;
  - the CPU-written buffer (`WriteToSubresource`), bound through
    `DCompositionCreateSurfaceHandle` → `CreatePresentationSurface` and a
    null-device DComp visual (`CreateSurfaceFromHandle`), presented and read
    back 48/48;
  - the buffer read as not available right after present (`IsAvailable` 0),
    as the guide says;
  - `GetLostEvent` gave a handle, not signalled.
- **Damage:** none at this API's level. A present replaces the buffer whole,
  so damage-limited work is again the program writing the buffer-age union.
  Scroll: none.
- **Loss:** the lost event, plus `IsPresentationSupported` at start.

### K. Windows.UI.Composition from plain Win32

- **Reachable** without XAML or a package: `CreateDispatcherQueueController`
  (thread-current, STA), `RoActivateInstance("Windows.UI.Composition.Compositor")`,
  `ICompositorInterop`, and `ICompositorDesktopInterop::CreateDesktopWindowTarget`
  all succeeded.
- **Drawing surfaces** need `CreateGraphicsDevice`, which refused a null
  device (`E_INVALIDARG`) and accepted the D3D11 device. That makes it the
  same route as H, with a WinRT runtime added.
- **Gain:** none for GUI.Forms; it adds `runtimeobject` and `coremessaging`.

### L. `IDCompositionDesktopDevice::CreateSurfaceFromHwnd` (lesser known)

A null-device DComp tree can show another window's redirection surface as a
visual (device, visual and commit all succeeded; not read back). It uses no GPU
symbol, but it composes a GDI window, so it is route A with more machinery. It
might matter for a future compositor tree, not for direct buffers.

### Not tested, listed

- **Undocumented D3DKMT present calls:** out of scope; they are GPU kernel
  thunks.
- **`DCompositionCreateSurfaceHandle`'s comment,** which says a surface can be
  bound "to a GDI bitmap" (SDK `dcomp.h`, near line 113): there is no public
  API for that binding (unconfirmed).
- **D2D bitmaps with CPU access:** read-only, so not a write route.

## 3. Remote sessions

- This machine cannot host an RDP session (Windows 11 Home), and the run's
  session is the console (`SM_REMOTESESSION=0`). So no RDP-specific behaviour
  was observed. The owner's stream is a capture of this console, so every
  route above behaves for the owner as measured.
- **For real RDP sessions (unconfirmed by experiment; for a Pro/Server host
  later):**
  - The GDI routes (A-D) are remoted as bitmap updates of what changed, and
    ScrollWindowEx gives the RDP stack a screen-to-screen move (expected,
    unconfirmed).
  - DXGI's `Present1` scroll rectangle is documented as used "to optimize
    presentation in terminal server and indirect display scenarios"
    (DXGI_PRESENT_PARAMETERS).
  - Whether a D3D device in an RDP session is hardware or WARP depends on the
    host's RDS GPU policy. The lab reports the adapter
    (`dxgi.adapter … software_flag`) so a run on an RDP host would show it.
  - The capability report should treat a remote session (`SM_REMOTESESSION`)
    as a reason to prefer the GDI path until such a run says otherwise.

## 4. Copy counts, side by side (CPU raster → buffer the compositor reads)

- A, B, C: 1 CPU-side GDI or USER copy of the damage.
- D: 1, or 2 with a retained frame.
- E: 1 GDI copy into a GPU buffer, plus a runtime GPU copy of the undamaged
  and scrolled area each present.
- F, G, H (texture form): 1 GPU copy issued by the program.
- H (GetDC form): 1 GDI copy.
- I, J: 1 driver copy (`WriteToSubresource`) of the buffer-age-2 damage union.
  It never scans out (CPU-writable textures can't be displayable).

No route removes the copy. The direct routes move it from GDI to the driver;
whether that is cheaper must be measured on hardware without a remote stream,
not on Shadow.

## 5. Damage and scroll, side by side

| Route | Damage-limited present | Host scroll |
|---|---|---|
| A, B, D | `rcPaint` (measured) | ScrollWindowEx (measured: only the strip repainted) |
| C | `prcDirty`, full-frame source (measured) | none |
| E, F, G | `Present1` dirty rects (measured) | `Present1` scroll rect; the runtime moves pixels (measured) |
| H | partial `BeginDraw` (measured) | `IDCompositionSurface::Scroll` (measured) |
| I | dynamic texture dirty rects, 24H2 (measured); the writer covers buffer age 2 | none |
| J | none (whole buffer per present) | none |

## 6. Alpha and DWM

- **Per-pixel alpha without blur-behind:** C (ULW) and the DComp-hosted
  routes G-J.
- **HWND flip chains (E, F):** cannot be premultiplied (measured refusal).
- **A, B, D:** keep today's `DwmEnableBlurBehindWindow` route, with DWM
  corner and border handling unchanged.
- **Popups and Splash:** ULW is the GPU-free way to drop blur-behind (the
  `apply_windows_popup_frame` frame rules still apply).

## 7. Loss, refusal and the fallback each needs

- **A-D:** BOOL / GDI_ERROR. The host's existing recovery applies, and they
  are the fallback themselves.
- **E-H:**
  - device removed or reset comes from `Present`, `GetDC` or `CheckDeviceState`;
  - E and F poison the HWND for GDI, so falling back means recreating the
    window;
  - G-J use a DComp visual. On a redirected window (not
    `WS_EX_NOREDIRECTIONBITMAP`), `SetRoot(nullptr)` + `Commit` should
    uncover the GDI content, with an exposure repaint from the CPU raster at
    the same pixels (expected, not measured: the lab used non-redirected
    windows). This is the fallback shape a Windows direct path would need.
- **I and J:** also `CheckCompositionTextureSupport` /
  `IsPresentationSupported` at capability time, the presentation lost event,
  and device removal.

## 8. Recommendation and owner decisions

**Now, with no GPU symbol (no decision needed; for the Windows backend once the
shim is folded):**

a. Keep DIB section + BitBlt as the Windows safe path and default (route A,
   today's code).
b. Add host scroll through ScrollWindowEx, reported as the Windows host-scroll
   capability: it presents only the exposed strip instead of each copy's
   whole clip.
c. Offer ULW (route C) for popup-kind windows that need per-pixel alpha, as an
   alternative to blur-behind, with prcDirty damage. That is a host decision
   inside `src/surface/windows/`.
d. Report "direct buffers: unsupported" on Windows until question 1 is
   answered.

**Fallback chain if a direct route is admitted:** composition texture
(22H2+; dynamic texture with dirty rectangles on 24H2+), then Composition
Swapchain (22000.194+ with WDDM 2.0), then DIB + BitBlt. Each step is
capability-tested at window creation. The direct steps run in a DComp visual
over a redirected window, so a loss drops to GDI on the same window with an
exposure repaint.

**Question 1: which Windows routes may GUI.Forms use?** Every Windows route
that composes CPU pixels without GDI needs a Direct3D 11 device (a GPU API
symbol) just to obtain the buffer, and the plan says those are listed for the
owner before they are written.
1. GDI only (routes A-D). Direct buffers are unsupported on Windows, and the
   blind-buffer rule and AGENTS.md stay as they are.
2. Admit a presentation-only D3D11 device for routes I and J: create a device;
   create CPU-writable, shared default textures; `Map` /
   `WriteToSubresource` / `Unmap`, plus an `ID3D11Fence` wait. No draws, no
   shaders, no copy commands, capability-reported, with GDI fallback. This
   needs an AGENTS.md amendment ("no GPU capability" today forbids creating
   the device).
3. Option 2 plus GPU copy commands (`CopySubresourceRegion` from an upload
   texture), which opens E-H and DXGI flip from Windows 8 on. That breaks
   "never issues GPU commands".
4. Option 2 plus the GDI-compatible forms (DXGI surface or DComp surface
   `GetDC` + BitBlt), so GDI writes into the GPU buffer without D3D commands.
   This adds HWND-poisoning (E) or atlas surfaces (H) and gains nothing over
   2.

*Recommendation:* 1 now. Revisit 2 only after a non-remote Windows machine
shows the driver copy beating the GDI copy for the same workload. The copy
count is the same either way, and Shadow's timings are not evidence.

**Question 2: the remote-session rule.** Should the Windows capability report
prefer the GDI path whenever `SM_REMOTESESSION` is set, even if question 1
admits a direct route?
1. Yes, always. (Recommended until an RDP host run shows otherwise.)
2. No; let the direct route run and rely on refusal to fall back.

**Question 3 (decided, owner 2026-11-17):** the lab source is dropped from
gui_forms; this summary and the results file remain, and the source stays in
`D:/gw_present_lab`.

## 9. Open and unconfirmed

- RDP-session behaviour of every route: no RDP host on this machine (section 3).
- That GDI-to-DComp fallback on a redirected window keeps pixels identical
  (section 7). The next experiment for whoever writes the backend.
- The MSVC vtable order of `IDCompositionDynamicTexture::SetTexture`
  (section 2.I).
- Composition-texture and presentation-manager behaviour on adapters without
  `MapOnDefaultTextures` (expected: CPU-writable default textures are refused,
  and only GPU-copy forms remain).
- The minimum version of `ICompositorDesktopInterop` (believed to be Windows 10
  1803; not re-verified).

## 10. Evidence files

- `build-notes/WINDOWS_PRESENTATION_SURFACES_results.txt`: the complete output
  of the first run. The second run was identical except for pointer and offset
  values.
- The lab source as run: `D:/gw_present_lab` on Shadow (not in the repository).
