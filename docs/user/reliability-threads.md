# Threads

STXlib's threading layer (STXlib `include/stxlib/threads.hpp`) is part of
the shield. It provides `Thread`, `Mutex` and `ConditionVariable` over each
host's native primitives, one interface module per host family
(`src/hosted/<family>/threads_<family>.cpp`). It does not use `std::thread`,
`std::mutex` or `std::condition_variable`: STXlib builds without
exceptions, and those types could not report a failed start without them.

% In progress: GUI.Forms' own std::thread and std::mutex uses are being moved onto this layer
% (branch claude/shield-threads-files); until that lands, GUI.Forms still uses the pinned C++
% library for them (see the shield's limits on the first page of this section).

## What it guarantees

- **No platform types in the header.** `Mutex` and `ConditionVariable` keep
  their host objects in fixed private storage, and each family module
  checks that its host object fits. `Thread` keeps its shared state in a
  block from STXlib's allocator, so a thread started in one module may be
  joined or destroyed in another.
- **The same stack on every host.** A thread's stack is the size the caller
  states, or `default_thread_stack_bytes` (4 MiB), and never the host's
  default. Host defaults vary: 128 KiB on musl, 512 KiB on macOS, 1 MiB on
  Windows, and 8 MiB from `RLIMIT_STACK` on glibc. A size outside 256 KiB to
  1 GiB is refused, and every thread has a guard page below its stack.
- **Lock misuse is trapped.** Locking a `Mutex` the thread already holds, or
  unlocking one it does not hold, fails fast in check builds
  (`STXLIB_LOCK_CHECKS`: Debug and sanitizer builds). In other builds it is
  undefined, as with the host's mutex. The death tests `recursive_lock` and
  `foreign_unlock` cover both cases.
- **Waits cannot miss a notification.** A wait takes a named predicate,
  which is checked under the lock before every host wait. The wait cannot
  return on a spurious wake-up, and it reports `timed_out` only once
  `MonotonicTime` has reached the deadline with the predicate still false.
- **Failures by kind.** A host error in a lock, an unlock or a condition
  wait cannot be seen by a correct program. It is a defect, and it fails
  fast with `internal_failure` and the host's code. Running out of
  resources when starting a thread is a result the caller handles.
- **Identity without destructors.** No thread-local storage in STXlib has a
  non-trivial destructor. A thread's identity lives in `stxlib_state`, is
  issued once, and is never reused.
- **Thread records end on the thread.** A thread STXlib starts, including
  every `AtomicPool` worker, ends its per-thread records as the last act of
  its body, on the thread itself. The release callbacks therefore run
  outside the loader lock that a Windows thread's exit hook holds, and the
  exit hook that follows releases nothing again.
- **Cancellation outside locked sections.** STXlib acts on a POSIX
  cancellation only outside its locked sections. Inside them, cancellation
  is held off until the next cancellation point after the section.

## What is built on it

`AtomicPool` (STXlib `thread_pool.hpp`) runs each task of a batch exactly
once. Small batches run on the caller in order, and larger ones run on the
workers. Its conformance test runs one source against the original C
implementation and against STXlib's pool, and both must pass. Beneath the
layer, on 64-bit Arm Linux and FreeBSD, the atomic operations call STXlib's
own outline-atomics helpers, not the toolchain's. See
[The shield](reliability).

**Limits.** STXlib's proof report lists the threading properties as tested,
not proved. Examples: 64 sequential threads, 8 threads each making 20,000
locked increments, and 8 racing `call_once` callers. The rule for timeouts
holds by construction and is not checked by a tool.
