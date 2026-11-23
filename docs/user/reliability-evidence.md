# Evidence

## Sanitizer runs

House style treats memory errors and undefined behaviour as defects
wherever they occur, tests included. The whole suite runs under
AddressSanitizer and UndefinedBehaviorSanitizer, and any report counts as a
failure. The sanitizer tree is `tools/ci_build.py --sanitize`, and it also
turns on the checked borrows. The run of 2026-11-22 (macOS arm64, Apple M4,
clang 22.1.8) gave these results:

- **GUI.Forms, whole suite, ASan+UBSan:** 620 of 620, with no ASan, UBSan,
  LSan or borrow-sanitizer report. The window and screen tests, run from a
  main-drive tree, passed 40 of 43 with no report. Two of the failures pass
  when run alone. The third is a frame-timing probe that misses its budget
  under sanitizer slowdown.
- **GUI.Forms C ABI under ThreadSanitizer** (`tools/abi_tsan.py`): 53 of 53,
  with no report.
- **STXlib:** 124 tests in each of 12 modes, with no sanitizer report. See
  [Memory and the allocator](reliability-memory).

% Not on main yet, and not yet run: the obligation counts below (STXlib claude/ic-full ea52d1c).

STXlib's proof obligations stand at 141 of 148 discharged by deduction.
For each of the 7 open ones, the record names the test or misuse block that
covers it (see [Constraints](reliability-constraints)). None of these
checks has been run yet.

Sanitizers find defects on the paths that ran. They do not show that the
other paths are free of them.

## Hostile event handlers

GUI.Forms finishes a state change before it raises a notification
("commit, then notify"), so nothing taken from its state is carried across
a call into application code. `hostile_handlers_tests` in gui_forms_tests
tests that rule. It attaches a handler to each event a stock control raises
while changing state. From inside the first notification, the handler does
one destructive thing:

- clears the items;
- replaces them with a different count;
- detaches the control;
- drops the last strong reference;
- destroys the window.

The control and window must then be consistent. That means no crash, no
throw out of dispatch, and no sanitizer report. Focus and capture must be
only on attached controls, every reported index must lie inside the model,
and a layout, a paint and a semantic snapshot must all succeed. The run
covers every subject, event and action, each on a fresh window, in the
sanitizer tree. Events the script never raises are listed, so gaps in
coverage are visible.

This week the test found two places where a handler destroyed its window
and the dispatching code read from it afterwards:

- Key dispatch told the tooltip route about a key after `key_down` handlers
  had destroyed the window: ten heap-use-after-free cases.
- A `SplitChange` handler destroyed the window mid-drag, and with it the
  drag engine, which then read its own state.

Both now hold the window's lifetime for the length of the call, and return
as soon as the window is gone.

## Deterministic rendering and digests

- **Text and the theme engine.** One text engine (STXlib's shaper, light
  hinting and shared sRGB blending) produces identical output on macOS,
  Windows and Linux. A hash-compared identity test enforces this. The theme
  engine's gate requires identical pixels on every host, and no frames once
  a window settles. While CI builds without running tests, these
  cross-host comparisons do not run there.
- **Floating point.** STXlib's bit-identity hashes are covered in
  [Math you can reproduce](reliability-math).
- **Assets.** The bundled fonts are checked against
  `assets/fonts/bundled-fonts.sha256` at build time, and the build fails
  when a face is missing or differs. Theme packages are checked with the
  library's own SHA-256.
- **Routing.** The node graph's link router gives the same routes on every
  host: it uses a fixed order, breaks ties by insertion sequence and does
  no hashing.

## Integrity counters

- **At run time,** GUI.Forms counts refusals instead of hiding them, and
  reports each through {src}`include/gui_forms/metrics/metrics/metrics.hpp`.
  An arrange outside the layout slot and a use of a stale image identity
  each print a diagnostic line at the 1st, 2nd, 4th and 8th occurrence, and
  so on. Each paint counts the parts the theme engine drew with its
  built-in recipe instead of their primitive.
- **In the records,** {src}`tools/integrity_counts.py` counts the audit
  findings found, fixed and declined, by source and severity. Each count
  traces to its finding IDs and the commits that changed the code. The tool
  estimates nothing.

## The C ABI's stability rules

{src}`include/gui_forms/c/gf_abi.h` is the contract.
{src}`tools/abi_audit.py` reads it in a restricted dialect, records it in
`manifests/abi/gf_abi.json`, and rejects anything outside the dialect or
any change that breaks the sealed baseline. The rules are:

- fixed-width integers, `double`, pointers and typedef'd function pointers
  only; no `enum`, `bool`, `size_t` or unions;
- records have no implicit padding, a size that is a multiple of 8, and
  records over 16 bytes pass by pointer;
- one exported function, `gf_abi_get_table`; within a major version a table
  only grows at its tail, a slot is never removed or changed, and a
  constant never changes its value;
- every function returns a `gf_status`, and failure leaves every output
  unchanged.

From 1.0 the audit rejects every break. Until then it records each break
with its reason.
