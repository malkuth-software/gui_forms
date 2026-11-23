# Errors, gates and failing fast

## Unwinding only where it is defined

An exception may unwind only between the point where it was thrown and the
gate that catches it. That stretch is always C++ code compiled with
exceptions and written for them. An unwind never passes through a C frame,
the host's event loop, a client's callback or STXlib's own code, which is
built with `-fno-exceptions` and so has no cleanup code that an unwind could
run. A failure crosses a boundary as an error record GUI.Forms owns.

The gates sit at these boundaries:

| Boundary | Gate | What happens to an exception |
|---|---|---|
| Every C ABI slot | `call_slot` in {src}`src/abi/stable/registry/abi_registry.hpp` over STXlib's `call_gate_classified` | recorded, and the slot returns a `gf_status` |
| Native entries from the system: window procedures, COM and UI Automation methods, AT-SPI tables, AppKit selectors, timers | `call_failing_fast` in {src}`src/core/failure/fail_fast.hpp` | recorded; the process fails fast |
| Calls into client code: callbacks, class-table entries | STXlib's `invoke_callback_classified` | recorded; the process fails fast |
| Trampoline entries, and STXlib's interface modules where they call into the host's C++ library | STXlib's `call_gate` | recorded and returned as a failure result |

Every C ABI slot goes through one template:

```cpp
const stxlib::GateFrame frame{
    .operation = "gui_forms.c_abi",
    .first = slot.table_id,
    .second = slot.slot_index,
    .kind = GF_SHADOW_FRAME_KIND_LIBRARY_CALL,
};
const stxlib::OperationResult<gf_status> outcome =
    stxlib::call_gate_classified(slot_classifier, frame, call, arguments...);
if (outcome.succeeded()) { return *outcome.value(); }
return fail_with_record(slot, *outcome.error());
```

A thread cancellation or `pthread_exit` unwinds as a forced unwind. A gate
that catches one fails fast with a record naming cancellation, and never
turns it into a result. Code that reaches a POSIX cancellation point while
holding an STXlib lock keeps cancellation off for that stretch.

**Limit.** GUI.Forms' C++ API still lets C++ exceptions propagate to and
from C++ callers, as ordinary C++ does. The design records them as they
pass, but does not convert them. Release builds of STXlib already use no
exceptions; GUI.Forms' release builds move to none after 1.0, and the gates
are written so that, compiled without exceptions, each one becomes a direct
call that passes the result through.

## Shadowing: knowing where it happened

By the time a `catch` block runs, the frames between the throw and the
catch are gone, so a plain handler can only report the boundary. Shadowing
keeps the throw site visible without hooking the C++ runtime.

Every logical frame (a library call, input dispatch, an event callback, a
layout or paint pass) is opened by a wrapper that owns a scope object. The
wrapper calls its target and marks the frame closed when the target
returns. The scope's destructor runs on every way out. If it finds its frame
still open, it knows it is running inside an unwind. The innermost such
frame copies the logical stack, exactly as it was at the throw, into a
fixed-size record for its thread, without allocating. A development build
also takes a native trace at that point, while those frames still exist.
The gate's handler then adds what only a handler can know: the exception's
type and message.

This works the same on every host, imposes nothing on foreign code, and
costs one store per normal frame exit. It also works across modules, which
`std::uncaught_exceptions()` would not when each module has its own static
C++ runtime. It cannot see native frames between the throw and the
innermost shadow frame. The proof report lists these behaviours as tested,
not proved (STXlib `tests/shadow_tests.cpp`, `tests/two_module_tests.cpp`).

## Failing fast

A failure that cannot be carried as a value ends the process abnormally
with one report, so that the host keeps its crash report or core file.
That covers an exception out of a client's callback, an exception at a
native entry, and a broken internal invariant (`detail::fail_fast(site,
message)`). STXlib's default report line:

```text
stxlib: fail fast: <operation>: <kind> [code <n>] [foreign <family> <value>]
    [type <mangled type>]: <message>
```

GUI.Forms installs its own reporter. It writes the whole record (kind,
message, frames and, in development builds, the native trace) through
STXlib's console, after taking the thread's failure snapshot so that a dump
holds the record too. Nothing is counted and nothing continues. Our code
never calls `exit` or `quick_exit`: fail fast keeps `abort`, and process
termination releases none of STXlib's state, so nothing is freed twice on
the way out.

Failing fast is for defects, not for expected failures. Expected
alternatives are results.

## Errors as records

- **Results.** `OperationStatus` and `OperationResult` are tagged unions
  built only through named factories and read through named queries, so a
  value and a failure cannot both be present (STXlib
  `docs/CODE_STYLE_APPENDIX.md`, A4). Classification is fixed: for example
  `std::bad_alloc` becomes `out_of_memory` and `std::invalid_argument`
  becomes `invalid_argument` (STXlib `src/core/exception_shadow.cpp`).
- **C ABI.** Every function returns a `gf_status`. On failure every output
  is left unchanged, except that `GF_STATUS_BUFFER_TOO_SMALL` sets the
  required size. The failing thread's detail is copied out through the
  runtime table ({src}`include/gui_forms/c/gf_abi.h`).
- **Early exits.** Every call into code we do not control may exit early.
  At each such call site the code states one of two forms: *whole*, where
  every invariant is complete before the call and no lock is held, or
  *guarded*, where a live guard completes the partial state. The allocation
  path never throws, never `longjmp`s and never calls code that can.
  Exhaustion is a named `out_of_memory`.
