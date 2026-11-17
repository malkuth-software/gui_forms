# stx_fontpack

`stx_fontpack` packs a set of font files into one compressed container and
loads them back, byte for byte, into memory a program owns. It is the
first-party package of the font pack (stage 3 of the font pack plan). The
format and every measurement come from the experiment at
`notes:experiments/font-pack` (5bf77f9): on the 37 faces of GUI.Forms'
`assets/fonts`, the pack is 42.5% of the font bytes.

It depends only on `stx_zlib` (for CRC-32) and
follows the layout and rules of the stx packages (`stx_png`): it builds and
installs on its own, includes no GUI.Forms header, and carries no
third-party types in its headers. It is written in this repository beside
the stx package until it moves to `malkuth-software/stx` unchanged.

## Surface

- `stx_fontpack/runtime.hpp`: the two capabilities a caller lends.
  - `Allocator`: every byte the loader holds comes from it and goes back to
    it, with the size it was taken with. `standard_allocator()` wraps
    `malloc`.
  - `Executor`: a fork-join `run_workers` over at most `kMaxWorkers` workers.
    The package starts no threads; `serial_executor()` runs every worker in
    turn on the calling thread.
- `stx_fontpack/loader.hpp`: the loader (`stx_fontpack::stx_fontpack`).
  - `pack_open` checks the container against the limits in
    `stx_fontpack/limits.hpp` and fills a fixed-capacity index. It allocates
    nothing.
  - `pack_load_group` decodes one group into storage it takes from the
    allocator. It checks every font against its stored CRC-32, and a refusal
    says why (`GroupRefusal`, `refusal_name`).
  - `LoadedSet` decodes the eager groups when it opens and a lazy group the
    first time one of its fonts is asked for. A refused group is never
    retried.
  - A loaded font's bytes can go straight to `FT_New_Memory_Face`.
- `stx_fontpack/packer.hpp`: the build-time packer (`stx_fontpack::packer`).
  - It reads a group manifest (`group <name> eager|lazy`, then file-name
    patterns).
  - Packing is deterministic: the same inputs give the same pack whatever the
    worker count.
  - It refuses to write a pack its loader would refuse.
- `stx_fontpack` (target `stx_fontpack::tool`): `pack`, `verify`, `unpack`
  and `embed` from the command line. `pack` loads its own output and compares
  every font with its input before it writes anything. `embed` writes a pack
  as the body of a C++ array initializer, for a program that compiles its
  pack in.
- `stx_fontpack_bench` (built with the tests, `tools/fontpack_bench.cpp`):
  times `pack_load_group` per group and `set_open` for the eager groups, on
  `serial_executor()` or worker threads. `tools/bench_loader.py` runs it as
  the experiment was measured (the median of 25 rounds per group, and 20
  fresh processes for the eager groups) and records the machine, compiler
  and flags.
- `stx_fontpack_objects`: the loader's objects, for a consumer that compiles
  them into its own static library rather than linking the loader (GUI.Forms
  Core, which embeds its pack).

The container layout and stream grammar are described in
`src/detail/format.hpp`, and the codec in `src/detail/codec.hpp`.

## Bounds

The limits are checked before any allocation:

- At most 16 groups and 256 fonts per pack.
- A name is at most 128 bytes.
- A font is at most 64 MiB.
- A group holds at most 256 MiB of fonts, which must be exactly the sum its
  fonts declare.
- A group's decoded streams are at most 4 times its font bytes, and exactly
  what its streams declare.
- A group's font bytes are at most 64 times its coded size.

So a damaged or hostile pack can make the loader allocate no more than the
format allows for the bytes actually present. The tests change each claim
and check that the loader refuses it before asking for font storage.

## Build, test and install

```text
cmake -S stx_zlib -B build/stx_zlib -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build/stx_zlib && cmake --install build/stx_zlib
cmake -S stx_fontpack -B build/stx_fontpack -DCMAKE_PREFIX_PATH=<prefix> -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build/stx_fontpack
ctest --test-dir build/stx_fontpack --output-on-failure
cmake --install build/stx_fontpack
cmake -S stx_fontpack/tests/installed -B build/stx_fontpack_consumer -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build/stx_fontpack_consumer && ctest --test-dir build/stx_fontpack_consumer
```

`stx_fontpack_tests` runs the round trip, claims, damage and per-group decode
tests on synthetic inputs. With `-DSTX_FONTPACK_TEST_FONT_DIR=<dir>`, a second
test (`stx_fontpack_font_tests`) also packs every `.ttf` and `.otf` in that
directory; GUI.Forms passes `assets/fonts`. Every test allocation goes through
a checking allocator. It refuses a release of the wrong size and requires
every load, loaded or refused, to give everything back. The damage test is
meant to run under the sanitizers as well.

## Not covered

The experiment's own limits still apply:

- Only the fonts in `assets/fonts` have been packed.
- CFF2, variable fonts and font collections are stored verbatim, untransformed.
- The packer holds every input in memory.
