# Third-party sources

This repository carries no third-party sources (owner, 2026-11-05). They are
kept in the stx repository (malkuth-software/stx) at the commit `stx.lock`
names, and `cmake/Stx.cmake` fetches that commit in every configuration and
hands its `third_party/` to the build as `GUI_FORMS_STX_THIRD_PARTY_DIR`:

| stx path | What | GUI.Forms uses it for |
|---|---|---|
| `third_party/harfbuzz/` | HarfBuzz 14.6.0 at `c7a7457b`, without `test/` and `perf/` | shaping (`GUI_FORMS_ENABLE_HARFBUZZ_TEXT`) |
| `third_party/freetype/` | FreeType 2.14.2 at `f4205da1` | glyph loading and rasterisation, with the options header in `cmake/freetype/` |
| `third_party/libunibreak/` | libunibreak 8.0 at `28a2756b` | UAX #14 line breaking in the text engine (`cmake/LineBreak.cmake`) |
| `third_party/md4c/` | MD4C 0.6.0 | the markdown framework's CommonMark parser |
| `third_party/sheenbidi/` | SheenBidi 2.9.0 | bidirectional ordering in the text adapter |
| `third_party/unicode/` | the Unicode 17 licence and the provenance of GUI.Forms' generated grapheme tables | the tables' attribution |
| `stx_skia/` | the CPU Skia derivative (Skia `chrome/m152`, pruned and converted) | the Skia adapters, built by this build with its flags (`add_subdirectory`) |

Each stx directory keeps its licence, its PROVENANCE file and its intake
record (stx `third_party/README.md`), unchanged from when they were here. The
moved files were blob-for-blob equal to this repository's at 06a78004.

What stays here:

- `fetch_showcase_fallback_fonts.sh`, which refreshes the Noto fallback fonts
  bundled in `assets/fonts/` from their pinned upstream files;
- `SKIA_BUILD_EVIDENCE.md`, GUI.Forms' record of how its Skia builds were made;
- miniaudio, which `cmake/Audio.cmake` fetches at its pin into
  `third_party/miniaudio/` (ignored), for audio builds.

No Skia type may leave `src/render/skia/`. Linux accessibility dynamically
links the system ATK/AT-SPI bridge (LGPL 2.1 or later); it does not use GTK
widgets.
