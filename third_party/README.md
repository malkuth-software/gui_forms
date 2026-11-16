# Third-party sources

This repository carries no third-party sources (owner, 2026-11-05). They are
kept in the stx repository (malkuth-software/stx) at the commit `stx.lock`
names. `cmake/Stx.cmake` builds them once, at that commit, into the stx
package (`cmake/stx_package/`), which the build links; it fetches the stx
source only when the package must be built:

| stx path | What | GUI.Forms uses it for |
|---|---|---|
| `third_party/freetype/` | FreeType 2.14.2 at `f4205da1` | glyph loading and rasterisation, with the options header in `cmake/freetype/` |
| `third_party/unicode/` | the Unicode 17 licence and the provenance of GUI.Forms' generated grapheme tables | the tables' attribution |
| `stx_skia/` | the CPU Skia derivative (Skia `chrome/m152`, pruned and converted) | the Skia adapters, linked from the stx package |

HarfBuzz, libunibreak, MD4C and SheenBidi left on 2026-11-16: GUI.Forms
shapes with STXlib's shaper, reads Markdown with stxlib::markdown and takes
line breaks and bidi from stxlib::textlayout (STXlib, in the stx package).

Each stx directory keeps its licence, its PROVENANCE file and its intake
record (stx `third_party/README.md`), unchanged from when they were here. The
moved files were blob-for-blob equal to this repository's at 06a78004.

What stays here:

- `fetch_showcase_fallback_fonts.sh`, which downloads the pinned upstream
  Noto files the bundled fallback subsets are cut from (into
  `build/fallback-font-sources/`, for `tools/fallback_font_subsets.py`), and
  Noto Emoji and the Noto licences into `assets/fonts/`;
- `SKIA_BUILD_EVIDENCE.md`, GUI.Forms' record of how its Skia builds were made;
- miniaudio, which `cmake/Audio.cmake` fetches at its pin into
  `third_party/miniaudio/` (ignored), for audio builds.

No Skia type may leave `src/render/skia/`. Linux accessibility dynamically
links the system ATK/AT-SPI bridge (LGPL 2.1 or later); it does not use GTK
widgets.
