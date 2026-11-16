# Repository tools

The scripts and programs under `tools/` that a contributor runs. Commands run
from the repository root. Each Python script prints its full options with
`--help`.

The shared CI tools and the checkers (`check_house_style.py`,
`check_lifetime_rules.py`, `check_state_machines.py`, `windows_toolchain.py`,
the `ci_*.py` tools and the rest that `tools/harness_link.py` lists) live in
template_harness, checked out beside this repository at the commit
`harness.lock` pins. Their `tools/` paths below are forwarding shims to it:
[GUI.Forms builds through template_harness](../building/template-harness.md).

## Documentation and the control registry

`tools/control_registry.py`
: Loads and validates `manifests/controls.json`. `check` validates the records
  and their header coverage, `render` writes the control indexes under
  `docs/controls/generated/`, `readme-table` prints or splices
  (`--write README.md`) the README control table, and `emit-cpp` writes or,
  with `--check`, verifies `demo/gallery_catalog.inc`. `members` writes the
  member manifest `manifests/members.json` (`tools/member_manifest.py`): every
  control's properties, events and methods against Windows Forms, supported,
  deferred, excluded or unclassified; `--catalogue` reads stage W's Windows
  Forms catalogue again, `--runtime` dotnet/runtime's reference sources for
  `Component` and `BackgroundWorker`, and `--check` fails when the manifest and the
  registry, schema or headers disagree.
  `python3 tools/control_registry.py check`
  `python3 tools/control_registry.py members --check`

`tools/winforms_divergences.py`
: Checks `manifests/winforms-divergences.json`, the record of every difference
  from a Windows Forms control's observable behaviour (each entry complete,
  with its reason and decision), and `render` writes
  `docs/controls/generated/winforms-divergences.md`.
  `python3 tools/winforms_divergences.py check`

`tools/generate_library_docs.py`
: Generates the API reference pages under `docs/api/` and the documented
  examples test `tests/doc_examples_tests.cpp`. `--check` regenerates into a
  temporary directory and fails if the committed output differs.
  `python3 tools/generate_library_docs.py --check`

`tools/api_reference.py`
: Extracts the exact declaration inventory `docs/reference/inventory.json`
  with `clang -ast-dump=json` and validates the reviewed contracts in
  `docs/reference/contracts/`. Without options it rewrites the inventory;
  `--check` fails on a stale inventory or an invalid contract. The dump
  depends on the compiler build, so run it with LLVM 22.1.8 (`--clang`).
  The headers compile with the include directories a configured build tree
  records (`--build`): gui_forms_core's and its public dependencies', which
  CMake writes to `api_reference_include_directories.txt` in that tree.
  `python3 tools/api_reference.py --check --build /path/to/build --clang /path/to/clang++`

## House style

`tools/check_house_style.py`
: The spelling scanner the `house-style` CI job runs (see
  [Style guide](style.md)). Takes files or directories, reports one line per
  finding and exits non-zero on any finding or on an empty scope.
  `python3 -B tools/check_house_style.py src/core/image tests/png_writer_tests.cpp`

`tools/check_lifetime_rules.py`
: The borrow and foreign-type checks the `house-style` job runs (house style
  sections 1 and 2). Heuristics over masked source, like the spelling
  scanner; they do not check destruction order. After the harness's rules
  the shim applies GUI.Forms' member-accessor rule
  (`tools/check_member_views.py`): an inline accessor returning a view of a
  data member (`std::span`, `std::string_view`, `BorrowedSpan`,
  `BorrowedText`) is qualified `const&` and has a deleted `&&` overload.
  The `house-style` CI job calls the harness's checker directly, so this
  member-accessor rule runs only through the shim.
  `python3 -B tools/check_lifetime_rules.py tests/semantic_tests.cpp`

`tools/lifetime_lint.py`
: Clang's experimental lifetime-safety analysis over the whole tree, in a
  build tree of its own (`gui-forms-lifetime-lint` beside the plain tree),
  report-only: it prints the findings, marks the six known false positives
  of the cp28 run (a pointer taken through a local `std::unique_ptr` that
  then moves into a container) and always exits 0. The checkpoint verify on
  the M4 runs it; `--stx-prefix` links an stx package already built.
  `python3 -B tools/lifetime_lint.py --jobs 4 --stx-prefix <stx-package>`

