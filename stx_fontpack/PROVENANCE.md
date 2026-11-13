# stx_fontpack provenance

The format, the codec, the font transforms, the packer and the loader are
original first-party C++. They were written for GUI.Forms in the font pack
experiment (`notes:experiments/font-pack` on the notes `gui_forms` branch, at
5bf77f9) and converted here to production form:

- the loader's memory comes from a caller's `Allocator`;
- its work runs on a caller's `Executor`, and the package starts no threads;
- CRC-32 comes from `stx_zlib::crc32`;
- the bit scans use `<bit>`, with no compiler builtins.

The ideas the transforms follow are public format descriptions, not code:

- The glyf/loca transform (triplet-coded points, separate streams per glyph
  field) follows the description in the W3C WOFF 2.0 recommendation. No
  WOFF2 or Brotli source is copied.
- rANS entropy coding follows Jarek Duda's published description. The
  static tables, interleaved states and context clustering are this
  package's own.
- CFF charstrings are tokenised according to Adobe Technical Notes 5176 and
  5177.

`src/detail/crc_combine.hpp` joins two CRC-32 values. It uses zlib's
method (zlib 1.2.12, `multmodp` and `x2nmodp`, Copyright (C) 1995-2022 Jean-loup
Gailly and Mark Adler, zlib licence): polynomial multiplication modulo the
CRC polynomial, and a table of x^(2^k). It is rewritten in this package's
style, with the table computed at compile time. stx_zlib provides `crc32`
but no combine, so the loader carries this one. It is used to join the
1 MiB pieces of a large font that several workers check.

No font data is part of this package. The tests generate their inputs, or
read a directory the build names.
