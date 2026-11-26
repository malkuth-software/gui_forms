# Platform host entry points

The headers in this directory are the public, portable entry points for the
native hosts: `macos_host.hpp` (`run_macos`, `MacHostOptions`),
`windows_host.hpp` (`run_windows`, `WindowsHostOptions`) and `linux_host.hpp`
(`run_linux`, `LinuxHostOptions`). Each takes a retained `Window` model and an
options struct. Native types never appear here; honest platform differences are
reported through `HostCapabilities` (`macos_capabilities()`,
`windows_capabilities()`, `linux_capabilities()`).

## Linux: the program sets the character-type locale

The Linux host never calls `setlocale`: the process locale is the program's,
not the library's. Xlib opens the input method under the process `LC_CTYPE`,
so input methods named by the environment (`XMODIFIERS`, such as ibus or
fcitx) and the locale's compose sequences work only when the program has set
it. An application calls `std::setlocale(LC_CTYPE, "")` at the start of `main`,
before `run_linux` or `run_linux_application`. Without it, in the C locale,
each key still delivers its own UTF-8 text, but no input method or compose
sequence takes part. Every Linux program this repository ships does so.

## Frame capture contract

Every options struct carries the same two fields with identical semantics:

- `std::string capture_path` — when non-empty, after the first presented frame
  that covers the whole window the host encodes the presented framebuffer as
  PNG to that path, prints exactly one JSON line to stdout,

  ```text
  {"automation":"capture","saved":true,"path":"/abs/or/relative/file.png"}
  ```

  (`saved` is `false` when encoding or writing failed; `path` is the string
  that was passed, JSON-escaped), and then requests close through the ordinary
  close path. The process exit code is `0` when the file was saved and
  non-zero otherwise, including when the window closed before any frame
  covered it.
- `std::optional<Size> capture_size` — when set, the window is created at that
  logical size instead of `initial_size`.

Hosts that implement the contract set `HostCapability::frame_capture` in their
capability report. What differs between hosts is only how the adapter reaches
its framebuffer: the AppKit host and the X11 host encode the private Skia CPU
front raster (premultiplied BGRA8, written with straight alpha); the Win32
host encodes its presented 32-bit DIB through the renderer-neutral core PNG
writer as an opaque image, because GDI leaves the DIB alpha byte undefined.
Captured pixels are device pixels, so a 2x display yields a 2x image.

## Demo program command line

The Gallery, Correspondence, Explore and lab executables share one parser
(`demo/program_options.hpp` in
[gui_forms_devkit](https://github.com/malkuth-software/gui_forms_devkit)):

```text
<program> [--mcp-shared | --mcp-fcfs] [--mcp-script <file.jsonl>] [--capture <path.png>] [--size <W>x<H>] [--scale <factor>]
```

- `--capture <path>` sets `capture_path`; `--size WxH` sets `capture_size`
  (positive numbers, e.g. `--size 1280x800`).
- `--mcp-shared`, `--mcp-fcfs`, `--mcp` (the MCP help) and
  `--mcp-script <file>` are the library's
  automation arguments (MCP; `gui_forms/automation/marks/automation_marks.hpp`):
  the parser skips them, and the library reads them from the process's own
  arguments on every host.
- `--scale` is recorded as a hint only; hosts take the device scale from the
  display.
- An unknown argument, a missing value or a malformed value prints usage to
  stderr and exits with code 2 before any window is created.

Example (any platform):

```text
gui-forms-gallery --capture gallery.png --size 1280x800
```

prints the capture line, closes, and exits `0` when `gallery.png` was written.