`tools/check_state_machines.py`
: The state-machine checker (house style sections 2 and 3). With
  `--build-dir`, it runs the compiler's switch warnings over a configured
  ninja tree. The `state-machine-gate` CI job
  (`.github/workflows/state-machines.yml`) runs it with `--gate`, which
  fails on any F1 or F2. `--changed-from BASE` checks only the translation
  units that the C and C++ changes since BASE can affect, and passes at once
  when there are none. The daily run checks the whole tree.
  `python3 -B tools/check_state_machines.py --root . --build-dir <tree> --gate --changed-from origin/main`

`tools/check_native_boundaries.py`
: Lists the native entries under `src/host/` (Windows `CALLBACK` and COM
  methods, Linux ATK and GLib callbacks, AppKit selectors) and fails on any
  that neither calls `call_failing_fast` nor is `noexcept`, unless it is in
  `tools/native_boundary_baseline.txt`, which may only shrink and has been
  empty since cp29 (every entry is routed). ctest runs it and its
  `--self-test`.
  `python3 -B tools/check_native_boundaries.py --root .`

`tools/sanitizer/lsan.supp`
: LeakSanitizer suppressions for sanitized runs (local trees and the
  `sanitizers` CI job, which runs only when requested): system libraries
  only, never a first-party frame.

`tools/house_policy_check/`
: A Clang LibTooling checker for the full C++ subset, run over a build's
  compilation database and writing a JSON ledger; it is not linked into any
  product. Build it into `.build/house-policy-check/` against a current LLVM,
  configure the main build with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then run
  the wrapper. Inventory mode records findings without failing; set
  `GUI_FORMS_HOUSE_POLICY_MODE=closure` to fail on any violation. Optional
  arguments write pointer-arrow and `auto` rewrite plans (applied with
  `apply_pointer_rewrites.py` and `apply_auto_type_rewrites.py`), select a
  `native` or `mingw` database, and widen the scope from `production`
  (`src/`) to `first-party`. The wrapper uses Homebrew LLVM and the Xcode SDK,
  so it runs on macOS.
  `tools/house_policy_check/run_inventory.sh "$PWD/build" ledger.json`

## The C ABI and other generated sources

The `house-style` CI job runs each of these with `--check` (or `check`), so a
stale generated file fails it. Run the generator and commit its output with
the change that needs it.

`tools/abi_audit.py`
: Records and audits the C ABI (`include/gui_forms/c/gf_abi.h`). `extract`
  writes or checks the manifest under `manifests/abi/`, `check` applies the
  ABI rules and the recorded baselines (`--mode blocking` fails on any
  break), `probe` writes or checks the layout probe, `exports` checks a built
  library's exports and `coverage` requires every slot to be called. It
  parses with clang, so pass `--clang` with LLVM 22.1.8. ctest runs the
  blocking check in every native build (`gui_forms_abi_audit`).
  `python3 -B tools/abi_audit.py check --clang /path/to/clang --mode blocking`

`tools/abi_bindings.py`
: Generates the C# interop (`csharp`) and the C++ wrappers (`cpp`) from the
  audited manifest.
  `python3 -B tools/abi_bindings.py csharp --check`

`tools/abi_class_tables.py`
: Generates the C ABI 1.3 class tables, through which C and C# code
  overrides a control's `on_*` hooks, and checks that the ledger only grows
  (`--previous`).
  `python3 -B tools/abi_class_tables.py --check`

`tools/abi_event_catalog.py`
: Generates the C ABI's event catalog from the interaction declarations.
  `python3 -B tools/abi_event_catalog.py --check`

`tools/drawing_bridge.py`
: Generates the System.Drawing facade's native bridge
  (`generated/facade-v1/System.Drawing.Common/DrawingBridge.g.cs`) from its
  template, `tools/drawing_bridge/DrawingBridge.cs.in`, the retired facade
  generator's bridge template. Both imports load the C ABI library,
  `gui_forms_abi<GF_ABI_MAJOR>`, which holds the drawing ABI and its raster
  service. Edit the template, not the generated file.
  `python3 -B tools/drawing_bridge.py --check`

`tools/export_annotations.py`
: Checks that every public declaration carries its library's export macro
  (`check`), adds missing ones (`apply`) and writes the export lists under
  `cmake/exports/` (`exports`).
  `python3 -B tools/export_annotations.py check`

