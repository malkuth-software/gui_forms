"""Print the factory bundle's sizes and character coverage, for docs/user/factory-bundle.md.

Reads only files in the tree:

- assets/fonts/bundled-fonts.sha256: the faces the font pack embeds;
- assets/fonts/font-pack.manifest: the pack's groups (a face goes to the first
  group with a pattern matching its name, as cmake/FontPack.cmake does);
- the faces themselves: byte sizes, and the characters each face's Unicode
  cmap maps to a glyph other than zero (fontTools);
- the licence and attribution files that are installed beside the library.

With --pack <file>, it also reads a built pack (the build writes
generated/bundled_fonts/bundled_fonts.fpk) and prints each group's coded body
size from the container header (stx_fontpack/src/detail/format.hpp).

    python3 -B tools/factory_bundle_sizes.py [--pack <build>/generated/bundled_fonts/bundled_fonts.fpk]
"""
import argparse
import fnmatch
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "assets/fonts"

# Unicode blocks worth reporting for the text faces: (name, first, last).
BLOCKS = [
    ("Basic Latin", 0x0020, 0x007E),
    ("Latin-1 Supplement", 0x00A0, 0x00FF),
    ("Latin Extended-A", 0x0100, 0x017F),
    ("Latin Extended-B", 0x0180, 0x024F),
    ("Greek and Coptic", 0x0370, 0x03FF),
    ("Cyrillic", 0x0400, 0x04FF),
    ("Hebrew", 0x0590, 0x05FF),
    ("Arabic", 0x0600, 0x06FF),
    ("Devanagari", 0x0900, 0x097F),
    ("Bengali", 0x0980, 0x09FF),
    ("Gurmukhi", 0x0A00, 0x0A7F),
    ("Latin Extended Additional", 0x1E00, 0x1EFF),
    ("Hiragana", 0x3040, 0x309F),
    ("Katakana", 0x30A0, 0x30FF),
    ("CJK Unified Ideographs", 0x4E00, 0x9FFF),
    ("Hangul Syllables", 0xAC00, 0xD7AF),
]

LICENCE_PATTERNS = ["LICENSE-*.txt", "OFL-*.txt", "ATTRIBUTION.md"]


def read_faces():
    names = []
    for line in (FONTS / "bundled-fonts.sha256").read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        digest_and_name = line.split()
        names.append(digest_and_name[1])
    return names


def read_groups():
    groups = []
    for line in (FONTS / "font-pack.manifest").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("group "):
            words = line.split()
            groups.append({"name": words[1], "load": words[2], "patterns": [], "faces": []})
        else:
            groups[-1]["patterns"].append(line)
    return groups


def assign(groups, faces):
    for face in faces:
        for group in groups:
            matched = [p for p in group["patterns"] if fnmatch.fnmatchcase(face, p)]
            if matched:
                group["faces"].append(face)
                break
        else:
            sys.exit(f"{face} matches no group")


def mapped_characters(path):
    font = TTFont(str(path), lazy=True)
    cmap = font.getBestCmap() or {}
    order = font.getGlyphOrder()
    characters = set()
    for code, glyph in cmap.items():
        if glyph != order[0]:
            characters.add(code)
    font.close()
    return characters


def read_varint(data, position):
    value = 0
    shift = 0
    while True:
        byte = data[position]
        position += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, position
        shift += 7


def read_pack(path):
    data = path.read_bytes()
    if data[:4] != b"FPK2":
        sys.exit(f"{path} is not an FPK2 pack")
    position = 4
    count, position = read_varint(data, position)
    groups = []
    for _ in range(count):
        length, position = read_varint(data, position)
        name = data[position:position + length].decode()
        position += length + 1
        fonts, position = read_varint(data, position)
        font_bytes, position = read_varint(data, position)
        _streams, position = read_varint(data, position)
        body, position = read_varint(data, position)
        for _ in range(fonts):
            length, position = read_varint(data, position)
            position += length
            _size, position = read_varint(data, position)
            position += 4
        groups.append((name, fonts, font_bytes, body))
    return len(data), groups


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pack", type=Path, help="a built bundled_fonts.fpk")
    arguments = parser.parse_args()

    faces = read_faces()
    groups = read_groups()
    assign(groups, faces)

    print("Faces by pack group (raw bytes):")
    total = 0
    for group in groups:
        group_bytes = 0
        for face in group["faces"]:
            size = (FONTS / face).stat().st_size
            group_bytes += size
            print(f"  {group['name']:<10} {face:<34} {size:>10,}")
        total += group_bytes
        print(f"  {group['name']:<10} {len(group['faces'])} faces{'':<26} {group_bytes:>10,}  ({group['load']})")
    print(f"  all        {len(faces)} faces{'':<26} {total:>10,}")
    fallback = sum(sum((FONTS / f).stat().st_size for f in g["faces"])
                   for g in groups if g["name"] in ("scripts", "emoji", "cjk"))
    print(f"  without the fallback groups (scripts, emoji, cjk): {total - fallback:,}")

    licence_bytes = 0
    licence_files = sorted({p for pattern in LICENCE_PATTERNS for p in FONTS.glob(pattern)})
    for path in licence_files:
        licence_bytes += path.stat().st_size
    print(f"\nLicence and attribution files installed: {len(licence_files)}, {licence_bytes:,} bytes")

    print("\nCharacters mapped (regular or only face of each family):")
    representatives = [
        "PortsmouthRapids.ttf", "Portsmouth.ttf", "PortsmouthMono.ttf", "Carlito-Regular.ttf",
        "Cousine-Regular.ttf", "Selawik-Regular.ttf", "Arimo-Regular.ttf", "Tinos-Regular.ttf",
        "NotoSansArabic-Regular.ttf", "NotoSansHebrew-Regular.ttf", "NotoSansDevanagari-Regular.ttf",
        "NotoSansBengali-Regular.ttf", "NotoSansGurmukhi-Regular.ttf", "NotoEmoji-Regular.ttf",
        "NotoSansCJKjp-Regular.otf",
    ]
    union = set()
    for face in representatives:
        characters = mapped_characters(FONTS / face)
        union |= characters
        blocks = []
        for name, first, last in BLOCKS:
            in_block = sum(1 for c in characters if first <= c <= last)
            if in_block:
                blocks.append(f"{name} {in_block}")
        print(f"  {face:<34} {len(characters):>6,}  {'; '.join(blocks)}")
    print(f"  union of the above                 {len(union):>6,}")

    if arguments.pack:
        size, packed = read_pack(arguments.pack)
        packed_font_bytes = sum(group[2] for group in packed)
        print(f"\nPack {arguments.pack}: {size:,} bytes, {100.0 * size / packed_font_bytes:.2f}% of {packed_font_bytes:,}")
        for name, fonts, font_bytes, body in packed:
            print(f"  {name:<10} {fonts:>2} fonts {font_bytes:>12,} -> {body:>12,} ({100.0 * body / font_bytes:.2f}%)")
        bodies = sum(group[3] for group in packed)
        print(f"  bodies {bodies:,}; header and padding {size - bodies:,}")
        fallback_names = ("scripts", "emoji", "cjk")
        kept_fonts = sum(group[2] for group in packed if group[0] not in fallback_names)
        kept_bodies = sum(group[3] for group in packed if group[0] not in fallback_names)
        print(f"  without the fallback groups: bodies {kept_bodies:,} of {kept_fonts:,} ({100.0 * kept_bodies / kept_fonts:.2f}%)")


if __name__ == "__main__":
    main()
