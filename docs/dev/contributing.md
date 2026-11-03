# Contributing

GUI.Forms accepts contributions through pull requests on GitHub. Before you
open one, read this page and the project's `AGENTS.md`, which states the
boundaries the build enforces. A change that honours the boundaries, carries
a test and reports what it measured is easy to review; a change that argues
for a new dependency or a new platform assumption is not.

## Boundaries that are not negotiable

- C++20. Objective-C++ only under `src/host/macos/`. Win32, COM, WIC, MSAA
  and UIA only under `src/host/windows/`. X11 only under `src/host/linux/`.
- No .NET, Java, browser engine or Godot dependency in the native runtime.
- No GPU: nothing compiles, links, probes or exposes Ganesh, Graphite, GL,
  Vulkan, Metal, Direct3D, Dawn or WebGPU.
- Skia only through the private CPU adapter in `src/render/skia/`; no Skia
  type in a public header, control API, DML, C ABI or renderer-neutral test.
- PNG (`stx_png`) is the only image codec, behind `src/core/image/`. HarfBuzz, FreeType, SheenBidi and libunibreak
  are the only text dependencies, pinned by revision.
- The core is retained. No immediate-mode control API, no perpetual redraw.
- The portable core and host protocol encode no AppKit, Win32, Wayland or X11
  assumption. Honest platform differences are capability-reported adapter
  behaviour.
- Controls draw only the bundled fonts. Do not add host font lookups; the
  GUI.Drawing C API's family lookup described in
  [Text engine](../user/text-engine) is the one existing exception.

Boundary tests run under `ctest`, so a violation fails before review.

## Code style

The project follows the house style, [`HOUSE_STYLE.md`](https://github.com/malkuth-software/notes/blob/main/HOUSE_STYLE.md), which can be
summarised as: spell the type, name the behaviour, write in execution order.
In practice:

- Explicit types at declarations; no `auto`. Distinguish types whose
  confusion causes bugs (byte offsets vs. character positions, identities vs.
  indices) with wrapper types.
- Named functions, methods and functors instead of lambdas and anonymous
  callbacks. Retained state has named fields.
- One recognisable operation per expression; calculate, then return.
- Initialize every variable. Group related data in records; use enums for
  alternatives.
- Validate shape, extent and capacity before a kernel, not per element. Keep
  allocation, logging and callbacks out of per-element loops.
- Explicit ownership and explicit session shutdown; declare context before
  the worker that borrows it.

`tools/check_house_style.py` reports spelling violations: `auto`, `->`,
trailing returns, structured bindings, lambdas, defaulted comparisons,
coroutines and ranges pipelines. CI runs it over every first-party directory
and fails on any finding, so run it before you push:

```sh
python3 -B tools/check_house_style.py include src demo tests examples tools file_manager_demoboard stx_vorbis
```

It is a review aid, not a certification: ownership, lifetimes and
initialization still need a human reader.

`tools/check_lifetime_rules.py` checks two lifetime rules the same way and
CI runs it beside the spelling scanner: a function that returns a pointer or
reference borrowed from a container or snapshot argument must delete its
rvalue overload, and a function pointer obtained with `GetProcAddress` or
`dlsym` must take its type from the function's declaration (`decltype`, or a
`static_assert` against it). The `sanitizers` job builds and runs the whole
suite on Linux under AddressSanitizer, UndefinedBehaviorSanitizer and
LeakSanitizer (`python tools/ci_build.py --sanitize`); any report fails it.
It runs only when requested by hand (`workflow_dispatch` with
`run_sanitizers`); the gate is the local sanitized run of every test when a
checkpoint is folded.

The same command is the standard local sanitizer tree, kept by every build
tree on every platform: it builds `.build/native-<host>-<arch>/gui-forms-sanitize`
with CI's flags and runs the suite, or with `--tests REGEX` builds and runs
only what the matching tests need, incrementally after the first build.
`--test-jobs N` runs N tests at once. `--skip-labels gallery_accessibility`
leaves out the Gallery-wide accessibility tests (each walks every Gallery page
through MSAA, UI Automation, AT-SPI or AX; together the slowest sanitized
tests by far) from a local run and from its narrowed build; CI passes no
label and still runs them. The sanitized objects carry
`-fdebug-compilation-dir=.`, so checkouts with the CI layout share them
through one `ccache`; a second checkout of the same sources compiles
almost nothing. A rerun with nothing changed skips the dependency checkout
and the configure. AddressSanitizer and
UndefinedBehaviorSanitizer run everywhere; LeakSanitizer runs on Linux and
macOS, since the Windows MinGW runtime has none. Host code is sanitized only
where it builds, so the Windows and macOS hosts depend on these local trees.

