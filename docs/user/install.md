# Install

GUI.Forms is distributed as source. You build it once with the pinned LLVM 22
toolchain, install it to a prefix, and consume that prefix from your own CMake
project through `find_package(GUIForms)`. This page gets you from an empty
machine to an installed SDK. [Building from source](../building/index) covers
every option, the tests and the demonstration programs.

## Supported platforms

| Platform | Minimum | Host | Notes |
|---|---|---|---|
| macOS | 14.0, Apple silicon (arm64) only | AppKit | Default build on a Mac |
| Windows | 10 (x64) | Win32 | Native build in MSYS2 CLANG64 |
| Linux | Ubuntu 24.04 CI targets (x64, arm64) | X11 | Opt-in; needs X11 and ATK development packages |

Intel Macs are not supported. Wayland sessions need XWayland for the X11
host. The portable core (`Core`, `Controls`, `Drawing`, `Threading`) is host
independent, but its build still requires the pinned LLVM version below.

## Prerequisites

GUI.Forms requires **LLVM 22.1.x**. The configure step fails with any other
compiler, including the system Clang, because the compiler identity is part of
the compiler-cache contract. You also need CMake 3.25 or newer, Ninja, Git,
Python 3.12 or newer and, optionally, `ccache`.

macOS
: Install the Xcode command-line tools and `brew install cmake ninja ccache llvm@22`.
  Put `$(brew --prefix llvm@22)/bin` first on `PATH`. Homebrew's prebuilt
  libc++ targets a newer OS than GUI.Forms supports, so build the matching
  runtimes once and point the toolchain file at them:

  ```sh
  python3 tools/build_macos_runtimes.py --work /tmp/llvm-runtimes \
      --prefix "$PWD/.build/toolchain/llvm-22.1.8-macos14" --jobs 4
  export GUI_FORMS_LLVM_RUNTIME="$PWD/.build/toolchain/llvm-22.1.8-macos14"
  ```

Linux (Ubuntu 24.04)
: Add the `llvm-toolchain-noble-22` repository from apt.llvm.org, then
  install `clang-22 libc++-22-dev libc++abi-22-dev libunwind-22-dev cmake ninja-build ccache
  generate-ninja pkg-config libx11-dev libxext-dev libxrandr-dev
  libxcursor-dev libxfixes-dev libatk1.0-dev libatk-bridge2.0-dev`. Put
  `/usr/lib/llvm-22/bin` first on `PATH`. GUI.Forms builds against LLVM's
  libc++ 22 and libc++abi on Linux, as on macOS and Windows, never the
  distribution's libstdc++, and the C++ runtime is the harness's own, not
  the distribution's libc++ packages: that clang only bootstraps it. Build
  it once with that compiler and point `GUI_FORMS_LLVM_RUNTIME` at it:

  ```sh
  runtime="$PWD/.build/toolchain/$(python3 template_harness/harness/linux_runtime_manifest.py --directory-name)"
  python3 template_harness/harness/build_linux_runtimes.py --work "$PWD/.build/llvm-runtimes" \
      --prefix "$runtime" --jobs 4
  export GUI_FORMS_LLVM_RUNTIME="$runtime"
  ```

  It holds position-independent, hermetic static archives of libc++,
  libc++abi and libunwind from LLVM 22.1.8 for your C library (glibc or
  musl). `cmake/llvm22.cmake` refuses to configure without them, compiles
  against their headers and links them statically, so Linux programs carry
  libc++ and need no C++ runtime library installed, and `gui_forms_abi`
  carries it too. A C++ program that uses the SDK builds with the same
  toolchain file and runtime. On musl (Alpine 3.24) install `clang22
  compiler-rt` as the bootstrap compiler; the toolchain file also links
  compiler-rt there.

Windows
: Install MSYS2 and, in the CLANG64 environment, the packages listed in
  `tools/windows_toolchain.json` (clang 22.1.8, lld, cmake, ninja, ccache,
  winpthreads). Build from the CLANG64 shell.

On every platform export the compiler explicitly:

```sh
export CC=clang CXX=clang++ OBJCXX=clang++
```

## Configure, build, install

