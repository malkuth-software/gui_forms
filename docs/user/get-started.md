(get-started)=
# Get started

You build GUI.Forms applications with the devkit,
[malkuth-software/gui_forms_devkit](https://github.com/malkuth-software/gui_forms_devkit).
You do not build GUI.Forms itself: the devkit fetches a prebuilt GUI.Forms
SDK and a compiler cache, and builds only your code. This page takes you from
an empty machine to a running program, then to an app of your own.

## The devkit

The devkit is a public, MIT-licensed repository that holds:

- the **devkit harness** (`harness/`): the bootstrap, the SDK fetch, the
  compiler cache restore, the build guard, the app template and its
  reference app;
- **Pocket Reference** (`pocket_reference/`), the demonstrator views
  (`views/`) and the programs (`programs/`), all built against the same SDK
  your app uses.

`devkit.toml` pins GUI.Forms by major version (1) with a floor. A build takes
the newest green GUI.Forms build in that range: the newest build whose SDK
and cache are published for every platform. GUI.Forms makes no breaking
change within a major, so the devkit follows each new build without a pin
change.

You need Git, Python 3.11 or later, CMake 3.25 or later, Ninja and ccache.
On Windows you also need Git for Windows, whose Git Bash runs the scripts.

## 1. Bootstrap

One command clones the devkit and sets up its workspace: the pinned LLVM 22
toolchain, the C++ runtime, the compiler cache and the build environment.

macOS:

```sh
git clone https://github.com/malkuth-software/gui_forms_devkit && sh gui_forms_devkit/harness/bootstrap.sh --workspace gui_forms_devkit --project gui_forms_devkit
```

Linux:

```sh
git clone https://github.com/malkuth-software/gui_forms_devkit && sh gui_forms_devkit/harness/bootstrap.sh --workspace gui_forms_devkit --project gui_forms_devkit
```

Windows (PowerShell):

```powershell
git clone https://github.com/malkuth-software/gui_forms_devkit; powershell -File gui_forms_devkit\harness\bootstrap.ps1 --workspace gui_forms_devkit --project gui_forms_devkit
```

Add `--dry-run` to see every step without changing anything. Running the
command again updates the workspace instead of starting over. On Linux the
bootstrap prints the packages to install with `sudo` rather than installing
them itself.

The bootstrap writes the build environment to `.ci/local-build-env.sh` (and
`.ci/local-build-env.ps1` for PowerShell). Load it in every shell you build
from, so the compiler and the cache settings match the ones the cache was
made with.

## 2. Build

The build fetches the prebuilt SDK and compiles only the devkit's own code;
GUI.Forms is not compiled.

macOS:

```sh
cd gui_forms_devkit
. .ci/local-build-env.sh
python3 -B harness/harness/resolve_gui_forms.py resolve
python3 -B harness/harness/resolve_gui_forms.py fetch --platform macos-arm64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=harness/harness/cmake/llvm22.cmake
cmake --build build
```

Linux (on an arm64 machine, use `--platform linux-arm64`):

```sh
cd gui_forms_devkit
. .ci/local-build-env.sh
python3 -B harness/harness/resolve_gui_forms.py resolve
python3 -B harness/harness/resolve_gui_forms.py fetch --platform linux-x64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=harness/harness/cmake/llvm22.cmake
cmake --build build
```

Windows (PowerShell):

```powershell
cd gui_forms_devkit
. .ci\local-build-env.ps1
python -B harness/harness/resolve_gui_forms.py resolve
python -B harness/harness/resolve_gui_forms.py fetch --platform windows-x64
```

On Windows the configure step takes GUI.Forms' Windows toolchain file with
its own C++ runtime. The devkit's CI prepares it (its `windows-own-runtime`
action); the harness has no local command for it yet.

`resolve` picks the GUI.Forms build and records it in
`.ci/gui_forms-resolved.json`. `fetch` downloads that build's SDK, checks its
SHA-256 and unpacks it at `.sdk/gui-forms-sdk`. The SDK is always unpacked at
that path, whatever the build, so the compiler cache keeps hitting when
GUI.Forms moves on.

### The build guard

The devkit's CMake module (`harness/harness/cmake/DevkitBuildGuard.cmake`)
checks the build when configuring ends and again before the first compile.
If the build would compile GUI.Forms, or would not use the prebuilt SDK and
the restored compiler cache, it stops with:

```text
devkit build guard: likely misconfiguration preventing efficient incremental building.
```

Each finding below that line names what does not match and how to fix it.
The usual causes:

- the SDK is missing or is not the resolved build: run `resolve` and
  `fetch` again;
- the build environment is not loaded, so the compiler, the ccache settings
  or the locale differ from the ones the cache was made with: load
  `.ci/local-build-env.sh` (or `.ps1`) and configure again;