`tools/property_codemod.py`
: Generates and checks the property families' code from
  `tools/property_families/*.json` (`generate`, `rewrite`, `check`,
  `selftest`).
  `python3 -B tools/property_codemod.py check`

`tools/control_schema.py`, `tools/stock_anatomies.py`, `tools/stock_specimens.py`
: Check and render the control schema (`manifests/control-schema.json`,
  written from a laid-out Gallery), and project it into the stock anatomy
  table the core compiles and the stock specimen catalogue.
  `python3 tools/control_schema.py check`
  `python3 -B tools/stock_anatomies.py --check`

`tools/dither_tables.py`
: Writes the engine's blue-noise dither table for gradient dithering.
  `python3 -B tools/dither_tables.py --check`

`tools/paint_census.py`
: Counts control code that paints without the theme engine; the count in
  `manifests/paint_census.json` may only fall.
  `python3 -B tools/paint_census.py check`

`tools/host_trig_check.py`
: Refuses the host's `sin`, `cos`, `tan`, `atan2` and `hypot` (and their C
  forms) in `src/core`, `src/controls` and `src/render`, which call the
  deterministic functions of `engine_math.hpp` instead; the allowance in
  `manifests/host_trig_allowance.json` may only shrink.
  `python3 -B tools/host_trig_check.py check`

`tools/check_test_registration.py`, `tools/check_test_asserts.py`
: Refuse test programs the build never runs, and checks in test code that
  `NDEBUG` would remove.
  `python3 -B tools/check_test_registration.py`

`tools/warning_baseline.py`
: Counts the distinct warning sites per directory in a build's compiler
  output and compares them with `tools/warning_baseline.json`, per host.
  `python3 -B tools/warning_baseline.py check --log build.log --host macos`

## Migrations

`tools/flatten_control_headers.py`
: Moved the control headers to `include/gui_forms/controls/<name>.hpp`
  (checkpoint 29); `check` fails on any remaining old include spelling, and
  `tools/control_header_moves.json` lists the moves for consumers.
  `python3 -B tools/flatten_control_headers.py check`

`tools/migrate_right_to_left.py`
: Rewrites the retired `layout_direction` spelling to `right_to_left`.
  `python3 -B tools/migrate_right_to_left.py --write src/`

`tools/relocate_colours.py`
: Moves colours written in form documents into a theme sheet.
  `python3 -B tools/relocate_colours.py --sheet themes/app/app.theme.css --no-dark forms/main.form`

## Build, install and packaging

`tools/ci_build.py`
: Builds and tests the toolkit exactly as the native CI job does: builds or
  reuses the stx package (the CPU renderer and the text stack), configures
  with `cmake/llvm22.cmake`, runs `ctest`, installs into a scratch SDK and
  builds `examples/reference/` against it. It expects the checkout in a
  directory named `gui_forms` and writes to `.build/native-<host>-<arch>/`
  in the workspace that holds it. `--gallery` also builds the programs;
  `--sanitize` builds the sanitizer tree ([Contributing](contributing.md));
  `--test-targets-only` is the pull-request build (the whole suite, nothing
  installed); `--install-only` installs the SDK without running the tests.
  `python3 tools/ci_build.py --jobs 4`

`tools/build_sdk.py`, `tools/package_sdk.py`
: The source SDK. `package_sdk.py` archives a release's tree with the stx
  checkout `stx.lock` names as `gui-forms-sdk-<version>-source.tar.gz`;
  `build_sdk.py` builds and installs GUI.Forms from that archive (or this
  tree) for one target, with `--features all` adding the optional
  components.
  `python3 tools/build_sdk.py --target macos-arm64 --prefix /path/to/sdk`

`tools/verify_install.py`
: Builds an independent consumer against an installed SDK prefix and checks
  that requests for missing CMake components fail. `--native` exercises the
  `Application` component, `--relocate` copies the SDK to a new prefix first,
  and `--toolchain` and `--runner` build the consumer with another CMake
  toolchain and run its tests through an emulator. `--feature-components`
  (with `--native`) also builds and runs `tests/installed_feature_consumer/`
  against a `build_sdk.py --features all` prefix, through the `TextMasks` and
  `AudioLoopTransport` components. Without it, those two must be rejected.
  `--sdk-consumer` (with `--native`) builds and runs `tests/sdk_consumer/`,
  which links every public component from the prefix's archives (the
  controls, a composite, an application font family, the drawing raster
  seam, `TextMasks` and the C ABI with `gd_get_api_v0`);
  `--sdk-consumer-text-masks OFF` and `--sdk-consumer-c-abi OFF` describe a
  prefix without those components.
  `python3 tools/verify_install.py /path/to/prefix --native`

