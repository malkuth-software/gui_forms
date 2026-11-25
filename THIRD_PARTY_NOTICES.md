# Third-party notices

GUI.Forms is MIT-licensed (`LICENSE`). Its program packages and its
installed SDK also carry, or statically embed, the third-party components
below. Each is listed with its version or pinned revision, the licence
GUI.Forms uses it under, the platforms it ships on, and where its licence
text is: under `licenses/` in a program package (on macOS,
`Contents/Resources/licenses/`), and under `share/licenses/GUIForms/` in the
installed SDK. Fonts carry their licences beside them in `fonts/`.

This list was audited from the build on 2026-10-21: the link lines of the
packaged programs, `packaging/licenses.json`, the CMake install rules, the
fonts' `ATTRIBUTION.md`, the Unicode data provenance and the provenance
records of the vendored sources. The Windows rows were checked again on
2026-11-24, after the switch to STXlib's own runtime, from the link commands
`tools/own_runtime.py evidence` reads: every Windows link names libc++
22.1.8 (`libc++.lib`, `libc++experimental.lib`), STXlib's startup and C++
runtime, STXlib's import libraries and GUID data (`ucrtbase.lib`,
`kernel32.lib`, `uuid.lib` and the others) and GUI.Forms' and stx's own
archives, and nothing else. gui_forms_devkit's `programs/tools/package_programs.py` checks every
program package against this file (see the end of this file).

## Components

