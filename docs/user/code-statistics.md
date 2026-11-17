# Code statistics

GUI.Forms is C++20 from its controls down to its codecs and its CPU
renderer. This page counts that code: the C++ (Objective-C++ included) and
the C in this repository, in the stx packages it builds with (stx_zlib,
stx_png, stx_conv, stx_raster3d, stx_rtf2md, stx_vorbis and stx_skia) at the
commit `stx.lock` pins, and in STXlib
at the commit stx's `stxlib.lock` pins.

## What is counted

`tools/code_statistics.py` reads the files git tracks at the HEAD commit of
each checkout, so the same commits always give the same numbers and build
output never enters them. It needs no build and no network.

- **Languages.** C++ is `.cpp .cc .cxx .hpp .hh .inc .ipp .mm` and every
  `.h` that is not C. C is `.c`, and a `.h` whose nearest sources are mostly
  `.c`, or that declares a C interface (it tests `__cplusplus` and uses no
  C++) or sits beside one. The C ABI's headers are C.
- **Lines.** Blank lines hold only white space. Comment lines hold comment
  text and no code. Code lines are the rest, preprocessor lines and the
  contents of string literals included. The scanner follows `//` and
  `/* */` comments, string and character literals with their prefixes and
  escapes, raw strings, digit separators and Objective-C's `@"..."`.
- **Characters** are Unicode code points of each file read as UTF-8;
  **bytes** are the file sizes.
- **Components** are top-level directories, with `src/` split by subsystem
  and `third_party/` by library.
- **Provenance** comes from the repositories' own records:
  - *first-party original*: written for these repositories, including
    the owner's own code wherever it is copied (BFFT inside stx_vorbis and
    the C thread pool threadpool_atomic_fast in STXlib);
  - *first-party converted*: rewritten from an outside code base, as the
    `PROVENANCE.md` records state: stx_skia's `skia/` (from Skia, converted
    to the house style under `stx_skia/house_style_allowance.txt`),
    stx_zlib's inflate (from zlib's puff), stx_raster3d and stx_conv's
    surfaces (from conv-teapot);
  - *third-party*: code from outside parties, copied in and not
    converted. None since 2026-11-16, when FreeType, HarfBuzz,
    libunibreak, MD4C and SheenBidi left stx's `third_party/` for STXlib's
    own font tables, shaper, text layout and Markdown reader; the row
    stays, at zero.
- **The headline figure** is first-party C++, original and converted, over
  all three repositories.

## Counting it yourself

```sh
python3 -B tools/code_statistics.py <stx checkout> <STXlib checkout>
```

The tool prints the tables below; `--markdown FILE` writes them as Markdown
and `--json FILE` writes every count with stable keys. A checkout given as
`NAME=PATH` is reported under `NAME`. The report notes whether the stx
checkout is at the commit `stx.lock` pins (and STXlib at the commit stx's
`stxlib.lock` pins), and names a pinned checkout that was not given.

The `Code statistics` workflow (`.github/workflows/code-statistics.yml`)
runs the tool on every push to `main` and on request, writes the tables to
its job summary and uploads the JSON report as the `code-statistics`
artifact.

## The numbers

Counted by `tools/code_statistics.py` at gui_forms `2f06e9a4bf8c` (2026-11-16), stx `658139f53154` (2026-11-16), stxlib `12354dd18876` (2026-11-16).

| Figure | Files | Lines | Code | Characters |
|---|---:|---:|---:|---:|
| first-party C++ (gui_forms, stx and STXlib) | 4,149 | 1,283,119 | 1,022,945 | 59,432,468 |

**By language and provenance**