Sanitized trees carry line tables, not full debug information, by default
(`--debug-info lines`, `-gline-tables-only`): every report frame keeps its
file, line and column; objects and programs are about 60 to 70% smaller and
compiles about 28% faster than with `-g`. Frames of our code then lose their
namespaces and parameter types, and a debugger loses locals. While hunting a
defect, build with full `-g` and switch back afterwards:
`ci_build.py --sanitize --debug-info full`, or
`GUI_FORMS_SANITIZER_DEBUG_INFO=full` in the environment. A tree rebuilds when
the mode changes. On CI, the sanitizer job uses `full` when the head commit's
message contains `[sanitizer-full-g]`, and keys its cache by the mode. LeakSanitizer
suppressions (`tools/sanitizer/lsan.supp`) therefore name functions without a
namespace where the name allows.

Every test carries one area label, `area_<area>`: `core`, `codec`, `audio`,
`render`, `controls`, `docs`, `host`, `gui` (the Gallery, demoboard and lab
tests) or `a11y` (the accessibility clients and providers), so
`ctest -L area_gui` runs one area. `tools/test_areas.json` holds both the
name patterns that give the labels (`cmake/TestAreas.cmake`) and the rules
that map source paths to the areas whose tests read them. `--changed-from REF`
on a sanitized run builds and runs only the tests the paths changed since
`REF` (its merge base with `HEAD`, plus uncommitted and untracked files) can
affect; `python tools/change_selection.py --base REF --format areas` prints
the areas and the reason for each path, and with `--tree` and the default
format a `ctest -R` pattern. Core sources reach every area but `gui` and
`a11y`; controls, render and host sources reach those too; codec and audio
sources reach only their own area; a test source selects its own test;
documentation, planning and results reach no test. A path in the
touches-everything set (`CMakeLists.txt`, `cmake/`, `third_party/`,
`.github/`, `assets/`, the headers under `include/gui_forms/` outside the
controls, compression and audio directories, the selection tool and its
rules, `tools/ci_build.py`, `tools/sanitizer/`, and the shared test support
and fixtures) or a path no rule names runs every test. CI's sanitizer job
selects this way on pull requests and runs the whole suite on main.

## Tests and evidence

Every behavioural change ships with a test. Core and control behaviour is
tested renderer-free against the headless host; painting against the
identity and raster tests; host behaviour in the platform job for that host.
A visual claim ("looks right") is not evidence; a counter, a trace or a hash
is.

Performance claims follow the same rule. Nothing is called fast or
lightweight without the workload and the measurement being recorded, and
negative results are kept. Measurements and experiment reports are kept in
the project's notes repository; see [Evidence](evidence).

## Documentation

User documentation is this Sphinx site under `docs/`, built with
`sphinx-build -W`; see `docs/README.md` for how to build it locally. Write in
the present tense for a user of the library. Control pages are generated
from `manifests/controls.json` and the headers; add a control there, not in
the generated files. Architecture decisions are ADRs in the notes
repository, listed under [Architecture decisions](decisions).

## Workflow

1. Fork and branch from `main`.
2. Build and run `ctest` on at least one platform; CI runs macOS arm64, Linux
   x64, Linux arm64 and Windows x64.
3. Keep unrelated working-tree changes out of the commit. Preserve user files.
4. Describe in the pull request what changed, which tests cover it, and the
   exact commands and environment you used for any measurement.
5. Expect review to ask for the boundary and the test first and the feature
   second.

## Licence

GUI.Forms is MIT licensed. Bundled fonts carry their own licences in
`assets/fonts/`; third-party sources keep theirs under `third_party/` and
are installed under `share/licenses/`. A contribution is accepted under the
project licence.
