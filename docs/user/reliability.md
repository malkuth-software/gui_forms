(reliability)=
# Reliability by design

The premise: the same toolkit should be usable in a workshop tool and in
equipment where a failure is costly. This section describes the mechanisms
that make GUI.Forms and its interfacing library, STXlib, less likely to fail,
and less likely to do harm when an error does occur. For each mechanism it
gives what it guarantees, where it is checked, and its limits. Where a
property is proved, the text names what the proof assumes. Where it is only
tested, the text says so. Nothing here is a certification.

## The shield

```{figure} ../_static/stxlib_shield.svg
:alt: Your program at the top reaches GUI.Forms through gated interfaces. GUI.Forms sits inside STXlib, which wraps it on both sides and underneath and stands between it and the host's C library and operating system. A separate column shows the platform hosts, which call the windowing system directly behind fail-fast gates.
:width: 100%

STXlib wraps GUI.Forms and stands between it and the host's C library and
operating system. Windowing, input and accessibility go through the platform
hosts. The pinned C++ library inside the program is the other direct route.
```

STXlib treats every interface it touches as foreign: the host's C library,
the host's C++ library, the operating system and kernel, and the program on
the other side of GUI.Forms. It protects GUI.Forms from those interfaces,
and it protects your program from GUI.Forms. It loads alongside the host's
C library and owns whatever can be owned, so its behaviour is the same
whatever the host does. A host difference that shows inside is treated as a
defect of the boundary.

The layering gives four things.

**One audited surface for what the code can reach.** Only STXlib's
interface modules may make a system call, include an operating-system
header or name a kernel object, and a checker enforces this on the compiled
output. For components that matter most, the object-surface check lists
every symbol the component's compiled objects leave undefined and fails on
anything outside a short list, each entry with its reason (STXlib's
`tools/check_object_surface.py`). For STXlib's allocator on POSIX
hosts, that list is ten entries: `mmap`, `mprotect`, `munmap`,
`sched_yield`, `sysconf`, three `pthread` key calls, `abort` and the
thread-local access routine. On 64-bit Arm Linux and FreeBSD, the compiler
turns atomic operations into calls to helper functions from the toolchain's
runtime. STXlib defines all 100 of those helpers itself, and a check
confirms that the linked programs take every one from STXlib.

**Checks that do not depend on the host's C++ library.** Each shipped
program, and the C ABI library, is one module. It imports no library but
the system's: no first-party shared library, and no C++ runtime
({src}`cmake/CheckSystemImports.cmake` refuses anything else). The C++
library is a pinned copy linked in, not whatever the machine has installed.
STXlib's own checks run inside the program in every build, not only under a
debugger or a sanitizer. For example, the allocator stops the process with
a recorded reason when a block is released twice.

**Errors stop at gates.** At the C ABI, at native entries from the system
and at calls into client code, a failure crosses as an error record
GUI.Forms owns, never as an unwinding exception. See
[Errors, gates and failing fast](reliability-errors).

**The same behaviour on every host.** Text is shaped and drawn by one
engine, math comes from owned kernels with stated accuracy, and promised
floating-point results are checked bit for bit across hosts and processor
tiers. See [Math you can reproduce](reliability-math) and
[Evidence](reliability-evidence).

### What the shield does not cover yet

Two routes do not pass through STXlib, and the figure shows both:

- **The platform hosts** call the windowing system, input and accessibility
  interfaces directly, since that is their job. Every callback the system
  makes into GUI.Forms enters through a fail-fast gate, and
  {src}`tools/check_native_boundaries.py` keeps that true for every native
  entry point.
- **The C++ standard library.** GUI.Forms' own code still uses it for
  containers, threads, locks and some file access, and that library calls
  the host's C library itself. It is the pinned copy linked into the
  program, so its own behaviour does not depend on the host's C++ library. Moving that use
  inward, behind STXlib's types, is continuing work.

## How the rest fits inside

- [Memory and the allocator](reliability-memory): every datum has an owner
  and a lifetime, and STXlib's allocator checks its own bookkeeping.
- [Errors, gates and failing fast](reliability-errors): unwinding happens
  only where it is defined, and a failure that cannot be carried as a value
  ends the process with a full record.
- [Math you can reproduce](reliability-math): owned transcendental
  functions with measured or proved bounds.
- [Constraints, state machines and builds](reliability-constraints):
  behaviour declared once and checked by generated code, a fuzzer and
  proofs; switches that must say what an unexpected input does; and a build
  that leaves out whatever it does not need.
- [Evidence](reliability-evidence): sanitizer runs, hostile event handlers,
  rendering digests, integrity counters and the C ABI's stability rules.

```{toctree}
:hidden:

reliability-memory
reliability-errors
reliability-math
reliability-constraints
reliability-evidence
```
