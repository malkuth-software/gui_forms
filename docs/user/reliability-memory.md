# Memory and the allocator

## Ownership and lifetimes

Every datum has one owner and a lifetime tied to it. Ownership moves
explicitly and leaves the former owner empty, and observational links do
not own. Memory STXlib allocates is released only by STXlib, and STXlib
never releases memory it did not allocate. The house style enforces this
with checks that run on every change:

- **Borrows cannot outlive their source.** A function that returns a borrow
  of an argument deletes its rvalue overload, so a borrow of a temporary
  does not compile (`std::span<const RoutedLink> routes() const&& = delete;`).
  {src}`tools/check_lifetime_rules.py` checks this rule, and
  {src}`tools/lifetime_lint.py` runs clang's lifetime analysis.
- **Checked borrows.** In the sanitizer tree (`GUI_FORMS_CHECKED_BORROWS`),
  each lending container keeps a generation, and each borrowed view carries
  a stamp. A view used after its owner changed structure, which
  AddressSanitizer misses when the old bytes are still mapped, ends the
  process with where the view was lent and where the owner last changed
  ({src}`include/gui_forms/borrow/borrowed_view/borrowed_view.hpp`). In an
  ordinary build the same names are the plain `std::span` and pointer
  types, at no cost.
- **No unbounded recursion.** Recursion over runtime-shaped data (control
  trees, documents, parsers, nested dispatch) is forbidden unless a maximum
  depth is enforced before traversal. Deep owning structures are torn down
  iteratively.
- **Every `thread_local` is trivially destructible.** Real per-thread state
  lives in records STXlib owns and releases once, at thread exit, through
  its own hook.

## stx_minmallocpp, STXlib's allocator

STXlib's own allocator is a conversion of mimalloc v3's design to house-style
C++ (STXlib `src/state/stx_minmallocpp/`). It is selected with
`STXLIB_ALLOCATOR=stx_minmallocpp`, and the high-integrity configuration
requires it.

**Design.** Each thread allocates without a lock from its own heap: for
each size class, the current page's current free word and a queue of the
pages it owns. Each page tracks its blocks in bit words. A *free* word holds
blocks the owner may hand out, a *remote* word holds blocks other threads
released (set with `fetch_or`), and a *summary* marks the non-zero free
words. A heap owns a page only while it will visit it. A full page is
abandoned at once. A release into an abandoned page claims it only if the
releasing thread is allocating that class and holds less than 4 MiB of it.
Otherwise the release sets the block's remote bit. A thread that only
releases therefore owns nothing and takes no lock. Heaps live in a table
that is never unmapped, and an owner identity is never reused. Every step
that touches shared memory is marked (`STX_MINMALLOCPP_STEP`), so tools can
cut the code at those points.

**Integrity stops.** The allocator checks its own bookkeeping. When it finds a contradiction, it records which one for a debugger
or core file, then ends the process (`Integrity` in `page.hpp`):
`released_twice` (a remote bit and a free bit name the same block),
`released_into_freed_page`, `live_count` (more blocks merged than were
live), all three in every build, and `check` (an entry or exit check
failed, in builds with `STXLIB_STX_MINMALLOCPP_CHECKS`). A release of an address
that is not the first byte of one of its live blocks is refused before
anything is written. That this refusal also reads nothing near the address
is argued from the code, not proved.

**Reach.** On POSIX hosts the allocator's compiled objects leave exactly
these symbols undefined: `mmap`, `mprotect`, `munmap`, `sched_yield`,
`sysconf`, `pthread_key_create`, `pthread_setspecific`,
`pthread_getspecific`, `abort` and the thread-local access routine.
`stxlib_stx_minmallocpp_surface` checks this on every build. On 64-bit Arm
Linux and FreeBSD, the atomic helpers come from STXlib itself.

## How it is tested

- **Sanitizers**, 2026-11-22, macOS arm64, Apple M4, clang 22.1.8:
  - STXlib's 124 tests pass in all 12 modes. The modes are debug and secure
    (Release with libc++ extensive hardening, `FORTIFY_SOURCE=2` and tags),
    both on this allocator, then UBSan, ASan+UBSan, ASan with strict
    options, LSan, TSan and the integer checks, plus UBSan, the integer
    checks and TSan with this allocator forced, and ASan+UBSan without
    exceptions (88 tests).
  - The one LSan failure is expected: `stxlib_allocator_exit_tests` returns
    from `main` on purpose while its workers still run.
  - On Linux glibc aarch64, after the outline-atomics change, 131 of 131
    pass in debug, plain, ASan, UBSan, integer and ASan-on-this-allocator
    modes.
- **Every ordering of the steps, within a bound.** `stx_minmallocpp_interleave`
  turns each step mark into a scheduler yield. It runs every ordering of
  five two-thread scenarios on one page with up to N forced switches, each
  ordering in a fresh process. At N = 3 that is 56,114 orderings, and all of
  them keep the properties: no check fails, no entitled release is refused,
  nothing deadlocks, and every page is given back. Four deliberately broken
  protocols are each caught at N = 1. The limits: two threads, one page,
  sequentially consistent ordering. It is a test, exhaustive only within
  that bound.
- **Entry and exit checks** in the direct tests (89 checks), 50 runs each
  under Release, TSan and ASan+UBSan. **Exit tests** cover thread exit and a
  return from `main` while workers still run.
- **Behavioural declarations and a fuzzer.** The allocator's calls,
  refusals and identity rule are declared once
  (`src/state/stx_minmallocpp/allocator.ic.toml`), and a block fuzzer drives
  them (see [Constraints](reliability-constraints)).

% Not on main yet: the page-protocol proof below is on STXlib's claude/ic-full branch (d044e38).

**The page protocol, proved relative to premises.** STXlib's deducer
(`tools/ic_deduce.py`) discharges this statement: for any number of threads,
pages and blocks, and any interleaving, the page protocol never reaches an
integrity stop, never refuses a held block's release, and keeps its
invariant. The invariant is that the held, free and remote bits partition
each word's blocks, the summary covers every non-zero free word, and the
counts agree. The proof assumes these premises:

- the C++ memory model's coherence;
- each segment between step marks is atomic (the step model);
- sequentially consistent operations are linearizable;
- the queue and abandoned-list contracts;
- a caller releases only a block it holds, and only once;
- only a page's owner writes its free words, summary and live count, and
  other threads write only a remote word, by `fetch_or`;
- the listed steps are every change to the invariant's words and counters.

The last premise is checked by matching the writes the tool finds against
the steps it cites. The others are assumptions.

**Speed.** Measured only on the M4, with `tests/memory_bench.cpp` through
`stxlib::memory`, best of 7, three runs: 0.13 to 0.95 of the host `malloc`'s
time on every workload. Not measured on Linux, FreeBSD or Windows (STXlib
`proofs/REPORT.md`).
