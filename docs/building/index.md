# Build from source

```{note}
This section is for contributors to GUI.Forms. You do not need it to build
an application: apps build against the prebuilt SDK the devkit fetches. See
[Get started](../user/get-started).
```

[Installation](../user/install) gives the shortest route to an installed SDK. This page
covers the full build: options, dependencies, the test cache the tests build
against, the demonstration programs, Windows build constraints and the
compiler cache that CI publishes.
All commands run from the repository root.

## Standard native build

First follow [Installation](../user/install.md#configure-build-install) to configure for
your platform. Linux requires an enabled X11 host; the CPU renderer,
`stx_skia` from the stx package (below), is linked on every platform.
Every platform builds against LLVM's libc++ 22: the pinned macOS 14
runtime, on Windows libc++ built against STXlib's own runtime
(`tools/own_runtime.py`; below), and on Linux the
harness's own runtime. `template_harness/harness/build_linux_runtimes.py`
builds libc++, libc++abi and libunwind from the pinned LLVM 22.1.8 as
position-independent, hermetic static archives for the host's C library
(glibc or musl) into `.build/toolchain/llvm-22.1.8-linux-<arch>-<libc>`;
set `GUI_FORMS_LLVM_RUNTIME` to that prefix, as on macOS. The runtime is
then the same on every distribution, and `gui_forms_abi` can carry it
(Alpine's own `libc++abi.a` is not position independent). The
distribution's clang 22 (apt.llvm.org's `clang-22`; Alpine's `clang`,
`compiler-rt`) is only the bootstrap compiler. On Linux
`cmake/llvm22.cmake` refuses to configure without the archives, compiles
against the runtime's headers, links the archives with `-nostdlib++` (they
end every C++ link line), and the configure refuses a tree whose C++
library is not libc++ 22. The Clang plugin
(`GUI_FORMS_LOCALIZABLE_PLUGIN`) is the exception: it is built with the
distribution's libstdc++, because the compiler it loads into links that.
Linux programs, like Windows ones, carry libc++ statically: each
executable and shared library holds its own copy, and a program package
depends on no C++ runtime library. gui_forms_devkit's `programs/tools/package_programs.py` refuses a
Linux package in which any binary still needs `libc++`, `libc++abi` or
`libunwind` as a shared library.
Then:

```sh
cmake --build build --parallel
```

On macOS the result includes `build/GUI.Forms Gallery.app`; open it with
`open "build/GUI.Forms Gallery.app"`. On Windows the executable is
`build/GUI.Forms Gallery.exe`, with a `fonts/` directory beside it that
holds the fonts' licences and attribution only: the program reads its faces
from the font pack compiled into it. On Linux the gallery needs
`-DGUI_FORMS_ENABLE_LINUX_HOST=ON` and an X11 display (a virtual one from
`xvfb-run` works).

## Options

