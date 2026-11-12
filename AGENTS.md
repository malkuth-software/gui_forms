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
python3 -B tools/check_house_style.py include src lab demo tests examples tools file_manager_demoboard fuzz
python3 -B tools/check_lifetime_rules.py include src lab demo tests examples tools file_manager_demoboard
python3 -B tools/export_annotations.py check
python3 -B tools/control_registry.py members --check
python3 -B tools/abi_class_tables.py --check
```

The third fails on a public declaration under `include/gui_forms/` that lacks
its library's export macro (`GUI_FORMS_API`), or when the shipped libraries'
export lists (`cmake/exports/`) differ from the marks; `python3 -B
tools/export_annotations.py apply` adds the missing marks and `exports`
rewrites the lists. Commit both.

The fourth fails when the member manifest (`manifests/members.json`, 1.0
condition 3) and the registry, schema or public headers disagree: a control,
property, event or method that is not listed, or a listed one that no longer
exists, or an entry without a status, reason or question. `python3 -B
tools/control_registry.py members` writes it again (keeping the statuses
people set), and `python3 -B tools/generate_library_docs.py` and `python3 -B
tools/control_registry.py render` regenerate the pages read from it.

The fifth fails when the C ABI's class tables (`gf_abi_classes.h`, the C++
bridges, the C# hook classes, the conformance part and their ledger
`manifests/abi/class_tables.json`) differ from what the member manifest's
`raiser` fields and the public headers give, or when the ledger would lose,
reorder or renumber an entry, a segment or a class id (append-only, the class
tables decision). `python3 -B tools/abi_class_tables.py` writes them again.

`stx_skia/` and every third-party source (FreeType, HarfBuzz, libunibreak,
md4c, SheenBidi, the Unicode data) live in the stx repository since
2026-11-08; this repository pins that checkout in `stx.lock` and carries no
third-party sources. `stx_skia/`'s own style check, with its shrink-only
allowance, runs there (`stx_skia/AGENTS.md` in stx).

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

## Consumers inside this repository

The control gallery (`demo/`) began as the first proving slice; it is now one
consumer among others and is held to the same public seams.

The separately specified `file_manager_demoboard/` is an admitted first-party
consumer: it reproduces the File Manager interface using deterministic
in-memory fixtures and only public GUI.Forms seams. It is still not the
shipping frontend, may not read the real filesystem or parent sources at
runtime, and may not implement missing reusable GUI.Forms capabilities inside
the consumer project. Its local `AGENTS.md` is mandatory for work in that
directory.

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
- Portsmouth Rapids 1.0 faces are supplied for the Gallery.
  **GIVEN forward direction:** M4/M9 use HarfBuzz, FreeType, bundled
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

## Codec experiments

The owned codecs' oracle, fuzz and benchmark experiments (`validate_inflate.py`,
`validate_png.py`, the `benchmark_*` runners, `review_deflate_memory.py`, the
pinned-reference manifests and `codec-differential.yml`) live in
malkuth-software/stx with the codecs: `tools/CODEC_EXPERIMENTS.md` there.
