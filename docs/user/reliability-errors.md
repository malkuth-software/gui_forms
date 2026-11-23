# When something goes wrong

Every program meets errors: a handler with a bug, a file that is not
there, a call made in the wrong state. What decides whether a user sees a
glitch, a crash or nothing at all is what the toolkit does next. GUI.Forms
follows one rule: **an error either comes back as a value, with the
program unchanged, or it stops the program at once with a record of what
happened and where.** There is no third outcome where the program carries
on in a state nobody intended.

## The usual approach, and how it fails

Interface toolkits are driven by callbacks: the toolkit calls your code
when something happens, your code calls the toolkit back. When an error
happens in the middle of that, the common answers are these.

- **Leave it undefined.** Qt's documentation says that throwing an
  exception from a slot invoked through a signal connection "is considered
  undefined behaviour, unless it is handled within the slot", and that
  after catching an exception thrown within Qt the only supported recovery
  is to leave the event loop and clean up before exiting
  ([Qt, *Exception Safety*](https://doc.qt.io/qt-6/exceptionsafety.html)).
- **Log and keep going.** In GLib and GTK, a failed precondition check
  logs a critical message and returns from the function, and the
  documentation says the function "should be considered to have undefined
  behaviour" at that point
  ([GLib, *Warnings and Assertions*](https://docs.gtk.org/glib/warnings.html)).
  The program continues.
- **Catch everything and continue.** Windows Forms lets an application
  "catch all unhandled exceptions thrown by Windows Forms components and
  continue operating"
  ([`Application.SetUnhandledExceptionMode`](https://learn.microsoft.com/en-us/dotnet/api/system.windows.forms.application.setunhandledexceptionmode)).
  In a web page, and so in Electron's renderer, an exception in an event
  listener stops that script while the page keeps running
  ([MDN, `error` event](https://developer.mozilla.org/en-US/docs/Web/API/Window/error_event)).

Each of these fails in a recognisable way. Undefined behaviour shows up
later and somewhere else: a crash in the event loop that points at nothing
the developer wrote. Continuing is worse in a quieter way. The exception
left a handler halfway through: a list was cleared but its selection
was not, a setting was written but the screen was not redrawn, one of
two linked values was updated and the other not. The program now runs in
a state no test covered, and the user sees a glitch, or a wrong value,
with no report. When
a report does exist, it usually names the boundary where the exception was
caught, because the frames where it was thrown are already gone.

## What GUI.Forms does instead

### Errors stop at gates

An exception may unwind only between the point where it was thrown and the
gate that catches it. That stretch is always C++ written and compiled for
exceptions. An unwind never passes through a C frame, the operating
system's event loop, your callback or STXlib, which is built without
exceptions and so has no cleanup code an unwind could run half of. Every
boundary has its gate:

| Boundary | Gate | What happens to an exception |
|---|---|---|
| Every C ABI slot | `call_slot` in {src}`src/abi/stable/registry/abi_registry.hpp`, over STXlib's `call_gate_classified` | recorded; the slot returns a `gf_status` |
| Entries from the operating system: window procedures, COM and UI Automation methods, AT-SPI tables, AppKit selectors, timers | `call_failing_fast` in {src}`src/core/failure/fail_fast.hpp` | recorded; the process fails fast |
| Calls into your code: callbacks, class-table entries | STXlib's `invoke_callback_classified` | recorded; the process fails fast |
| Trampolines, and STXlib's modules where they call the host's C++ library | STXlib's `call_gate` | recorded and returned as a failure result |

**Why it holds.** The gates are not a convention. Every C ABI slot goes
through one template, so no slot can forget its gate.
{src}`tools/check_native_boundaries.py` fails the build when a native entry
point does not go through `call_failing_fast`. STXlib's own code is
compiled with `-fno-exceptions`, and the gates see to it that no exception
ever has to cross it. A thread
cancellation or `pthread_exit`, which unwinds as a forced unwind on glibc,
is never turned into a result: a gate that meets one fails fast and says
so.

### Expected failures are values, and leave nothing behind

A file that cannot be opened, a call in the wrong state, a buffer that is
too small: these are results, not exceptions. STXlib's `OperationStatus`
and `OperationResult` hold either a value or a failure, never both, and are
built only through named factories, so a caller cannot read a value that
is not there. At the C ABI every function returns a `gf_status`, and on
failure every output is left unchanged, except that
`GF_STATUS_BUFFER_TOO_SMALL` reports the size needed
({src}`include/gui_forms/c/gf_abi.h`). A failed call is therefore one you
can simply retry or abandon; there is nothing to repair.

### Defects stop the program, with a record

A failure that cannot be carried as a value is a defect: an exception out
of your callback, an exception at an entry from the operating system, a
broken internal invariant. GUI.Forms ends the process abnormally with one
report, so the operating system keeps its crash report or core file. The
report gives the kind, the message, the exception's type, and the logical
stack at the point of the throw.

Stopping is the conservative choice, and it is deliberate. A process that
stops can be restarted by its supervisor, its window manager or its user in
a known state. A process that continues after a defect can do anything,
including the wrong thing quietly. Failing fast also makes defects visible
during development, where they are cheap to fix, instead of in the field.

Two details keep the stop clean. GUI.Forms' own code never calls `exit` or
`quick_exit`, so no exit-time destructors run over damaged state. And
process termination releases none of STXlib's state, so nothing is freed
twice on the way out.

### Knowing where it happened: shadowing

By the time a `catch` block runs, the frames between the throw and the
catch are gone, so an ordinary handler can only name the boundary.
GUI.Forms keeps the throw site visible without hooking the C++ runtime.
Every logical frame (a library call, input dispatch, an event callback, a
layout or paint pass) is opened by a scope object whose destructor runs on
every way out. If it runs while its frame is still open, an unwind is in
progress, and the innermost such frame copies the logical stack, as it was
at the throw, into a fixed-size record for its thread, without allocating.
A development build also takes a native trace there, while those frames
still exist.

This works the same on every host, costs one store per normal frame exit,
and works across modules that each carry their own static C++ runtime,
where `std::uncaught_exceptions()` would not
(STXlib `tests/shadow_tests.cpp`, `tests/two_module_tests.cpp`).

## Limits

- GUI.Forms' C++ API still lets C++ exceptions pass to and from C++
  callers, as ordinary C++ does. They are recorded as they pass, but not
  converted. Release builds of STXlib already use no exceptions; GUI.Forms'
  release builds move to none after 1.0, and every gate then compiles to a
  direct call.
- Shadowing cannot see native frames between the throw and the innermost
  logical frame. STXlib's proof report lists shadowing as tested, not
  proved.
- Failing fast assumes that something outside the process can restart it
  or tell the user. For equipment that must keep a display alive, that
  supervisor is part of the system design, as it would be for any other
  process.
