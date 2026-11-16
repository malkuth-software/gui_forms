# The factory bundle

The factory bundle is the set of fonts GUI.Forms ships inside every
program: eight families in 37 faces, checked before they ship and compiled
into the library. Stock controls, themes and GUI.Forms' own interface (the
window manager, dialogs, menus, tooltips, help) draw only these faces
(`FontContext::factory_bundle`). Your composites and sub-surfaces may load
fonts of their own ([Application font families](app-fonts)), and they may
also use the factory bundle, with the benefits below.

## What is in it

| Family | Faces | Licence | Characters |
|---|---|---|---|
| Portsmouth Rapids | regular, light, italic, bold, bold italic | MIT | 349 |
| Portsmouth | regular, light, italic, bold, bold italic | MIT | 349 |
| Portsmouth Mono | regular (fixed pitch) | MIT | 349 |
| Carlito | four styles; metric-compatible with Calibri | SIL OFL 1.1 | 2,117 |
| Cousine | four styles; metric-compatible with Courier New | SIL OFL 1.1 | 2,282 |
| Selawik | regular, semibold, bold; nearest to Segoe UI | SIL OFL 1.1 | 348 |
| Arimo | four styles; metric-compatible with Arial | SIL OFL 1.1 | 3,010 |
| Tinos | four styles; metric-compatible with Times New Roman | SIL OFL 1.1 | 3,011 |

Portsmouth Rapids is the interface face: titles, buttons, labels and menus
(`FontRole::control`). Carlito is the content face (editable text, lists,
fields) and Cousine the monospace face. Portsmouth, Portsmouth Rapids and
Portsmouth Mono are the GUI.Forms owner's own typeface, licensed under MIT
(`assets/fonts/LICENSE-PortsmouthRapids.txt`); the other families are
unmodified upstream files under the SIL Open Font License 1.1, each with its
licence beside it (`assets/fonts/OFL-*.txt`). The "Characters" column counts
the Unicode characters the family's regular face maps to a glyph.

What the text faces cover:

- **Portsmouth** (all three families): Basic Latin, Latin-1 and Latin
  Extended-A, so Western and Central European languages, plus 12 characters
  of Latin Extended-B and the arrows the controls draw.
- **Carlito, Arimo, Tinos, Cousine**: the Latin blocks above, Latin
  Extended-B and Latin Extended Additional (Vietnamese among others), Greek
  (127 to 135 characters) and Cyrillic (254 to 256). Arimo, Tinos and
  Cousine also cover the Hebrew block (87).
- **Selawik**: Basic Latin, Latin-1 and most of Latin Extended-A.

### Fallback faces

Seven Noto faces draw what the text faces lack. Fallback is chosen per
grapheme cluster, so a Japanese word in a Latin label takes the CJK face for
that word only. Their coverage is generated from the faces themselves
(`src/core/resources/fonts/bundled_font_coverage.inc`).

| Face | Scripts | Characters |
|---|---|---|
| Noto Sans CJK JP | Han (20,976 unified ideographs), Hiragana, Katakana, Hangul (11,172 syllables) | 44,810 |
| Noto Sans Arabic | Arabic | 1,161 |
| Noto Sans Devanagari | Devanagari (Hindi, Marathi, Nepali) | 272 |
| Noto Sans Bengali | Bengali | 173 |
| Noto Sans Gurmukhi | Gurmukhi (Punjabi) | 154 |
| Noto Sans Hebrew | Hebrew | 145 |
| Noto Emoji | Monochrome emoji and symbols | 887 |

