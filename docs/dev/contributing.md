# Contributing

GUI.Forms accepts contributions through pull requests on GitHub. Before you
open one, read this page and the project's `AGENTS.md`, which states the
boundaries the build enforces. A change that honours the boundaries, carries
a test and reports what it measured is easy to review; a change that argues
for a new dependency or a new platform assumption is not.

## Boundaries that are not negotiable

- C++20. Objective-C++ only in the macOS host (`src/surface/macos/`, and the
  macOS program entry points in `src/host/macos/`). AppKit only under
  `src/surface/macos/`. Win32, COM, WIC, MSAA and UIA only under
  `src/surface/windows/`. X11 and Wayland only under `src/surface/linux/`.
  The rest of `src/host/` is platform-neutral host logic.
- No .NET, Java, browser engine or Godot dependency in the native runtime.
- No GPU: nothing compiles, links, probes or exposes Ganesh, Graphite, GL,
  Vulkan, Metal, Direct3D, Dawn or WebGPU.
- Skia only through the private CPU adapter in `src/render/skia/`; no Skia
  type in a public header, control API, DML, C ABI or renderer-neutral test.
- PNG (`stx_png`) is the only image codec, behind `src/core/image/`. There are no
  third-party text dependencies: font reading, shaping, hinting, bidi, line
  breaking, script runs and Markdown are STXlib's (since 2026-11-16).
- The core is retained. No immediate-mode control API, no perpetual redraw.
- The portable core and host protocol encode no AppKit, Win32, Wayland or X11
  assumption. Honest platform differences are capability-reported adapter
  behaviour.
- Interface text draws only the bundled fonts, and programs cannot
  register fonts with the interface engine (the owner's interface and
  content split, 2026-11-13). Do not add host font lookups or a font
  registration API for interface text; the GUI.Drawing C API's family
  lookup described in [Text engine](../user/text-engine) serves a program's
  own drawing and is the one existing exception.

The boundary checks (the `cmake/` scripts that [Build from
source](../building/index.md#dependency-boundaries) lists, and the header
audits) run in
[gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests) against
the test cache, so a violation fails before review.

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
coroutines and ranges pipelines. The `house-style` CI job runs it over every
first-party directory, with the six other checks below, and fails on any
finding (the standard-surface census only reports there for now), so run
all seven before you push:

```sh
python3 -B tools/check_house_style.py include src lab demo examples tools fuzz
python3 -B tools/check_lifetime_rules.py include src lab demo examples tools
python3 -B tools/export_annotations.py check
python3 -B tools/control_registry.py members --check
python3 -B tools/abi_class_tables.py --check
python3 -I tools/check_std_rules.py --config tools/std_rules.toml
python3 -I tools/std_surface_census.py check
```

`AGENTS.md` says what each one checks and how to fix a finding;
[Repository tools](tools.md) describes the tools. The spelling scanner is a
review aid, not a certification: ownership, lifetimes and initialization
still need a human reader.

`tools/check_lifetime_rules.py` checks two lifetime rules: a function that
returns a pointer or reference borrowed from a container or snapshot
argument must delete its rvalue overload, and a function pointer obtained
with `GetProcAddress` or `dlsym` must take its type from the function's
declaration (`decltype`, or a `static_assert` against it).

## Sanitizers

Every build tree keeps a sanitizer tree beside its ordinary one, on every
platform it builds on:

```sh
python3 -B tools/ci_build.py --sanitize --test-cache <dir>
```

It builds `.build/native-<host>-<arch>/gui-forms-sanitize` with CI's flags,
with borrowed views checked (`GUI_FORMS_CHECKED_BORROWS`), and packs the
sanitize test cache into `<dir>`. AddressSanitizer, UndefinedBehaviorSanitizer
and LeakSanitizer run on Linux and macOS; Windows builds no sanitized tree
([Building from source](../building/index.md#sanitized-test-runs)). The tests then run against
that cache from a gui_forms_tests checkout, and any report fails them:

```sh
python3 tools/ci_tests.py --kind sanitize --archive <dir>/<archive> --unpinned
```

Run it over the tests your change touches before pushing C++, and the whole
suite before a stage gate. In gui_forms_tests, `ci_tests.py --tests REGEX`
narrows the run to the matching tests (a `ctest -R` pattern), and
`--changed BASE` to the tests whose own sources changed there. Host code is sanitized
only where it builds, so the Windows and macOS hosts depend on these local
trees. The sanitized objects carry `-fdebug-compilation-dir=.`, so checkouts
with the CI layout share them through one `ccache`; a second checkout of the
same sources compiles almost nothing.

The `sanitizers` CI job builds the same tree on Linux, with Clang 22 and
libc++ 22 like every Linux build, and packs its test cache; gui_forms_tests'
`sanitizers` job runs the whole suite against it. Both run only when
requested by hand (`workflow_dispatch` with `run_sanitizers`); the gate is
the local sanitized run of every test when a checkpoint is folded. The same
gate compiles every changed file against libstdc++ (syntax only, on macOS or
the Linux VM), because macOS and Windows both build with libc++ and
libstdc++-only failures otherwise surface only in CI.

A plain run without `--sanitize` builds the tree, installs the SDK, checks
its notices (and on Windows its DLLs' imports), and builds and runs the
installed reference examples against it; with `--test-cache` it also packs
the test cache. `--install-only` stops after the SDK checks and packs
nothing: it is for a consumer's smoke build at a pin GUI.Forms' CI has
already built (File Manager's bootstrap smoke).

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

## Tests and evidence

Every behavioural change ships with a test. The tests live in
[gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests), so a
change here and its test land as a pair: the test moves its
`gui_forms.lock` pin to the commit that carries the change. Core and control
behaviour is tested renderer-free against the headless host; painting
against the identity and raster tests; host behaviour on the platform of
that host.
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
2. Build on at least one platform with `tools/ci_build.py --test-cache <dir>`,
   then run gui_forms_tests against that cache
   ([Build from source](../building/index.md#tests)); CI builds macOS arm64,
   Linux x64, Linux arm64 and Windows x64.
3. Keep unrelated working-tree changes out of the commit. Preserve user files.
4. Describe in the pull request what changed, which tests cover it, and the
   exact commands and environment you used for any measurement.
5. Expect review to ask for the boundary and the test first and the feature
   second.

## Licence

GUI.Forms is MIT licensed. Bundled fonts carry their own licences in
`assets/fonts/`; third-party sources live in the stx repository (pinned in
`stx.lock`), keep their licences there, and are installed under
`share/licenses/`. A contribution is accepted under the
project licence.
