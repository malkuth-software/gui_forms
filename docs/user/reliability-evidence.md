# How we know

A reliability claim is only as good as the way it is checked. The usual
evidence for an interface toolkit is a suite of example-based unit tests,
some manual testing, perhaps a sanitizer run now and then on one platform,
and a long record of use. That record is real evidence, but it mostly
shows that the common paths work; the rare orderings and the hostile
inputs are exactly what it does not exercise. GUI.Forms' evidence is built
to reach those, and to say plainly which kind of evidence each claim
rests on.

## Three words, used strictly

- **Proved** means a deductive argument: a deduction checked by a tool, or
  a certificate in exact arithmetic. Every proof names its premises, and
  holds only relative to them. Exploring a finite set of states, however
  large, is not called a proof.
- **Tested** means the property was checked on the paths a test ran. An
  exhaustive test within a stated bound (every ordering with up to three
  switches, every float in a range) says so, with the bound.
- **Argued** means reasoned from the code by a person, and not yet checked
  by a tool.

STXlib's `proofs/REPORT.md` labels every property it lists with one of
these words, and the pages of this section follow it.

## Sanitizers, over everything, failing on any report

House style treats memory errors and undefined behaviour as defects
wherever they occur, tests included. The whole suite runs under
AddressSanitizer and UndefinedBehaviorSanitizer, and any report is a
failure; the sanitizer tree (`tools/ci_build.py --sanitize`) also turns on
the checked borrows ([Memory](reliability-memory)). The run of 2026-11-22
(macOS arm64, Apple M4, clang 22.1.8):

- **GUI.Forms, whole suite, ASan and UBSan:** 620 of 620, with no ASan,
  UBSan, LSan or borrow report. The window and screen tests, run apart,
  passed 40 of 43 with no report: two of the failures pass when run alone,
  and the third is a frame-timing probe that misses its budget under
  sanitizer slowdown.
- **GUI.Forms' C ABI under ThreadSanitizer** (`tools/abi_tsan.py`): 53 of
  53, no report.
- **STXlib:** 124 tests in each of 12 modes (debug and a hardened release
  on its own allocator; UBSan, ASan with UBSan, ASan with strict options,
  LSan, TSan and the integer checks on the host allocator; UBSan, the
  integer checks and TSan with its own allocator forced; and ASan with
  UBSan built without exceptions), with no sanitizer report. The one LSan
  failure is a test that returns from `main` on purpose while its workers
  still run.

Sanitizers find defects on the paths that ran. They do not show that the
others are free of them, which is why the tests below go looking for the
rare paths.

## Tests that go looking for trouble

- **Hostile handlers** destroy, clear and detach from inside every stock
  control's notifications ([State and lifecycle](reliability-state)).
- **Every ordering of the allocator's steps** within a bound, on the
  compiled code, with deliberately broken protocols to show the checker
  sees ([Memory](reliability-memory)).
- **A block fuzzer that knows what is legal**, generated from the same
  declarations as the checks, with a misuse mode that checks the declared
  refusal and that nothing changed ([State and lifecycle](reliability-state)).
- **Frozen bit-identity hashes** compared across hosts and SIMD tiers
  ([Math](reliability-math)).

## Checks on what was built, not on what was meant

- **Object surfaces.** For the components that matter most, a check lists
  every symbol their compiled objects leave undefined and fails on anything
  outside a short list with reasons (STXlib `tools/check_object_surface.py`).
- **System imports.** Each shipped program, and the C ABI library, imports
  only the operating system's libraries
  ({src}`cmake/CheckSystemImports.cmake`).
- **Assets.** The bundled fonts are checked against
  `assets/fonts/bundled-fonts.sha256` at build time; the build fails when a
  face is missing or differs.

## High-integrity builds

`GUI_FORMS_HIGH_INTEGRITY` turns one principle into a build: code that
would let a program take in outside bytes, or reach outside itself,
without the program asking is **not compiled**, rather than disabled. A
reviewer then checks the symbols a build defines and imports, not a promise
about its run-time behaviour.

```sh
cmake -DGUI_FORMS_HIGH_INTEGRITY=ON ...
```

Compiled out: environment and user-locale reads at start-up, the hosts'
virtual services, drops from other programs, frame capture, motion, the
theme request read from arguments and the environment, and the C ABI
entries for all of these, which become null slots. Entries a program must
call itself (application fonts, hosted content, user themes, inline
styles) stay compiled and are pruned from any program that does not call
them.

The option also fixes 17 settings and stops the configure if any is given
another value: hardening flags on; the automation channel, the audio
service, the fuzzers, the Gallery and the development text services off;
STXlib on its own allocator, with one math code path; Release, a static
C++ runtime and link-time pruning, so that a program holds exactly what it
reaches. `gui_forms_high_integrity_surface` checks the symbols of a linked
example program ({src}`cmake/HighIntegrity.cmake`).

## The C ABI does not move under you

{src}`include/gui_forms/c/gf_abi.h` is the contract, and
{src}`tools/abi_audit.py` reads it in a restricted dialect, records it in
`manifests/abi/gf_abi.json`, and rejects anything outside the dialect or
any change that breaks the sealed baseline:

- fixed-width integers, `double`, pointers and typedef'd function pointers
  only; no `enum`, `bool`, `size_t` or unions;
- records without implicit padding, sized in multiples of 8, and passed by
  pointer above 16 bytes;
- one exported function, `gf_abi_get_table`; within a major version a table
  only grows at its tail, a slot is never removed or changed, and a
  constant never changes its value;
- every function returns a `gf_status`, and failure leaves every output
  unchanged.

From 1.0 the audit rejects every break; until then it records each one with
its reason.

## Deterministic output

- **Text and themes.** One text engine (STXlib's shaper, light hinting and
  shared sRGB blending) produces identical output on macOS, Windows and
  Linux, enforced by a hash-compared identity test. The theme engine's gate
  requires identical pixels on every host, and no frames once a window
  settles.
- **Routing.** The node graph's link router uses a fixed order, breaks
  ties by insertion sequence and does no hashing, so it gives the same
  routes on every host.

## Counting instead of hiding

- **At run time,** GUI.Forms counts refusals instead of swallowing them,
  and reports each through {src}`include/gui_forms/metrics/metrics/metrics.hpp`.
  An arrange outside its layout slot or a use of a stale image identity
  prints a diagnostic at the 1st, 2nd, 4th and 8th occurrence, and so on,
  so a repeated fault is visible without flooding the log.
- **In the records,** {src}`tools/integrity_counts.py` counts the audit
  findings found, fixed and declined, by source and severity, each traced
  to its finding IDs and the commits that changed the code.

## Limits

- The sanitizer figures above are from macOS arm64; GUI.Forms' Linux and
  Windows sanitizer runs were not part of that run.
- While CI builds without running tests, the cross-host pixel and text
  comparisons and the state-machine gate do not block there; they are held
  at each checkpoint.
- Nothing here is a certification against a safety standard.
