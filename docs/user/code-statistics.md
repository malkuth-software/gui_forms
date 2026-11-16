# Code statistics

GUI.Forms is C++20 from its controls down to its codecs and its CPU
renderer. This page counts that code: the C++ (Objective-C++ included) and
the C in this repository, in the stx packages it builds with (stx_zlib,
stx_png, stx_conv, stx_raster3d, stx_rtf2md, stx_vorbis, stx_skia and the
third-party sources stx keeps) at the commit `stx.lock` pins, and in STXlib
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
    the C thread pool threadpool_atomic_fast in gui_forms and STXlib);
  - *first-party converted*: rewritten from an outside code base, as the
    `PROVENANCE.md` records state: stx_skia's `skia/` (from Skia, converted
    to the house style under `stx_skia/house_style_allowance.txt`),
    stx_zlib's inflate (from zlib's puff), stx_raster3d and stx_conv's
    surfaces (from conv-teapot);
  - *third-party*: code from outside parties, copied in and not
    converted: stx's `third_party/` (FreeType, HarfBuzz, libunibreak, MD4C,
    SheenBidi).
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

Counted by `tools/code_statistics.py` at gui_forms `659b0d2dcea7` (2026-11-16), stx `7db7b4f48a2c` (2026-11-15), stxlib `03a7630bebcf` (2026-11-15).

| Figure | Files | Lines | Code | Characters |
|---|---:|---:|---:|---:|
| first-party C++ (gui_forms, stx and STXlib) | 3,855 | 1,159,331 | 923,621 | 53,074,007 |

**By language and provenance**

| Language | Provenance | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| C++ | first-party original | 3,307 | 1,017,697 | 81,326 | 109,542 | 826,829 | 47,708,408 | 47,711,408 |
| C++ | first-party converted | 548 | 141,634 | 18,702 | 26,140 | 96,792 | 5,365,599 | 5,365,687 |
| C++ | third-party | 511 | 218,339 | 26,160 | 31,393 | 160,786 | 7,230,717 | 7,246,511 |
| C++ | all | 4,366 | 1,377,670 | 126,188 | 167,075 | 1,084,407 | 60,304,724 | 60,323,606 |
| C | first-party original | 66 | 23,953 | 1,541 | 4,606 | 17,806 | 1,298,322 | 1,298,322 |
| C | first-party converted | 2 | 202 | 26 | 80 | 96 | 8,599 | 8,599 |
| C | third-party | 651 | 299,687 | 49,678 | 73,207 | 176,802 | 9,937,990 | 9,942,778 |
| C | all | 719 | 323,842 | 51,245 | 77,893 | 194,704 | 11,244,911 | 11,249,699 |
| C++ and C | first-party original | 3,373 | 1,041,650 | 82,867 | 114,148 | 844,635 | 49,006,730 | 49,009,730 |
| C++ and C | first-party converted | 550 | 141,836 | 18,728 | 26,220 | 96,888 | 5,374,198 | 5,374,286 |
| C++ and C | third-party | 1,162 | 518,026 | 75,838 | 104,600 | 337,588 | 17,168,707 | 17,189,289 |
| C++ and C | all | 5,085 | 1,701,512 | 177,433 | 244,968 | 1,279,111 | 71,549,635 | 71,573,305 |

**By checkout**

| Checkout | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | C++ | 2,679 | 857,110 | 70,007 | 91,151 | 695,952 | 40,248,175 | 40,251,127 |
| gui_forms | C | 55 | 22,649 | 1,416 | 4,401 | 16,832 | 1,243,181 | 1,243,181 |
| stx | C++ | 1,281 | 411,124 | 47,300 | 59,378 | 304,446 | 14,940,748 | 14,956,618 |
| stx | C | 659 | 300,528 | 49,774 | 73,417 | 177,337 | 9,976,965 | 9,981,753 |
| stxlib | C++ | 406 | 109,436 | 8,881 | 16,546 | 84,009 | 5,115,801 | 5,115,861 |
| stxlib | C | 5 | 665 | 55 | 75 | 535 | 24,765 | 24,765 |

**By component**