`tools/sdk_consumer_link.py`
: The SDK-consumer link test, the `gui_forms_sdk_consumer_link` test: installs
  a build tree into a temporary prefix and builds `tests/sdk_consumer/`
  against that prefix alone, configured as the tree was. A symbol the
  installed archives lack fails the link; a separate program links
  `GUIForms::TextMasks` alone, so an empty interface fails too. The suite
  registers it in an installable tree (a native host, and no development
  prepared text). CI's tree builds the text masks for it
  (`GUI_FORMS_BUILD_TEXT_MASKS=ON`, as `build_sdk.py --features all`); in a
  tree without them, or without the C ABI library, it says what it could not
  check and CTest reports it skipped, not passed.
  `python3 tools/sdk_consumer_link.py --build-dir build --scratch build/sdk-consumer-link -- -GNinja`

`tools/package_programs.py`
: Packages the programs listed in `packaging/programs.json` from a build
  directory: an `.app` zip and `pkg` installer on macOS, a zip and NSIS
  installer on Windows, a `tar.gz` and Debian package on Linux, plus
  `SHA256SUMS.txt` and a `packages-<platform>.json` receipt. `--dry-run`
  prints the plan; `--no-installer` writes archives only.
  `python3 tools/package_programs.py --build-dir build --version 0.6.0 --out dist`

`tools/smoke_package.py`
: Extracts each packaged program, launches it with `--smoke-test` and the
  bundled fonts, and requires a clean exit, the pass line and at least one
  presented frame. `--install` also applies the installer and launches the
  installed copy. Results go to `smoke-<platform>.json`.
  `python3 tools/smoke_package.py --dist dist`

`tools/build_macos_runtimes.py`
: Builds the pinned LLVM runtimes (libc++ and its dependencies) with a
  macOS 14 arm64 minimum; the result is the prefix `GUI_FORMS_LLVM_RUNTIME`
  names. `tools/audit_macos_minimum.py PATH...` rejects Mach-O images outside
  that arm64 / macOS 14 contract.
  `python3 tools/build_macos_runtimes.py --work .build/runtimes-work --prefix .build/llvm-runtime`

`tools/prove_compiler_cache.py`
: Builds a clean source snapshot cold through a fresh compiler cache, then a
  relocated copy through the same cache, writes both timings and cache
  statistics to `receipt.json` in the scratch directory and fails if reuse
  changes the binary. Only copies under the scratch directory are edited.
  `python3 tools/prove_compiler_cache.py --source . --scratch .build/cache-proof --jobs 4`

## Windows and Wine

`tools/windows_toolchain.py`
: Packs, fetches and restores the exact MSYS2 CLANG64 tree recorded in
  `tools/windows_toolchain.json`, whose `clang.exe` is the compiler the cache
  manifests record. Requires Python 3.14.
  `python tools/windows_toolchain.py restore`

`gui_forms_windows_probe`
: The automation client built from `tools/windows_automation_probe.cpp` with
  every Windows host build. It sends one command (`snapshot`, `click <id>`,
  `activate <id>`, `capture [path]`, `close`, `list` and others) to a running
  program started with `--automation`.
  `wine gui_forms_windows_probe.exe click gallery.checkbox`

