# Threads

A program that works on the developer's machine and fails on the
customer's is often failing in its threads: a stack that was large enough
here and is not there, a lock taken twice, a wait that missed its wake-up.
These faults depend on the host, so testing on one host does not find
them. GUI.Forms takes its threads, locks and waits from STXlib, which makes
them behave the same everywhere and traps the misuse the host would
silently allow.

## The usual approach, and how it fails

Most C++ toolkits use the platform's threads directly, through
`std::thread` or a thin wrapper, and inherit the platform's defaults.

- **The stack size is whatever the host chooses.** `std::thread` has no
  way to ask for one. Qt's `QThread::setStackSize` lets you set one, and
  if you do not, "the operating system or runtime will choose a default
  value" ([Qt, `QThread`](https://doc.qt.io/qt-6/qthread.html)). Those
  defaults differ by a factor of up to sixty-four: 128 KiB on musl, where glibc's
  "generally ends up being 2-10 MB"
  ([musl wiki](https://wiki.musl-libc.org/functional-differences-from-glibc.html)),
  taken from the stack limit the process started with, commonly 8 MiB
  ([`pthread_create(3)`](https://man7.org/linux/man-pages/man3/pthread_create.3.html));
  512 KiB for secondary threads on macOS
  ([Apple, *Threading Programming Guide*](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/CreatingThreads/CreatingThreads.html));
  1 MiB on Windows
  ([Microsoft, *Thread Stack Size*](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-stack-size)).
  A parser or a layout pass that runs comfortably on a glibc desktop can
  overflow on Alpine Linux or on a macOS worker thread.
- **Misusing a lock is undefined.** Locking a `std::mutex` the thread
  already holds, or unlocking one it does not hold, is undefined
  behaviour: typically a deadlock in the field, or nothing at all until the
  day it corrupts what the lock protected.
- **A wait can miss or invent its wake-up.** A condition variable may wake
  spuriously, and a notification sent before the wait begins is lost
  unless the code checks a condition under the lock. Correct code needs a
  predicate loop each time; code without one works until the timing
  changes.
- **Failure to start is an exception.** `std::thread`'s constructor
  reports a failed start by throwing, which code built without exceptions
  cannot handle.

## What GUI.Forms does instead

Every thread GUI.Forms starts is an `stxlib::Thread`
(`src/core/threading/start_thread.hpp`), every lock an `stxlib::Mutex`,
every wait an `stxlib::ConditionVariable` predicate wait, and stop requests
use STXlib's `StopToken`. STXlib's threading layer
(STXlib `include/stxlib/threads.hpp`) sits over each host's native
primitives, one interface module per host family, and gives these
guarantees on every host (owner decision D11).

- **The same stack everywhere.** A thread's stack is the size the caller
  states, or 4 MiB, and never the host's default. A size outside 256 KiB to
  1 GiB is refused, and every thread has a guard page below its stack, so an
  overflow traps instead of running into other memory.
- **Lock misuse is trapped.** Locking a `Mutex` the thread already holds,
  or unlocking one it does not hold, fails fast in check builds (Debug and
  sanitizer builds), with a record. The death tests `recursive_lock` and
  `foreign_unlock` cover both cases. Code that needs re-entry says so with
  `RecursiveMutex`.
- **Waits cannot miss a notification.** Every wait takes a named
  predicate, checked under the lock before every host wait. It cannot
  return on a spurious wake-up, and it reports `timed_out` only once the
  monotonic clock has passed the deadline with the predicate still false.
- **Failures are sorted by kind.** A host error in a lock, an unlock or a
  wait cannot happen in a correct program, so it is a defect and fails fast
  with the host's code. Running out of resources when starting a thread is
  a result the caller handles.
- **Identity without destructors.** A thread's identity is issued once and
  never reused, and no thread-local storage in STXlib has a destructor, so
  nothing runs at thread exit in an order the host chooses.
- **Thread records end on the thread.** A thread STXlib starts ends its
  per-thread records as the last act of its body, on the thread itself. On
  Windows the release callbacks therefore run outside the loader lock that
  a thread's exit hook holds, which is where cleanup code otherwise
  deadlocks.
- **Cancellation never lands inside a locked section.** On POSIX hosts,
  STXlib holds cancellation off inside its own locked sections; a
  cancellation requested meanwhile takes effect at the next cancellation
  point after the section.

**Why it holds.** None of `std::thread`, `std::mutex` or
`std::condition_variable` is used: each host family's module calls the host
itself, and checks that the host's objects fit the fixed storage the
portable types reserve, so no platform type appears in a header.
GUI.Forms' standard-library rules check (`tools/check_std_rules.py`)
fails on any use of the standard thread, lock and wait vocabulary
(`std::thread`, `std::mutex`, `std::condition_variable` and their
relatives), and of `pthread` outside native code. On top of this layer, STXlib's `AtomicPool` runs each
task of a batch exactly once, and its conformance test runs the same
source against the original C implementation and against STXlib's pool.

% Not on main yet: GUI.Forms' threads, locks, waits and stop tokens moving onto STXlib are gui_forms
% branch claude/shield-threads-files, landing in checkpoint 37.

## Limits

- Lock misuse is trapped only in check builds; in a release build it is
  undefined, as with the host's mutex.
- STXlib's proof report lists the threading properties as tested, not
  proved: for example 64 sequential threads, 8 threads each making 20,000
  locked increments, and 8 racing `call_once` callers. The C ABI's 53 tests
  under ThreadSanitizer pass with no report. The timeout rule holds by
  construction and is not checked by a tool.
- In a browser, threads come from a pool of Web Workers made at start, and
  starting one more than the pool holds fails instead of waiting
  (`thread_pool_limit`).
