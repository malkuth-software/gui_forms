# State and lifecycle

Many interface bugs are not about memory or math at all. They are about
order: a control tells the application something changed before it has
finished changing, a handler acts on that half-finished state, and the
control carries on with indices or pointers that are no longer true. Or a
call arrives when an object is in a state its author never considered.
GUI.Forms closes these from three directions: a rule about order, a test
that attacks it, and a single declaration of every component's states
that generated checks, a fuzzer and proofs all read.

## Handlers cannot pull the floor out

### The usual approach, and how it fails

Toolkits raise events from deep inside their own state changes, and leave
it to the application not to do anything drastic in the handler. Qt's
documentation warns that deleting a `QObject` while it is handling an event
delivered to it can crash, and offers `deleteLater()`, which defers the
deletion until pending events are delivered
([Qt, `QObject`](https://doc.qt.io/qt-6/qobject.html)). The burden is the
application's: know which handlers may run inside which state changes, and
never close the window, clear the list or remove the control from there.
In practice that knowledge is incomplete, and the result is a use-after-free
that happens only when the user clicks in a particular order.

### What GUI.Forms does instead

**Commit, then notify.** A GUI.Forms component completes its state change
before it raises a notification, and carries nothing it took from its state
across the call into application code (house style, section 5). After a
handler returns, the component reads its state afresh. Where a handler may
destroy the window, the dispatching code holds the window's lifetime for
the length of the call and returns as soon as the window says it is gone.
Your handler may therefore clear the items, replace them, detach the
control, drop the last reference to it or close the window. You do not
need a `deleteLater`.

**Why it holds: a test that tries.** `hostile_handlers_tests` in
gui_forms_tests attaches a handler to each event each stock control raises
while it changes state. From inside the first notification the handler
does one destructive thing: clears the items, replaces them with a
different count, detaches the control, drops the last strong reference, or
destroys the window. Afterwards the program must be consistent: no crash,
no exception out of dispatch, no sanitizer report; focus and capture only
on attached controls; every index the control reports inside its model;
and a layout, a paint and an accessibility snapshot that succeed. Every
subject, event and action runs on a fresh window in the sanitizer build.
Events the script never raises are printed, so gaps in coverage are
visible rather than silent.

The test earns its keep. When it first ran it exposed six controls that
broke the rule. Later it found that key dispatch told the tooltip about a
key after a `key_down` handler had destroyed the window (ten
heap-use-after-free cases), and that a `SplitChange` handler destroying the
window mid-drag left the drag engine reading its own freed state. Both now
hold the window's lifetime across the call, and so do their siblings with
the same shape: menu actions, capture changes and focus changes.

## Behaviour declared once

### The usual approach, and how it fails

A component's rules usually live in several places at once: a sentence in
the documentation ("fails if called twice"), a check in the code, a
comment, a test, perhaps a fuzzer harness. Nothing makes them agree, and
over time they drift. The documentation promises one refusal, the code
gives another, and the fuzzer cannot tell misuse from a defect because it
does not know what is legal.

### What GUI.Forms does instead

Each stateful component has one declaration file beside its header
(`*.ic.toml`): its states, the states each call admits, what a call does
outside them and which refusal wins when several apply, its requirements,
its thread, and what it transfers. STXlib's `tools/ic_declarations.py`
generates from that one text the header's contract lines, the entry checks
the code calls, a column of the member manifest, the fuzzer's blocks and
the proof obligations. Because a stated range and its check come from the
same text, they cannot differ. Part of the C ABI's windows table:

```toml
states = ["created", "running", "ended"]
identity = { kind = "generational", stale = "value:GF_STATUS_STALE_HANDLE" }

[[call]]
name = "run"
in = ["created"]
outside = { running = "value:GF_STATUS_INVALID_STATE", ended = "value:GF_STATUS_DISPOSED" }
moves = "ended"
```

GUI.Forms declares all 27 C ABI tables, the lifecycles of `Component`,
`Window`, `HostSession` and `WindowDispatcher`, and the value rules of its
property families, whose setters now check them through generated
predicates. All 44 calls that can be refused for more than one reason say
which refusal wins, as read from the code.

**Why it holds.** Writing the declarations was itself a check. Reading the
code against the prose found eleven C ABI slots that did not call their
checks, declarations that promised refusals the code never gives
(`mdi_child_count`, `objects.create`), and contradictions such as
`windows.run`, whose prose said a second run fails, where the code reports
`INVALID_STATE` while running and `DISPOSED` once ended. Those are now declared as the code behaves, and the
generators keep them so.

**A fuzzer that knows what is legal.** The engine keeps every object's
declared state and schedules a legal block only where the declaration says
it may run. A crash, a sanitizer report or an integrity stop in legal mode
is therefore a defect in the code or the declaration, never misuse. In
misuse mode the engine breaks one rule on purpose and checks two things:
that the declared refusal comes back, and that the object is unchanged.
After every block the object's own reported state must match the predicted
one, which is why the C ABI gained state queries (`windows.lifecycle`,
`runtime.splashscreen_state`, beside `objects.state`). A minimiser writes
each failure out as an ordinary replay test.

**Proofs where they pay.** STXlib's deducer discharges obligations by
linear arithmetic, by popcount laws, by reading the guards in the source
and, with the per-class state tables, by following state refusals and
transitions. Each deduction records the hashes of its declaration and
sources and lapses when either changes. 141 of STXlib's 148 obligations are
discharged, each relative to premises it states; for each of the other 7
the record names the test or misuse block that covers it instead.

% Not on main yet: the GUI.Forms declarations, refusal orders and C ABI fuzz blocks are on gui_forms
% claude/ic3 (4606347a0); the deducer's typestate reader and the 141 of 148 count are on STXlib
% claude/ic-full (ea52d1c). STXlib's first declarations (allocator, containers, files) are on main.

## Every switch says what the unexpected does

House style requires a switch over a closed set to keep a default, and a
state machine to say what an unexpected input does in each state, sending
every such case, including ones believed impossible, to one defined
fallback. This matters most precisely where a value is "impossible": a
corrupted byte, a new enumerator added later, an input from a newer
peer. The state-machine gate reads the compiler's findings, not the text:
{src}`tools/check_state_machines.py` (template_harness's checker) runs
`clang++ -fsyntax-only` with `-Wswitch`, `-Wswitch-enum` and
`-Wswitch-default` over the build's commands and reports switches without a
default, and defaults that hide enumerators without a stated reason. It
also lists review candidates: ignored inputs, `if` chains with no final
`else`, and records whose fields can contradict each other. Checkpoints are
gated clean.

## Limits

- Fuzzer results are tests, not proofs, and the IC gate has no recorded
  run yet: its columns for states and edges reached, and for defects found,
  read "not run" (STXlib `proofs/ic/GATE.md`).
- A deduction holds relative to its premises, and reads the source as the
  deducer understands it; it is not a proof about the compiled code.
- The hostile-handler test covers the stock controls and the window's own
  events. A custom control written outside GUI.Forms gets the rule only if
  it follows it.
- In CI the state-machine job reports without blocking while CI builds
  without running tests; the gate is held at each checkpoint.