| Checkout | Component | Language | Files | Lines | Blank | Comment | Code | Characters | Bytes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| gui_forms | benchmarks | C++ | 20 | 6,749 | 480 | 484 | 5,785 | 313,101 | 313,153 |
| gui_forms | cmake | C | 4 | 251 | 35 | 68 | 148 | 8,835 | 8,835 |
| gui_forms | demo | C++ | 50 | 10,432 | 1,070 | 761 | 8,601 | 495,410 | 495,587 |
| gui_forms | examples | C++ | 22 | 1,501 | 172 | 174 | 1,155 | 63,910 | 63,910 |
| gui_forms | examples | C | 3 | 121 | 9 | 20 | 92 | 5,055 | 5,055 |
| gui_forms | file_manager_demoboard | C++ | 12 | 6,378 | 419 | 36 | 5,923 | 319,149 | 319,447 |
| gui_forms | fuzz | C++ | 16 | 2,251 | 176 | 209 | 1,866 | 106,041 | 106,041 |
| gui_forms | include | C++ | 716 | 109,935 | 11,240 | 32,836 | 65,859 | 5,216,571 | 5,216,856 |
| gui_forms | include | C | 3 | 6,992 | 387 | 3,452 | 3,153 | 357,743 | 357,743 |
| gui_forms | lab | C++ | 13 | 4,106 | 376 | 242 | 3,488 | 176,835 | 176,843 |
| gui_forms | src/abi | C++ | 74 | 40,281 | 3,698 | 2,664 | 33,919 | 1,805,764 | 1,805,764 |
| gui_forms | src/abi | C | 1 | 57 | 7 | 24 | 26 | 2,407 | 2,407 |
| gui_forms | src/audio | C++ | 5 | 1,640 | 18 | 42 | 1,580 | 79,424 | 79,424 |
| gui_forms | src/controls | C++ | 347 | 158,624 | 14,848 | 11,077 | 132,699 | 6,945,399 | 6,945,778 |
| gui_forms | src/core | C++ | 481 | 167,050 | 12,131 | 16,468 | 138,451 | 7,700,067 | 7,700,578 |
| gui_forms | src/core | C | 2 | 367 | 36 | 17 | 314 | 14,007 | 14,007 |
| gui_forms | src/host | C++ | 175 | 45,049 | 2,981 | 6,028 | 36,040 | 2,029,655 | 2,029,681 |
| gui_forms | src/platform | C++ | 1 | 84 | 15 | 22 | 47 | 3,647 | 3,647 |
| gui_forms | src/plots | C++ | 13 | 3,853 | 326 | 231 | 3,296 | 173,260 | 173,264 |
| gui_forms | src/render | C++ | 43 | 13,565 | 855 | 1,344 | 11,366 | 669,060 | 669,163 |
| gui_forms | src/runtime | C++ | 8 | 1,710 | 125 | 181 | 1,404 | 75,355 | 75,355 |
| gui_forms | stx_fontpack | C++ | 19 | 10,164 | 607 | 828 | 8,729 | 429,315 | 429,315 |
| gui_forms | tests | C++ | 627 | 262,265 | 19,607 | 16,564 | 226,094 | 13,114,186 | 13,115,208 |
| gui_forms | tests | C | 42 | 14,861 | 942 | 820 | 13,099 | 855,134 | 855,134 |
| gui_forms | tools | C++ | 37 | 11,473 | 863 | 960 | 9,650 | 532,026 | 532,113 |
| stx | stx_conv | C++ | 43 | 9,565 | 486 | 1,010 | 8,069 | 470,196 | 470,196 |
| stx | stx_png | C++ | 12 | 2,783 | 128 | 227 | 2,428 | 157,528 | 157,528 |
| stx | stx_png | C | 1 | 73 | 1 | 2 | 70 | 2,855 | 2,855 |
| stx | stx_raster3d | C++ | 13 | 3,682 | 277 | 424 | 2,981 | 172,882 | 172,882 |
| stx | stx_rtf2md | C++ | 21 | 9,470 | 571 | 563 | 8,336 | 373,443 | 373,443 |
| stx | stx_skia | C++ | 620 | 155,124 | 19,280 | 25,152 | 110,692 | 5,943,271 | 5,943,347 |
| stx | stx_skia | C | 2 | 202 | 26 | 80 | 96 | 8,599 | 8,599 |
| stx | stx_vorbis | C++ | 39 | 6,430 | 186 | 269 | 5,975 | 320,841 | 320,841 |
| stx | stx_vorbis | C | 5 | 566 | 69 | 128 | 369 | 27,521 | 27,521 |
| stx | stx_zlib | C++ | 16 | 4,479 | 124 | 227 | 4,128 | 211,876 | 211,876 |
| stx | third_party/freetype | C | 499 | 246,604 | 45,266 | 60,949 | 140,389 | 7,719,415 | 7,724,112 |
| stx | third_party/harfbuzz | C++ | 511 | 218,339 | 26,160 | 31,393 | 160,786 | 7,230,717 | 7,246,511 |
| stx | third_party/harfbuzz | C | 52 | 12,266 | 1,562 | 5,830 | 4,874 | 437,141 | 437,206 |
| stx | third_party/libunibreak | C | 34 | 17,559 | 390 | 1,627 | 15,542 | 828,133 | 828,151 |
| stx | third_party/md4c | C | 4 | 10,108 | 1,257 | 1,121 | 7,730 | 355,635 | 355,643 |
| stx | third_party/sheenbidi | C | 62 | 13,150 | 1,203 | 3,680 | 8,267 | 597,666 | 597,666 |
| stx | tools | C++ | 6 | 1,252 | 88 | 113 | 1,051 | 59,994 | 59,994 |
| stxlib | include | C++ | 65 | 10,938 | 1,285 | 4,266 | 5,387 | 496,546 | 496,559 |
| stxlib | proofs | C++ | 56 | 3,896 | 411 | 1,071 | 2,414 | 199,860 | 199,860 |
| stxlib | proofs | C | 3 | 305 | 20 | 62 | 223 | 11,093 | 11,093 |
| stxlib | src/algorithms | C++ | 68 | 24,799 | 2,104 | 3,314 | 19,381 | 1,074,081 | 1,074,093 |
| stxlib | src/core | C++ | 48 | 9,179 | 974 | 1,434 | 6,771 | 395,090 | 395,093 |
| stxlib | src/hosted | C++ | 73 | 10,463 | 1,189 | 2,180 | 7,094 | 438,935 | 438,935 |
| stxlib | src/state | C++ | 13 | 4,098 | 423 | 1,373 | 2,302 | 185,261 | 185,262 |
| stxlib | src/zones | C++ | 1 | 5,837 | 9 | 7 | 5,821 | 387,716 | 387,716 |
| stxlib | tests | C++ | 77 | 39,640 | 2,436 | 2,809 | 34,395 | 1,913,148 | 1,913,179 |
| stxlib | tests | C | 2 | 360 | 35 | 13 | 312 | 13,672 | 13,672 |
| stxlib | tools | C++ | 5 | 586 | 50 | 92 | 444 | 25,164 | 25,164 |