| Language | Provenance | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| C++ | first-party original | 3,601 | 1,141,485 | 89,488 | 125,844 | 926,153 | 54,066,869 | 54,070,180 |
| C++ | first-party converted | 548 | 141,634 | 18,702 | 26,140 | 96,792 | 5,365,599 | 5,365,687 |
| C++ | all | 4,149 | 1,283,119 | 108,190 | 151,984 | 1,022,945 | 59,432,468 | 59,435,867 |
| C | first-party original | 68 | 25,299 | 1,629 | 4,891 | 18,779 | 1,350,255 | 1,350,255 |
| C | first-party converted | 2 | 202 | 26 | 80 | 96 | 8,599 | 8,599 |
| C | all | 70 | 25,501 | 1,655 | 4,971 | 18,875 | 1,358,854 | 1,358,854 |
| C++ and C | first-party original | 3,669 | 1,166,784 | 91,117 | 130,735 | 944,932 | 55,417,124 | 55,420,435 |
| C++ and C | first-party converted | 550 | 141,836 | 18,728 | 26,220 | 96,888 | 5,374,198 | 5,374,286 |
| C++ and C | third-party | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C++ and C | all | 4,219 | 1,308,620 | 109,845 | 156,955 | 1,041,820 | 60,791,322 | 60,794,721 |

**By checkout**

| Checkout | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | C++ | 2,733 | 885,110 | 71,891 | 96,121 | 717,098 | 41,775,265 | 41,778,477 |
| gui_forms | C | 51 | 22,799 | 1,394 | 4,463 | 16,942 | 1,257,011 | 1,257,011 |
| stx | C++ | 784 | 197,532 | 21,346 | 28,197 | 147,989 | 8,000,978 | 8,001,054 |
| stx | C | 8 | 841 | 96 | 210 | 535 | 38,975 | 38,975 |
| stxlib | C++ | 632 | 200,477 | 14,953 | 27,666 | 157,858 | 9,656,225 | 9,656,336 |
| stxlib | C | 11 | 1,861 | 165 | 298 | 1,398 | 62,868 | 62,868 |

**By component**

