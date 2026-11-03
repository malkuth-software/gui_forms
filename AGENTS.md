# GUI.Forms implementation guardrails

This subtree is the independent GUI.Forms proving project. It may be built from
this directory without File Manager. The repository root may consume it; this
subtree may never include, link, inspect, or depend on sources outside
`gui_forms/`, except pinned third-party dependencies explicitly declared by its
own build.

## Code style (mandatory)

All first-party C++ and Objective-C++ in this repository follows the house
style. Its canonical text is `HOUSE_STYLE.md` on the `main` branch of
`malkuth-software/notes`
(https://github.com/malkuth-software/notes/blob/main/HOUSE_STYLE.md). Read it
in full before writing or changing code. One line per section, in its order
(most important first, the owner's 2026-10-27 ranking), so a skim still covers
all eleven:

1. **Memory use:** every datum has an owner and a lifetime; allocate before
   repeated processing and reuse; transfer ownership explicitly; raw
   addresses only at boundaries; name storage operations (fill, reserve,
   reset, clear, release) precisely; check size arithmetic; keep borrows
   valid through use (a function returning a borrow of an argument deletes
   its rvalue overload; declare a borrower after what it borrows); memory
   errors and undefined behaviour are defects in tests too, checked by
   sanitizer runs of the whole suite.
2. **Failure and foreign interfaces:** say what a failure leaves behind;
   validate before mutating; explicit results for expected alternatives and
   exceptions only at subsystem boundaries; switches and state machines keep
   a default that reaches one fallback; foreign interfaces state types,
   counts, ownership and errors, and take entry-point types from their
   declarations, never retyped by hand.
3. **Types and values:** spell types; separate confusable quantities (byte
   offsets and character positions, identities and indices) with wrapper
   types; one numeric precision per computation; named record fields, enums
   and explicit initialization; state array shape and capacity versus live
   length.
4. **Objects and authority:** parents own children and back links only
   observe; cross authority boundaries through named capabilities; define
   the fallback when an owner disappears (reject stale identities, stop
   revoked callbacks).
5. **Callbacks and execution:** named targets with explicit context; specify
   event order, reentrancy and revocation; name asynchronous work,
   completion, cancellation and shutdown; one execution context per piece of
   mutable state.
6. **Procedures and data:** state inputs, outputs and permitted mutation;
   fill caller-supplied storage for bulk work; name intermediate forms; state
   overlap rules.
7. **Admitted abstractions:** standard algorithms with named predicates;
   templates for declared choices; virtual dispatch and type erasure only
   where runtime variation is required; no universal callable frameworks or
   speculative portability layers.
8. **Repeated kernels:** no decisions inside the loop: a per-element branch
   marks an unfinished formulation, so find the reformulation rather than
   evaluating both alternatives; break the local paradigm, then restore
   exactness with corrections outside the interior, proved against a kept
   reference; bookkeeping lives outside the loop; then layout, hoisting and
   stated numerical freedoms.
9. **Statements and expressions:** write in execution order; no lambdas or
   anonymous callbacks; one recognizable operation per expression; calculate,
   then return; break chains that hide types, state or allocation.
10. **Source layout and comments:** meaningful source boundaries with the
    main sequence easy to find; no accessors or wrappers that add no
    invariant; explain non-obvious dimensions, lifetimes and surprising
    operations; remove stale comments.
11. **C++ spelling:** no `auto`, trailing returns or structured bindings;
    `(*pointer).member` instead of `->`; no lambdas; hand-written comparisons
    (no `= default`); no coroutines or ranges pipelines.

Match the surrounding code's naming and comment density.

Before committing, run the spelling check over everything you touched; the
`house-style` CI job runs it over every first-party directory and fails on any
finding:

```text
python3 -B tools/check_house_style.py include src demo tests examples tools file_manager_demoboard stx_vorbis stx_zlib stx_png stx_conv fuzz
python3 -B tools/check_lifetime_rules.py include src demo tests examples tools file_manager_demoboard stx_vorbis
python3 -B tools/check_stx_skia_style.py
```

The third covers `stx_skia/`, whose files imported from Skia still carry
findings while they are converted: it fails on any finding its shrink-only
allowance does not list (`stx_skia/AGENTS.md`).

The second checks two rules of sections 5 and 9: a function returning a
borrow of a container or snapshot argument deletes its rvalue overload, and a
function pointer looked up by name takes its type from the declaration. The
`sanitizers` CI job runs the whole suite under ASan, UBSan and LSan on Linux
and fails on any report, but only when requested by hand (owner ruling,
2026-11-03): the gate is the local sanitized run of every test when a
checkpoint is folded.

Every build tree keeps a sanitizer tree beside its ordinary one, on every
platform it builds on:

```text
python3 -B tools/ci_build.py --sanitize [--gallery] [--tests REGEX]
```

It builds `.build/native-<host>-<arch>/gui-forms-sanitize` with CI's flags
(ASan and UBSan everywhere, LSan on Linux and macOS; the Windows MinGW runtime
has none) and fails on any report. Run it over the tests your change touches
before pushing C++, and the whole suite before a stage gate.

The checks cover spelling and those two rules only. Ownership, lifetime, initialization and
naming still need review against the full document.

## Forward programme (read before starting new work)

Plans, stage briefs, measurements, experiments, decision records and working
notes are not kept in this repository. They live on the `gui_forms` branch of
`malkuth-software/notes` (https://github.com/malkuth-software/notes/tree/gui_forms),
under `planning/`, `results/`, `experiments/`, `decisions/` and `docs-notes/`
(the former `docs/notes/`). A code comment that cites `notes:<path>` means
`<path>` on that branch. Record new plans, gate measurements and evidence
there, not here.

The current programme (articulated controls, one theme engine, the design
language, accessibility verification, the C ABI, the C# sibling and the
visual tools) is defined in `planning/FORWARD_PROGRAMME.md` on that branch,
with the visual rules in `planning/DESIGN_LANGUAGE_SPECIFICATION.md`. Find
your stage there, start it only once its dependencies' gates are recorded,
and record your own gate's measurement. Open owner decisions are listed in
both files; do not assume them. Each stage has a working brief in
`planning/stages/` on that branch (why, what, how, current code, house-style
points, open decisions and lessons from the owner's discussions); read
`planning/stages/README.md` and your stage's brief before starting. What is
left before 0.5 is in `planning/ROADMAP_TO_0.5.md`. A readable stage board for
sibling threads is published at
https://claude.ai/artifact/D9d2SUvkiKQETiVnmwnEt4 (read it with the Artifact
tool); the planning files are authoritative where they differ.

## Scope of the first proving slice

Build a native macOS control-gallery demonstration and automated tests proving:

- retained controls and explicit ownership;
- stateful imperative mutation;
- typed layout/paint/hit-test dirtiness;
- nested update scopes and bounded flushes;
- damage-limited CPU painting;
- Forms-like focus, pointer, keyboard, and activation behavior;
- a Web.Forms-produced compiled/static form description with stable IDs, with
  the checked-in Gallery DML retained only as provisional evidence;
- instrumentation visible in logs and an optional on-screen diagnostics panel;
- CPU-only Skia hidden behind GUI.Forms drawing vocabulary;
- and a native AppKit host which can be launched, inspected, and closed.

This is a control gallery, not File Manager and not a file browser.

The separately specified `file_manager_demoboard/` is an admitted first-party
consumer exception to the gallery's visual subject: it reproduces the File
Manager interface using deterministic in-memory fixtures and only public
GUI.Forms seams. It is still not the shipping frontend, may not read the real
filesystem or parent sources at runtime, and may not implement missing reusable
GUI.Forms capabilities inside the consumer project. Its local `AGENTS.md` is
mandatory for work in that directory.

## Hard boundaries

- C++20 is admitted. Objective-C++ is admitted only in the macOS host adapter.
- No .NET, Java, browser engine, or Godot dependency.
- GUI.Forms has no GPU capability. Do not compile, link, initialize, probe, or
  expose Ganesh, Graphite, GL, Vulkan, Metal, Direct3D, Dawn, or WebGPU.
- Skia may be used only through a private CPU renderer adapter. No Skia type may
  appear in public headers, DML, demo-facing control APIs, C ABI, or tests of the
  renderer-neutral core.
- **GIVEN:** the GUI.Forms core and host protocol target Windows, macOS, and
  Linux equally. No portable semantic, event order, path/dialog model, or
  scheduler rule may encode AppKit, Win32, Wayland, or X11 assumptions. Honest
  platform differences are capability-reported adapter behavior, not hidden
  core policy. Private Skia extensions may use platform-specific CPU details but
  may not change or leak through the portable contract.
- PNG is the only image codec admitted to the renderer/resource core: the
  first-party `stx_png` (over `stx_zlib`), reached only through
  `src/core/image/`, for resources, the clipboard, imports, export and
  GUI.Drawing. The registry, renderers and hosts see codec-neutral types
  only. Other codecs are excluded.
- AppKit types remain inside `src/host/macos/`.
- Win32, COM, WIC, MSAA, and UIA types remain inside `src/host/windows/`.
- The core is retained; do not introduce an immediate-mode control API or a
  perpetual redraw loop.
- ADR-013's reservation of persistent side panels to File Manager was retired
  by the owner on 2026-10-13: any consumer may compose panels from the
  reusable panel controls, and owned dialogs remain first-class capabilities.
- Authored accessibility/help metadata is optional secondary hook data and is
  not required to instantiate a control. Supported stock controls still supply
  default semantic adapters; File Manager requires native accessibility
  publication and does not treat it as a user-disabled product mode.
- Portsmouth Rapids 1.0 faces are supplied for the current Gallery.
  That bounded historical proving slice retains Lucida Grande for editable,
  collection, metric, and field content until its fixture is deliberately
  migrated. **GIVEN forward direction:** M4/M9 use HarfBuzz, FreeType, bundled
  Portsmouth control faces, a selected bundled Tahoma/Calibri-like body face,
  and bounded bundled fallback packs. Portsmouth is the repository owner's own
  typeface, bundled under MIT (`assets/fonts/LICENSE-PortsmouthRapids.txt`);
  body-face coverage assets remain a gate; do not silently use arbitrary host
  fonts.
- Preserve user files and unrelated working-tree changes.

## Public seam

- Portable C++ headers live under `include/gui_forms/`.
- Renderer-neutral implementation lives under `src/core/` and
  `src/controls/`.
- Skia implementation lives under `src/render/skia/`.
- Native macOS code lives under `src/host/macos/`.
- Native Windows code lives under `src/host/windows/`.
- Demonstrations and their provisional static descriptions live under `demo/`;
  authoritative Web.Forms language work lives in the sibling `web_forms/`
  project and consumes only an explicitly negotiated public manifest.
- The standalone fixture-backed File Manager consumer specification and its
  eventual installed-package consumer live under `file_manager_demoboard/`.
- Tests live under `tests/`.
- Instrumentation must be queryable as structured counters/snapshots, not only
  prose logs.

The public ABI may begin as disciplined portable C++ for the spike, but no
compiler-specific type may be assumed to be the eventual stable C ABI.

Cross-project consumption is negotiated through
`docs-notes/ORCHESTRATOR_INTERFACE_NEGOTIATION.md` on the notes `gui_forms`
branch. GUI.Forms records its reply and evidence there; Orchestrator reconciles the canonical program contract. Do not
freeze an ABI merely because the current frontend experiment calls it.

## Agent ownership during the initial parallel build

- Core agent: `include/gui_forms/`, `src/core/`, core instrumentation, and core
  tests.
- Control-gallery agent: `src/controls/`, `demo/`, demo resources, and gallery
  interaction tests.
- Skia/host agent: `third_party/`, `src/render/skia/`, `src/host/macos/`, and
  dependency/build integration specific to those paths.
- Root agent: guardrails, cross-cutting build integration, final review,
  execution, visual inspection, and closure.

Do not edit another agent's owned files without first messaging that agent and
root. Communicate interface needs early. Preserve negative results and report
exact commands and environment.

## Build contract

The lower build must eventually support commands equivalent to:

```text
cmake -S gui_forms -B gui_forms/build
cmake --build gui_forms/build
ctest --test-dir gui_forms/build --output-on-failure
gui_forms/build/.../gui_forms_gallery
```

No benchmark result may be labelled fast or lightweight without recording the
workload and measurement. A working demonstration is evidence only for the
behaviors it exercises.

## Owned deflate: remote phase-2 experiment

From this checkout, with upstream zlib at the pin in
`tests/compression/oracle_sha256.json` already available locally:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/validate_inflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/inflate-study-strict --strict --seconds 60
```

Repeat without `--strict` and use `build/inflate-study-ordinary` for the ordinary
oracle. This is an isolated instrumented experiment, not the full-project or
24-hour acceptance gate. It uses the M4 selector and installs no host software.
The script checks the oracle source digests and compiler version, retains logs
and uses no leak suppressions. See `tests/compression/README.md` for scope.

Do not resynchronize a worktree while a build is using its M4 mirror. `m4build`
uses `rsync --delete`: remote-only dependency working files outside the excluded
build directories can be removed. Store generated experiment output under
`build/`; serialize mirror sync and full dependency builds.

For the isolated release benchmark, export the baseline from this checkout
before using the M4 mirror (its linked-worktree Git metadata is not portable):

```sh
mkdir -p build-zlib-study-sources/baseline
git archive 123f13cd include/gui_forms/compression/inflate.hpp src/core/compression/inflate.cpp | tar -x -C build-zlib-study-sources/baseline
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/benchmark_inflate.py --oracle build-zlib-study-sources/upstream-zlib --baseline build-zlib-study-sources/baseline --output build/inflate-benchmark --inspect-x64
```

The runner verifies baseline and oracle hashes. `--inspect-x64` only emits
assembly using the installed macOS SDK, generic SSE2 and no AVX; it does not
execute an x64 test. Decoder SIMD design targets common 128-bit SSE/NEON lane
groups in portable C++, following the owner's BFFT/Airspy direction. Do not
introduce platform-specific intrinsic forks or wider-register algorithms.

For the phase-3 encoder comparison, use the source-pinned oracle and LLVM runtime:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/benchmark_deflate.py --oracle build-zlib-study-sources/upstream-zlib --output build/deflate-comparison --png docs/library/captures/text_input_screen.png --png docs/library/captures/picture_box_screen.png --inspect-x64
```

The runner records source/corpus hashes, flags, paired timing spread and encoded
sizes. PNG cases are extracted filtered IDAT bytes, not recompressed PNG files.
Keep negative results. The SSE2 output is inspection evidence, not x64 execution.
For an already configured isolated sanitizer tree, force a clean build after a
mirror sync: source and remote object timestamps can otherwise disagree.

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin cmake --build build/stx-zlib-standalone-llvm22-sanitize --clean-first -j 8
ssh "$(/Users/ultimussecundai/.local/bin/m4host)" 'env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ctest --test-dir /Users/joshuahkuttenkuler/Developer/CodexBuilds/gui_forms-9709c6a35d08/build/stx-zlib-standalone-llvm22-sanitize -V'
```

This focused package check supplements, and does not replace, the required
`ci_build.py --sanitize` platform and stage gates above.

For an owner-requested automated memory integrity review:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/review_deflate_memory.py --oracle build-zlib-study-sources/upstream-zlib --output build/deflate-memory-review --seconds 120
```

The runner prints only `PASS` or `STOP`; do not inspect its detailed diagnostic
files in this chat. On `STOP`, the owner will use a Daybreak sibling. Do not
start further reviews without the owner's request. Preserve each review
directory before changing the harness. Its source snapshots, oracle digests
and logs identify exactly what was reviewed. The
independent installed-consumer fixture is `stx_zlib/tests/installed`; test a
relocated prefix with the same LLVM toolchain and sanitizer mode as its archive.

For the exact match-index algorithm comparison (no memory review is run):

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin GUI_FORMS_LLVM_RUNTIME=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-4e126d6/toolchain/llvm-22.1.8-macos14 python3 -B tools/benchmark_match_index.py --output build/match-index-shared --png-offset 1048576 --png docs/library/captures/text_input_screen.png --png docs/library/captures/picture_box_screen.png
```

Use a new output directory for each variant. `--no-sharing` disables carried
match proofs, and `--files-only` omits synthetic cases. Compare identical byte
samples and query schedules. Index timings do not establish whole-encoder
speed or production streaming behavior.

The capped radix experiment uses the same command with `--radix-finger` and a
new output directory, such as `build/match-index-radix-finger`. It runs 1052
deterministic boundary fixtures before measurements. `--verify-only` runs those
algorithm fixtures without timing; omit PNG arguments in that mode.
`--greedy-only` measures only phrase-boundary queries while retaining all
insertion and expiration work. `--radix` selects the eager-maxima comparison.
See `notes:results/2026-10-20-zlib/phase-three/match-index/RADIX.md` for invariants and
the distinction between index memory and the total streaming encoder budget.

The random-maintenance variant additionally selects `--inline-radix
--small-radix --singleton-radix --lazy-radix`. `--random-sweep` adds three
fixed random seeds at 128 KiB and 1 MiB; `--random-only` skips other synthetic
cases. `--assembly` retains compiler assembly, and `--restrict-index` applies
the same disjoint-index contract to both compared kernels. See
`notes:results/2026-10-20-zlib/phase-three/match-index/RANDOM_REFACTOR.md` for results
and limitations. Use `--greedy-only` to reproduce its query schedule.

## Owned PNG: remote phase-5b experiments

The private sample decoder's sanitizer/oracle and benchmark commands are:

```sh
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/validate_png.py --libpng build-zlib-study-sources/libpng --zlib build-zlib-study-sources/upstream-zlib --output build/png-final --seconds 60
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B tools/benchmark_png.py --libpng build-zlib-study-sources/libpng --zlib build-zlib-study-sources/upstream-zlib --workloads build/png-final/workloads --output build/png-benchmark
```

The first command generates the second's workloads. Serialize them and every
other mirror/build operation. Oracle pins and file hashes are checked before
compiling. The sanitizer comparison uses portable-C libpng; the benchmark
uses its NEON implementation on arm64. Neither is a production dependency
change. Full-suite sanitizer verification still uses `tools/ci_build.py`. The recorded
helper, `notes:results/2026-10-19-zlib/png/full-suite-driver.sh`, supplies its
logical workspace layout on the hashed M4 mirror. Copy it into `build/` of this
checkout first; it runs from the checkout's root:

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh build/full-suite-driver.sh
```

## PNG CRC suitability experiment

The standalone comparison is first-party experimental code, not a new runtime
library dependency. It is kept at `notes:experiments/png-crc-suitability/`;
`run.py` finds this checkout two directories above itself, so copy that
directory to `experiments/png-crc-suitability/` in a checkout before running
it. It compiles the existing pinned upstream zlib both natively
and with its ARM CRC opcode path disabled, verifies complete CRCs under
sanitizers, and compares chunk costs and whole PNG decoding. Run it after
`validate_png.py` has generated the workload directory:

```text
/Users/ultimussecundai/.local/bin/m4build -- env PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin python3 -B experiments/png-crc-suitability/run.py --zlib build-zlib-study-sources/upstream-zlib --workloads build/png-hinted-check/workloads --output build/png-crc-suitability-final
```

Keep performance trials sequential with builds, fuzzing and suite execution.
Inspect assembly before crediting the degree-261 quartet with register-resident
state; source arrays alone do not establish that allocation. See
`notes:experiments/png-crc-suitability/README.md` for provenance and measurement limits.

Repeat the CRC experiment with `--baseline-isa` and a separate output directory
to measure baseline Armv8-A candidate code without Apple-target three-input XOR
fusion. This option is specific to the arm64 measurement host; the candidate
algorithm itself has no platform intrinsics.