| Option | Default | Effect |
|---|---|---|
| `GUI_FORMS_TEST_CACHE` | OFF | Describe the tree for the test cache (`cmake/TestCache.cmake`); `tools/test_cache.py pack` packs it for [gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests), and `tools/ci_build.py` turns it on |
| `GUI_FORMS_ENABLE_SKIA` | ON | Link the private CPU-only renderer (`stx_skia`), the renderer on every host |
| `GUI_FORMS_ENABLE_MACOS_HOST` | ON on macOS | Build the AppKit host and `GUIForms::Application` |
| `GUI_FORMS_ENABLE_WINDOWS_HOST` | ON on Windows | Build the Win32 DIB host |
| `GUI_FORMS_ENABLE_LINUX_HOST` | OFF | Build the X11 host; requires Linux and Skia |
| `GUI_FORMS_BUILD_AUDIO` | OFF | Build `GUIForms::Audio` |
| `GUI_FORMS_AUDIO_VORBIS_BACKEND` | `stx` | Ogg decoder: `stx` or `stb` |
| `GUI_FORMS_INSTALL_FALLBACK_FONTS` | ON | Embed the nine Noto fallback faces in the font pack, and install their OFL texts as the `fallback_fonts` component (see [Fallback fonts](#fallback-fonts)) |
| `GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY` | OFF | Development builds only: read the bundled faces from `GUI_FORMS_FONT_DIR` or else this checkout's `assets/fonts` before the embedded pack (see [Embedded font pack](#embedded-font-pack)) |
| `GUI_FORMS_FONTPACK_TOOL` | empty | A host-built `stx_fontpack` tool for a cross build, which cannot run the one it compiles |
| `GUI_FORMS_BUILD_C_ABI` | ON | Build the C ABI library, `gui_forms_abi` (`cmake/CAbi.cmake`) |
| `GUI_FORMS_BUILD_MODULE` | OFF | Build and install the `gui_forms` C++20 module (`import gui_forms;`, `GUIForms::Module`); needs CMake 3.28 or later with Ninja or a Visual Studio generator (`cmake/Module.cmake`) |
| `GUI_FORMS_ENABLE_WAYLAND_PRESENT` | OFF | Build the Wayland `wl_shm` presentation path; there is no Wayland host |
| `GUI_FORMS_REFERENCE_ARITHMETIC` | OFF | No floating-point contraction in first-party code, so every CPU gives the same pixels; `tools/ci_build.py` turns it on for the tree the tests build against, and deployment builds leave it off |
| `GUI_FORMS_LTO` | ON | Link Release builds with ThinLTO where the pinned toolchain's linker is lld |
| `GUI_FORMS_HARDENING` | ON | Stack protector, `_FORTIFY_SOURCE=2` (outside Debug and sanitized builds), CET or branch protection, Control Flow Guard on Windows, and full RELRO on Linux, for every first-party library and program |
| `GUI_FORMS_WARNINGS` | ON | Compile first-party code with `-Wall -Wextra`; `tools/warning_baseline.py` holds the count of warning sites per directory and fails a build that raises one |
| `GUI_FORMS_WARNINGS_AS_ERRORS` | OFF | Fail the build on any compiler warning in the library and its programs; `tools/ci_build.py`, and so every CI build, turns it on |
| `GUI_FORMS_CHECKED_BORROWS` | OFF | Check borrowed views against their owners; the sanitizer tree (`ci_build.py --sanitize`) turns it on. It changes the borrowed types, so a program and its consumers are built one way |
| `GUI_FORMS_BUILD_FORM_COMPILER` | ON (OFF when cross-compiling) | Build and install the form compiler that `gui_forms_add_form` runs |
| `GUI_FORMS_BUILD_GALLERY_MODEL` | ON when top level or for the test cache (OFF when cross-compiling) | Build the Gallery model (the control specimen catalogue), which the tests read and the SDK installs |
| `GUI_FORMS_INSTALL_GALLERY_MODEL`, `GUI_FORMS_INSTALL_LAB` | as `GUI_FORMS_BUILD_GALLERY_MODEL` | Install the Gallery model and the lab controls (`GalleryModel`, `Lab`, `DemoControls` components), which [gui_forms_devkit](https://github.com/malkuth-software/gui_forms_devkit) builds the Gallery and the demonstrators from; `tools/build_sdk.py` and `tools/ci_build.py` set them |
| `GUI_FORMS_LOCALIZABLE_PLUGIN`, `GUI_FORMS_EFFECT_VERIFY` | ON | Build the Clang plugin that records localizable literals and verifies effect programs; without it effects build unverified, with a warning |
| `GUI_FORMS_EFFECT_VERIFY_REQUIRED` | OFF | Fail the configure when effect programs cannot be verified, instead of building them unverified; Linux CI sets it |
| `GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB` | OFF | Bounded transactional DIB frames in the Windows host (development) |
| `GUI_FORMS_MCP` | ON | Build the automation and MCP capability; a launch with `--mcp` (or `GUI_FORMS_MCP=1`) serves MCP on standard input and output, and `--mcp-script <file.jsonl>` runs an automation script. The transport needs STXlib's `stxlib::mcp`; without it `--mcp` says so. Off in a high-integrity build |
| `GUI_FORMS_MCP_TEST_CONTROLS` | ON in Debug, else OFF | Build the automation's test controls (`control suspend_input`); never in a shipped program |

The remaining `GUI_FORMS_BUILD_TEXT_*` and `GUI_FORMS_BUILD_PREPARED_TEXT` /
`GUI_FORMS_BUILD_TEXT_MASKS` options build development-only text services and
probes. They are not installed.

## Third-party dependencies

Dependencies are pinned. This repository carries no third-party sources: they
come from the stx repository at the commit `stx.lock` names, built once into
the stx package (`cmake/Stx.cmake`, `cmake/stx_package/`), which the build
links and never compiles. Configuring builds the package into
`<build>/stx-package` (or `GUI_FORMS_STX_PACKAGE_DIR`) when none is there or
its stamp (the stx commit, the recipe, the compilers, flags and choices)
differs, and reuses it otherwise; `GUI_FORMS_STX_PREFIX` links a package built
elsewhere as is, and `FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX` names a local stx
checkout, whose edits each build brings into the package. The package holds:

| stx path | Contents |
|---|---|
| `stx_skia/` | the CPU renderer, a pruned CPU Skia derivative, with its contract tests in stx's own CI |
| `third_party/unicode/` | the Unicode data |
| `stx_zlib/`, `stx_png/`, `stx_conv/`, `stx_rtf2md/`, `stx_vorbis/` | the first-party compression, PNG, CONV interpolation, rich-text and Ogg Vorbis libraries |
| `stx_raster3d/` | the first-party 3D rasterizer behind `Plot3DView` |
| STXlib | built by stx at the commit its own `stxlib.lock` names and installed beside the other packages: the text stack (`stxlib::glyphs`, `stxlib::textlayout`, `stxlib::markdown`), the shared state (`stxlib::state`, a static archive) and the algorithms (`stxlib::algorithms`) |

`stx.lock` is the only pin for both stx and STXlib. The row height index of
the virtual row views (`VirtualRowsView`, `TreeListView`, `DataGridView`) is
an adapter over STXlib's `RowHeightIndex`, the compiled facade over
`stx_btree`'s sequence tree (`src/controls/collections/row_height_index.hpp`).

Audio pulls miniaudio through `FetchContent` with digest checks. No package
manager is consulted at configure time.

## Tests

The tests live in their own repository,
[gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests)
(owner, 2026-10-29 and 2026-11-17). They never compile GUI.Forms: they build
against a **test cache**, one archive per commit and platform that
`tools/ci_build.py --test-cache <dir>` packs (`tools/test_cache.py`): the
installed SDK, this tree's sources and headers, and every library, object
library and program it built, each loaded there as an imported target of the
same name. CI publishes the caches of every push to main and to a checkpoint
branch as the prerelease `test-cache-<commit>`; gui_forms_tests'
`gui_forms.lock` names the commit it builds against. Editing a test
recompiles that test, and a change here reaches the tests when they move
their pin to it. This tree's own check is the installed SDK's: its notices,
its imports on Windows, and the installed reference examples
(`examples/reference`), which `ci_build.py` builds and runs against it.

To run the tests against a tree of your own, pack its cache and point
gui_forms_tests at it:

```sh
python3 -B tools/ci_build.py --test-cache ../caches
cd ../gui_forms_tests
python3 tools/ci_tests.py --archive ../caches/gui_forms_test_cache-<platform>.tar.gz --unpinned
```

## Sanitized test runs

`python3 -B tools/ci_build.py --sanitize --test-cache <dir>` is the standard
sanitized tree on every platform, packed as the sanitize test cache that
gui_forms_tests' `tools/ci_tests.py --kind sanitize` runs the suite against: AddressSanitizer and
UndefinedBehaviorSanitizer everywhere, LeakSanitizer on Linux and macOS,
every report stopping its test, with borrowed views checked
(`GUI_FORMS_CHECKED_BORROWS`); [Contributing](../dev/contributing.md) has
the details. On macOS the sanitizer flags reach CMake through `CFLAGS`,
`CXXFLAGS`, `OBJCXXFLAGS` and `LDFLAGS` rather than `-DCMAKE_<LANG>_FLAGS`.
`cmake/llvm22.cmake` puts the pinned libc++ headers and runtime link flags
in `CMAKE_<LANG>_FLAGS_INIT`, which a command-line `-DCMAKE_<LANG>_FLAGS`
replaces: the tree then compiles against Homebrew's libc++ headers (the
first macOS attempt failed in `dock_site.cpp` with "'from_chars' is
unavailable: introduced in macOS 26.0") and leaves the Objective-C++ host
uninstrumented. CMake reads those variables only when it first configures a
tree, so delete a macOS `gui-forms-sanitize` tree configured before this
change. On Linux the sanitizer flags still arrive as `-DCMAKE_<LANG>_FLAGS`,
which would drop the toolchain's `-stdlib=libc++` the same way; there
`cmake/llvm22.cmake` appends the libc++ flags to such a value, so the
sanitized tree builds against libc++ 22 like the plain one (LeakSanitizer
then sees libc++'s allocations, as on macOS, and AddressSanitizer checks
libc++'s container annotations).

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
leaks above are still reported. Two application start-up sites were added
later, after a native application test reported them once in 21 runs:
`_NSInitializeAppContext` (the HIToolbox application object made by
`-[NSApplication init]`) and `aeInitializeFromHIToolbox` (AppleEvents
set-up on the first event fetch).

The baseline's first run also found a GUI.Forms leak, now fixed: a
completed `DispatchWork` kept its callable, because moving a
`std::function` need not empty the source (libc++ copies a callable held in
its small buffer), so a callable that owned a holder of its own
`DispatchOperation` formed a cycle.

The tests named in these runs, and the notes on running them under the
sanitizers (tests that skip an allocation-balance check under
AddressSanitizer, tests that exceed their time limit under parallel load),
belong to [gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests).
This repository keeps the suppression files, `tools/sanitizer/lsan.supp` and
`tools/sanitizer/tsan.supp`, because the sanitized tree is built here.

Windows builds no sanitized tree. The sanitizer runtimes for
x86_64-pc-windows-msvc (compiler-rt's `clang_rt.asan_dynamic` and the rest)
are built for, and import, Microsoft's C and C++ runtime DLLs, which the own
runtime replaces, and the target has no LeakSanitizer or ThreadSanitizer at
all. The sanitized runs are Linux's and macOS's over the same sources; the
Windows-only code (`src/surface/windows/`, `src/host/windows/`) runs under
the Windows suite without them.

## ThreadSanitizer

The C ABI promises release from any thread, deferred destruction on the UI
thread, `begin_invoke` from any thread and a thread handover before the first
window (`gf_abi.h`, "Threads"). ThreadSanitizer checks those promises on
Linux and macOS. The Windows target has no ThreadSanitizer, so the
procedure below does not run there.

```sh
python3 -B tools/abi_tsan.py [--tests-checkout DIR] [--jobs N] [--tests REGEX]
```

The tests live in gui_forms_tests, so the run has two halves. This tool
configures `.build/native-<host>-<arch>/gui-forms-tsan` with the toolkit's
usual options for the test cache and `-fsanitize=thread -fno-omit-frame-pointer`
(Release at `-O1 -gline-tables-only`, no link-time optimisation), builds it
whole and packs it as a `tsan` test cache into
`.build/native-<host>-<arch>/gui-forms-tsan-cache`. It then runs
gui_forms_tests' `tools/abi_tsan.py --archive <that cache>` from the
checkout beside this one (`--tests-checkout` names another; `--pack-only`
stops after packing and prints the command). That tool configures
`.build/<platform>-tsan` in the tests checkout against the cache, builds only
the programs the tests labelled `c_abi` and `tsan` run, and runs them with
`TSAN_OPTIONS=halt_on_error=1`, so any report fails its test. The `c_abi` set
is the same as `ctest -L c_abi`, which includes the conformance kit's thread
checks. Two tests are left out: `gui_forms_abi_csharp`, because the .NET host
is not instrumented and TSan's runtime must load at process start, and the
Windows-only `gui_forms_abi_msvc_consumer`.

The flags reach CMake through `CFLAGS`, `CXXFLAGS`, `OBJCXXFLAGS` and `LDFLAGS`
at the tree's first configure, as for the macOS sanitized tree above. The
tool refuses a `gui-forms-tsan` tree whose cache lacks `-fsanitize=thread`;
delete that tree and run again.

gui_forms_tests' `tools/sanitizer/tsan.supp` holds the suppressions. Like
`lsan.supp`, it may name frames inside system libraries only, each with the
host and the report it answers. A race in GUI.Forms code is a defect to fix, never a suppression.

To record a run, keep the tool's output and the host (OS version,
architecture, `clang --version`) with the results in the notes repository
(`results/`). Record a clean run and every report found and fixed.

## Windows builds and Wine

Windows builds target x86_64-pc-windows-msvc on STXlib's own runtime
(owner, 2026-11-24): no MinGW, Visual C++ tools or Windows SDK. Follow
[Installation](../user/install): `tools/own_runtime.py build` makes the
runtime (STXlib at the commit stx pins, libc++ 22.1.8 static with
`_LIBCPP_NO_VCRUNTIME`), and `cmake/llvm22-windows-msvc.cmake` with
`GUI_FORMS_MSVC_RUNTIME=OWN` builds against it. The include order is
libc++, clang's resource headers, STXlib's C library headers and STXlib's
Windows API headers, behind `-nostdinc`. Every link passes `-nostdlib` and
`/NODEFAULTLIB`, and names the runtime's archives and import libraries.

Every Windows executable and DLL links libc++ and STXlib's startup and C++
runtime statically and imports Windows system DLLs only, the C library
among them as `ucrtbase.dll` by name. Every GUI.Forms library and STXlib's
state are static archives linked into the program, so it is one module
(owner decision, 2026-11-15) and `llvm-objdump -p <binary>` lists only
Windows system DLLs. gui_forms_devkit's `programs/tools/package_programs.py` fails a package whose
binaries import any other DLL: a GUI.Forms, STXlib or bundled library, a
Microsoft C or C++ runtime (msvcrt, vcruntime, msvcp, the api-ms-win-crt
forwarders), or a GCC or LLVM runtime.

CI proves this on every Windows build (`tools/own_runtime.py evidence`).
The compiler's `#include` search list (`clang -v`) must be exactly those
four directories. No link map (`GUI_FORMS_LINK_MAP`, on in CI; lld-link
`/lldmap` beside every image, which names every input by its path) may name a MinGW, Visual C++ or Windows SDK
file. Every DLL the tree built, `gui_forms_abi1.dll` among them, must
import only system DLLs and `ucrtbase.dll`.

The target can be checked from a Mac: `tools/own_runtime.py build` cross
builds the runtime with Homebrew's `llvm@22` and `lld`, and a configure with
a toolchain file that sets `CMAKE_SYSTEM_NAME` to `Windows` before including
`cmake/llvm22-windows-msvc.cmake` compiles the tree for the target. Running
a program still needs Windows or Wine.

## One module per program

Each independently delivered program holds exactly one copy of any routine.
A C++ program links `Threading`, `Application` (the engine, its renderer and
the host), STXlib's state and the text stack statically, so `otool -L`,
`ldd` or the PE imports show only system libraries. A C or C# program loads
one library, the C ABI library (`gui_forms_abi1.dll`,
`libgui_forms_abi.so.1`, `libgui_forms_abi.1.dylib`), which holds its own
engine with the drawing ABI and its raster folded in, imports only system
libraries and exports exactly its allowlisted `gf_*` entry points and
`gd_get_api_v0` (the raster is installed through the drawing table's
`raster_initialize`), and on Windows the Win32 compatibility shim's user32
and gdi32 names (`IsWindow`, `GetDC`, `BitBlt` and the rest of
`src/surface/windows/win32_compat_shim.def`; the import libraries leave them
out). `cmake/CheckCAbiExports.cmake` and `cmake/CheckSystemImports.cmake`
check both statements; gui_forms_tests runs them against the test cache. A
process uses one or the other, never both. The localizable-literal plugin is
a Clang plugin loaded by the compiler and never ships in a program.

Release links prune what a program does not reach: sources are compiled
with `-ffunction-sections -fdata-sections` and linked with `--gc-sections`
(`-dead_strip` on macOS). `GUI_FORMS_LTO` (default on) adds link-time
optimisation where the toolchain is pinned for it (LLVM 22's lld on every
platform). gui_forms_devkit's `programs/tools/package_programs.py` strips the shipped binaries. On macOS
the C++ runtime is linked statically when the pinned runtime tree carries
`libc++.a` and `libc++abi.a` (`GUI_FORMS_STATIC_CXX_RUNTIME`); until then a
macOS package bundles the LLVM runtime dylibs and nothing else.

Windows builds target the Windows 10 API level (`_WIN32_WINNT=0x0A00`). A
Windows host build produces `GUI.Forms Gallery.exe` (one module), the
and a `fonts/` directory of font licences, all in the build directory.

To run a Windows build under Wine, copy its build directory to the Mac and
run the program from there: it loads no DLL of its own and no runtime
DLL. Run `wine "GUI.Forms Gallery.exe"`; add
`--mcp-script <file.jsonl>` to run an automation script (one tool call a
line, the library's MCP automation). The Gallery runs through the Win32
host's single-window entry (`host::run_windows`), which starts the
automation as `Application::run` does. Until 1.0.1 it did not, and every
demonstrator ignored `--mcp` and `--mcp-script` on every host.

`tools/run_windows_wine_smoke.sh <build-dir>` launches the gallery with an
automation script (`--mcp-script`) that clicks three page rows and three
controls by stable ID and captures the window; the run then closes through
the real `WM_CLOSE` path, and the script checks the log: six clicks, the
script's capture, the Win32 DIB
renderer with bundled fonts, no rejected host events, exactly one close
request, and a closed, shut-down host. The log and capture
(`wine-gallery-smoke.jsonl`, `wine-gallery-smoke-script.jsonl.1.png`) go to the build
directory or `GUI_FORMS_EVIDENCE_DIR`. `tools/run_windows_gallery_text_smoke.sh`
runs the Windows text test program (`gui_forms_windows_text_tests.exe`, built
by gui_forms_tests and copied into the build directory) under Wine before
driving the text page, and
`tools/run_windows_gallery_services_smoke.sh` drives the animation,
dispatcher and host-service pages. The scripts need `wine` (override with
`WINE_BINARY`) and `rg`. When the build is configured on macOS with `wine`
found, the `gui_forms_windows_wine_smoke` and
`gui_forms_windows_gallery_services_smoke` targets run the first and last of
these against the build directory. No CI job runs them. What CI checks on
every platform is that each packaged program, and each windowed installed
example, answers `--mcp` (`tools/check_mcp_launch.py`). With `llvm-objdump` available,
`gui_forms_windows_boundary_audit` rejects a gallery that imports a GPU or
managed runtime DLL or lacks `GDI32.dll` and `USER32.dll`.

## Install prefix and SDK layout

`cmake --install build` installs headers, libraries, the CMake package and the
font licences under `CMAKE_INSTALL_PREFIX`. The exact contents are listed in
[Installation](../user/install.md#what-gets-installed). CI installs into a scratch prefix and
then builds `examples/reference/` against it, so the installed package is
tested on every platform, not only the build tree.

### Embedded font pack

The GUI.Forms runtime reads its bundled faces from one font pack compiled into
Core, never from files beside the program. The build packs every face of
`assets/fonts/bundled-fonts.sha256` with the `stx_fontpack` tool, in the
groups of `assets/fonts/font-pack.manifest` (`cmake/FontPack.cmake`), and
the `gui_forms_font_pack_round_trip` test in gui_forms_tests checks the built
pack against the files. At run time the interface group (every Portsmouth face and Carlito) is
decoded on the first request for any face, every other group on the first
request for one of its faces, on the core worker pool. Each face is checked
against the CRC-32 the pack stores for it; a refused group is reported once
on stderr and by `bundled_font_pack_refusal()`, and its faces draw as missing.
A build configured with `-DGUI_FORMS_DEVELOPMENT_FONT_DIRECTORY=ON` reads the
directory `GUI_FORMS_FONT_DIR` names, or else this checkout's `assets/fonts`,
and falls back to the pack; shipped builds leave it off. GUI.Drawing's raster
C API reads the same pack, so no face file is installed, copied beside a
module, put in a program's bundle or folder, or packaged by
gui_forms_devkit's `programs/tools/package_programs.py`: only the licences and the attribution.

### Fallback fonts

The nine Noto fallback faces (Japanese and Korean CJK subsets, a minimal
Chinese face, minimal Arabic, Bengali, Devanagari and Gurmukhi faces, Hebrew
and Emoji) are about 5.7 MB of font bytes. `tools/fallback_font_subsets.py`
cuts the subsets from the upstream files and documents each character set. The install has two
licence components: `fonts` holds the other faces' licences and the
attribution, `fallback_fonts` the Noto OFL texts (`OFL-Noto.txt`,
`OFL-NotoSansCJKjp.txt`, `OFL-NotoEmoji.txt`).

- **SDK install.** `GUI_FORMS_INSTALL_FALLBACK_FONTS` is ON by default, so
  the pack embeds the faces and `cmake --install build` installs their
  licences. Configure with
  `-DGUI_FORMS_INSTALL_FALLBACK_FONTS=OFF` to leave the fallback faces out, or
  install the components one at a time with
  `cmake --install build --component fonts` and
  `--component fallback_fonts`. The installed package sets
  `GUIForms_FALLBACK_FONTS` to the producer's choice.
- **App packages.** A program packaged by gui_forms_devkit
  (`programs/tools/package_programs.py`, from `programs/packaging/programs.json`) sets
  `"fallback_fonts": true` or `false`, which must match the build's
  `GUI_FORMS_INSTALL_FALLBACK_FONTS`, and the packager stages the Noto
  licences to match. A package carries no face file.
- **Without them.** The runtime does not need the fallback faces. With
  `GUI_FORMS_INSTALL_FALLBACK_FONTS=OFF` the pack leaves them out too
  (`assets/fonts/font-pack-without-fallback.manifest`). They are
  optional faces, so a pack or directory is accepted without them, and text that
  only they cover falls back as it does for any missing face: it draws as
  missing glyphs. `bundled_font_diagnostics()` reports `fallback_faces_found`
  out of `fallback_faces_expected` for the most recent lookup, and
  `bundled_font_notice()` names the absent faces. The notice is recorded, not
  written to stderr, because leaving the faces out is a deliberate choice.

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

The project's boundaries are checked, not left to convention: C++20
everywhere, AppKit only in `src/surface/macos/`, Win32 only in
`src/surface/windows/`, X11 only in `src/surface/linux/`, Skia only in
`src/render/skia/`, PNG as the only image decoder, and no GPU API anywhere.
The checks are CMake scripts in `cmake/`: `check_renderer_boundaries.cmake`
(no Skia, Core Graphics, AppKit, Metal or OpenGL symbol in the
renderer-free core, and no GPU framework in the renderer benchmark's
dependencies; the `gui_forms_renderer_boundary_audit` target), `check_windows_boundaries.cmake`
(the `gui_forms_windows_boundary_audit` target), `CheckCAbiExports.cmake`,
`CheckSystemImports.cmake` and `CheckObjectSurface.cmake`. gui_forms_tests
runs them, with its header audits, against the test cache, so a change that
breaks a boundary fails there before it reaches review.

```{toctree}
:hidden:

../user/install
fast-builds
reusing-ci-caches
template-harness
```
