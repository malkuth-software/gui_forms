#!/usr/bin/env python3
"""Generate the help search's Unicode fold table (GAPS H2).

Help search folds text before indexing and matching (fold_help_text in
src/core/help/helpbook_section/). ASCII is folded inline; this table holds
every other code point whose fold differs from itself. The fold of one code
point is, in order:

1. full case folding (CaseFolding.txt statuses C and F, Python's
   str.casefold) and compatibility decomposition (NFKD), repeated until
   neither changes the text, so ligatures, full-width forms and
   presentation forms meet their plain lower-case letters;
2. diacritics dropped: the combining marks of the Combining Diacritical
   Marks blocks (U+0300-U+036F, U+1AB0-U+1AFF, U+1DC0-U+1DFF, U+20D0-U+20FF,
   U+FE20-U+FE2F). Marks of other blocks (Indic vowel signs and viramas,
   Hebrew points, Arabic harakat) are kept, since they change the word;
3. separators (general category Z*) and punctuation (P*) become one space,
   so split_help_words breaks words there as it does at ASCII punctuation;
4. canonical composition (NFC) of what remains, so Hangul and other
   decomposed letters return to their composed forms.

The fold is a choice for search, not a normalization form: it equates
"Résumé", "RESUME" and "resume", and the "ﬁ" ligature with "fi". The
generator checks that folding is idempotent on every code point.

The data comes from the Python interpreter's unicodedata module, which must
report the Unicode version the repository pins (17.0.0, as the grapheme
tables). Standard library only.

  python3 -B tools/unicode/generate_search_fold_tables.py           # write
  python3 -B tools/unicode/generate_search_fold_tables.py --check   # verify
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import unicodedata

UNICODE_VERSION: str = "17.0.0"
ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parent.parent.parent
OUTPUT: pathlib.Path = ROOT / "src" / "core" / "text" / "unicode" / "unicode_search_fold_data.hpp"

DIACRITIC_BLOCKS: tuple[tuple[int, int], ...] = (
    (0x0300, 0x036F),
    (0x1AB0, 0x1AFF),
    (0x1DC0, 0x1DFF),
    (0x20D0, 0x20FF),
    (0xFE20, 0xFE2F),
)

# Pool offsets are 16-bit and lengths 8-bit in the emitted records.
MAXIMUM_POOL_BYTES: int = 0xFFFF
MAXIMUM_FOLD_BYTES: int = 0xFF


def is_diacritic(character: str) -> bool:
    point: int = ord(character)
    for first, last in DIACRITIC_BLOCKS:
        if first <= point <= last:
            return True
    return False


def is_separator(character: str) -> bool:
    category: str = unicodedata.category(character)
    return category.startswith("Z") or category.startswith("P")


def fold_text(text: str) -> str:
    """The search fold of `text`, applied code point by code point."""
    pieces: list[str] = []
    for character in text:
        pieces.append(fold_point(character))
    return "".join(pieces)


def fold_point(character: str) -> str:
    if ord(character) < 0x80:
        if "A" <= character <= "Z":
            return character.lower()
        return character
    # Case folding and decomposition repeat until neither changes the text:
    # a compatibility decomposition can yield a capital (U+03D2 to U+03A5).
    decomposed: str = unicodedata.normalize("NFKD", character.casefold())
    while True:
        refolded: str = unicodedata.normalize("NFKD", decomposed.casefold())
        if refolded == decomposed:
            break
        decomposed = refolded
    kept: list[str] = []
    for piece in decomposed:
        if is_diacritic(piece):
            continue
        if is_separator(piece):
            kept.append(" ")
            continue
        kept.append(piece)
    return unicodedata.normalize("NFC", "".join(kept))


def build() -> tuple[list[tuple[int, int, int]], bytes]:
    """The records (code point, pool offset, byte length) and the UTF-8 pool."""
    if unicodedata.unidata_version != UNICODE_VERSION:
        raise SystemExit(f"unicodedata reports Unicode {unicodedata.unidata_version}; the pin is {UNICODE_VERSION}")
    records: list[tuple[int, int, int]] = []
    pool: bytearray = bytearray()
    shared: dict[bytes, int] = {}
    for point in range(0x80, 0x110000):
        if 0xD800 <= point <= 0xDFFF:
            continue
        character: str = chr(point)
        if unicodedata.category(character) == "Cn":
            continue
        folded: str = fold_point(character)
        if folded == character:
            continue
        again: str = fold_text(folded)
        if again != folded:
            raise SystemExit(f"U+{point:04X}: the fold is not idempotent ({folded!r} folds to {again!r})")
        encoded: bytes = folded.encode("utf-8")
        if len(encoded) > MAXIMUM_FOLD_BYTES:
            raise SystemExit(f"U+{point:04X}: fold of {len(encoded)} bytes exceeds {MAXIMUM_FOLD_BYTES}")
        offset: int | None = shared.get(encoded)
        if offset is None:
            offset = len(pool)
            pool.extend(encoded)
            shared[encoded] = offset
        records.append((point, offset, len(encoded)))
    if len(pool) > MAXIMUM_POOL_BYTES + 1:
        raise SystemExit(f"fold pool of {len(pool)} bytes exceeds 16-bit offsets")
    return records, bytes(pool)


def render(records: list[tuple[int, int, int]], pool: bytes) -> str:
    lines: list[str] = [
        "// Generated by tools/unicode/generate_search_fold_tables.py. DO NOT EDIT.",
        f"// Unicode {UNICODE_VERSION} (the Python unicodedata module, version checked).",
        "// The help search fold of every non-ASCII code point whose fold differs",
        "// from itself: full case folding, NFKD, Combining Diacritical Marks",
        "// dropped, separators and punctuation to one space, then NFC.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace gui_forms::unicode_data {",
        "",
        "// One folded code point: its fold is `length` bytes of search_fold_pool",
        "// from `offset` (zero bytes for a dropped diacritic).",
        "struct SearchFoldRecord final {",
        "    std::uint32_t code_point;",
        "    std::uint16_t offset;",
        "    std::uint8_t length;",
        "};",
        "",
        f"inline constexpr std::size_t search_fold_record_count = {len(records)}U;",
        f"inline constexpr std::size_t search_fold_pool_bytes = {len(pool)}U;",
        "",
        "// Sorted by code point, each once.",
        "inline constexpr std::array<SearchFoldRecord, search_fold_record_count> search_fold_records{{",
    ]
    for point, offset, length in records:
        lines.append(f"    {{0x{point:X}U, {offset}U, {length}U}},")
    lines.append("}};")
    lines.append("")
    lines.append("// The folds' UTF-8 bytes; records share equal folds.")
    lines.append("inline constexpr std::array<std::uint8_t, search_fold_pool_bytes> search_fold_pool{{")
    for start in range(0, len(pool), 16):
        chunk: bytes = pool[start:start + 16]
        lines.append("    " + " ".join(f"0x{byte:02X}U," for byte in chunk))
    lines.append("}};")
    lines.append("")
    lines.append("} // namespace gui_forms::unicode_data")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify the committed table instead of writing it")
    arguments: argparse.Namespace = parser.parse_args()
    records, pool = build()
    text: str = render(records, pool)
    if arguments.check:
        current: str = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != text:
            print(f"search fold: {OUTPUT} is stale; rerun without --check", file=sys.stderr)
            return 1
        print(f"search fold: {OUTPUT} is current ({len(records)} records, {len(pool)} pool bytes)")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"search fold: wrote {OUTPUT} ({len(records)} records, {len(pool)} pool bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
