# Build from source

[Installation](../user/install) gives the shortest route to an installed SDK. This page
covers the full build: options, dependencies, tests, the demonstration
programs, Windows build constraints and the compiler cache that CI publishes.
All commands run from the repository root.

## Standard native build

First follow [Installation](../user/install.md#configure-build-install) to fetch the text
sources and configure for your platform. Linux requires an enabled X11 host;
the CPU renderer, `stx_skia/`, builds with the tree on every platform.
Then:

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On macOS the result includes `build/GUI.Forms Gallery.app`; open it with
`open "build/GUI.Forms Gallery.app"`. On Windows the executable is
`build/GUI.Forms Gallery.exe` with a `fonts/` directory beside it. On Linux
the gallery needs `-DGUI_FORMS_ENABLE_LINUX_HOST=ON` and an X11 display
(`xvfb-run` works for tests).

## Options

| Option | Default | Effect |
|---|---|---|
| `GUI_FORMS_BUILD_GALLERY` | ON when top level | Build the control gallery demonstration |
| `GUI_FORMS_BUILD_TESTS` | ON when top level | Build the automated tests |
| `GUI_FORMS_ENABLE_SKIA` | ON | Build the private CPU-only Skia painter (macOS and Linux) |
| `GUI_FORMS_ENABLE_MACOS_HOST` | ON on macOS | Build the AppKit host and `GUIForms::Application` |
| `GUI_FORMS_ENABLE_WINDOWS_HOST` | ON on Windows | Build the Win32 DIB host |
| `GUI_FORMS_ENABLE_LINUX_HOST` | OFF | Build the X11 host; requires Linux and Skia |
| `GUI_FORMS_ENABLE_HARFBUZZ_TEXT` | ON with Skia or Win32 | Build the pinned HarfBuzz/FreeType text engine |
| `GUI_FORMS_BUILD_AUDIO` | OFF | Build `GUIForms::Audio` |
| `GUI_FORMS_AUDIO_VORBIS_BACKEND` | `stx` | Ogg decoder: `stx` or `stb` |
| `GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD` | OFF | Build the fixture-backed explorer consumer |
| `GUI_FORMS_BUILD_BENCHMARKS`, `GUI_FORMS_BUILD_FUZZERS` | OFF | Measurement harnesses and libFuzzer entry points |

The remaining `GUI_FORMS_BUILD_TEXT_*` and `GUI_FORMS_BUILD_PREPARED_TEXT` /
`GUI_FORMS_BUILD_TEXT_MASKS` options build development-only text services and
probes. They are not installed.

## Third-party dependencies

Dependencies are pinned. Fetch HarfBuzz and FreeType explicitly before
configuration. The CPU renderer, `stx_skia/` (a pruned CPU Skia derivative),
is part of the repository and builds with the tree, as SheenBidi is vendored.

| Script | Purpose |
|---|---|
| `fetch_text_stack.sh` | HarfBuzz and FreeType |
| `fetch_linebreak.sh` | libunibreak (UAX #14 line breaking), required with `fetch_text_stack.sh` before configuring `GUI_FORMS_ENABLE_HARFBUZZ_TEXT=ON` |

Audio pulls miniaudio through `FetchContent` with digest checks. No package
manager is consulted at configure time.

## Tests

`ctest` runs the renderer-free core and control tests, the headless host
conformance traces, the painter and text-identity tests, the boundary audits
(no Skia, AppKit, Win32 or X11 type in a public header; no GPU symbol in any
Skia archive) and, when a native host is enabled, that platform's host tests.
The text-identity test compares rendered text against checked-in hashes; it is
the regression check for the covered corpus and rendering profile, not an
exhaustive proof for all text.

Native host tests that open real windows run in CI on each platform. On a
development machine without a display, configure with the host option off
and the renderer-free tests still cover the retained engine.

Tests that open native windows which take focus, capture or activation
carry one of two labels, by what they need from the desktop:

- `foreground`: any desktop of their own. On a development machine they run
  on a private desktop of the same window station, where nothing is ever
  foreground, so they never take the user's foreground and may run in
  parallel, without a lock.
- `input_desktop`: the desktop the user sees. These are the UI Automation
  and MSAA client tests (off the input desktop every UI Automation pattern
  call waits about 2 s inside UIAutomationCore) and the compositor probe (DWM
  does not composite another desktop). They carry the `desktop_foreground`
  resource lock, so one ctest run never runs two at once; across ctest runs
  on one machine, run them under a machine-wide lock.

```text
python -B <private-desktop tool> -- ctest --test-dir build -L foreground
python -B <lock tool> -- ctest --test-dir build -L input_desktop
ctest --test-dir build -LE "foreground|input_desktop"
```

Or set a launcher per group, as a CMake list (CMake 3.29 or later), and a
plain `ctest` runs each test through its group's launcher:
`GUI_FORMS_FOREGROUND_LAUNCHER` (the private-desktop tool) and
`GUI_FORMS_INPUT_DESKTOP_LAUNCHER` (the lock tool). Each is a cache variable;
when the cache value is empty, the environment variable of the same name is
read at every configure. CI leaves both empty: each runner has a desktop of
its own.

A test that reads a service only the input desktop has (the cursor
position, the foreground window, screen pixels) accepts that service's
documented refusal only when it is not on the input desktop
(`tests/support/windows_desktop.hpp`).

## Sanitized test runs

`python3 -B tools/ci_build.py --sanitize [--tests REGEX]` is the standard
sanitized tree on every platform: AddressSanitizer and
UndefinedBehaviorSanitizer everywhere, LeakSanitizer on Linux and macOS,
every report stopping its test ([Contributing](../dev/contributing.md) has
the details). On macOS the sanitizer flags reach CMake through `CFLAGS`,
`CXXFLAGS`, `OBJCXXFLAGS` and `LDFLAGS` rather than `-DCMAKE_<LANG>_FLAGS`.
`cmake/llvm22.cmake` puts the pinned libc++ headers and runtime link flags
in `CMAKE_<LANG>_FLAGS_INIT`, which a command-line `-DCMAKE_<LANG>_FLAGS`
replaces: the tree then compiles against Homebrew's libc++ headers (the
first macOS attempt failed in `dock_site.cpp` with "'from_chars' is
unavailable: introduced in macOS 26.0") and leaves the Objective-C++ host
uninstrumented. CMake reads those variables only when it first configures a
tree, so delete a macOS `gui-forms-sanitize` tree configured before this
change.

On macOS LeakSanitizer reports thousands of blocks that Apple's
frameworks keep for the life of the process: caches, per-thread state,
class initialisation, XPC connections, and work still queued in libdispatch
at exit, whose queue pointers are signed on arm64e so LeakSanitizer cannot
follow them. The first macOS run (2026-10-19, Apple M4, macOS 26.5,
Xcode 26.5, the seven macOS host tests) had between 23 and about 900 such
reports per test, none allocated directly by GUI.Forms code.
`tools/sanitizer/lsan.supp` carries a macOS baseline for them, under one
rule: a suppression matches any frame of a leak's allocation stack, and
AppKit, CoreFoundation, Foundation, CoreGraphics, QuartzCore, HIToolbox,
libdispatch and the Objective-C runtime call into GUI.Forms (event
dispatch, drawing, timers, dispatch), so those libraries are never named
whole; only their internal allocation sites are. Whole libraries are listed
only where they never call into GUI.Forms (ColorSync, vImage, CoreUI,
SwiftUICore, the XPC and service frameworks and similar). Deliberate leaks
added for the check were all still reported: in `main`, in a dispatch
callback inside the AppKit run loop, in `-[GUIFormsView drawRect:]` during
a Core Animation commit, and a leaked Objective-C object and CF string. One
category is knowingly hidden: what a GUI.Forms work item still queued in
libdispatch at exit owns. When a new macOS release adds reports, name the
internal allocation site that holds them, never a library that calls into
GUI.Forms or a function on the path from it, and check that the deliberate
leaks above are still reported.
On 2026-10-28 two application start-up sites were added after one run in 21
of `gui_forms_application_native_tests` reported them:
`_NSInitializeAppContext` (the HIToolbox application object made by
`-[NSApplication init]`) and `aeInitializeFromHIToolbox` (AppleEvents
set-up on the first event fetch). Leaks added in `main` and in a
window-close callback run from the main dispatch queue inside the AppKit run
loop were still reported.

The baseline's first run also found a GUI.Forms leak, now fixed: a
completed `DispatchWork` kept its callable, because moving a
`std::function` need not empty the source (libc++ copies a callable held in
its small buffer), so a callable that owned a holder of its own
`DispatchOperation` formed a cycle (`gui_forms_macos_host_close_tests`).
`gui_forms_owned_event_tests` skips its allocation-balance assertion under
AddressSanitizer on macOS as on Windows: the pinned libc++ dylib's
allocations do not reach the test's replacement `operator new` (-24000
blocks over its 1000 buttons), and LeakSanitizer found nothing there.
`gui_forms_macos_idle_visibility_tests` exceeded its own 15-second limit
once while LeakSanitizer scanned at exit; rerun it alone before treating
that as a finding.

`tools/run_sanitizer_suite.sh <build-directory>` builds the toolkit, the
Gallery and every test with AddressSanitizer and UndefinedBehaviorSanitizer
(`-fsanitize=address,undefined -fno-omit-frame-pointer`, RelWithDebInfo)
and runs the whole suite. Use a build directory of its own; sanitized
objects do not mix with a normal build. It is a local procedure; CI does not
run it.

```sh
sh tools/run_sanitizer_suite.sh .build/sanitize
```

Both sanitizers link on the pinned Windows toolchain (MSYS2 CLANG64, LLVM
22.1.8). The defaults are the Windows native configuration, and further
CMake arguments after the directory reach the first configure; only the
Windows configuration has been run so far. Its `-DCMAKE_<LANG>_FLAGS`
replace the macOS toolchain flags as described above, so on macOS use
`ci_build.py --sanitize`. ASan stops a test at its first
report. UBSan reports without stopping, so a UBSan report does not fail its
test: the script reads ctest's `Testing/Temporary/LastTest.log`, prints
every sanitizer report it finds and exits non-zero on a report or a failed
test. LeakSanitizer is not available on Windows, so leaks are not checked
there.

On a Windows 11 machine with 8 cores, a full sanitized build took 30 to 40
minutes (6 and 3 compile jobs) and the suite about 140 seconds, three tests
at a time (`GUI_FORMS_SANITIZER_JOBS`, default 3). Some sanitized tests run
much longer than normal (`gui_forms_windows_gdi_compat_shim_tests` took 85
seconds), and a test that overlaps them can exceed its own fixed timeout.
`gui_forms_live_surface_idle_wake_tests` (15-second limit) passes alone in
about 0.1 seconds, takes 4.5 to 7 seconds while the shim test runs (about 5
seconds in a normal build too) and timed out once. Rerun a test that fails
only under parallel load on its own (`ctest --test-dir <dir> -R <name>`)
before treating it as a finding.

## Windows builds and Wine

Use the native MSYS2 CLANG64 instructions in [Installation](../user/install).
There is no cross-build from macOS or Linux: the former MinGW-w64 GCC
toolchain file, which the LLVM 22.1.x configure check rejected, and the
script that staged GCC's runtime DLLs beside a cross-built program were
retired on 2026-10-21. Windows binaries never use a GCC runtime.

Every Windows executable and DLL links the LLVM runtimes (libc++, libc++abi,
libunwind, compiler-rt), winpthreads and the MinGW-w64 CRT statically
(`add_link_options(-static)` in `CMakeLists.txt`), so none imports a runtime
DLL. `llvm-objdump -p <binary>` lists only Windows system DLLs, and
GUI.Forms' own DLLs where a program uses them (`libgui_forms_threading.dll`).
`tools/package_programs.py` fails a package whose binaries import a GCC or
LLVM runtime DLL.

Windows builds target the Windows 10 API level (`_WIN32_WINNT=0x0A00`). A
Windows host build produces `GUI.Forms Gallery.exe` (statically linked),
`libgui_forms_threading.dll`, the automation client
`gui_forms_windows_probe.exe` and a `fonts/` directory, all in the build
directory.

To run a Windows build under Wine, copy its build directory to the Mac and
run the program from there: `libgui_forms_threading.dll` sits beside it and
no runtime DLL is needed. Run `wine "GUI.Forms Gallery.exe"`; add
`--automation` to accept commands from the probe.

`tools/run_windows_wine_smoke.sh <build-dir>` launches the gallery with
`--automation`, clicks three page rows and three controls by stable ID,
captures the framebuffer, closes through the real `WM_CLOSE` path and checks
the structured log: six handled clicks, a saved capture, the Win32 DIB
renderer with bundled fonts, no rejected host events, exactly one close
request, and a closed, shut-down host. The log and capture
(`wine-gallery-smoke.jsonl`, `gallery-automation.bmp`) go to the build
directory or `GUI_FORMS_EVIDENCE_DIR`. `tools/run_windows_gallery_text_smoke.sh`
runs the Windows text tests under Wine before driving the text page, and
`tools/run_windows_gallery_services_smoke.sh` drives the animation,
dispatcher and host-service pages. The scripts need `wine` (override with
`WINE_BINARY`) and `rg`. When the build is configured on macOS with `wine`
found, the `gui_forms_windows_wine_smoke` and
`gui_forms_windows_gallery_services_smoke` targets run the first and last of
these against the build directory. With `x86_64-w64-mingw32-objdump`
available, `gui_forms_windows_boundary_audit` rejects a gallery that imports a
GPU or managed runtime DLL or lacks `GDI32.dll` and `USER32.dll`.

The native Windows build in MSYS2 uses the same host sources with
`cmake/llvm22.cmake`.

## Install prefix and SDK layout

`cmake --install build` installs headers, libraries, the CMake package and the
font directory under `CMAKE_INSTALL_PREFIX`. The exact contents are listed in
[Installation](../user/install.md#what-gets-installed). CI installs into a scratch prefix and
then builds `examples/reference/` against it, so the installed package is
tested on every platform, not only the build tree.

## Compiler cache

CI compiles through `ccache` with `CCACHE_COMPILERCHECK=content` and, after
tests pass, publishes a cache archive per platform to a `build-<sha>`
pre-release. Those releases are build inputs, not application releases.
A consumer that restores one still compiles and tests its own code; a cache
miss is an ordinary compilation. [Fast, reproducible builds](fast-builds) explains
the whole method (the pinned configuration, exact reuse of tested trees and
the published caches), and [Reusing CI caches](reusing-ci-caches) covers the
archive layout, the manifest, restoring a cache and copying the method.
The CI tools that do this live in template_harness, pinned by `harness.lock`
([GUI.Forms builds through template_harness](template-harness)).

## Dependency boundaries

The build enforces the project's boundaries as tests rather than as
conventions: C++20 everywhere, Objective-C++ only in `src/host/macos/`, Win32
only in `src/host/windows/`, X11 only in `src/host/linux/`, Skia only in
`src/render/skia/`, PNG as the only image decoder, and no GPU API anywhere.
A change that violates one of these fails `ctest` before it reaches review.

```{toctree}
:hidden:

fast-builds
reusing-ci-caches
template-harness
```