- ccache is not installed, or is not the compiler launcher;
- a GUI.Forms source tree is on the build's path, so its sources would be
  compiled.

The guard works the same when an IDE runs the build. A missing C++ runtime
on macOS or Linux is a warning, not a stop: the bootstrap builds one, once
per workspace. To build anyway, configure with
`-DGUI_FORMS_DEVKIT_ALLOW_FULL_BUILD=ON` or set
`GUI_FORMS_DEVKIT_ALLOW_FULL_BUILD=1` in the environment, and expect a much
longer build.

## 3. Run

Pocket Reference opens on the GUI.Forms reference.

macOS:

```sh
open build/pocket_reference/PocketReference.app
```

Linux:

```sh
build/pocket_reference/PocketReference
```

Windows:

```powershell
build\pocket_reference\PocketReference.exe
```

The same build holds the Gallery, Explore, Correspondence and the other
demonstrators under `build/programs/`.

## 4. Your own app

The devkit's app template (`harness/templates/app/`) is what every app
starts from. Its reference app, `harness/example_app/`, is the template
filled in: a greeter form, its test, the interaction test that holds hover
and drag to no layout work, and `reference/`, the retained custom control to
copy when you write your own. The template's `AGENTS.md` carries the rules
in its "Building on GUI.Forms" section, for you and for any coding agent you
work with.

The harness's `new_app` command, which turns the template into a new
repository, works on Malkuth's own repositories only. Until it can create an
app in yours, start from the reference app's sources, beside the devkit:

```sh
mkdir hello_forms && cp -R gui_forms_devkit/harness/example_app/src gui_forms_devkit/harness/example_app/tests gui_forms_devkit/harness/example_app/reference hello_forms/
```

Give it a `CMakeLists.txt` that builds through the devkit's module, so it
uses the same SDK, cache and build guard:

```cmake
cmake_minimum_required(VERSION 3.25)
project(HelloForms LANGUAGES CXX)

include("${GUI_FORMS_DEVKIT_WORKSPACE}/harness/harness/cmake/DevkitBuildGuard.cmake")
gui_forms_devkit_use_sdk()
gui_forms_devkit_cache_launcher()

set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
find_package(GUIForms 1.0 CONFIG REQUIRED COMPONENTS Application Controls)
enable_testing()

add_executable(greeter_tests src/greeter.cpp tests/greeter_tests.cpp)
target_compile_features(greeter_tests PRIVATE cxx_std_20)
target_include_directories(greeter_tests PRIVATE src)
target_link_libraries(greeter_tests PRIVATE GUIForms::Controls)
add_test(NAME greeter_tests COMMAND greeter_tests)

add_executable(interaction_tests src/greeter.cpp reference/link_page.cpp reference/sample_document.cpp
    tests/interaction_tests.cpp)
target_compile_features(interaction_tests PRIVATE cxx_std_20)
target_include_directories(interaction_tests PRIVATE src reference)
target_link_libraries(interaction_tests PRIVATE GUIForms::Controls)
add_test(NAME interaction_tests COMMAND interaction_tests)

add_executable(hello_forms src/greeter.cpp src/main.cpp)
target_compile_features(hello_forms PRIVATE cxx_std_20)
target_include_directories(hello_forms PRIVATE src)
target_link_libraries(hello_forms PRIVATE GUIForms::Application)
if(APPLE)
    set_target_properties(hello_forms PROPERTIES MACOSX_BUNDLE TRUE)
    set(hello_forms_fonts "$<TARGET_BUNDLE_CONTENT_DIR:hello_forms>/Resources/fonts")
else()
    set(hello_forms_fonts "$<TARGET_FILE_DIR:hello_forms>/fonts")
endif()
add_custom_command(TARGET hello_forms POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}" "${hello_forms_fonts}")

gui_forms_devkit_build_guard()
```

Then, from the directory that holds `gui_forms_devkit/` and `hello_forms/`
(macOS and Linux):

```sh
. gui_forms_devkit/.ci/local-build-env.sh
cmake -S hello_forms -B hello_forms/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=gui_forms_devkit/harness/harness/cmake/llvm22.cmake -DGUI_FORMS_DEVKIT_WORKSPACE="$PWD/gui_forms_devkit"
cmake --build hello_forms/build
ctest --test-dir hello_forms/build
```

Run it with `open hello_forms/build/hello_forms.app` on macOS, or
`hello_forms/build/hello_forms` on Linux.

## Next

- [Your first window](first-window) builds the shape GUI.Forms recommends:
  a form document, the model it binds, a theme and an event handler.
- [Custom controls](custom-controls) is the tutorial for writing a control
  of your own, after checking that no stock control fits.
- [Mockups](mockups) shows a form and a theme from files alone, with no
  compile: the devkit's mockup factory reloads them as you save.
- [Talk to a running program](talk-to-a-program) connects an agent or a
  script to any GUI.Forms program over MCP.
