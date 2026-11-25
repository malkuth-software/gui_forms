(get-started)=
# Get started

You build GUI.Forms applications with the devkit,
[malkuth-software/gui_forms_devkit](https://github.com/malkuth-software/gui_forms_devkit).
You do not build GUI.Forms itself: the devkit fetches a prebuilt GUI.Forms
SDK and builds only your code. This page takes you from an empty machine to
a running app of your own.

## The devkit

The devkit is a public, MIT-licensed repository that holds:

- the **devkit harness** (`harness/`): the bootstrap, the app template and
  its reference app, the SDK fetch, the build guard and the checks;
- **Pocket Reference** (`pocket_reference/`), the demonstrator views
  (`views/`) and the programs (`programs/`), built against the same SDK your
  app uses.

An app pins GUI.Forms by major version with a floor, in its `devkit.toml`. A
build takes the newest green GUI.Forms build in that range: the newest build
whose SDK is published for every platform. GUI.Forms makes no breaking
change within a major, so an app follows new builds without a pin change.

You need Git, Python 3.11 or later, CMake 3.25 or later, Ninja and ccache.
On macOS and Linux you also need LLVM 22.1.8 (clang) on `PATH`. On Windows
you need Git for Windows, whose Git Bash runs the bootstrap; the build
restores the LLVM toolchain itself.

## 1. Bootstrap

Clone the devkit.

macOS:

```sh
git clone https://github.com/malkuth-software/gui_forms_devkit
```

Linux:

```sh
git clone https://github.com/malkuth-software/gui_forms_devkit
```

Windows (PowerShell):

```powershell
git clone https://github.com/malkuth-software/gui_forms_devkit
```

## 2. Create your app

The bootstrap's `new-app` command makes an app from the devkit's template
in a new directory, copies the devkit into it at `.devkit/`, and makes the
first commit with `git init`. It pushes nothing and adds no remote.

macOS and Linux:

```sh
sh gui_forms_devkit/harness/bootstrap.sh new-app hello_forms --license mit
```

Windows (PowerShell):

```powershell
powershell -File gui_forms_devkit\harness\bootstrap.ps1 new-app hello_forms --license mit
```

The name is in snake case. The app goes in `./hello_forms` unless you name
a directory after it. `--title` sets the window title, `--license` picks
`mit`, `proprietary`, `polyform-noncommercial` or `none` (the default), and
`--strict` turns on the devkit's stricter house checks. `--dry-run` shows
what it would write.

The app holds:

| Path | What |
|---|---|
| `src/`, `tests/` | a greeter form, its entry point, the form's test and the interaction test, which holds hover and drag to no layout work |
| `reference/` | the reference custom control, a retained control to copy when you write your own |
| `CMakeLists.txt`, `build.py` | the build against the prebuilt SDK, with the build guard |
| `devkit.toml` | the GUI.Forms pin (major and floor) and the checks' profile |
| `harness.toml`, `harness.lock` | the project file the devkit's tools read, and the devkit commit the app uses |
| `AGENTS.md`, `CLAUDE.md` | the rules for building on GUI.Forms, for you and for any coding agent you work with |

## 3. Build

macOS and Linux:

```sh
cd hello_forms
python3 build.py --jobs 4
```

Windows (PowerShell):

```powershell
cd hello_forms
python build.py --jobs 4
```

`build.py` resolves the GUI.Forms pin to one build, downloads that build's
SDK, checks its SHA-256 and unpacks it at `.sdk/gui-forms-sdk`. It then sets
up the toolchain and the C++ runtime, writes the build environment to
`.ci/local-build-env.sh` (and `.ps1`), configures with CMake and Ninja,
builds and runs the tests. GUI.Forms is not compiled.

The first build on macOS and Linux also builds the C++ runtime once, which
takes several minutes. On Windows it restores GUI.Forms' published LLVM
toolchain and its own C++ runtime, and builds the runtime only when no
published one fits. To do that Windows step on its own, run
`powershell -File .devkit\harness\bootstrap.ps1 own-runtime --workspace .`
in the app after a first build: it prints the runtime settings and the
`CMAKE_TOOLCHAIN_FILE` to configure with.

Later builds stay on the same GUI.Forms build until you move them with
`python3 build.py --resolve`; `--floor` builds against the floor itself.
`--configure-only`, `--no-tests` and `--dry-run` do what they say.
`python3 build.py check` runs the checks for house style, lifetime rules and
retained mode over `src`, `tests` and `reference`.

### The build guard

The devkit's CMake module (`DevkitBuildGuard.cmake`, included by the app's
`CMakeLists.txt`) checks the build when configuring ends and again before
the first compile. If the build would compile GUI.Forms, or would not use
the prebuilt SDK and the compiler cache, it stops with:

```text
devkit build guard: likely misconfiguration preventing efficient incremental building.
```

Each finding below that line names what does not match and how to fix it.
The usual causes:

- the SDK is missing or is not the resolved build: run
  `python3 build.py --resolve`;
- the compiler, the ccache settings or the locale differ from the ones the
  SDK was built with, usually because a build ran outside `build.py` without
  the build environment: load `.ci/local-build-env.sh` (or `.ps1`) first;
- ccache is not installed, or is not the compiler launcher;
- a GUI.Forms source tree is on the build's path, so its sources would be
  compiled.

The guard works the same when an IDE runs the build: configure once with
`python3 build.py --configure-only`, then point the IDE at the build
directory. A missing C++ runtime on macOS or Linux is a warning, not a stop.
To build anyway, run `python3 build.py --allow-full-build`, configure with
`-DGUI_FORMS_DEVKIT_ALLOW_FULL_BUILD=ON` or set
`GUI_FORMS_DEVKIT_ALLOW_FULL_BUILD=1`, and expect a much longer build.

## 4. Run

macOS:

```sh
open .build/native-macos-arm64/hello_forms.app
```

Linux (on arm64, `native-linux-arm64`):

```sh
.build/native-linux-x64/hello_forms
```

Windows:

```powershell
.build\native-windows-x64\hello_forms.exe
```

## Pocket Reference and the demonstrators

The devkit builds its own programs the same way. One command sets up its
workspace and builds Pocket Reference, the Gallery, Explore, Correspondence
and the other demonstrators against the prebuilt SDK.

macOS and Linux:

```sh
sh gui_forms_devkit/harness/bootstrap.sh --workspace gui_forms_devkit --project gui_forms_devkit
```

Windows (PowerShell):

```powershell
powershell -File gui_forms_devkit\harness\bootstrap.ps1 --workspace gui_forms_devkit --project gui_forms_devkit
```

Pocket Reference is then at
`gui_forms_devkit/.build/native-<platform>/pocket_reference/` and the other
programs under `programs/` beside it.

## Next

- [Your first window](first-window) builds the shape GUI.Forms recommends
  in your app: a form document, the model it binds, a theme and an event
  handler.
- [Custom controls](custom-controls) is the tutorial for writing a control
  of your own, after checking that no stock control fits.
- [Mockups](mockups) shows a form and a theme from files alone, with no
  compile: the devkit's mockup factory reloads them as you save.
- [Talk to a running program](talk-to-a-program) connects an agent or a
  script to any GUI.Forms program over MCP.