Run all commands from the repository root. Everything GUI.Forms takes from
the stx repository (the CPU renderer, the stx libraries and every third-party
source) is linked as one built package (`cmake/Stx.cmake`). Configuring fetches
the stx commit pinned in `stx.lock` and builds that package in the build tree,
once; a later configure reuses it while its inputs are unchanged. Pass
`-DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX=<path>` to build from a local stx
checkout instead, or `-DGUI_FORMS_STX_PREFIX=<dir>` to link a package that is
already built (the configure stops unless it was built from `stx.lock`'s commit
with this configuration's choices). Use the configure command for your
platform.

macOS (with `GUI_FORMS_LLVM_RUNTIME` set as above):

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/llvm22.cmake \
    -DCMAKE_INSTALL_PREFIX="$PWD/sdk"
```

Linux:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/llvm22.cmake \
    -DCMAKE_INSTALL_PREFIX="$PWD/sdk" \
    -DGUI_FORMS_ENABLE_LINUX_HOST=ON
```

Windows (in MSYS2 CLANG64):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/llvm22.cmake \
    -DCMAKE_INSTALL_PREFIX="$PWD/sdk" \
    -DGUI_FORMS_ENABLE_WINDOWS_HOST=ON
```

Build, test and install the selected configuration:

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

The CPU renderer, `stx_skia/`, builds with the tree from the stx checkout,
as do HarfBuzz, FreeType, SheenBidi and MD4C; libunibreak (also in stx) is
needed only for the optional development text-mask profile. Linux native tests need an X11 display;
use `xvfb-run -a ctest --test-dir build --output-on-failure` on a headless runner.

Pass the absolute path to the same `cmake/llvm22.cmake` to every consumer
project. On macOS the toolchain also fixes the SDK, the arm64
architecture, the 14.0 deployment target and the bundled libc++ runtime, and a
consumer built with different settings is not binary compatible.

## What gets installed

| Path under the prefix | Content |
|---|---|
| `include/gui_forms/` | Public portable headers |
| `${CMAKE_INSTALL_LIBDIR}/` (usually `lib/`) | `GUIForms::Core`, `Controls`, `Drawing`, `Threading`, `GUIForms::Application` (when a native host is enabled) and `GUIForms::Audio` (when enabled), all static archives linked into the program |
| `${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms/` | `GUIFormsConfig.cmake` and exported target files |
| C ABI files, when a native host is enabled | `include/gui_forms/c/gf_abi.h` (with the other headers), the one shared library, self-contained (`libgui_forms_abi.1.dylib`, `libgui_forms_abi.so.1`, or `gui_forms_abi1.dll` in `bin/` with its import libraries; on Windows it also carries the Win32 compatibility shim), `lib/pkgconfig/gui_forms_abi.pc`, and `share/GUIForms/abi/` (the audited manifest `gf_abi.json` and the export allowlist) |
| `share/GUIForms/fonts/` | The bundled faces' licences and attribution (the faces themselves are compiled into the library), installed with a native host; exported as `GUIForms_FONT_DIR`. The Noto fallback faces' OFL texts are the separate `fallback_fonts` component, present when `GUI_FORMS_INSTALL_FALLBACK_FONTS` was ON (the default; `GUIForms_FALLBACK_FONTS` says which) |
| `share/licenses/` | Third-party notices |

Nothing from Skia, AppKit, Win32 or X11 appears in the installed headers;
those remain private adapters inside the `Application` library.

### Checked borrowed views

A build configured with `GUI_FORMS_CHECKED_BORROWS=ON` (the sanitizer tree:
`tools/ci_build.py --sanitize` sets it) checks every borrowed view against its
owner ([Retained tree](retained-tree.md#borrowed-views)). The borrowed types
differ between the two modes, so a consumer is built the same way as the SDK
it links: an installed checked SDK adds `GUI_FORMS_CHECKED_BORROWS=1` to the
compile definitions of its `Core`, `Controls` and `Application` targets, and `GUIForms_CHECKED_BORROWS`
says which mode the prefix holds. A checked consumer linked against an ordinary
library fails at link time, naming `gui_forms_checked_borrows_v1`, instead of
misbehaving at run time.

### Package components

`find_package(GUIForms CONFIG REQUIRED COMPONENTS ...)` accepts these
components. A component the prefix lacks fails at configure time with a
message saying which build provides it.

| Component | Target | Installed when |
|---|---|---|
| `Core`, `Controls`, `Drawing`, `Threading` | `GUIForms::Core`, `::Controls`, `::Drawing`, `::Threading` | Always |
| `Application` | `GUIForms::Application` | A native host is enabled |
| `Audio` | `GUIForms::Audio` | `GUI_FORMS_BUILD_AUDIO` (on in CI and in `build_sdk.py`) |
| `TextMasks` | `GUIForms::TextMasks` | `GUI_FORMS_BUILD_TEXT_MASKS` with a native host (`build_sdk.py --features all`) |
| `AudioLoopTransport` | `GUIForms::AudioLoopTransport` | `GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT` with audio (`build_sdk.py --features all`) |
| `FreeType`, `HarfBuzz` | `GUIForms::FreeType`, `GUIForms::HarfBuzz` | `GUI_FORMS_INSTALL_TEXT_STACK` with the text engine (`build_sdk.py --features all`) |
| `CAbi` | `GUIForms::CAbi` | A native host is enabled: the C ABI library, which links `Application` |
| `CAbiConformance` | `GUIForms::CAbiConformance` | With `CAbi`: the C ABI conformance kit ([Running the C ABI conformance kit](c-abi-conformance-kit)) |
| `GalleryModel`, `Lab`, `DemoControls` | `GUIForms::GalleryModel`, `::Lab`, `::DemoControls` | `GUI_FORMS_INSTALL_GALLERY_MODEL` with `GUI_FORMS_INSTALL_LAB` (`build_sdk.py --features all`): the Gallery's model, for a project that builds the Gallery program from an installed SDK |

`TextMasks` is an interface on `Application`, which already contains the
text-mask service. Link it beside `Application`, never beside `Core`; see
[Consume the SDK](consume-the-sdk). It installs `gui_forms/text_mask.hpp`
and `gui_forms/text_mask/`. It also installs the prepared-text types, layout
and service headers that the text-mask API includes for its vocabulary;
the prepared-text service itself is not installed.

`AudioLoopTransport` names an `Audio` library built with the loop transport.
That `Audio` carries the `GUI_FORMS_AUDIO_LOOP_TRANSPORT` definition, which
`audio.hpp` needs to declare the transport, and installs
`gui_forms/audio/loop_transport/`.

```cmake
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application TextMasks Audio AudioLoopTransport)
target_link_libraries(my_app PRIVATE GUIForms::Application GUIForms::TextMasks GUIForms::AudioLoopTransport)
```

`FreeType` and `HarfBuzz` are the pinned FreeType 2.14.2 and HarfBuzz 14.6.0
that GUI.Forms' text engine links, for a product that calls them directly.
They install as the static libraries GUI.Forms builds, with its options:
FreeType without zlib, so it refuses gzip-compressed fonts, and HarfBuzz with
its FreeType functions (`hb-ft.h`). Their headers keep the upstream layouts,
`include/freetype2/` and `include/harfbuzz/`, and each target puts its
directory on the include path, so `#include <ft2build.h>` and
`#include <hb.h>` work. `GUIForms::HarfBuzz` links `GUIForms::FreeType`.
GUI.Forms' own libraries hold their own copies, so pass no FreeType or
HarfBuzz object between them and your code. The licences are
`FreeType-FTL.txt` and `HarfBuzz-COPYING.txt` in `share/licenses/GUIForms/`.
FreeType's licence asks a product that ships it to credit it in its
documentation; `THIRD_PARTY_NOTICES.md` there carries the credit.

```cmake
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS FreeType HarfBuzz)
target_link_libraries(my_compositor PRIVATE GUIForms::HarfBuzz GUIForms::FreeType)
```

## The source SDK

Each tested release publishes one platform-neutral source SDK,
`gui-forms-sdk-<version>-source.tar.gz`, listed in its `SHA256SUMS.txt`. It
holds one directory, `gui-forms-sdk-<version>/`, containing:

- the release commit's tree, as `git archive` writes it;
- the pinned third-party sources the build would otherwise fetch, each at
  the path its fetch script or CMake file reads first, so configuring from
  the archive fetches none of them:

  | Path | Source | Pinned in |
  |---|---|---|
  | `stx/` | the stx repository: `stx_skia/`, the stx libraries and every third-party source (HarfBuzz without `test/` and `perf/`, FreeType, libunibreak, MD4C, SheenBidi, the Unicode data) | `stx.lock` |
  | `third_party/miniaudio/` | miniaudio | `cmake/Audio.cmake` |

- `SDK_MANIFEST.json`, which names the build entry point (`"build":
  "tools/build_sdk.py"`) and the top directory. It also gives each vendored
  source's path, upstream, pinned commit and tree digest, and lists what a
  build still downloads (`network_inputs`) or needs installed
  (`prebuilt_inputs`). Its `revision` is the release commit, and
  `compiler_cache` names that commit's compiler-cache pre-release (see
  [Build with CI's compiler cache](#build-with-cis-compiler-cache)).

The archive carries no Git metadata and no build output. Build and install
GUI.Forms for your host from it:

```sh
tar -xzf gui-forms-sdk-<version>-source.tar.gz
cd gui-forms-sdk-<version>
python3 tools/build_sdk.py --target <platform> --prefix "$HOME/gui-forms-sdk"
```

`<platform>` is one of `macos-arm64`, `windows-x64`, `linux-x64` and
`linux-arm64`. Only the platform of the machine running the build is
accepted until per-target cross-compile toolchains exist.

`build_sdk.py` runs the target's own preparation and points the configure
step at the archive's `stx/` checkout. It then configures the toolkit with
the options CI uses, minus the tests and the demonstration programs, builds
it with `--jobs` jobs in `--work` (default `.build/sdk-<platform>`), and
installs it into `--prefix`. The [Prerequisites](#prerequisites) still apply,
including `GUI_FORMS_LLVM_RUNTIME` on macOS. Consume the installed prefix
as in [Verify the installation](#verify-the-installation).

`--features` chooses which optional components are built:

`default`
: The components CI builds and tests: `Core`, `Controls`, `Drawing`,
  `Threading`, the host's `Application` with the HarfBuzz/FreeType text
  engine, and `Audio` with the stx Vorbis decoder.

`all`
: The default set, plus every optional component a product may link:
  `TextMasks` (`GUI_FORMS_BUILD_TEXT_MASKS`), `AudioLoopTransport`
  (`GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT`), `FreeType` and `HarfBuzz`
  (`GUI_FORMS_INSTALL_TEXT_STACK`), and `GalleryModel` with `Lab` and
  `DemoControls` (`GUI_FORMS_INSTALL_GALLERY_MODEL`,
  `GUI_FORMS_INSTALL_LAB`). One such install serves every product;
  template_harness's lockstep build uses it.

The prepared-text service (`GUI_FORMS_BUILD_PREPARED_TEXT`) is in neither
set. It cannot be installed, and it changes `Application`'s declarations for
every consumer. The probes, benchmarks, fuzzers and diagnostics build
programs or change behaviour rather than adding a component, so they are left
out as well.

### What a source build still downloads

| Input | Platforms | From |
|---|---|---|
| The LLVM 22.1.8 source tarball, digest pinned in `tools/macos_runtime_manifest.py` | macOS, once | github.com/llvm/llvm-project releases, by `tools/build_macos_runtimes.py`, to build the macOS 14 runtime |

The CPU renderer, `stx_skia/`, and the text stack are in the archive's
`stx/` checkout, so nothing is downloaded for them. On every platform the toolchain itself (LLVM 22.1, CMake,
Ninja, Git and Python) is installed as in [Prerequisites](#prerequisites),
not shipped.

A fully offline build therefore needs only the installed toolchain, and on
macOS an already built `GUI_FORMS_LLVM_RUNTIME` prefix.

### Build with CI's compiler cache

CI compiles every tested commit, and each platform's native job of the push
to main publishes its ccache directory in the commit's compiler-cache
pre-release, `build-<commit>`, as `gui-forms-cache-<platform>.tar.gz`
(`.ccache/` and `cache-manifest.json`). It does so as soon as that platform
passes, and the release that carries the source SDK is published only after
all four have passed. `SDK_MANIFEST.json` records it:

```json
"compiler_cache": {
  "release": "build-<revision>",
  "asset": "gui-forms-cache-{platform}.tar.gz",
  "platforms": ["macos-arm64", "windows-x64", "linux-x64", "linux-arm64"],
  "layout": {"ccache_basedir": "the workspace", "source": "gui_forms",
             "work": ".build/native-{platform}", "note": "..."}
}
```

`release` is `null` when the SDK was packaged outside that push (a tag
build), which cannot promise the caches. A build from the SDK hits the cache
only in CI's layout, with the same LLVM 22.1.8 toolchain:

- the SDK's top directory unpacked as `<workspace>/gui_forms`;
- the cache archive extracted into `<workspace>` (it creates `.ccache/`);
- `CCACHE_DIR=<workspace>/.ccache`, `CCACHE_BASEDIR=<workspace>`,
  `CCACHE_COMPILERCHECK=content`, and `ccache` as the compiler launcher
  (`CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER`);
- `python3 gui_forms/tools/build_sdk.py --target <platform> --prefix <dir>
  --work <workspace>/.build/native-<platform>`, which compiles in
  `.build/native-<platform>/gui-forms`,
  as CI does;
- the toolchain's own headers at CI's paths, since they lie outside the
  workspace and their absolute paths are hashed: Homebrew's `llvm@22` on
  macOS, `/usr/lib/llvm-22` on Linux, and on Windows the CLANG64 toolchain
  extracted at `D:\a\_temp\msys64`, where CI's runners restore it.

The cache only accelerates: the source stays authoritative, and a missing
archive, a wrong digest or another layout just means compiling more, with
the same result. Check the archive against the digest GitHub lists for the
asset before extracting it.

## Verify the installation

The repository ships standalone consumer examples that use only the installed
package. Build them against your prefix:

```sh
cmake -S examples/reference -B build-examples -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PWD/sdk" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/llvm22.cmake"
cmake --build build-examples --parallel
ctest --test-dir build-examples --output-on-failure
```

Three of those tests deliberately configure a consumer that links both the
static core and `Application`; they pass only when CMake rejects the
combination. That rejection is the installed SDK working as intended. See
[Consume the SDK](consume-the-sdk) for the linking rule behind it.
