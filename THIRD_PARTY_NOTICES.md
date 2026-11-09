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
records of the vendored sources. `tools/package_programs.py` checks every
program package against it (see the end of this file).

## FreeType

FreeType is used under the FreeType License (FTL), which GUI.Forms elects
from FreeType's dual licence (the FTL or the GNU GPL version 2). No GPL
licence text is shipped.

Portions of this software are copyright © 2026 The FreeType Project (www.freetype.org). All rights reserved.

(2026 is the release year of the pinned FreeType 2.14.2, 2026-03-01; its
source files carry "Copyright (C) 1996-2025".)

## Components

| Component | Version or revision | Licence used | Ships on | Package licence file | SDK licence file |
|---|---|---|---|---|---|
| GUI.Forms | this release | MIT | all | `licenses/GUIForms.txt` | `LICENSE` |
| threadpool_atomic_fast (the owner's thread pool) | falseywinchnet/backend `6eb51f84` | MIT | all (in `libgui_forms_threading`) | `licenses/threadpool_atomic_fast.txt` | `threadpool_atomic_fast-LICENSE.txt` |
| HarfBuzz | 14.6.0, `c7a7457b` | HarfBuzz licence ("Old MIT") | all (the `--features all` SDK also installs the static library and headers) | `licenses/HarfBuzz.txt` | `HarfBuzz-COPYING.txt` |
| FreeType | 2.14.2, `f4205da1` | FreeType License (FTL), elected from FTL/GPLv2 | all (the `--features all` SDK also installs the static library and headers) | `licenses/FreeType-FTL.txt` | `FreeType-FTL.txt` |
| SheenBidi | 2.9.0, `829d9991` | Apache-2.0 | all | `licenses/SheenBidi.txt` | `SheenBidi-LICENSE.txt` |
| MD4C (the markdown framework's parser) | 0.6.0, `7fc1815a` | MIT | all | `licenses/MD4C.txt` | `MD4C-LICENSE.txt` |
| stx_zlib (the owned zlib codec) | in this repository | MIT; the zlib License notices of the puff-derived inflate and the zlib-informed deflate are retained | all | `licenses/stx_zlib.txt`, `licenses/stx_zlib-puff.txt` and `licenses/stx_zlib-zlib.txt` | `stx_zlib-LICENSE.txt`, `stx_zlib-LICENSE-puff.txt` and `stx_zlib-LICENSE-zlib.txt` (and `share/licenses/stx_zlib/`) |
| stx_png (the owned PNG codec) | in this repository | MIT | all | `licenses/stx_png.txt` | `stx_png-LICENSE.txt` (and `share/licenses/stx_png/`) |
| stx_conv (the owner's CONV resampler) | in this repository | MIT | all | `licenses/stx_conv.txt` | `stx_conv-LICENSE.txt` (and `share/licenses/stx_conv/`) |
| stx_rtf2md (the owner's RTF and markdown converter) | in this repository | MIT | all | `licenses/stx_rtf2md.txt` | `stx_rtf2md-LICENSE.txt` (and `share/licenses/stx_rtf2md/`) |
| Unicode Character Database (grapheme tables) | Unicode 17.0.0 | Unicode License v3 | all | `licenses/Unicode.txt` | `Unicode-LICENSE.txt` |
| Skia (in stx_skia) | `chrome/m152`, `2a9b593b`, pruned and converted in stx's `stx_skia/` | BSD-3-Clause | all | `licenses/Skia.txt` | `Skia-LICENSE.txt` |
| stx_skia (the CPU renderer derived from Skia) | in the stx repository, at `stx.lock`'s commit | MIT; Skia's BSD-3-Clause notices are retained | all | `licenses/stx_skia.txt` | `stx_skia-LICENSE.txt` |
| LLVM libc++ and libc++abi | 22.1.8 | Apache-2.0 WITH LLVM-exception | macOS (bundled dylibs), Windows (statically linked) | Windows `licenses/LLVM/libc++-LICENSE.TXT`; macOS `licenses/LLVM/libcxx-LICENSE.TXT` and `licenses/LLVM/libcxxabi-LICENSE.TXT` | `libc++-LICENSE.txt` (Windows) |
| LLVM libunwind | 22.1.8 | Apache-2.0 WITH LLVM-exception | macOS (bundled dylib), Windows (statically linked) | `licenses/LLVM/libunwind-LICENSE.TXT` | `libunwind-LICENSE.txt` (Windows) |
| LLVM compiler-rt builtins | 22.1.8 | Apache-2.0 WITH LLVM-exception | Windows (statically linked) | `licenses/LLVM/compiler-rt-LICENSE.TXT` | `compiler-rt-LICENSE.txt` |
| MinGW-w64 winpthreads | 14.0.0 (CLANG64 `r426.g4564ee4b5`) | MIT and BSD-3-Clause (its COPYING) | Windows (statically linked) | `licenses/winpthreads-COPYING.txt` | `winpthreads-COPYING.txt` |
| MinGW-w64 runtime (CRT startup, libmingwex, libuuid GUID data) | 14.0.0 (CLANG64 `r426.g4564ee4b5`) | the MinGW-w64 runtime licence (its COPYING; see the note below) | Windows (statically linked) | `licenses/mingw-w64-runtime-COPYING.txt` | `mingw-w64-runtime-COPYING.txt` |
| miniaudio | `f40cf03f` | MIT No Attribution (its alternative 2) | SDK only (`GUIForms::Audio`) | not in a program | `miniaudio-LICENSE.txt` |
| stx_vorbis (the owner's Vorbis decoder) | in this repository | MIT | SDK only (`GUIForms::Audio`) | not in a program | `stx_vorbis-LICENSE.txt` |
| BFFT (the owner's transform library, inside stx_vorbis) | falseywinchnet/bfft `0f75ca79` | MIT | SDK only (`GUIForms::Audio`) | not in a program | `bfft-LICENSE.txt` |
| Portsmouth and Portsmouth Rapids fonts (the owner's) | 1.0 | MIT | all | `fonts/LICENSE-PortsmouthRapids.txt` | font directory |
| Carlito font | `3a810cab` | SIL OFL 1.1 | all | `fonts/OFL-Carlito.txt` | font directory |
| Cousine font | `c0fbdb43` | SIL OFL 1.1 | all | `fonts/OFL-Cousine.txt` | font directory |
| Arimo font | 1.33, `4a6255f2` | SIL OFL 1.1 | all (the SDK and the GUI.Drawing raster module, not program packages) | `fonts/OFL-Arimo.txt` | font directory |
| Tinos font | 1.340, `3b4482a9` | SIL OFL 1.1 | all (the SDK and the GUI.Drawing raster module, not program packages) | `fonts/OFL-Tinos.txt` | font directory |
| Selawik font | 1.01, release archive SHA-256 `3f62c51e` | SIL OFL 1.1 | all (the SDK and the GUI.Drawing raster module, not program packages) | `fonts/OFL-Selawik.txt` | font directory |
| Noto Sans CJK JP font | `f8d15753` | SIL OFL 1.1 | all | `fonts/OFL-NotoSansCJKjp.txt` | font directory |
| Noto Emoji font | 1.05, `9a5261d8` | SIL OFL 1.1 | all | `fonts/OFL-NotoEmoji.txt` | font directory |
| Noto Sans Arabic, Hebrew, Devanagari, Bengali and Gurmukhi fonts | `assets/fonts/language-font-provenance.json` | SIL OFL 1.1 | all | `fonts/OFL-Noto.txt` | font directory |

Font provenance (commits and byte hashes) is in `fonts/ATTRIBUTION.md`.

## Not shipped

- libunibreak 8.0 (`28a2756b`): linked only by the development text-mask
  profile (`GUI_FORMS_BUILD_TEXT_MASKS`), which is never installed or
  packaged.
- System libraries a program loads from the operating system and that
  GUI.Forms does not distribute: on Linux, the C and C++ runtimes, X11 and
  the ATK/AT-SPI bridge (LGPL 2.1 or later, dynamically linked); on macOS,
  the system frameworks; on Windows, the system DLLs.

## Note on the MinGW-w64 runtime licence

The MinGW-w64 runtime's COPYING also reproduces the LGPL 2.1 notice of
header and IDL files imported from Wine. The Windows build was checked on
2026-10-21 for what of those files reaches a binary:

- Ten such headers are included by GUI.Forms objects (`uiautomation.h`,
  `uiautomationclient.h`, `uiautomationcore.h`, `uiautomationcoreapi.h`,
  `msxml.h`, `wincodec.h`, `dcommon.h`, `dxgiformat.h`, `exdisp.h` and
  `objectarray.h`, the last six generated from Wine IDL files), by 91
  objects, found from the build's header dependencies (`ninja -t deps`).
  `wincodec.h` was the Windows host's WIC PNG decoder, which Z-stage I2
  removed the same day; it is no longer included, so the list now has nine.
- No function from them is compiled into an object: every code symbol the
  91 objects define (`llvm-nm --defined-only`) is GUI.Forms', HarfBuzz's,
  FreeType's, the C++ runtime's or a test's. Besides code, the objects hold
  the compiler's type-information records for the COM interfaces those
  headers declare, and references to interface identifiers.
- The interface identifiers (16-byte GUID constants) come from MinGW-w64's
  `libuuid.a` (`uuid.o`, under the runtime licence above), whose values those
  headers declare. The Windows Gallery links 1,821 of them.

## The packaging check

`tools/package_programs.py` fails a program package when:

- this file is missing from its `licenses/`, or lacks the FreeType credit
  above;
- a licence file in the package (under `licenses/`, or a font licence in
  `fonts/`) is not named in this file, or a `packaging/licenses.json` entry
  names a component this table lacks;
- any file in the package carries a GNU General Public License text (its
  title in capitals, as every GPL, LGPL and AGPL text begins), or a file
  name contains "GPL".