`tools/run_windows_wine_smoke.sh`, `tools/run_windows_gallery_text_smoke.sh`, `tools/run_windows_gallery_services_smoke.sh`
: Run a Windows build of the gallery under Wine through the probe and assert
  on the structured log; see [Windows builds and Wine](../building/index.md#windows-builds-and-wine).
  They need `wine` (`WINE_BINARY`, default `/opt/homebrew/bin/wine`) and
  `rg`. The plain and text smokes accept `GUI_FORMS_EVIDENCE_DIR` to redirect
  their logs and captures.
  `tools/run_windows_wine_smoke.sh build-windows-x64`

## Measurement, data and test machines

`tools/code_statistics.py`
: Counts the C++ and C at HEAD of this repository and of the checkouts named
  after it (stx, STXlib): files, lines (blank, comment, code), characters
  and bytes, by component, language and provenance, and the first-party
  C++ headline figure, from the git trees with no build. `--markdown` and `--json` write the other forms;
  `.github/workflows/code-statistics.yml` runs it on `main`
  ([code statistics](../user/code-statistics.md)).
  `python3 -B tools/code_statistics.py ../stx ../stxlib`

`tools/integrity_counts.py`
: Counts audit findings found, fixed and declined, by source and severity,
  for the high-integrity report (owner, 2026-11-16). It reads only records:
  the audit folders' findings and decline sections, the queue entries, and
  the commit messages of the named histories, and resolves the coders' short
  forms (`02-F7`, `F1 to F5`) against the last full finding ID in the same
  record. Each finding is traceable to its IDs, commits and queue entries;
  `--markdown` and `--json` write the other forms. The audit folders live
  outside the repository, so every path is an argument.
  `python3 -B tools/integrity_counts.py --audit <audit folder> --queue <queue> --repo gui_forms=.@HEAD --repo stx=../stx@<pin>`

`tools/png_admission_probe/`
: A standalone consumer of an installed SDK that times `validate_png` and
  `ImageRegistry::load_png` separately on generated uncompressed PNGs and
  prints six samples without a threshold. `fixture.py` writes the fixtures and
  refuses to overwrite them. Its `README.md` has the full procedure.
  `python3 tools/png_admission_probe/fixture.py .build/png-admission-data`

`tools/unicode/generate_grapheme_tables.py`
: Regenerates the Unicode 17.0.0 grapheme-break tables
  (`src/core/text/unicode/unicode_grapheme_data.hpp`) and conformance cases
  (`tests/unicode_grapheme_break_cases.hpp`) from the downloaded Unicode data
  files, whose hashes it checks.
  `python3 tools/unicode/generate_grapheme_tables.py --grapheme GraphemeBreakProperty.txt --derived DerivedCoreProperties.txt --emoji emoji-data.txt --test GraphemeBreakTest.txt --tables-output src/core/text/unicode/unicode_grapheme_data.hpp --tests-output tests/unicode_grapheme_break_cases.hpp`

`tools/profile_hot_paths.py`
: Phase E, E5: on Linux, builds the recorded workloads (the Gallery painted
  through the CPU Skia adapter, `benchmarks/paint_profile_workload.cpp`; the
  text engine and DocumentView benchmarks) with frame pointers, samples each
  under `perf record`, and writes `hot-paths.json` and `hot-paths.md`: every
  function's share of the samples, self and inclusive, and each area's
  (GUI.Forms, Skia, HarfBuzz, FreeType, runtime, kernel). Shares, not
  timings. `.github/workflows/profile-linux.yml` runs it weekly and on demand.
  `python3 gui_forms/tools/profile_hot_paths.py run --output hot-paths --jobs 4`

`tools/linux_vm/`
: A Lima definition for an Alpine aarch64 (musl) guest with Clang 22 and
  libc++ 22 (with LLVM's unwinder and compiler-rt), Xvfb and Weston, for building and graphically testing the Linux host. It covers
  aarch64 musl only; CI's Ubuntu jobs cover glibc and x86-64.
  `limactl create --name=gui-forms-linux tools/linux_vm/gui-forms-linux.yaml`

Unit tests for the scripts
: `tools/test_*.py` cover the scripts above; the `house-style` job runs each
  file on its own.
  `python3 -B -m unittest discover -s tools -p test_control_registry.py`

## CI plumbing

These run inside the workflows and are rarely run by hand:
`ci_reuse.py` and `ci_promote.py` reuse or promote validation of an identical
tree; `ci_tree_receipt.py` and `release_tests.py` let the docs and
programs jobs carry an earlier run of the same tree; `compiler_identity.py`, `cache_manifest.py`, `publish_cache.py` and
`prune_cache_releases.py` describe, publish and prune the compiler-cache
pre-releases; `macos_runtime_manifest.py` and `prove_macos_runtime_restore.py`
check the published macOS runtime payload; `publication_version.py` and
`publish_tested_release.py` select a version and publish tested packages.
