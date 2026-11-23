# Memory and the allocator

Memory errors are the classic way for a long-running program to fail
badly: not at the faulty line, but much later and somewhere else, after
the error has quietly corrupted something another part of the program
relies on. GUI.Forms attacks them from two sides. Its own code gives every
datum an owner and a lifetime and checks the rules mechanically. And
STXlib, underneath, has its own allocator, built so that a memory error in
the program cannot corrupt the allocator, and so that the allocator's own
mistakes, if it had any, would stop the process instead of spreading.

## The allocator

### The usual approach, and how it fails

General-purpose allocators are tuned first for speed and memory use, and
most of them keep part of their bookkeeping inside the memory they manage.

- **glibc's malloc** keeps a size header beside every block, and threads
  its free lists through the freed blocks themselves. Its per-thread cache
  detects a second free by a key written into the freed block, which it
  then confirms by a scan; the check is a heuristic, and a key copied into
  application data can match by chance
  ([the glibc patch that hardened it](https://sourceware.org/pipermail/libc-alpha/2021-July/128617.html)).
  Its behaviour can be changed from outside the program through tunables
  ([glibc manual](https://sourceware.org/glibc/manual/latest/html_node/Memory-Allocation-Tunables.html)).
- **mimalloc** threads its free lists through free blocks too. Its secure
  modes encode those pointers, validate freed addresses, add guard pages
  and pad blocks to catch overflows and double frees; in its minimal
  secure mode the free-list entries are not encoded, and its README notes
  they can be corrupted by a buffer overflow
  ([mimalloc](https://github.com/microsoft/mimalloc)). Options are read
  from `MIMALLOC_*` environment variables.
- **jemalloc** catches double frees with run-time assertions in debug
  builds; its scan of the thread cache for a repeated pointer has no
  effect unless the library was built with `--enable-debug`. It is
  configured at run time through `MALLOC_CONF` and `/etc/malloc.conf`
  ([jemalloc manual](https://jemalloc.net/jemalloc.3.html)).

How this fails in practice: when the program writes past the end of a
block, or writes to a block it has freed, it overwrites the allocator's
own data. Nothing happens at the faulty line. Many allocations later the
allocator follows a corrupted link, hands out the same memory twice, or
crashes inside `malloc` in an unrelated thread. A double free that the
check misses turns into two owners for one block. The report, if there is
one, points at the allocator, not at the bug. And because behaviour can
be changed through the environment, the same binary does not necessarily
run the same way on two machines.

### What STXlib does instead: stx_minmallocpp

stx_minmallocpp is STXlib's own allocator (STXlib
`src/state/stx_minmallocpp/`), the default in every STXlib build except
sanitizer builds, and required by GUI.Forms' high-integrity configuration.
It is inspired by and derived from mimalloc v3, from which it takes its
architecture: thread heaps that allocate without a lock, pages that hold
blocks of one size, the separation of what a page's owner frees from what
other threads free, the rule that a page is owned only while its owner
will use it again, address space reserved in large regions and cut into
slices, and an address map consulted without touching the address. Around
that architecture it was rebuilt for a different priority, integrity
first, and gained the features below. No mimalloc source text is used.

**1. The allocator never reads or writes a block's memory, live or
free.** Every block's state is two bits held outside the pages: *free*
(the owner may hand it out) and *remote* (another thread released it and
the owner has not yet merged it). Both clear means live. All metadata sits
at the start of each region, behind slices that are never committed. A
use-after-free or an overflow in your code can damage your own data, but
it cannot rewrite a free list, because there is none in the blocks to
rewrite.

**2. Every release is checked, in every build.** A release looks up the
address in the directory and the page's slice table, computes the block
index with one multiplication, and checks that the address is exactly the
first byte of a block and that its bit says live. An address that is not
the start of a live block (a double release, a pointer into the middle of
a block, a foreign pointer, an address inside the metadata) is refused
with a result. The refusal reads and writes nothing at or near the address
itself; that is deduced from the source, relative to the premise that the
metadata tables are not the block or its neighbours. Aligned allocation
needs no second mechanism (there is a proof that the class of the size
rounded up to the alignment is always aligned), so there are no interior
pointers to confuse the check.

**3. Contradictions stop the process with a reason.** Some double releases
can only be seen when two threads' releases meet. When the allocator finds
its own bits contradicting each other, it records which contradiction it
found, for a debugger or a core file, and ends the process
(`Integrity` in `page.hpp`): `released_twice` (a free bit and a remote bit
name the same block), `released_into_freed_page` and `live_count` (more
blocks merged than were live). These three are checked in every build. A
fourth, `check`, covers entry and exit conditions in builds with
`STXLIB_STX_MINMALLOCPP_CHECKS`, which the direct tests always use.

**4. Ownership rules that make the protocol provable.** Only a page's
owner writes its free words, its summary and its live count; any other
thread writes only a remote word, with one atomic OR. A full page is given
up at once. A release into an abandoned page claims it only if the
releasing thread is allocating that size class and holds less than 4 MiB
of it, so a thread that only frees owns nothing and takes no lock. Heap
identities are issued once and never reused; heap records are never
unmapped. Each step that touches memory another thread may touch is
marked in the source (`STX_MINMALLOCPP_STEP`), so tools can cut the code
at exactly those points.

**5. Every slow path has a stated bound.** Free runs of slices are filed
in a two-level segregated index (TLSF, implemented from Masmano, Ripoll,
Crespo and Real, ECRTS 2004), so finding a run, splitting it and
coalescing it on release take a fixed number of steps, whatever the
program's history. Memory is decommitted by count, when a unit goes idle
past a limit, not by a timer. The bounds are listed in the source; where a
cost is not constant (the host's own calls, waiting for a lock, a retry
caused by another thread's step on the same page) the source says so.

**6. Nothing outside the program changes it.** No environment variable,
no option, no clock, no entropy, no output, no statistics. Failure to get
memory is a null result at the call and changes nothing else: no `errno`,
no handler, no waiting. Nothing in it can throw or call back, and it calls
no host function that is a cancellation point, so it cannot be unwound or
cancelled halfway through a change. Threads may call it during their own
exit and after their exit hook has run. On Windows, once process exit has
begun (when other threads may have been stopped while holding a lock), it
takes no lock at all.

### Why it holds

The decisive property, that concurrent releases never corrupt a page, is
proved, not just tested. STXlib's deducer (`tools/ic_deduce.py`) discharges
this statement: for any number of threads, pages and blocks and any
interleaving, the page protocol never reaches an integrity stop, never
refuses the release of a held block, and keeps its invariant. The
invariant is that the held, free and remote bits partition each word's
blocks, the summary covers every non-zero free word, and the counts
agree. Each step is decided bit by bit and the counters as finite sums
(the method is credited to an earlier allocator study, sxv). The proof
holds relative to its premises:

- the C++ memory model's coherence;
- each stretch of code between step marks is atomic;
- sequentially consistent operations are linearizable;
- the queue and abandoned-list contracts;
- a caller releases only a block it holds, and only once;
- only a page's owner writes its free words, summary and live count, and
  other threads write only a remote word, by `fetch_or`;
- the take step works on a non-zero word, and takes its lowest bit.

The ownership premise is checked rather than assumed: the deducer matches
every write it finds in the source against the steps the proof covers.
Each deduction records the hashes of its declaration and sources and lapses
when they change.

The proof is backed by tests of the compiled code. A checker turns each
step mark into a scheduler yield and runs every ordering of five
two-thread scenarios on one page, with up to three forced switches, each
ordering in a fresh process: 56,114 orderings, with no failed check, no
wrongly refused release, no deadlock and no page left behind. To show the
checker can see, four deliberately broken protocols were run through it,
and each was caught at one forced switch.

% Not on main yet: the page-protocol and refusal deductions are on STXlib's claude/ic-full branch
% (ea52d1c).

### What it costs

Measured on one machine only (Apple M4, macOS arm64), with
`tests/memory_bench.cpp` through `stxlib::memory`, best of seven, three
runs: between 0.13 and 0.95 of the host `malloc`'s time on every workload,
and nearly flat, about 3.7 to 3.8 ns per allocate or release from 16 bytes
to 64 KiB. Against mimalloc v3.6.1, measured on the same machine while it
was still in the tree: slower by under a nanosecond per call on blocks up
to 256 bytes (3.7 to 3.8 ns against 3.0 to 3.4) than mimalloc without
release checking, level at 1 KiB, and faster from 8 KiB up, in batches of
4 KiB blocks, at 1 MiB and across threads. Against mimalloc with its own
checking on, it was level on the smallest blocks and faster on everything
else. A churn of 100,000 live blocks remains slower than unchecked
mimalloc (16 to 17 ns against about 14). Not measured on Linux, FreeBSD or
Windows (STXlib `proofs/REPORT.md`).

### Limits

- **What it serves.** stx_minmallocpp serves the memory STXlib allocates:
  its containers, text, glyph stores and thread records. GUI.Forms' general
  heap, its `std::` containers and strings, still goes through the C++
  library to the host's `malloc`.
- **Sanitizer builds** use the host allocator, which the sanitizers
  intercept.
- **What is not proved.** The address-identity rule (every address that is
  not a live block's start is refused) is covered by tests, not deduced.
  The free-run index is argued and tested. The proof is of the page
  protocol, relative to the premises above, not of the whole allocator.
- **A stray write still hits something.** The guard slices stop an
  underflow from a region's first page reaching its tables, but a write
  past the end of a region can reach whatever the host mapped next.
- **Unsupported:** `fork` in a multithreaded process (STXlib starts
  children with `posix_spawn`), and addresses above 2^48.

## Ownership and lifetimes in GUI.Forms

GUI.Forms' own code follows the house style's first rule: every datum has
one owner and a lifetime tied to it, ownership moves explicitly and leaves
the former owner empty, and observing links do not own. Rules that can be
checked are checked on every change.

- **A borrow cannot outlive its source.** A function that returns a borrow
  of an argument deletes its rvalue overload, so borrowing from a temporary
  does not compile ({src}`tools/check_lifetime_rules.py`; clang's lifetime
  analysis through {src}`tools/lifetime_lint.py`).
- **Stale views are caught even when the bytes are still there.** In the
  sanitizer tree, each lending container keeps a generation and each
  borrowed view a stamp. A view used after its owner changed shape, which
  AddressSanitizer misses while the old memory is still mapped, ends the
  process with where the view was lent and where the owner last changed
  ({src}`include/gui_forms/borrow/borrowed_view/borrowed_view.hpp`). In an
  ordinary build the same names are plain spans and pointers, at no cost.
- **No unbounded recursion.** Recursion over runtime-shaped data (control
  trees, documents, parsers) is admitted only with a depth limit enforced
  before traversal, and deep structures are torn down iteratively, so a
  deep document cannot overflow the stack.
- **Every `thread_local` is trivially destructible**, so nothing runs at
  thread exit in an order the host chooses.
- **Memory errors are defects in tests too.** The whole suite runs under
  AddressSanitizer and UndefinedBehaviorSanitizer, and any report fails it
  ([How we know](reliability-evidence)).
