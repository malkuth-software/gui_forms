# Focus labs: handoff

Branch `claude/perf-focus-labs`, cut from main at 66a7f2e5b (merge of #480).
It adds only this directory. No project source is changed.

## What this is

Three measured studies, each a standalone program that takes the project's
own code as the baseline, checks a rearranged version for equal output, and
times both. The rearranged versions are prototypes: they show what to do and
prove it gives the same bytes. They are not the change itself.

| lab | baseline | result on the Windows machine, idle | same output |
|---|---|---|---|
| `composite_lab.cpp` | `src/core/text/composite/text_composite.cpp`, compiled in | 2.3 to 12 times unclipped; 20 to 46 times under a rounded clip; 3.4 to 5.1 times scaled; 3.9 to 5 times for glyph coverage | 40,000 randomised blits, byte for byte |
| `bake_lab.cpp` | `engine_primitives::bake_material`, linked | 3 times at 120x28, 10 at 400x300, 25 at 1000x700 | no row differs |
| `exec_lab.cpp` | `gui_drawing::render::SkiaExecutor`, linked | opaque 1:1 image draw 18 times; batched rectangles 4.7 times; batched lines 1.5 times | same bytes; no pixel differs |

Method, figures and the reasoning are in `PERFORMANCE_FOCUS_STUDIES.md`
(sections 3 FS1, 7.1, 7.2, 7.3). Triage rows are in
`PERFORMANCE_FOCUS_TRIAGE.md` (FS1, FS1r, FS10, FS6m).

## What to test on the Mac

The Windows figures are x86-64 without `-march`. The first questions for
arm64 are:

1. **Do the equality checks pass?** They must. Each lab prints its own
   verdict and exits non-zero on a mismatch (`composite_lab`), or prints the
   count of differing rows or pixels (`bake_lab`, `exec_lab`). The arithmetic
   is integer throughout in the compositor, so the bytes cannot depend on the
   processor. The bake prototype calls the project's own region bake and so
   inherits whatever it does.
2. **Does the compositor's flat byte pass vectorise under Apple clang or
   LLVM 22 for arm64?** On x86-64 the gain depends on it: my first attempt,
   with the same arithmetic in a per-pixel loop and 32-bit temporaries, did
   not vectorise and was slower than main on opaque parts. If `blend_bytes`,
   `weight_bytes` and `text_bytes` do not vectorise on arm64, the ratios will
   be far smaller. `-Rpass=loop-vectorize` on those three functions answers
   it.
3. **Are the ratios of the same order?**

## Building

`composite_lab.cpp` needs only the compositor's translation unit:

```text
clang++ -O3 -DNDEBUG -std=c++20 -I include -I src -I src/core/text/composite \
    benchmarks/focus_labs/composite_lab.cpp src/core/text/composite/text_composite.cpp \
    -o composite_lab
```

`bake_lab.cpp` and `exec_lab.cpp` link the built libraries. The simplest way
is two executables in `CMakeLists.txt` beside the existing tests that link
the same things: `bake_lab` as `gui_forms_engine_material_tests` links, and
`exec_lab` as `gui_forms_drawing_skia_tests` links (its include path has
`src/render/skia/executor`). On Windows I linked them by hand against an
existing build tree; `link_h.sh` records that link line.

## What folding it in would mean

**Compositor.** Inside `composite_part_raster` and `composite_text_coverage`:

- compute the interior as the lab's `interior_of` does, including the bands
  of a rounded clip (a pixel inside both rectangles whose centre is outside
  the four corner squares has whole coverage; main today gives a rounded
  clip no interior at all);
- work interior rows in fixed blocks through scratch on the stack: one pass
  prepares the operands and notes whether the block is wholly clear or
  wholly opaque, one flat pass over bytes does the arithmetic;
- leave the edge pixels on the existing per-pixel path;
- for scaled sources, compute each column's sample position once per call
  and sample a block into bytes for the same kernel.

The lab reaches the edges by calling the project's function with the clip
cut at whole pixels. That is a device for proving equality from outside; the
real change does the split inside, as main already does for a rectangular
clip. The lab does not cover the multiply, screen, plus and copy blends.

Two identities carry the exactness, both checked exhaustively by the lab at
start-up: `round(v / 255) == (t + (t >> 8)) >> 8` with `t = v + 128` for
`v` in 0..65025, and `(c * o * 256 + 32640) / 65280 == round(c * o / 255)`
for every pair of bytes.

Separately and smaller: a raster whose maker declares every pixel opaque can
be copied row by row (about 40 GB/s here). That needs one flag recorded at
bake time.

**Material bake.** When the resolved look does not vary across its interior
(no horizontal gradient, image, pattern or conic fill), bake the pixels near
the left and right edges and one interior column, and copy that column's
pixel across each row. The prototype does this from outside with
`bake_material_region`; inside `bake_look` it is a span decision made once
before the row loop. The same holds down the rows for a look that does not
vary vertically; that is not prototyped. The drop shadow and blur are part
filters applied after the look and were left out of the prototype.

**Drawing executor.** Three independent steps:

- an image drawn at its own size, opaque, with no attributes, under a
  translation and a rectangular clip, is a row copy;
- set the canvas state when it differs from the previous command's, not for
  every command (0.7 to 1.3 microseconds per command today);
- draw a target that owns its pixels in place, or without the clone and
  copy-back (it costs twice a borrowed one).

## What these labs are not

- They are not in house style. They use lambdas in the timing harness and
  `__restrict` directly. The kernels themselves have no lambdas, no `auto`
  and no `->`, and choose their mode before the loop, so they translate
  directly.
- They are not wired into CMake or CTest.
- The timings were taken on one machine (AMD EPYC 9354, 8 logical
  processors, clang 22.1.8). Nothing here has been run on arm64.

## Guards for the real change

The existing byte-for-byte compositor comparison and the display trace
digests for the compositor; baked raster bytes before and after for the
bake; a byte comparison per command kind for the executor.