| Checkout | Component | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | benchmarks | C++ | 20 | 6,385 | 438 | 483 | 5,464 | 295,254 | 295,306 |
| gui_forms | demo | C++ | 50 | 10,440 | 1,070 | 761 | 8,609 | 496,793 | 496,970 |
| gui_forms | examples | C++ | 23 | 1,554 | 177 | 181 | 1,196 | 66,421 | 66,421 |
| gui_forms | examples | C | 3 | 121 | 9 | 20 | 92 | 5,055 | 5,055 |
| gui_forms | file_manager_demoboard | C++ | 12 | 6,383 | 419 | 36 | 5,928 | 320,543 | 320,841 |
| gui_forms | fuzz | C++ | 16 | 2,253 | 176 | 209 | 1,868 | 106,105 | 106,105 |
| gui_forms | include | C++ | 724 | 112,372 | 11,456 | 33,954 | 66,962 | 5,347,842 | 5,348,183 |
| gui_forms | include | C | 3 | 7,170 | 391 | 3,565 | 3,214 | 367,629 | 367,629 |
| gui_forms | lab | C++ | 13 | 4,107 | 376 | 242 | 3,489 | 177,016 | 177,024 |
| gui_forms | src/abi | C++ | 74 | 40,768 | 3,732 | 2,690 | 34,346 | 1,828,279 | 1,828,279 |
| gui_forms | src/abi | C | 1 | 57 | 7 | 24 | 26 | 2,407 | 2,407 |
| gui_forms | src/audio | C++ | 5 | 1,640 | 18 | 42 | 1,580 | 79,424 | 79,424 |
| gui_forms | src/controls | C++ | 347 | 159,412 | 14,894 | 11,168 | 133,350 | 7,018,572 | 7,018,951 |
| gui_forms | src/core | C++ | 503 | 178,869 | 12,839 | 18,300 | 147,730 | 8,329,098 | 8,329,777 |
| gui_forms | src/core | C | 2 | 367 | 36 | 17 | 314 | 14,007 | 14,007 |
| gui_forms | src/host | C++ | 190 | 48,159 | 3,246 | 6,668 | 38,245 | 2,169,274 | 2,169,299 |
| gui_forms | src/platform | C++ | 1 | 189 | 26 | 51 | 112 | 8,368 | 8,368 |
| gui_forms | src/plots | C++ | 13 | 3,853 | 326 | 231 | 3,296 | 173,260 | 173,264 |
| gui_forms | src/render | C++ | 46 | 14,710 | 924 | 1,813 | 11,973 | 737,182 | 737,285 |
| gui_forms | src/runtime | C++ | 8 | 1,715 | 125 | 183 | 1,407 | 75,637 | 75,637 |
| gui_forms | stx_fontpack | C++ | 20 | 10,422 | 625 | 858 | 8,939 | 440,043 | 440,043 |
| gui_forms | tests | C++ | 631 | 270,501 | 20,166 | 17,293 | 233,042 | 13,577,948 | 13,579,009 |
| gui_forms | tests | C | 42 | 15,084 | 951 | 837 | 13,296 | 867,913 | 867,913 |
| gui_forms | tools | C++ | 37 | 11,378 | 858 | 958 | 9,562 | 528,206 | 528,291 |
| stx | stx_conv | C++ | 43 | 9,565 | 486 | 1,010 | 8,069 | 470,196 | 470,196 |
| stx | stx_map | C++ | 14 | 4,747 | 206 | 212 | 4,329 | 290,947 | 290,947 |
| stx | stx_png | C++ | 12 | 2,783 | 128 | 227 | 2,428 | 157,528 | 157,528 |
| stx | stx_png | C | 1 | 73 | 1 | 2 | 70 | 2,855 | 2,855 |
| stx | stx_raster3d | C++ | 13 | 3,682 | 277 | 424 | 2,981 | 172,882 | 172,882 |
| stx | stx_rtf2md | C++ | 21 | 9,470 | 571 | 563 | 8,336 | 373,443 | 373,443 |
| stx | stx_skia | C++ | 620 | 155,124 | 19,280 | 25,152 | 110,692 | 5,943,271 | 5,943,347 |
| stx | stx_skia | C | 2 | 202 | 26 | 80 | 96 | 8,599 | 8,599 |
| stx | stx_vorbis | C++ | 39 | 6,430 | 186 | 269 | 5,975 | 320,841 | 320,841 |
| stx | stx_vorbis | C | 5 | 566 | 69 | 128 | 369 | 27,521 | 27,521 |
| stx | stx_zlib | C++ | 16 | 4,479 | 124 | 227 | 4,128 | 211,876 | 211,876 |
| stx | tools | C++ | 6 | 1,252 | 88 | 113 | 1,051 | 59,994 | 59,994 |
| stxlib | appliance | C | 6 | 1,196 | 110 | 223 | 863 | 38,103 | 38,103 |
| stxlib | include | C++ | 76 | 13,652 | 1,557 | 5,667 | 6,428 | 621,303 | 621,317 |
| stxlib | proofs | C++ | 56 | 3,896 | 411 | 1,071 | 2,414 | 199,860 | 199,860 |
| stxlib | proofs | C | 3 | 305 | 20 | 62 | 223 | 11,093 | 11,093 |
| stxlib | runtime | C++ | 124 | 38,560 | 3,282 | 4,772 | 30,506 | 1,593,815 | 1,593,815 |
| stxlib | src/algorithms | C++ | 129 | 59,218 | 3,769 | 6,362 | 49,087 | 3,147,529 | 3,147,541 |
| stxlib | src/core | C++ | 55 | 9,956 | 1,062 | 1,581 | 7,313 | 425,641 | 425,644 |
| stxlib | src/hosted | C++ | 76 | 10,968 | 1,238 | 2,240 | 7,490 | 459,500 | 459,500 |
| stxlib | src/state | C++ | 13 | 4,294 | 442 | 1,420 | 2,432 | 195,393 | 195,394 |
| stxlib | src/zones | C++ | 1 | 5,837 | 9 | 7 | 5,821 | 387,716 | 387,716 |
| stxlib | tests | C++ | 97 | 53,510 | 3,133 | 4,454 | 45,923 | 2,600,304 | 2,600,385 |
| stxlib | tests | C | 2 | 360 | 35 | 13 | 312 | 13,672 | 13,672 |
| stxlib | tools | C++ | 5 | 586 | 50 | 92 | 444 | 25,164 | 25,164 |