| Component | Version or revision | Licence used | Ships on | Package licence file | SDK licence file |
|---|---|---|---|---|---|
| GUI.Forms | this release | MIT | all | `licenses/GUIForms.txt` | `LICENSE` |
| stx_zlib (the owned zlib codec) | in this repository | MIT; the zlib License notices of the puff-derived inflate and the zlib-informed deflate are retained | all | `licenses/stx_zlib.txt`, `licenses/stx_zlib-puff.txt` and `licenses/stx_zlib-zlib.txt` | `stx_zlib-LICENSE.txt`, `stx_zlib-LICENSE-puff.txt` and `stx_zlib-LICENSE-zlib.txt` (and `share/licenses/stx_zlib/`) |
| stx_png (the owned PNG codec) | in this repository | MIT | all | `licenses/stx_png.txt` | `stx_png-LICENSE.txt` (and `share/licenses/stx_png/`) |
| stx_conv (the owner's CONV resampler) | in this repository | MIT | all | `licenses/stx_conv.txt` | `stx_conv-LICENSE.txt` (and `share/licenses/stx_conv/`) |
| stx_raster3d (the 3D plot renderer, from conv-teapot) | in this repository | MIT | all | `licenses/stx_raster3d.txt` | `stx_raster3d-LICENSE.txt` (and `share/licenses/stx_raster3d/`) |
| stx_rtf2md (the owner's RTF and markdown converter) | in this repository | MIT | all | `licenses/stx_rtf2md.txt` | `stx_rtf2md-LICENSE.txt` (and `share/licenses/stx_rtf2md/`) |
| STXlib (the boundary library: the row height index, the SIMD level, the worker threads and the atomic pool; its state is a static archive in each program) | malkuth-software/stxlib, at stx's `stxlib.lock` commit (in the stx package, at `stx.lock`'s commit) | MIT | all | `licenses/STXlib.txt` | `stxlib-LICENSE.txt` (and `share/licenses/stxlib/`) |
| stx_btree (the row height index's sequence tree, in STXlib) | in STXlib (`src/algorithms/stx_btree/`), at stx's `stxlib.lock` commit | MIT; a C++ form of bbtree by Xiangpeng Hao (MIT OR Apache-2.0, MIT taken), whose notice is retained | all | `licenses/stx_btree.txt` and `licenses/stx_btree-NOTICE.txt` | `stx_btree-LICENSE.txt` and `stx_btree-NOTICE.txt` |
| stx_minmallocpp (STXlib's allocator, in its state archive) | in STXlib (`src/state/stx_minmallocpp/`), at stx's `stxlib.lock` commit | MIT (STXlib's own code, under STXlib's licence); derived from mimalloc (MIT), whose notice its NOTICE retains | all (compiled into STXlib's static state archive, which each program and the C ABI library link once; sanitizer builds use the host allocator) | `licenses/stx_minmallocpp-NOTICE.txt` | `stx_minmallocpp-NOTICE.txt` |
| tz database (STXlib's zone data) | in STXlib (`third_party/tzdata/`) | public domain (its LICENSE) | all | `licenses/tzdata.txt` | `tzdata-LICENSE.txt` |
| Unicode Character Database (STXlib's case-folding and text layout tables) | Unicode 18.0.0 `CaseFolding.txt`, and the bidi, line-break and script data stxlib::textlayout reads, in STXlib (`third_party/unicode-18.0.0/`), generated into stxlib_algorithms | Unicode License v3 | all | `licenses/stxlib-Unicode.txt` | `stxlib-unicode-LICENSE.txt` |
| stxlib::glyphs (the glyph face the text rasterizers draw with, in STXlib) | in STXlib (`src/algorithms/glyphs/`), at stx's `stxlib.lock` commit | MIT (STXlib's own code); a conversion of the designs of ttf-parser and Skrifa/read-fonts (MIT OR Apache-2.0, MIT taken) and part of the FreeType Project's auto-hinter (FTL), whose notices its NOTICE retains | all | `licenses/stxlib-glyphs-NOTICE.txt` | `stxlib-glyphs-NOTICE.txt` |
| stxlib::markdown (in STXlib; carries MD4C's licence) | in STXlib (`src/algorithms/markdown/`), at stx's `stxlib.lock` commit | MIT; its NOTICE carries MD4C's licence (MIT) | all | `licenses/stxlib-markdown-NOTICE.txt` | `stxlib-markdown-NOTICE.txt` |
| HTML Standard named character references (stxlib::markdown's entity table) | WHATWG `entities.json`, in STXlib (`third_party/whatwg-entities/`), generated into stxlib_algorithms | CC-BY-4.0 (the HTML Standard; its LICENSE carries the attribution) | all | `licenses/whatwg-entities.txt` | `whatwg-entities-LICENSE.txt` |
| Heroicons (the stock icons) | 2.2.0, `ca7b62e`, converted into the library at build time (`assets/icons/heroicons/PROVENANCE.md`) | MIT | all | `licenses/Heroicons.txt` | `Heroicons-LICENSE.txt` and `Heroicons-PROVENANCE.md` |
| Unicode Character Database (grapheme tables) | Unicode 17.0.0 | Unicode License v3 | all | `licenses/Unicode.txt` | `Unicode-LICENSE.txt` |
| Skia (in stx_skia) | `chrome/m152`, `2a9b593b`, pruned and converted in stx's `stx_skia/` | BSD-3-Clause | all | `licenses/Skia.txt` | `Skia-LICENSE.txt` |
| stx_skia (the CPU renderer derived from Skia) | in the stx repository, at `stx.lock`'s commit | MIT; Skia's BSD-3-Clause notices are retained | all | `licenses/stx_skia.txt` | `stx_skia-LICENSE.txt` |
| LLVM libc++ and libc++abi | 22.1.8 | Apache-2.0 WITH LLVM-exception | macOS (bundled dylibs), Linux (statically linked) and Windows (libc++ only, statically linked; STXlib's runtime takes libc++abi's place) | `licenses/LLVM/libcxx-LICENSE.TXT`; macOS and Linux also `licenses/LLVM/libcxxabi-LICENSE.TXT` | `libc++-LICENSE.txt` (Windows); `libcxx-LICENSE.txt` and `libcxxabi-LICENSE.txt` (Linux) |
| LLVM libunwind | 22.1.8 | Apache-2.0 WITH LLVM-exception | macOS (bundled dylib) and Linux (statically linked); Windows unwinds with the system's table-based exception handling | `licenses/LLVM/libunwind-LICENSE.TXT` | `libunwind-LICENSE.txt` (Linux) |
| miniaudio | `f40cf03f` | MIT No Attribution (its alternative 2) | SDK only (`GUIForms::Audio`) | not in a program | `miniaudio-LICENSE.txt` |
| stx_vorbis (the owner's Vorbis decoder) | in this repository | MIT | SDK only (`GUIForms::Audio`) | not in a program | `stx_vorbis-LICENSE.txt` |
| BFFT (the owner's transform library, inside stx_vorbis) | falseywinchnet/bfft `0f75ca79` | MIT | SDK only (`GUIForms::Audio`) | not in a program | `bfft-LICENSE.txt` |
| Portsmouth and Portsmouth Rapids fonts (the owner's) | 1.0 | MIT | all | `fonts/LICENSE-PortsmouthRapids.txt` | font directory |
| Carlito font | `3a810cab` | SIL OFL 1.1 | all | `fonts/OFL-Carlito.txt` | font directory |
| Cousine font | `c0fbdb43` | SIL OFL 1.1 | all | `fonts/OFL-Cousine.txt` | font directory |
| Arimo font | 1.33, `4a6255f2` | SIL OFL 1.1 | all (the SDK and the GUI.Drawing raster module, not program packages) | `fonts/OFL-Arimo.txt` | font directory |
| Tinos font | 1.340, `3b4482a9` | SIL OFL 1.1 | all (the SDK and the GUI.Drawing raster module, not program packages) | `fonts/OFL-Tinos.txt` | font directory |
| Selawik font | 1.01, release archive SHA-256 `3f62c51e` | SIL OFL 1.1 | all (program packages since themes may name it; the windows-professional theme does) | `fonts/OFL-Selawik.txt` | font directory |
| Noto Sans CJK JP, KR and SC subsets (cut from Noto Sans CJK JP) | `f8d15753`, `assets/fonts/language-font-provenance.json` | SIL OFL 1.1 | all | `fonts/OFL-NotoSansCJKjp.txt` | font directory |
| Noto Emoji font | 1.05, `9a5261d8` | SIL OFL 1.1 | all | `fonts/OFL-NotoEmoji.txt` | font directory |
| Noto Sans Hebrew font; Noto Sans Arabic, Devanagari, Bengali and Gurmukhi minimal subsets | `assets/fonts/language-font-provenance.json` | SIL OFL 1.1 | all | `fonts/OFL-Noto.txt` | font directory |

Font provenance (commits and byte hashes) is in `fonts/ATTRIBUTION.md`.

## Not shipped

- System libraries a program loads from the operating system and that
  GUI.Forms does not distribute: on Linux, the C and C++ runtimes, X11 and
  the ATK/AT-SPI bridge (LGPL 2.1 or later, dynamically linked); on macOS,
  the system frameworks; on Windows, the system DLLs, the C runtime
  `ucrtbase.dll` among them.
- STXlib's Windows C library headers, Windows API headers, startup, C++
  runtime and import libraries, which the Windows binaries are built and
  linked with, are STXlib's own code under its MIT licence (the STXlib row);
  its Windows API headers are generated from the windows-rs metadata
  (MIT OR Apache-2.0, MIT taken), whose notice STXlib's source carries
  (`src/hosted/windows/api/NOTICE`). No compiler-rt
  builtin is linked: the build needs none on x86_64.
- The MinGW-w64 build (CLANG64), which links winpthreads and the MinGW-w64
  runtime, is no longer packaged or released; GUI.Forms 1.0 ships the
  own-runtime build only.

## The packaging check

gui_forms_devkit's `programs/tools/package_programs.py` fails a program package when:

- this file is missing from its `licenses/`, or lacks the FreeType credit
  above;
- a licence file in the package (under `licenses/`, or a font licence in
  `fonts/`) is not named in this file, or a `packaging/licenses.json` entry
  names a component this table lacks;
- any file in the package carries a GNU General Public License text (its
  title in capitals, as every GPL, LGPL and AGPL text begins), or a file
  name contains "GPL".
