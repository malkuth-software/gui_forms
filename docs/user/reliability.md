(reliability)=
# Reliability by design

GUI.Forms should be as plausible in a woodshed as in a submarine, on a
tablet as on a space station. There is no special edition for the second
kind of place: the same toolkit is built, by design, to be less likely to
glitch and less likely to fail, and, when an error does happen, less
likely to take the rest of the program with it. It is written for the
engineers who have to answer for what the screen shows.

This section explains how. Each page takes one way that interface
software usually goes wrong, says how the common approach handles it and
where that breaks in practice, then shows what GUI.Forms and STXlib, its
interfacing library, do instead, why that holds, and where to check it in
the code, the tests and the proofs. Where something is proved, the page
names what the proof assumes; where it is only tested, the page says so.
This is high integrity by design. It is not a certification, and it is not
a formal proof of the whole toolkit.

## What you get, in order of importance

1. **A failure stops where it happens.** An error inside GUI.Forms, inside
   your handler or inside the operating system's callback never unwinds
   through code that was not written for it, and never leaves a window
   half-changed and running. An expected failure comes back as a value; a
   defect ends the process at once with a record of what happened and
   where. See [When something goes wrong](reliability-errors).
2. **A memory error in the program cannot corrupt the allocator.**
   STXlib's allocator keeps all of its bookkeeping outside the memory it
   hands out, checks every release in every build, and refuses a bad one
   without touching the address. Its page protocol is proved, relative to
   stated premises, for any number of threads. See [Memory and the allocator](reliability-memory).
3. **Handlers cannot pull the floor out.** A control finishes its state
   change before it tells your code, so a handler that clears the list,
   detaches the control or destroys the window leaves a consistent
   program. A test does exactly that to every stock control, in the
   sanitizer build. See [State and lifecycle](reliability-state).
4. **The same behaviour on every host.** Threads get the same stack and
   the same lock rules on every platform, and the math gives the same bits
   on every processor and compiler, so neither changes with the machine a
   program happens to run on. See [Threads](reliability-threads) and
   [Math you can reproduce](reliability-math).
5. **Claims you can check.** Sanitizer runs that fail on any report, tests
   that try every ordering of the allocator's steps, proofs that list their
   premises, checks on the compiled objects, and a build that leaves out
   whatever could read outside input unasked. See
   [How we know](reliability-evidence).

## The shield

```{figure} ../_static/stxlib_shield.svg
:alt: Your program at the top reaches GUI.Forms through gated interfaces. GUI.Forms sits inside STXlib, which wraps it on both sides and underneath and stands between it and the host's C library and operating system. A separate column shows the platform hosts, which call the windowing system directly behind fail-fast gates.
:width: 100%

STXlib wraps GUI.Forms and stands between it and the host's C library and
operating system. Windowing, input and accessibility go through the platform
hosts, behind fail-fast gates.
```

Most toolkits sit directly on whatever the machine provides: its C
library's allocator, its threads with their default stacks, its math
library, its C++ runtime. That is convenient, and it is also why the same
application can be solid on one machine and fragile on another. Each of
those libraries makes its own choices about accuracy, limits and what to do
with a mistake, and those choices differ between glibc, musl, macOS and
Windows.

GUI.Forms treats every interface outside itself as foreign, and STXlib
owns what can be owned. That has four consequences.

**What the code can reach is listed and checked.** Only STXlib's interface
modules may call the operating system. For the components that matter
most, a check reads the compiled objects and fails on any undefined symbol
outside a short list, each entry with its reason (STXlib
`tools/check_object_surface.py`). The allocator's list on POSIX hosts is
ten entries long: `mmap`, `mprotect`, `munmap`, `sched_yield`, `sysconf`,
three `pthread` key calls, `abort` and the thread-local access routine.
Where the compiler would call its runtime's helpers for atomic operations
(64-bit Arm Linux and FreeBSD), STXlib defines all 100 of them itself, and
a check confirms that the linked programs use STXlib's.

**Programs do not depend on the machine's C++ library.** Each shipped
program, and the C ABI library, is one module that imports only the
operating system's libraries ({src}`cmake/CheckSystemImports.cmake`
refuses anything else). The C++ library is a pinned copy linked in, so an
update on the machine cannot change its behaviour.

**Errors stop at gates.** At the C ABI, at every entry from the operating
system and at every call into your code, a failure crosses as a record
GUI.Forms owns, never as an unwinding exception.

**Behaviour is the same on every host.** Text is shaped and drawn by one
engine; threads, locks, files and math come from STXlib; promised
floating-point results are compared bit for bit across hosts and processor
tiers.

% Threads, locks, files and math moving onto STXlib are gui_forms branches claude/shield-threads-files
% and claude/shield-math, landing in checkpoint 37. Until then GUI.Forms uses the pinned C++ library
% for threads and locks, and engine_math for its math.

### What the shield does not cover

Two routes do not pass through STXlib, and the figure shows both.

- **The platform hosts** call the windowing system, input and
  accessibility interfaces directly, since that is their job. Every
  callback the system makes into GUI.Forms enters through a fail-fast gate,
  and {src}`tools/check_native_boundaries.py` keeps that true for every
  native entry point.
- **The C++ standard library** still provides GUI.Forms' containers and
  strings, its general heap (through the host's `malloc`) and
  `std::filesystem::path` for file names. It is the pinned copy, so its
  behaviour does not depend on the machine, but its allocations do not get
  the allocator's checks described on the memory page. Moving the rest
  inward is continuing work.

## The pages

```{toctree}
:maxdepth: 1

reliability-errors
reliability-memory
reliability-state
reliability-threads
reliability-math
reliability-evidence
```
