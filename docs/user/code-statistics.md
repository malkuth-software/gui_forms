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
Counted by `tools/code_statistics.py` at gui_forms `c0e1a31a8d28` (2026-11-18), stx `0fc2a32b393b` (2026-11-17), stxlib `b78906e7f1fb` (2026-11-17).

| Figure | Files | Lines | Code | Characters |
|---|---:|---:|---:|---:|
| first-party C++ (gui_forms, stx and STXlib) | 3,591 | 1,033,427 | 805,367 | 46,813,441 |

**By language and provenance**

| Language | Provenance | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| C++ | first-party original | 3,043 | 891,789 | 71,102 | 112,116 | 708,571 | 41,446,936 | 41,449,173 |
| C++ | first-party converted | 548 | 141,638 | 18,702 | 26,140 | 96,796 | 5,366,505 | 5,366,593 |
| C++ | all | 3,591 | 1,033,427 | 89,804 | 138,256 | 805,367 | 46,813,441 | 46,815,766 |
| C | first-party original | 158 | 30,209 | 2,155 | 5,392 | 22,662 | 1,396,579 | 1,396,579 |
| C | first-party converted | 2 | 202 | 26 | 80 | 96 | 8,599 | 8,599 |
| C | all | 160 | 30,411 | 2,181 | 5,472 | 22,758 | 1,405,178 | 1,405,178 |
| C++ and C | first-party original | 3,201 | 921,998 | 73,257 | 117,508 | 731,233 | 42,843,515 | 42,845,752 |
| C++ and C | first-party converted | 550 | 141,840 | 18,728 | 26,220 | 96,892 | 5,375,104 | 5,375,192 |
| C++ and C | third-party | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C++ and C | all | 3,751 | 1,063,838 | 91,985 | 143,728 | 828,125 | 48,218,619 | 48,220,944 |

**By checkout**

| Checkout | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | C++ | 2,108 | 622,064 | 52,152 | 80,606 | 489,306 | 28,578,829 | 28,580,967 |
| gui_forms | C | 43 | 22,667 | 1,241 | 4,353 | 17,073 | 1,103,764 | 1,103,764 |
| stx | C++ | 784 | 197,532 | 21,346 | 28,197 | 147,989 | 8,000,978 | 8,001,054 |
| stx | C | 8 | 841 | 96 | 210 | 535 | 38,975 | 38,975 |
| stxlib | C++ | 699 | 213,831 | 16,306 | 29,453 | 168,072 | 10,233,634 | 10,233,745 |
| stxlib | C | 109 | 6,903 | 844 | 909 | 5,150 | 262,439 | 262,439 |

**By component**

| Checkout | Component | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | benchmarks | C++ | 21 | 6,548 | 452 | 498 | 5,598 | 302,191 | 302,307 |
| gui_forms | demo | C++ | 50 | 10,471 | 1,075 | 766 | 8,630 | 498,142 | 498,319 |
| gui_forms | examples | C++ | 24 | 1,592 | 180 | 191 | 1,221 | 67,980 | 67,980 |
| gui_forms | examples | C | 3 | 121 | 9 | 20 | 92 | 5,055 | 5,055 |
| gui_forms | file_manager_demoboard | C++ | 9 | 4,802 | 351 | 32 | 4,419 | 227,607 | 227,888 |
| gui_forms | fuzz | C++ | 16 | 2,253 | 176 | 209 | 1,868 | 106,109 | 106,109 |
| gui_forms | include | C++ | 726 | 112,889 | 11,487 | 34,241 | 67,161 | 5,375,224 | 5,375,565 |
| gui_forms | include | C | 5 | 10,804 | 391 | 3,596 | 6,817 | 480,749 | 480,749 |
| gui_forms | lab | C++ | 13 | 4,115 | 376 | 244 | 3,495 | 177,417 | 177,425 |
| gui_forms | src/abi | C++ | 73 | 40,040 | 3,688 | 2,596 | 33,756 | 1,794,995 | 1,794,995 |
| gui_forms | src/abi | C | 35 | 11,742 | 841 | 737 | 10,164 | 617,960 | 617,960 |
| gui_forms | src/audio | C++ | 5 | 1,650 | 18 | 45 | 1,587 | 79,796 | 79,796 |
| gui_forms | src/controls | C++ | 347 | 160,086 | 14,918 | 11,394 | 133,774 | 7,057,284 | 7,057,663 |
| gui_forms | src/core | C++ | 508 | 179,787 | 12,935 | 18,602 | 148,250 | 8,382,470 | 8,383,153 |
| gui_forms | src/host | C++ | 72 | 9,726 | 760 | 1,362 | 7,604 | 426,964 | 426,968 |
| gui_forms | src/platform | C++ | 1 | 189 | 26 | 51 | 112 | 8,477 | 8,477 |
| gui_forms | src/plots | C++ | 13 | 3,862 | 326 | 235 | 3,301 | 173,839 | 173,843 |
| gui_forms | src/render | C++ | 46 | 15,012 | 940 | 1,929 | 12,143 | 754,521 | 754,560 |
| gui_forms | src/runtime | C++ | 8 | 1,622 | 125 | 188 | 1,309 | 72,655 | 72,655 |
| gui_forms | src/surface | C++ | 117 | 40,950 | 2,622 | 5,804 | 32,524 | 1,867,604 | 1,867,625 |
| gui_forms | stx_fontpack | C++ | 20 | 10,432 | 625 | 864 | 8,943 | 440,725 | 440,725 |
| gui_forms | tools | C++ | 39 | 16,038 | 1,072 | 1,355 | 13,611 | 764,829 | 764,914 |
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
| stxlib | include | C++ | 76 | 13,703 | 1,558 | 5,692 | 6,453 | 627,250 | 627,264 |
| stxlib | proofs | C++ | 56 | 3,898 | 411 | 1,071 | 2,416 | 200,088 | 200,088 |
| stxlib | proofs | C | 3 | 305 | 20 | 62 | 223 | 11,093 | 11,093 |
| stxlib | runtime | C++ | 124 | 38,560 | 3,282 | 4,772 | 30,506 | 1,593,815 | 1,593,815 |
| stxlib | src/algorithms | C++ | 129 | 59,282 | 3,769 | 6,362 | 49,151 | 3,155,650 | 3,155,662 |
| stxlib | src/core | C++ | 55 | 10,048 | 1,066 | 1,595 | 7,387 | 435,441 | 435,444 |
| stxlib | src/hosted | C++ | 115 | 21,923 | 2,317 | 3,837 | 15,769 | 940,695 | 940,695 |
| stxlib | src/hosted | C | 97 | 5,000 | 676 | 594 | 3,730 | 198,158 | 198,158 |
| stxlib | src/state | C++ | 13 | 4,298 | 442 | 1,424 | 2,432 | 195,642 | 195,643 |
| stxlib | src/zones | C++ | 1 | 5,837 | 9 | 7 | 5,821 | 387,716 | 387,716 |
| stxlib | tests | C++ | 125 | 55,696 | 3,402 | 4,601 | 47,693 | 2,672,173 | 2,672,254 |
| stxlib | tests | C | 2 | 360 | 35 | 13 | 312 | 13,672 | 13,672 |
| stxlib | tools | C++ | 5 | 586 | 50 | 92 | 444 | 25,164 | 25,164 |
| stxlib | tools | C | 1 | 42 | 3 | 17 | 22 | 1,413 | 1,413 |