Together the regular faces and the fallback faces map 49,532 distinct
characters. The fallback faces are optional: a build configured with
`GUI_FORMS_INSTALL_FALLBACK_FONTS=OFF` leaves them out
([Fallback fonts](../building/index.md#fallback-fonts)). Colour emoji are not
included.

## What it guarantees

- **Checked before it ships.** Every face is pinned by SHA-256 in
  `assets/fonts/bundled-fonts.sha256`, the list the pack is built from, and
  a development build that reads faces from a directory refuses one that
  does not match. The packer loads its own output and compares every face
  with its file before it writes the pack, and
  `gui_forms_font_pack_round_trip` checks the built pack again. At run time
  each face is checked against the CRC-32 the pack stores for it.
- **Never quarantined.** Quarantine is for application fonts that fail to
  open, shape or rasterize. A factory-bundle face cannot be quarantined, so
  stock controls never lose their text partway through a session.
- **The same pixels on every host.** One shaper, one rasterizer and one
  blending rule draw the same faces on macOS, Windows and Linux
  ([Text engine](text-engine)). `gui_forms_text_identity_raster_tests`
  compares a fixed corpus against checked-in hashes on every CI platform.
- **Never host fonts.** Stock controls do not enumerate or load system
  fonts, so a machine's installed fonts cannot change what they draw.
- **Application fonts stay out of stock controls.** A family your program
  loads draws only inside composites and sub-surfaces. Anywhere else it is
  dropped and counted (`app_font_statistics().refused_uses`).

## How small it is

The faces are packed into one font pack and compiled into the library as a
byte array, so a program ships no font files, only the licences.

| Pack group | Loaded | Faces | Font bytes | Packed bytes | Packed share |
|---|---|---:|---:|---:|---:|
| interface (Portsmouth, Carlito) | at first use of any face | 15 | 4,681,212 | 1,104,298 | 23.59% |
| monospace (Cousine) | when text needs it | 4 | 1,210,076 | 334,559 | 27.65% |
| families (Selawik, Arimo, Tinos) | when text needs it | 11 | 4,371,368 | 1,163,901 | 26.63% |
| scripts (Arabic, Hebrew, Devanagari, Bengali, Gurmukhi) | when text needs it | 5 | 739,616 | 236,978 | 32.04% |
| emoji | when text needs it | 1 | 418,804 | 243,364 | 58.11% |
| cjk | when text needs it | 1 | 16,467,736 | 8,819,191 | 53.55% |
| **Whole pack** | | 37 | 27,888,812 | 11,903,673 | 42.68% |

- Without the fallback groups the faces are 10,262,656 bytes, and their
  packed groups 2,602,758 bytes (25.36%).
- The pack's header and padding are 1,382 bytes of the whole pack.
- The licence and attribution files are 10 files, 47,678 bytes.

How these were computed: font bytes are the files in `assets/fonts`,
grouped by `assets/fonts/font-pack.manifest`. Packed bytes are each group's
coded size, read from the header of the pack a macOS arm64 build wrote on
2026-11-16. Packing is deterministic, so every build of the same faces and
manifest writes the same pack. `python3 -B tools/factory_bundle_sizes.py`
prints every figure on this page from the tree. To read a pack you built
yourself, add
`--pack <build>/generated/bundled_fonts/bundled_fonts.fpk`.

## How fast it is

**Unpacking, as first measured.** These figures were measured on
2026-11-13 on a Mac mini M4 (10 hardware threads, clang 22.1.8, `-O2`). The
code was the font pack experiment that `stx_fontpack`, the loader GUI.Forms
ships, was converted from. It has the same format and codec
(`stx_fontpack/PROVENANCE.md`). The workload decodes the 37 faces from pack
bytes in memory to font bytes in memory, including the CRC-32 check. It used
the same groups as above, except that Cousine was in the families group.
Each figure is the median of 25 rounds in one process.

| Decoded | 1 thread | 4 threads | 10 threads |
|---|---:|---:|---:|
| interface group | 20.06 ms | 6.03 ms | 4.19 ms |
| cjk group | 88.69 ms | 27.33 ms | 19.60 ms |
| every group in turn | 139.20 ms | 44.84 ms | 33.46 ms |

In 20 fresh processes, decoding the interface group took 4.5 ms at the
median and 6.7 ms at the slowest. The same run opened all 37 decoded faces
with FreeType and loaded 14,698 sampled glyphs with no failure. A program
pays for the interface group once, at the first request for any face. It
pays for each other group only when text first needs one of that group's
faces, so a program that never shows Japanese never decodes the CJK group.

**Unpacking with the shipped loader.** The same workload run on
`stx_fontpack` itself, with `stx_fontpack/tools/bench_loader.py`. The
1-thread column uses the loader's serial executor. The other columns use
worker threads that start for each step, so a program that lends a running
thread pool pays less.

<!-- Filled from bench_loader.py's loader-timings.md: machine, compiler, flags, date. -->
Measured on: to be recorded.

| Decoded | 1 thread | 4 threads | all threads |
|---|---:|---:|---:|
| interface group | | | |
| monospace group | | | |
| families group | | | |
| scripts group | | | |
| emoji group | | | |
| cjk group | | | |
| eager groups (`set_open`) | | | |
| every group in turn | | | |

| Eager groups, 20 fresh processes | Median | Slowest |
|---|---:|---:|
| 1 thread | | |
| all threads | | |

**Not yet measured.** No unpacking times are recorded yet for Windows or
Linux hosts, or for a window's first text with the faces already decoded.
Those are the next measurements. Until they exist, this page claims no speed
beyond the tables above.

**Glyph start-up, a candidate rasterizer.** STXlib's glyph face is not yet
used by GUI.Forms. On 2026-11-16 it was measured on the same machine beside
FreeType 2.14.2, taking the least of 15 runs:

- Opening a face takes 0.2 to 0.6 µs, against FreeType's 2 to 7 µs.
- From font bytes to the first hinted glyph's mask takes 43 to 68 µs for
  the Latin faces (FreeType: 52 to 92 µs). Noto Sans CJK takes 912 µs
  (FreeType: 388 µs), so CJK is not at parity.
- The first draw of the 95 ASCII characters takes 158 to 279 µs (FreeType:
  151 to 212 µs). Drawing them again at another subpixel position takes 37
  to 65 µs (FreeType: 122 to 219 µs).

## Using it in your own composites and content

Stock controls use the factory bundle without being asked. Your composites
and sub-surfaces choose, and the factory bundle has the following benefits:

- **Consistency.** Your text matches the stock controls around it: the same
  faces, metrics and rendering.
- **Size.** It costs your program nothing more, because the pack is already
  in the library. A font of your own adds its own bytes.
- **No licensing work.** The faces are MIT or SIL OFL 1.1, and the SDK
  installs their licences and attribution in `share/GUIForms/fonts/`
  ([Install](install)). Ship those files with your program and every
  face is covered.
- **Determinism.** Text lays out and draws the same on every host and every
  machine, and these faces are never quarantined.
- **Coverage.** The fallback faces already cover the scripts listed above.
  A face of your own still falls back to them for characters it lacks.

## Choosing it, and how application fonts compare

- **A composite's look** names factory-bundle families in `font-family`, or
  a role (`control`, `content`, `monospace`), as a theme does
  ([The theme language](theme-language)). In code, `FontSpec::family` takes
  a bundled family's fixed number, such as `carlito_font_family`
  (`gui_forms/typography/font_families/font_families.hpp`).
- **Controls in a sub-surface** draw the factory bundle unless your program
  gives them a font of its own.
- **An application family** is loaded with `load_app_font_family`, is
  validated first, and gets a number from 256 up. It draws only in a
  composite's look and in a sub-surface's controls
  (`FontContext::composite` and `FontContext::content`). If the text engine
  cannot use it, it is quarantined for the rest of the process and the
  factory bundle draws instead. Its glyph gaps are filled from the factory
  bundle's fallback faces. See [Application font families](app-fonts).
