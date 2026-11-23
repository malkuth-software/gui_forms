# Constraints, state machines and builds

## Behaviour declared once

% Not on main yet: the GUI.Forms declarations, refusal orders and the C ABI fuzz blocks are on
% gui_forms claude/ic3 (4606347a0); STXlib's engine, deducer and its later declarations are on
% STXlib claude/ic-full (d044e38). STXlib's first declarations (allocator, containers, files) are
% on STXlib main.

Each stateful component has one declaration file beside its header
(`*.ic.toml`). It states the component's states, the states each call
admits, what a call does outside them, its requirements, its thread, and
what it transfers. STXlib's `tools/ic_declarations.py`, which
template_harness shares, generates the following from it: the header's
`// ic:` contract lines, the entry checks the code calls, a constraints
column in the member manifest, fuzzer blocks and proof obligations. Because
the stated range and its check come from one text, they cannot differ. Part
of the C ABI's windows table:

```toml
states = ["created", "running", "ended"]
state_query = { call = "gf_windows_table.lifecycle", observes = { ... } }
identity = { kind = "generational", stale = "value:GF_STATUS_STALE_HANDLE" }

[[call]]
name = "run"
in = ["created"]
outside = { running = "value:GF_STATUS_INVALID_STATE", ended = "value:GF_STATUS_DISPOSED" }
moves = "ended"
```

When a call can be refused for more than one reason, the declaration says
which refusal wins. All 44 such calls in GUI.Forms declare it, as read from
the code. In the C ABI, each argument clause is checked before the subject
is entered:

```toml
requires = [{ holds = "not_null(count)", refuses = "value:GF_STATUS_INVALID_ARGUMENT", check = "mdi_child_count_count" }]
refusal_order = ["mdi_child_count_count", "outside"]
```

GUI.Forms declares all 27 C ABI tables, the C++ lifecycle of `Component`,
`Window`, `HostSession` and `WindowDispatcher`, and the property families'
value rules, which the setters now check through generated predicates.
The C ABI gains state queries (`windows.lifecycle`,
`runtime.splashscreen_state`, beside the existing `objects.state`), so the
fuzzer can observe the declared states.

## The block fuzzer: legal and misuse modes

The generator writes one legal block per call and source state, misuse
blocks for each clause and bounded variable, and outside blocks for each
refused state. The engine keeps every object's declared state and schedules
a legal block only where its states and requirements hold. A crash, a
sanitizer report or an integrity stop in legal mode is therefore a defect
in the code or in the declaration, never misuse. In misuse mode the engine
breaks one rule on purpose and checks two things: that the declared refusal
comes back, and that the object is unchanged. After every block, the
object's own reported state must equal the predicted one. libFuzzer drives
the engine through a custom mutator, and a minimiser writes each failure out
as an ordinary replay test. In GUI.Forms, `tools/ic_abi_fuzz.py` writes the
C ABI block table, including ordered blocks that break a clause on a
subject in the wrong state and expect the declared winner.

**Limits.** Fuzzer results are tests, not proofs. STXlib's gate record
(`proofs/ic/GATE.md`) does not yet hold a recorded run: its columns for
states and edges reached, and for defects found, still read "not run".

## Proofs, relative to premises

STXlib's deducer (`tools/ic_deduce.py`) discharges obligations three ways:
by linear arithmetic, by popcount laws, and by reading the guards in the
source. Each deduction records the hashes of its declaration and sources,
and holds only while they match. 97 of STXlib's 148 obligations are
discharged, each relative to premises it states. The other 51 are open.
Most of them are state refusals and transitions that the deducer does not
yet handle. The largest discharged obligation is the allocator's page
protocol, with its premises listed in
[Memory and the allocator](reliability-memory).

## The state-machine gate

House style requires a switch over a closed set to have a default, and a
state machine to say what an unexpected input does in each state. The
checker reads the compiler's findings, not the text:
template_harness's `check_state_machines.py` (forwarded by
{src}`tools/check_state_machines.py`) runs `clang++ -fsyntax-only` with
`-Wswitch`, `-Wswitch-enum` and `-Wswitch-default` over the build's
commands. It reports two kinds of finding:

- **F1:** a switch without a default;
- **F2:** an enumeration switch whose default hides enumerators without
  stating a reason, or a vocabulary mapping that does not list every
  enumerator.

It also lists review candidates: ignored inputs, if-chains with no final
`else`, and records whose fields can contradict each other. Checkpoints are
gated clean. In CI the job reports without blocking while CI builds without
running tests.

## High-integrity builds

`GUI_FORMS_HIGH_INTEGRITY` follows one rule: code that would let a program
take in outside bytes or reach outside itself without asking is not
compiled, rather than disabled. A reviewer then checks the symbols a build
defines and imports, not its run-time behaviour.

```sh
cmake -DGUI_FORMS_HIGH_INTEGRITY=ON ...
```

What is compiled out:

- environment and user-locale reads at start-up;
- the hosts' virtual services, drops from other programs, and frame
  capture;
- motion;
- the theme request read from arguments and the environment;
- the C ABI entries for all of these, which become null slots.

What stays compiled: entries a program must call itself (application fonts,
hosted content, user themes, inline styles), pruned from any program that
does not call them.

The option also fixes 17 settings and stops the configure if any is given
another value. Among them:

- hardening flags on;
- the automation channel, the audio service, the fuzzers, the Gallery and
  the development text services off;
- STXlib on its own allocator, with one math code path;
- Release, a static C++ runtime and link-time pruning.

`gui_forms_high_integrity_surface` checks the symbols of a linked example
program ({src}`cmake/HighIntegrity.cmake`).
