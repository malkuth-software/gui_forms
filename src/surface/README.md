# The platform surface

Every call GUI.Forms' native hosts make into a platform API lives here
(owner, 2026-11-16, host adapter consolidation). After 1.0 this folder
moves into STXlib as its surface. Moving it changes no behaviour.

| Folder | Platform APIs |
| --- | --- |
| `macos/` | AppKit, CoreGraphics, CoreText, IOSurface, Metal (present only) |
| `windows/` | Win32, COM, UIA, MSAA, DWM, GDI |
| `linux/` | Xlib, xcb, XInput2, Xrandr, Present, DRI3, Wayland, GBM, GIO (AT-SPI) |
| `posix/` | `mmap`, sockets, child processes (hosted content, sealed font pages) |
| `web/` | Emscripten (the program half's calls and the page half) |

Each platform keeps the subfolders it had under `src/host/<platform>/`
(`application/`, `view/`, `accessibility/`, `services/`, `compositor/`,
`fonts/`, `locale/`, and so on), at the same depth, so relative includes
into `src/core/` and `src/render/` are unchanged.

`src/host/` keeps the platform-neutral host logic and includes no platform
header:
- `src/host/headless/`, the headless host;
- `src/host/surface/`, the single-surface window manager;
- per platform, the program entry points (`*_main.cpp`, `*_main.mm`), the
  `macos_host.hpp` and `windows_host.hpp` include shims, and the pure rules
  the tests drive directly (Linux input rules, XSETTINGS parsing, strftime
  and ICU date patterns, live-surface damage and front reuse, the display
  link trace, the browser's input, present and dialog rules).

Not yet separated: `windows/application/windows_host.cpp` (with its `.inc`
parts), `linux/application/linux_host.cpp` and the macOS view and
application sources still interleave neutral host logic (scheduling, damage,
capability reports) with platform calls inside one translation unit. Lifting
that logic into `src/host/` behind a narrow interface is a rewrite, not a
move, and waits for a change that can be built on each platform.
