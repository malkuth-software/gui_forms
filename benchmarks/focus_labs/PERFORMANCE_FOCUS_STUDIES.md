# Performance focus studies, 2026-11-11

Audit only. Nothing in any project tree was changed, and nothing here is
assigned: every item is an option for the owner.

## 1. What this is based on

- **Code read:** `origin/main` 66a7f2e5b (merge of #480, checkpoint cp27),
  extracted to `D:\gui_forms_perf_lab\tree`. The earlier audit read ae189ff4b;
  main has since gained about 48,800 lines (plots, final controls, menu and
  help systems, splash baking, the SDR present path).
- **Measurements:** small standalone programs in `D:\gui_forms_perf_lab\lab`
  (raw notes in `WORKING_NOTES.md` there). Each compiles the project's own
  source file as the baseline, with the project's Release flags
  (`-O3 -DNDEBUG -std=c++20`, clang 22.1.8, no `-march`), and compares a
  rearranged version against it for equal output and for time. The heat-map
  lab links the built libraries of `D:\gui_forms_h` (c3f1824a9), read only,
  so its Skia figures are the real raster's.
- **Machine:** AMD EPYC 9354, 8 logical processors, at 100% load from other
  builds for the whole session (6 ninja, about 20 clang++). Absolute times
  are therefore high. Ratios are same-process, best of 5 to 15 repeats, and
  came out the same when repeated at the end of the session.

**Second session, same day.** The machine was then idle (7% load, no other
builds). Every lab was rerun and several were extended. Section 7 has the
idle figures and the new results; where a figure there differs from one in
section 3, section 7 is the one to use. The ratios changed little.

Every candidate here is single-threaded and does less work than what it
replaces. Nothing gains by spending more processor.

## 2. Why the DDC is fast, apart from the instructions it uses

Per block of 32 input samples the DDC does 32 multiplies in the mixer and 512
multiply-adds in the filter, and almost nothing else. Nine things make that
possible. Each is stated with its counterpart in GUI.Forms.

1. **Bookkeeping is paid per block, never per element.** The wrap test, the
   phase update, the pointer advances and the length countdown happen once
   per 32 samples. Inside the block there is no test, no modulo and no call.
   *Counterpart:* a pixel loop that tests the blend mode, the clip and the
   source alpha for every pixel pays bookkeeping per element.
2. **History is a window sliding over slack, not a ring and not a shift.**
   The queue is 32 times longer than the window. The filter always reads one
   contiguous stretch; the 32 kept samples are copied back to the start once
   every 62 blocks. *Counterpart:* a scrolling waterfall, a ring of samples,
   a scrolled canvas.
3. **Tables are longer than their period.** The rotation table holds 64
   entries for a period of at most 32, so any starting phase yields 32
   contiguous entries. *Counterpart:* a colour pattern repeated across one
   block, so the pixel loop reads it flat.
4. **The producer writes the layout the consumer wants.** The mixer has to
   store its output anyway, so it stores it in the order that turns the
   filter's final reduction into one operation. The reordering costs nothing
   in the loop: it lives in tables built at set-up. *Counterpart:* a bake
   that records "every pixel opaque" so the compositor copies rows; a heat
   map that produces pixels, not rectangles.
5. **The shape is fixed before the loop.** Tap count, block size and outputs
   per block are constants, so the compiler unrolls everything and no
   remainder is handled in the kernel. The caller supplies whole blocks.
   *Counterpart:* a lane count or blend mode chosen by a run-time test inside
   the pixel loop (house style section 3 already forbids it).
6. **Each datum is loaded once and shared.** 31 loads serve 16 outputs.
   *Counterpart:* a layout computed once per change, not once per query; an
   ancestor chain walked once per pass, not once per node.
7. **Independent chains overlap.** Sixteen accumulators advance together, so
   the latency of one is hidden behind the others. *Counterpart:* the lanes
   of a blur, and rows processed side by side.
8. **The working set stays in the first-level cache.** The queue is 16 KB.
   Every sample is touched while hot. *Counterpart:* three whole-image sweeps
   of a blur against three passes over one row held in cache.
9. **It was measured down to a named residual, against an exact reference.**
   The plain-C path stays as the oracle, and the author knows what the
   remaining cost is. *Counterpart:* the byte-for-byte compositor test, and
   section 4 below, where two of my own statements were wrong.

Two properties follow from these and matter more than any one of them: the
state between calls is two integers and a buffer, and the inner loop contains
no decision at all.

## 3. Focus studies

### FS1. The pixel compositor (measured, same bytes)

`src/core/text/composite/text_composite.cpp`. Every glyph run, cached part,
baked material and paint group reaches the surface through
`composite_text_coverage` (:183-246) and `composite_part_raster` (:460-556).

Main has, since my last read, split unscaled part rasters into edge and
interior spans and copies opaque pixels as words (:435-456). What remains per
pixel:

- `blend_part_pixel` (:330-365) tests the blend mode again for each pixel
  and computes four weighted channels with a 64-bit multiply each, although
  at full opacity the weight leaves the channel unchanged;
- `composite_text_coverage` (:224-244) computes the clip's coverage for
  every covered pixel, including under a rectangular clip where it is always
  a whole unit;
- neither loop can be vectorised by the compiler, because of the 64-bit
  arithmetic and the early exits.

The lab version keeps the arithmetic and changes the flow. Edges go through
the project's function unchanged. The interior is worked in blocks of 64
pixels: one pass prepares each block's operands in scratch on the stack and
notes whether the block is wholly clear or wholly opaque; one flat pass over
bytes does the arithmetic in 16-bit lanes with no decision inside. Two
identities make that exact, both proved exhaustively in the lab:

- `round(v / 255) == (t + (t >> 8)) >> 8` with `t = v + 128`, for every
  `v` in 0..65025;
- `(c * o * 256 + 32640) / 65280 == round(c * o / 255)` for every pair of
  bytes.

12,000 randomised blits (fractional and whole-pixel clips, both target kinds,
every opacity, sources hanging off every edge) gave identical bytes.

| case | main, ns/pixel | lab, ns/pixel | ratio |
|---|---|---|---|
| part 800x600 translucent, opacity 255 | 14.7 | 1.06 | 13.8 |
| part 800x600 translucent, opacity 180 | 14.2 | 1.39 | 10.2 |
| part 800x600 opaque | 1.21 | 0.49 | 2.5 |
| part 800x600 opaque, declared opaque by its maker | 1.23 | 0.115 | 10.7 |
| part 800x600 opaque, opacity 180 | 14.6 | 1.41 | 10.4 |
| part 120x28 button-like, opacity 255 | 3.57 | 1.22 | 2.9 |
| part 120x28 button-like, opacity 180 | 14.4 | 1.59 | 9.1 |
| part 24x24, opacity 255 | 6.82 | 2.03 | 3.4 |
| text 600x400 glyph-like, colour alpha 255 | 3.24 | 0.65 | 5.0 |
| text 600x400 glyph-like, colour alpha 160 | 3.24 | 0.90 | 3.6 |
| text 90x16 label | 4.05 | 1.04 | 3.9 |

Two things to note. Anything drawn at an opacity below 1 (fades, disabled
states, animated transitions) costs main 14 ns per pixel whatever it
contains: a 1920x1080 fade is about 30 ms per frame on this machine, against
about 3 ms. And a raster whose maker declares it wholly opaque copies at
about 35 GB/s; that needs one flag recorded at bake time (principle 4).

My first attempt kept the same arithmetic in a per-pixel loop with 32-bit
temporaries. It did not vectorise and was *slower* than main on opaque parts
(6.4 against 1.3 ns per pixel). The gain is from the block structure.

Not covered: rounded clips (every pixel still takes the coverage
arithmetic), the multiply, screen, plus and copy blends, and the scaled path
(:528-554), which computes `sample_position` with a 64-bit division per pixel
per axis and bounds-tests sixteen texel reads per pixel. The same method
applies (positions per column computed once per call); I did not measure it.

*Guard:* the existing byte-for-byte compositor comparison and identity
hashes. Integer arithmetic only, so every host still produces the same bytes.

### FS2. Heat maps and waterfalls (measured)

A heat map goes through four stages
(`src/plots/mark_painters/mark_painters.cpp:121-155`):

1. a colour per cell (`sample_colormap_span`,
   `src/plots/colormap/colormap.cpp:170-183`);
2. a rectangle per cell, two `to_device` calls each;
3. the recorder copies both arrays (`recording_painter.cpp:360-364`);
4. replay calls `Painter::fill_rects_each`
   (`src/core/types/painter/painter_marks.cpp:166-173`), which is one virtual
   `fill_rect`, and so one Skia `drawRect`, per cell. The Skia raster keeps
   that base implementation (`skia_raster.hpp:141-143`).

| step | main | candidate | ratio | same result |
|---|---|---|---|---|
| 256 x 128 cells onto 1024x512 | 11.3 ms (345 ns per cell) | rows of words: 0.09 ms | 129 | same bytes |
| 1024 x 256 cells onto 1024x512 | 89.3 ms (340 ns per cell) | rows of words: 0.52 ms | 170 | same bytes |
| colour, viridis, per value | 57.7 ns | map prepared once: 19.2 ns | 3.0 | all 262,144 equal |
| colour, default (linear light), per value | 97.2 ns | prepared once, guessed encode: 24.0 ns | 4.1 | all 262,144 equal |
| colour, either, per value | 58-97 ns | 4096-entry table: 2.4-2.7 ns | 24-36 | no: at most 1 level |

A 1024 x 256 heat map therefore costs about 105 ms per frame on this machine
(colours plus rectangles), nearly all of it in 262,144 separate draw calls.
The values in the lab were random, the worst case for main's per-value stop
search and binary search; real spectra are smoother and its colour cost will
be lower.

`sample_colormap_span` re-validates the map for every value (:148 calls
:135-145), searches the stops linearly (:157), calls `std::round` four times,
and in linear light runs a binary search of 255 thresholds three times per
value (:126-133). None of that depends on the value's index.

For a live waterfall the right flow is the DDC's queue (principle 2): a row
store twice the visible height, the new row colourised once on arrival and
written above the window, the window slid by one row. Measured against moving
the image down each frame:

| size | move the image | sliding window | ratio |
|---|---|---|---|
| 1024x512 | 116 us per frame | 0.25 us per frame | 464 |
| 2048x1024 | 254 us per frame | 0.47 us per frame | 538 |

The window is always one contiguous top-down raster, which is exactly what
`present_dib_band` (`src/host/windows/raster/dib_band_present.hpp:33-51`)
hands to GDI. There is no such control or mark on main today; `paint_heatmap`
is the only route, and it repaints every cell every frame.

*Options, smallest first:* (a) the raster overrides `fill_rects_each` and
writes rows of words when the cells are opaque, on whole pixels, under a
scale-and-translate matrix and a rectangular clip (same bytes; falls back
otherwise); (b) prepare the colour map once per call (same colours); (c) a
colour table (changes colours by at most one level: the owner's decision);
(d) a waterfall mark or control built on a sliding row store (new surface
area: a design decision).

### FS3. The live sample path (measured; one hypothesis refuted)

`src/plots/series_ring_channel/series_ring_channel.cpp` and
`include/gui_forms/plots/series_column/series_column.hpp`. A sample is copied
three times: producer to ring (`write`, :42-50), ring to scratch (`consume`,
:67-76), scratch to column (`push_overwrite`, column :379-381). The first two
compute `sequence % capacity_` per sample; the third calls `slot()` per
element in three separate loops.

The lab kept the channel's sequence protocol and the column's bookkeeping,
and resolved the wrap once per block (at most two contiguous runs). Final
column contents, gap counts and dropped counts were identical in every case.

| case (ns per value) | write: main | runs, atomics kept | runs, memcpy | consume + column: main | runs, atomics kept |
|---|---|---|---|---|---|
| 65536 x 1 stream, block 4096 | 2.51 | 1.91 (x1.3) | 0.25 (x10) | 5.63 | 1.28 (x4.4) |
| 65536 x 2 streams, block 4096 | 1.58 | 1.90 (x0.8) | 0.25 (x6.3) | 4.41 | 1.26 (x3.5) |
| 65536 x 4 streams, block 4096 | 1.17 | 1.86 (x0.6) | 0.28 (x4.2) | 3.00 | 1.43 (x2.1) |
| 4096 x 2 streams, block 256 | 1.70 | 2.07 (x0.8) | 0.23 (x7.4) | 4.68 | 1.29 (x3.6) |

**The writer result is negative.** I expected the per-sample modulo to
dominate. On this processor it does not: with the element-wise
`std::atomic_ref` store kept, resolving the wrap once per block gains nothing.
The cap is the atomic store itself, which the compiler will neither merge nor
vectorise: 4 to 7 GB/s. A plain block copy runs at about 30 GB/s but is a
formal data race under the present protocol, so I do not propose it as is.

The reader side gains 2.1 to 4.4 times from runs alone, atomics kept.

4 to 7 GB/s is far above any sample rate a plot will see, so the writer is
not a problem today. If it ever is, the DDC's answer is ownership, not a
faster copy: the producer fills a block it alone owns and publishes the
block, so nothing is read while written, a plain copy is legal, and the
torn-sample logic disappears (house style section 8: one execution context
per piece of mutable state).

### FS4. The three-box blur (measured, same values)

`src/core/engine/primitives/kernel/box_blur.cpp`, used for shadows (one
lane), material blur and glass (four lanes). This is the engine's own kernel,
not `stx_conv`.

- `BoxBlurWork` holds a 32-bit word per lane, 16 bytes per pixel at four
  lanes, for values that never exceed 255. `prepare_box_blur` (:156-161)
  zero-fills two such images per bake.
- `blur_three_boxes` (:163-184) makes six sweeps of the whole image.
- `across_rows` (:55-79) loops `for lane < lanes` with a run-time lane count
  inside the pixel loop.

The lab does the three passes across per row, through two byte rows of
scratch that stay in cache (principle 8), holds bytes between passes, and
fixes the lane count at compile time (principle 5). Same values throughout
the result region.

| case | main | lab | ratio |
|---|---|---|---|
| shadow 600x200, 1 lane, half 8 | 1.10 ms | 0.56 ms | 2.0 |
| material blur 800x600, 4 lanes, half 12 | 19.9 ms | 9.7 ms | 2.0 |
| glass backplane 1920x1080, 4 lanes, half 16 | 92.6 ms | 43.2 ms | 2.1 |
| small shadow 160x60, 1 lane, half 4 | 0.08 ms | 0.04 ms | 2.2 |

A full-HD glass blur at 43 ms is still slow. The remaining cost is one 64-bit
multiply per element per pass (24 per pixel); I did not go further.

### FS5. Markers (measured, not the same result)

`SkiaRaster::draw_markers` (`skia_raster.cpp:2446-2470`) does save,
translate, two `drawPath` and restore per marker: the outline is
scan-converted twice per marker. The drawing executor's batch
(`skia_executor.cpp:590-617`) also builds a new offset path per marker per
pass.

| case | main | one marker rasterised once and stamped | ratio | difference |
|---|---|---|---|---|
| 20,000 circles of 6 px | 90 ms (4.5 us each) | 16 ms | 6 | rims differ by up to 4 levels |
| 20,000 circles of 10 px | 134 ms (6.7 us each) | 24 ms | 6 | rims differ by up to 8 levels |

The stamp went through main's compositor, not the lab's faster one. It could
only ever be exact for centres on one sub-pixel phase (which is what the
occupancy grid's thinning produces), and even there Skia's own blend rounds
differently from the shared compositor. So this changes pixels and is the
owner's decision.

### FS6. The drawing executor and the native surface (read, not measured)

This is the path SDR#'s drawing takes.

- `skia_executor.cpp:1173-1188`: every command does `restoreAll`, `save`,
  `resetMatrix`, `clipRect`, `setMatrix`, a new `SkPaint` and
  `configure_paint`, whether or not the state changed from the command
  before. Principle 1: bookkeeping per element.
- `skia_executor.cpp:1270-1281`: each `draw_image` copies the whole image
  into a vector (`remapped_pixels`, :109-111, even with no remap table) and
  then again into `SkData::MakeWithCopy`. Two full copies of the source per
  draw. A program that draws its waterfall bitmap every frame pays both.
- `skia_executor.cpp:1150-1153, :1330-1338`: a target that owns its pixels
  is cloned, drawn, snapshotted and copied back: two full-surface copies per
  flush. (A borrowed DIB is drawn in place; that was fixed in dac28e039.)
- `DrawingCommand`
  (`include/gui_forms/drawing/graphics_recorder/graphics_recorder.hpp:66-94`)
  carries state, pen, font, format, path, image, attributes, brush, two
  vectors and a string in every command.
- `src/abi/drawing_platform_windows.cpp:363-369`: `force_opaque` rewrites
  the alpha of the whole section on every capture and refresh.
- `:408-449` (first capture): a zero-filled vector, two `memcpy` and an alpha
  pass, where one pass that copies and sets alpha would do.
- `:516-527` (refresh): `memcpy` of a row, then a second pass over the same
  row to set alpha.
- `:552-558`: `GetModuleHandleW` and `GetProcAddress` by name on every
  publish.
- `:643-720`: every HDC lease creates a DIB section and a DC, copies the
  bitmap in, makes a marking pass, a resolving pass, and copies it out.
- `:43-49, :166-177`: three rounded divisions by 255 per pixel in
  `export_hbitmap` (the exact shift form in FS1 applies).

### FS7. The display list (size and recording cost measured)

`src/core/display/command/display_command.hpp:100-137`. A `DisplayCommand` is
**320 bytes**: six shared pointers, a string, a vector and a font spec in
every command, `save` and `restore` included. `recording_painter.cpp` never
reserves `commands_`.

| | ns per command |
|---|---|
| record, walk and destroy, as main | 111 |
| the same with `reserve` | 32 |
| a 32-byte record in a reused store | 1.6 |

Only re-recorded chunks pay this, so it matters when many controls repaint
(resize, theme change, scrolling a large list). Replay
(`replay_display_chunk.cpp:224-229`) is one virtual call per command into
Skia, which dominates; the record's size mostly costs cache.

*Options:* (a) reserve from the previous recording's count: one line, 3.4
times; (b) split the record into a small fixed part and side tables for
text, rasters and marks: a larger change that touches every reader.

### FS8. Build cost (measured under load)

| measurement | result |
|---|---|
| `clang++ --version` | 3.1 to 6.9 s |
| empty file, `-fsyntax-only` | 2.8 to 3.6 s |
| six standard headers | 8.9 to 12.8 s |
| `gui_forms/types.hpp` | 13.3 s |
| `gui_forms/control.hpp` | 19.6 s |
| `gui_forms/gui_forms.hpp` | 41 to 42 s (161,697 preprocessed lines, 609 headers) |
| `tests/paint_marks_raster_tests.cpp` (273 lines), no precompiled header | 35 to 53 s; front end 98.6%, back end 0.7 s |
| the same test's static link (10.1 MB) | 4.8 to 5.3 s |

Three separate things show in these numbers.

1. **Oversubscription.** Six ninja instances and about twenty compilers were
   running on eight logical processors. Each ninja assumes it owns the
   machine. Starting the compiler took 3 to 7 seconds before it read a byte.
   Everything else in the table is inflated by the same factor. Nothing
   coordinates job counts across worktrees.
2. **The front end is the whole compile.** The traced test spent 55.2 of
   56.0 seconds parsing: 968 classes, 3,394 class instantiations, 3,063
   function instantiations, 848 constraint checks, to compile 273 lines. The
   umbrella header costs about four times the standard headers alone, and
   167 of 336 test sources include it. The costliest single items were
   `Theme` and `ThemeRule` (the implicit copy of `ThemeRule`, instantiated
   through `std::vector<ThemeRule>`, is itself among the largest).
3. **471 executables linked statically**, about 10 MB and 5 seconds each;
   one build tree is 3.6 GB.

Main does now have `cmake/TestPrecompiledHeader.cmake`. My earlier S9 said
there was no precompiled header; that is out of date. The precompiled header
addresses item 2 for the tests that share a group; it does not address 1 or
3, and a change to any public header still invalidates it.

*Options:* (a) one job pool for the machine (a jobserver or a fixed share of
jobs that every worktree's build takes), so six builds do not each run eight
jobs: the largest factor and the cheapest change; (b) tests include the
headers they use, not the umbrella; (c) move the heavy value types' special
members out of line (`Theme`, `ThemeRule`, the layout workspaces), so
including them does not instantiate their copies; (d) fewer, larger test
executables per area, or tests linked against shared libraries, to cut link
time and disk; (e) check whether antivirus real-time scanning covers the
toolchain and build directories (it is on; I could not read the exclusion
list without administrator rights).

### FS9. Smaller items

- `Control::publish_change` (`control.cpp:3315-3334`) takes a
  `std::function` built by each caller. Measured in isolation: 59 ns against
  5 ns for a named function and context. Small in a frame; it adds up when a
  form sets thousands of properties.
- The plot engine measures axis and legend text on every paint
  (`plot_engine.cpp:296, :576, :716, :758`), each a full shaping call, then
  shapes again to draw. A live plot pays that every frame. Same family as
  N56.
- The glyph run rasteriser (`glyph_run_raster.cpp:172-288`) sorts, copies an
  outline per distinct glyph, renders each occurrence, appends bitmap rows
  one `insert` at a time, and combines with a division per pixel that is the
  identity wherever glyph boxes do not overlap. It is cached per retained
  command, so it is paid when text, scale or phase changes.

### FS10. The material bake (read, not measured)

`src/core/engine/primitives/material/material_primitive.cpp`. `bake_look`
(:2069-2082) calls `material_pixel` (:1174-1266) for every pixel of a part.
This runs when a part is first shown and whenever its size, state or scale
changes, so it sets how smooth a resize or a theme change is.

What is already right: an outer shadow's integrals are computed once per
column and once per row (`fill_columns`, `fill_row`, :914-944), not per
pixel. That is principle 6.

What is decided per pixel that does not vary per pixel:

- every fill's kind, in a `switch` (`fill_colour`, :984-1043), and every
  layer list's length, for each pixel: the loops run pixel-outer,
  layer-inner. The DDC runs tap-outer, output-inner, so each step is one
  uniform operation over a row;
- whether the pixel is in the box's interior (`box_coverage`, :1165-1172,
  two rectangle tests), although `wide_interior` and `tall_interior` are
  known as rectangles before the loop and could bound a span;
- `stop_colour` (:970-982) searches the stops and premultiplies both
  neighbouring stops for each pixel, although in the interior the coverage is
  1 and the stops' premultiplied colours are constants;
- a vertical linear gradient's position depends only on the row, and a
  horizontal one's only on the column, yet both are evaluated at every pixel;
- `nearest_edge` (:1238) is found per pixel whenever rings exist.

All arithmetic is in doubles (`Unit` is four doubles), converted to bytes at
`store`. I did not build a measurement: there is no bake benchmark among the
built programs, and constructing materials by hand would have taken the
session. The first step would be a benchmark over the stock themes' materials
at a few sizes; the bake budget in `material_bake_policy.hpp` already counts
pixels and would give the workload.

## 4. Corrections to what I said before

1. **S1 said "about eight integer divisions per pixel".** The compiler
   already turns division by a constant into a multiply, so there are no
   division instructions in those loops. The cost is the 64-bit multiplies,
   the per-pixel decisions and the fact that the loop cannot be vectorised.
   The remedy I listed (tables, exact shift forms) was not sufficient by
   itself: see FS1, where the first attempt was slower.
2. **FS3:** the per-sample modulo is not the writer's cost on this machine.
3. **S9:** main has a test precompiled header.
4. **S1's interior and edge split** is already on main for unscaled part
   rasters.

## 5. What I did not read

You asked me to read the code that remains first. I did not finish that. I
put the session into the tight loops and the measurements, and the read of
the remainder is still owed:

- the theme material and motion lowering (about 4,600 lines) and the theme
  compiler beyond rule lowering;
- the form model, generator and editor;
- the dialogs, and the navigation pane and combo box beyond their layout and
  paint paths;
- the last 170 lines of the macOS draw function;
- most of the 48,800 lines added to main since ae189ff4b: I read the plots
  library's kernels, the ring channel, the colour map, the mark painters, the
  new raster and executor paths, the Windows drawing platform and the band
  present, and outlined the plot engine; I did not read the final controls,
  the menu and help systems, splash baking, or the hosts' changes;
- the tests, `tools/` and most of `docs/`.

## 6. Options for the owner

**a. Compositor (FS1).** Same bytes, 2.5 to 14 times, on the path every
frame takes. The larger part is the block kernel; the opaque flag on a
raster is a separate, smaller step.

**b. Heat map through the raster (FS2 a and b).** Same bytes and same
colours, about 130 to 170 times on the draw and 3 to 4 times on the colours.

**c. Waterfall store (FS2 d).** New surface area; needs a design.

**d. Blur (FS4).** Same values, 2 times.

**e. Sample path, reader side (FS3).** Same contents, 2 to 4 times. Low
urgency.

**f. Drawing executor and native surface (FS6).** Not measured. The two
image copies per draw and the per-command state reset are the first things I
would measure, with SDR# as the workload.

**g. Build (FS8).** Option (a), one job pool across worktrees, costs the
least and addresses the largest factor.

**h. Things that change pixels (colour table, stamped markers).** Only on
your decision.

**i. Finish the read (section 5).**

My recommendation is g first, because it gives every other piece of work its
time back; then a and b, which are the two measured, same-result changes with
the largest effect; then f as a measurement before any change.

(Section 7 revises this list after the second session.)

## 7. Second session: idle machine, further experiments

### 7.1 Compositor (FS1), idle, extended to rounded clips and scaled sources

40,000 randomised blits identical, byte for byte, now including rounded
clips and scaled sources.

| case | main, ns/pixel | lab, ns/pixel | ratio |
|---|---|---|---|
| part 800x600 translucent, opacity 255 | 8.29 | 0.70 | 11.8 |
| part 800x600 translucent, opacity 180 | 8.29 | 0.96 | 8.7 |
| part 800x600 opaque | 0.72 | 0.31 | 2.3 |
| part 800x600 opaque, declared opaque | 0.71 | 0.096 | 7.4 |
| part 120x28 button-like, opacity 255 | 2.01 | 0.82 | 2.5 |
| part 120x28 button-like, opacity 180 | 8.15 | 1.07 | 7.6 |
| part 24x24 | 2.59 | 1.18 | 2.2 |
| **part 800x600 opaque, rounded clip, radius 12** | **16.2** | **0.35** | **46** |
| **part 800x600 translucent, rounded clip** | **16.1** | **0.74** | **22** |
| **part 120x28 button-like, rounded clip, radius 6** | **15.8** | **0.81** | **20** |
| part 400x300 shown at 800x600 (scaled) | 25.2 | 5.23 | 4.8 |
| part 800x600 shown at 600x450 (scaled) | 24.4 | 4.77 | 5.1 |
| part 60x14 shown at 120x28 (scaled) | 23.4 | 6.82 | 3.4 |
| text 600x400 glyph-like, colour alpha 255 | 3.49 | 0.78 | 4.5 |
| text 600x400 glyph-like, colour alpha 160 | 3.05 | 0.61 | 5.0 |
| text 90x16 label | 2.81 | 0.71 | 3.9 |

**The rounded clip is the largest single finding.** Under a rounded clip
main has no interior path at all (`text_composite.cpp:505-514`: "a rounded
clip leaves no interior: every pixel takes it"), so every pixel of anything
drawn inside a rounded control costs 16 ns, opaque or not: twenty times an
unclipped opaque part. But a pixel's coverage under a rounded clip is exactly
one unit whenever it lies inside both rectangles and its centre is outside
the four corner squares (:95-113), which is nearly all of them. The lab
treats the middle band of rows as whole-width interior and the top and
bottom bands as interior between the corner squares; only the corner squares
and fractional edges take the per-pixel path.

Scaled sources: the positions each column samples are computed once per
call; a sampler writes a block of premultiplied bytes; the same blend kernel
consumes it (principle 4).

### 7.2 Material bake (FS10), now measured, with a same-bytes prototype

Baseline, the project's `bake_material`, nanoseconds per pixel:

| material | 120x28 | 400x300 | 1000x700 | 1000x700 total |
|---|---|---|---|---|
| solid opaque fill | 22 | 18 | 17 | 12.5 ms |
| + border, radius 4 | 41 | 48 | 50 | 36.5 ms |
| vertical gradient + border | 45 | 56 | 58 | 42.2 ms |
| + drop shadow, blur 8 | 53 | 62 | 63 | 46.0 ms |
| + inset shadow | 66 | 76 | 78 | 57.0 ms |

A plain opaque rectangle costs 17 to 22 ns per pixel; writing those pixels
is under 0.1 ns each. In every one of these materials, every row is a single
repeated pixel from 16 pixels inside the box's left edge to 16 pixels inside
its right edge.

The prototype uses only the project's own `bake_material_region`: it bakes
the two side strips and one middle column, and copies the middle pixel
across each row. Not one row differed from the whole bake.

| material | 120x28 | 400x300 | 1000x700 |
|---|---|---|---|
| solid opaque fill | 2.6 | 7.5 | 17.8 (21.0 to 1.18 ms) |
| + border, radius 4 | 3.0 | 10.5 | 25.4 (49.9 to 1.96 ms) |
| vertical gradient + border | 3.0 | 10.8 | 24.9 (54.8 to 2.20 ms) |
| + inset shadow | 3.8 | 9.9 | 24.2 (76.6 to 3.17 ms) |

The cost goes from the part's area to its perimeter. The condition (the look
does not vary across its interior: no horizontal gradient, image, pattern or
conic fill) is a fact of the resolved look, decided once before the loop.
The same holds down the rows for a look that does not vary vertically; I did
not prototype that. The drop shadow is a part filter the region bake does
not draw, so it was left out of the prototype.

### 7.3 Drawing executor (FS6), now measured

| workload | main | less work | ratio | same result |
|---|---|---|---|---|
| waterfall frame: opaque 1024x400 image drawn 1:1, target owns its pixels | 1.27 ms | | | |
| the same, target borrows its pixels | 0.65 ms | rows copied: 0.035 ms | 18 | same bytes |
| 2,000 lines, one `draw_line` command each | 8.08 ms (4.0 us each) | one `draw_line_segments`: 5.50 ms | 1.5 | no pixel differs |
| 2,000 rectangles, one `fill_rectangle` each | 1.83 ms (914 ns each) | one `fill_rectangles`: 0.39 ms (194 ns each) | 4.7 | same bytes |
| the same rectangles | 194 ns each batched | words written directly: 18 ns each | 11 | same bytes |

The per-command cost is 0.7 to 1.3 microseconds whatever the command draws
(`skia_executor.cpp:1173-1188`). An image drawn at its own size, opaque,
with no attributes is a row copy; today it is two copies of the source and a
general image draw. A target that owns its pixels costs twice one that
borrows them.

### 7.4 Text through the real raster (new)

| call | cost |
|---|---|
| `measure_text_utf8("Mg")` | 1.55 us |
| `measure_text_utf8("Save")` | 1.77 us |
| `measure_text_utf8("Document properties")` | 3.50 us |
| `measure_text_utf8` of 72 characters | 9.59 us |
| the same answers from a memo keyed by text and font | 12 to 64 ns (117 to 151 times) |
| `draw_text_utf8("Save")`, unretained | 28.7 us |
| `draw_text_utf8("Document properties")`, unretained | 88.9 us |
| `draw_text_utf8` of 72 characters, unretained | 265.7 us |
| the same three, retained (coverage kept) | 1.4, 5.6 and 17.8 us |

Three corrections follow. S4 (line height from shaping "Mg") is 1.5
microseconds a call: minor unless it sits in a per-item loop. Unretained
drawing is about 3.7 microseconds per glyph, all of it FreeType rendering
each occurrence again; anything that draws text without a retained cache
(turned text, `replay_display_chunk.cpp:149-151`) pays it every frame. And a
retained draw's cost is the compositor's: FS1 cuts it by about four.

### 7.5 Build (FS8), idle

| measurement | idle | under six builds |
|---|---|---|
| `clang++ --version` | 0.14 s | 3.1 to 6.9 s |
| six standard headers | 0.8 s | 8.9 to 12.8 s |
| `gui_forms/control.hpp` | 2.4 to 3.1 s | 19.6 s |
| `gui_forms/gui_forms.hpp` | 5.9 to 9.2 s | 41 to 42 s |
| the 273-line test, no precompiled header | 9.4 s (front end 9.0 s) | 35 to 53 s |
| the same with an umbrella precompiled header | 1.2 to 1.3 s | |
| making that precompiled header | 9.9 s, 111 MB | |

One parse of the umbrella header peaks at 712 MB. The machine has 16 GB and
8 logical processors. 48 such compiles:

| jobs at once | wall time |
|---|---|
| 4 | 91.0 s |
| 8 | 64.7 s |
| 16 | 64.6 s |
| 48 | 205.2 s |

Six builds of eight jobs each is the last row: 48 times 712 MB is 34 GB
against 16 GB of memory, and the machine does a third of the work it could.
This is less work for the same result, not more processor: the same 48
compiles finish in a third of the time when no more than eight run at once.

Within one compile, constraint checking takes 3.2 of the 9.0 seconds
(848 checks), class parsing 7.5 and class instantiation 3.1 (these overlap).
By header, inclusive: collection controls 1.8 s, basic controls 1.8 s,
composition controls 1.5 s, `control.hpp` 1.5 s, container controls 1.5 s,
animation 1.25 s, theme 1.2 s.

### 7.6 The other labs, idle

| study | main | candidate | ratio |
|---|---|---|---|
| heat map 1024 x 256 cells | 48.2 ms (184 ns per cell) | 0.29 ms | 166 |
| heat map 256 x 128 cells | 6.09 ms | 0.05 ms | 113 |
| colour, viridis | 36.2 ns | 15.9 ns | 2.3 |
| colour, linear light | 75.8 ns | 19.7 ns | 3.8 |
| waterfall 1024x512, per frame | 37.4 us | 0.18 us | 212 |
| waterfall 2048x1024, per frame | 154.5 us | 0.40 us | 385 |
| blur, shadow 600x200 | 0.64 ms | 0.38 ms | 1.7 |
| blur, 800x600, 4 lanes | 16.1 ms | 6.2 ms | 2.6 |
| blur, 1920x1080, 4 lanes | 76.8 ms | 29.2 ms | 2.6 |
| sample path, reader, 1 stream | 3.04 ns | 0.67 ns | 4.5 |
| sample path, reader, 2 streams | 2.46 ns | 0.73 ns | 3.4 |
| sample path, writer, 2 streams | 0.89 ns | 1.63 ns | 0.6 (worse) |
| display command, record and destroy | 62.5 ns | 23.6 with reserve; 1.06 compact | 2.6; 59 |
| change notification | 33.5 ns | 4.0 ns | 8 |
| 20,000 markers, 6 px | 49.5 ms | 8.5 ms (pixels differ) | 6 |

The ring channel's writer as it stands is faster than my rearrangement of
it. That confirms the negative result: leave it.

A second negative result, on the blur. In section 3 I said its remaining
cost was the 64-bit multiply per element. I replaced it with an exact 32-bit
form (proved equal for every box half from 1 to 1024 and every window sum).
The time did not change: 13.1 ns per pixel before and after. So the multiply
is not what remains, and I do not know what is; the 2.6 times stands, the
explanation I gave for the rest does not.

### 7.7 Options, revised

**a. Compositor, including rounded clips (7.1).** Same bytes. 2 to 12 times
unclipped, 20 to 46 times under a rounded clip, 3 to 5 times scaled. This is
now clearly first among the pixel paths.

**b. Material bake by strips (7.2).** Same bytes. 3 times on a button, 10 on
a panel, 25 on a large one. It decides how a resize and a theme change feel.

**c. Heat map through the raster, and colour map prepared once (FS2).** Same
bytes and colours. 113 to 166 times and 2.3 to 3.8 times.

**d. Drawing executor (7.3).** An opaque 1:1 image draw as a row copy (18
times, same bytes); state set only when it changes between commands (the
per-command 0.7 to 1.3 microseconds); owned targets drawn without the clone
and copy-back.

**e. One job pool across builds (7.5).** A third of the wall time for the
same compiles on this machine.

**f. A measure memo (7.4).** The same sizes, 117 to 151 times per repeated
call. This is N56 with a number on it. A global shaping cache is a recorded
decision against; a memo of sizes only, keyed by text and font, is smaller
than that and may or may not fall under it. Your call.

**g. Blur (FS4).** Same values, 1.7 to 2.6 times.

**h. Sample path reader (FS3), display command reserve (FS7).** Small, same
result.

**i. Waterfall store.** New surface area; needs a design.

**j. Things that change pixels (colour table, stamped markers).** Only on
your decision.

**k. Finish the read (section 5).** Still owed.

My recommendation: e, because it costs nothing in the code and returns time
to everything else; then a and b together, since between them they cover
what a frame composites and what a resize bakes; then d, if SDR# is the
workload you care about most.
