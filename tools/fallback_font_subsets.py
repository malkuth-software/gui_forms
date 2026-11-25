#!/usr/bin/env python3
"""The slim bundled fallback faces: their character sets, the study that sized them, and the
subsetting that writes them into assets/fonts.

The owner's direction (2026-11-16): Japanese and Korean each get a proper CJK face; Chinese,
Arabic and the Indic scripts get a very minimal fallback; Hebrew stays in scope unchanged.
Every face is cut from the upstream Noto files named in SOURCES, never from host fonts.

    python3 -B tools/fallback_font_subsets.py study  --sources DIR [--corpus]
    python3 -B tools/fallback_font_subsets.py write  --sources DIR [--output assets/fonts]
    python3 -B tools/fallback_font_subsets.py coverage [--output FILE]

`--sources` holds the upstream files at the SHA-256 SOURCES records. They were in assets/fonts
until this change; to reproduce, extract them from the last commit that carried them:

    mkdir -p build/fallback-font-sources
    git archive 817c36b82 assets/fonts/NotoSansCJKjp-Regular.otf \
        assets/fonts/NotoSansArabic-Regular.ttf assets/fonts/NotoSansDevanagari-Regular.ttf \
        assets/fonts/NotoSansBengali-Regular.ttf assets/fonts/NotoSansGurmukhi-Regular.ttf \
        | tar -x --strip-components 2 -C build/fallback-font-sources

(third_party/fetch_showcase_fallback_fonts.sh downloads the same files from upstream.)

`write` is deterministic for a given fontTools version (FONTTOOLS_VERSION; the head table's
modification time is kept from the source). `coverage` regenerates
src/core/resources/fonts/bundled_font_coverage.inc from the faces in assets/fonts exactly as
`gui_forms_bundled_font_coverage_tests --write` does (the test checks the result against
STXlib's character map reader at every scalar).

Character sets come from Python's own codecs, so nothing is downloaded: JIS X 0208 (euc_jp),
JIS X 0213 (euc_jis_2004), KS X 1001 (euc_kr) and GB 2312 (gb2312). The 通用规范汉字表 first
level (3,500 hanzi) is not available offline; GB 2312 level 1 (3,755 hanzi, also ordered by
frequency) stands in for it.
"""

import argparse
import hashlib
import io
import lzma
import os
import sys
import zlib

from fontTools import version as fonttools_version
from fontTools import subset
from fontTools.ttLib import TTFont

FONTTOOLS_VERSION = "4.66.1"

REPOSITORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIRECTORY = os.path.join(REPOSITORY, "assets", "fonts")
COVERAGE_FILE = os.path.join(REPOSITORY, "src", "core", "resources", "fonts", "bundled_font_coverage.inc")

# The upstream faces the subsets are cut from, with the digests assets/fonts recorded for them.
SOURCES = {
    "NotoSansCJKjp-Regular.otf": "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5",
    "NotoSansArabic-Regular.ttf": "ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b",
    "NotoSansDevanagari-Regular.ttf": "385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d",
    "NotoSansBengali-Regular.ttf": "6300c5370cd688b0641343de4c786de6d412bb6c578d129dae75e93a0322dcab",
    "NotoSansGurmukhi-Regular.ttf": "bbde4d85fdfb998eff6921cb2c7a9a7924a1c95560a6aa9a06172530e4f596da",
}

# The fallback faces in registration order (bundled_font_set.cpp's face_uses), the order fallback
# selection tries them. Hebrew and emoji are the unmodified upstream files.
FALLBACK_ORDER = [
    "NotoSansCJKjp-Subset.otf",
    "NotoSansCJKkr-Subset.otf",
    "NotoSansCJKsc-Minimal.otf",
    "NotoSansArabic-Minimal.ttf",
    "NotoSansHebrew-Regular.ttf",
    "NotoSansDevanagari-Minimal.ttf",
    "NotoSansBengali-Minimal.ttf",
    "NotoSansGurmukhi-Minimal.ttf",
    "NotoEmoji-Regular.ttf",
]

# Packed-size estimates. No stx_fontpack tool can be run without compiling one, so packed bytes
# are estimated from xz -9e of each file, scaled by the ratio of the pack to xz -9e recorded for
# the same kind of font: the cjk group (16,467,736 bytes raw, 8.8 MB packed as recorded on
# 2026-11-16) against this script's xz of the same file for CFF faces, and the whole 37-face pack
# (42.5% against xz -9e 50.7%, notes:experiments/font-pack 5bf77f9) for TrueType faces.
CJK_GROUP_PACKED_BYTES = 8.8e6
PACK_OVER_XZ_TRUETYPE = 42.5 / 50.7


# ---------------------------------------------------------------- character sets


def ranges(*pairs):
    """The scalars of inclusive (first, last) pairs."""
    result = set()
    for first, last in pairs:
        result.update(range(first, last + 1))
    return result


def decode_double_byte(codec, first_row, last_row, lead_offset=0xA0, prefix=b""):
    """Every character of rows first_row..last_row of a 94x94 EUC plane, by row."""
    rows = {}
    for row in range(first_row, last_row + 1):
        found = set()
        for cell in range(1, 95):
            encoded = prefix + bytes([row + lead_offset, cell + 0xA0])
            try:
                text = encoded.decode(codec)
            except UnicodeDecodeError:
                continue
            if len(text) == 1:
                found.add(ord(text))
        rows[row] = found
    return rows


def union(rows, first, last):
    result = set()
    for row in range(first, last + 1):
        result |= rows.get(row, set())
    return result


def jis_x_0208():
    rows = decode_double_byte("euc_jp", 1, 94)
    return {
        "non_kanji": union(rows, 1, 15),
        "level_1": union(rows, 16, 47),
        "level_2": union(rows, 48, 94),
    }


def jis_x_0213_levels_3_4(jis_0208_all):
    plane_1 = decode_double_byte("euc_jis_2004", 1, 94)
    plane_2 = decode_double_byte("euc_jis_2004", 1, 94, prefix=b"\x8f")
    level_3 = union(plane_1, 1, 94) - jis_0208_all
    level_4 = union(plane_2, 1, 94) - jis_0208_all - level_3
    return level_3, level_4


def ks_x_1001():
    rows = decode_double_byte("euc_kr", 1, 94)
    hangul = union(rows, 16, 40)
    hanja = union(rows, 42, 93)
    return {"non_hangul_non_hanja": union(rows, 1, 15), "hangul": hangul, "hanja": hanja}


def gb_2312():
    rows = decode_double_byte("gb2312", 1, 94)
    return {"symbols": union(rows, 1, 15), "level_1": union(rows, 16, 55), "level_2": union(rows, 56, 87)}


# Kana and the Japanese punctuation and width forms beyond JIS X 0208 rows 1 to 8, including
# the other mapping tables' choices for the same JIS cells (wave dash, dashes, double vertical
# line, minus, yen, cent, pound, not sign).
KANA = ranges((0x3041, 0x3096), (0x3099, 0x309F), (0x30A0, 0x30FF), (0x31F0, 0x31FF), (0xFF61, 0xFF9F))
JAPANESE_EXTRAS = ranges((0x3000, 0x303F), (0xFF01, 0xFF60), (0xFFE0, 0xFFE6)) | {
    0x2014, 0x2015, 0x2016, 0x2225, 0x2212, 0xFF0D, 0x301C, 0xFF5E, 0x00A2, 0x00A3, 0x00A5, 0x00AC}
# The Hangul jamo blocks and every precomposed syllable.
HANGUL_SYLLABLES = ranges((0xAC00, 0xD7A3))
HANGUL_JAMO = ranges((0x1100, 0x11FF), (0x3131, 0x318E), (0xA960, 0xA97C), (0xD7B0, 0xD7C6), (0xD7CB, 0xD7FB))
KOREAN_EXTRAS = ranges((0x3000, 0x303F), (0xFF01, 0xFF60), (0xFFE0, 0xFFE6))
# Separators every minimal face keeps, so a run of the script is not split at its spaces.
SEPARATORS = {0x0020, 0x00A0, 0x200C, 0x200D}

# Arabic: the basic letters (with the Persian additions), tatweel, the vowel marks, both digit
# sets and the punctuation. No presentation forms (U+FB50 to U+FDFF, U+FE70 to U+FEFF) and no
# layout tables: the unshaped renderer draws each letter's nominal (isolated) glyph.
ARABIC_MINIMAL = ranges((0x0621, 0x063A), (0x0640, 0x0652), (0x0660, 0x066D), (0x06F0, 0x06F9)) | {
    0x060C, 0x061B, 0x061F, 0x0670, 0x0671, 0x067E, 0x0686, 0x0698, 0x06A9, 0x06AF, 0x06CC, 0x06D4}
# The Indic faces: the script's main block (letters, vowel signs, virama, digits) and the dandas,
# no conjunct or reordering glyphs and no layout tables.
DEVANAGARI_MINIMAL = ranges((0x0900, 0x097F))
BENGALI_MINIMAL = ranges((0x0980, 0x09FF), (0x0964, 0x0965))
GURMUKHI_MINIMAL = ranges((0x0A00, 0x0A7F), (0x0964, 0x0965))


class CharacterSets:
    def __init__(self):
        jis = jis_x_0208()
        self.jis_non_kanji = jis["non_kanji"]
        self.jis_level_1 = jis["level_1"]
        self.jis_level_2 = jis["level_2"]
        self.jis_0208 = self.jis_non_kanji | self.jis_level_1 | self.jis_level_2
        self.jis_level_3, self.jis_level_4 = jis_x_0213_levels_3_4(self.jis_0208)
        ks = ks_x_1001()
        self.ks_other = ks["non_hangul_non_hanja"]
        self.ks_hangul = ks["hangul"]
        self.ks_hanja = ks["hanja"]
        gb = gb_2312()
        self.gb_level_1 = gb["level_1"]
        self.gb_level_2 = gb["level_2"]

    def japanese(self, with_0213):
        result = KANA | JAPANESE_EXTRAS | self.jis_0208
        if with_0213:
            result = result | self.jis_level_3 | self.jis_level_4
        return result

    def korean(self, with_hanja):
        result = HANGUL_SYLLABLES | HANGUL_JAMO | KOREAN_EXTRAS | self.ks_other | self.ks_hangul
        if with_hanja:
            result = result | self.ks_hanja
        return result


# ---------------------------------------------------------------- fonts


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        digest.update(stream.read())
    return digest.hexdigest()


def verified_source(sources, name):
    path = os.path.join(sources, name)
    actual = sha256_of(path)
    if actual != SOURCES[name]:
        raise SystemExit(f"{path}: SHA-256 {actual}, expected {SOURCES[name]}")
    return path


def best_cmap(font):
    """The scalars mapped to a glyph other than .notdef, as the face's Unicode character map does."""
    cmap = font.getBestCmap()
    glyph_order = font.getGlyphOrder()
    notdef = glyph_order[0]
    return {code: glyph for code, glyph in cmap.items() if glyph != notdef and code <= 0x10FFFF}


def locl_substitution(font, language):
    """The single substitutions the face's locl feature applies for `language` (an OpenType
    language system tag), in lookup order, as one glyph-to-glyph map."""
    table = font["GSUB"].table
    lookup_indices = set()
    for script_record in table.ScriptList.ScriptRecord:
        for language_record in script_record.Script.LangSysRecord:
            if language_record.LangSysTag != language:
                continue
            for feature_index in language_record.LangSys.FeatureIndex:
                feature_record = table.FeatureList.FeatureRecord[feature_index]
                if feature_record.FeatureTag == "locl":
                    lookup_indices.update(feature_record.Feature.LookupListIndex)
    mapping = {}
    for lookup_index in sorted(lookup_indices):
        lookup = table.LookupList.Lookup[lookup_index]
        step = {}
        for subtable in lookup.SubTable:
            if lookup.LookupType == 7:
                subtable = subtable.ExtSubTable
            if subtable.LookupType != 1:
                raise SystemExit(f"locl lookup {lookup_index} for {language} is not a single substitution")
            step.update(subtable.mapping)
        mapping = {source: step.get(target, target) for source, target in mapping.items()}
        for source, target in step.items():
            mapping.setdefault(source, target)
    return mapping


def apply_language_forms(font, language):
    """Rewrites every Unicode cmap subtable so each character maps to the glyph locl gives it
    for `language`, and drops the variation-sequence subtable. Returns how many characters
    changed glyph."""
    mapping = locl_substitution(font, language)
    changed = set()
    kept = []
    for subtable in font["cmap"].tables:
        if subtable.format == 14:
            continue
        if subtable.isUnicode():
            for code, glyph in list(subtable.cmap.items()):
                target = mapping.get(glyph, glyph)
                if target != glyph:
                    subtable.cmap[code] = target
                    changed.add(code)
        kept.append(subtable)
    font["cmap"].tables = kept
    return changed


def rename_family(font, region_upper, region_lower):
    for record in font["name"].names:
        if record.nameID not in (1, 3, 4, 6, 16, 17):
            continue
        text = record.toUnicode()
        text = text.replace("CJK JP", "CJK " + region_upper).replace("CJKjp", "CJK" + region_lower)
        record.string = text


def subset_options(layout):
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.name_legacy = True
    options.notdef_outline = True
    options.recalc_timestamp = False
    options.recalc_bounds = True
    options.hinting = True
    options.glyph_names = False
    options.legacy_kern = False
    options.layout_closure = False
    options.layout_features = ["*"] if layout else []
    if not layout:
        options.drop_tables = list(options.drop_tables) + ["GSUB", "GPOS", "GDEF", "BASE", "JSTF", "DSIG"]
    return options


def cut(source_path, unicodes, language=None, region=None):
    """The subset of the face at `source_path` that keeps `unicodes` (those it maps), with
    `language`'s locl forms baked into its cmap, as bytes, and the scalars it keeps."""
    font = TTFont(source_path, recalcTimestamp=False)
    if language is not None:
        apply_language_forms(font, language)
        rename_family(font, region[0], region[1])
    mapped = set(best_cmap(font))
    kept = sorted(set(unicodes) & mapped)
    subsetter = subset.Subsetter(subset_options(layout=False))
    subsetter.populate(unicodes=kept)
    subsetter.subset(font)
    stream = io.BytesIO()
    font.save(stream)
    return stream.getvalue(), set(kept)


def xz_size(data):
    return len(lzma.compress(data, preset=9 | lzma.PRESET_EXTREME))


def zlib_size(data):
    return len(zlib.compress(data, 9))


# ---------------------------------------------------------------- the layout


def recommended_faces(sets, sources):
    """The faces `write` produces, in FALLBACK_ORDER, each as (name, bytes, covered scalars)."""
    cjk = verified_source(sources, "NotoSansCJKjp-Regular.otf")
    japanese_bytes, japanese = cut(cjk, sets.japanese(with_0213=False), "JAN ", ("JP", "jp"))
    korean_bytes, korean = cut(cjk, sets.korean(with_hanja=True), "KOR ", ("KR", "kr"))
    chinese_only = sets.gb_level_1 - japanese - korean
    chinese_bytes, chinese = cut(cjk, chinese_only, "ZHS ", ("SC", "sc"))
    faces = [
        ("NotoSansCJKjp-Subset.otf", japanese_bytes, japanese),
        ("NotoSansCJKkr-Subset.otf", korean_bytes, korean),
        ("NotoSansCJKsc-Minimal.otf", chinese_bytes, chinese),
    ]
    for name, source, characters in (
            ("NotoSansArabic-Minimal.ttf", "NotoSansArabic-Regular.ttf", ARABIC_MINIMAL),
            ("NotoSansDevanagari-Minimal.ttf", "NotoSansDevanagari-Regular.ttf", DEVANAGARI_MINIMAL),
            ("NotoSansBengali-Minimal.ttf", "NotoSansBengali-Regular.ttf", BENGALI_MINIMAL),
            ("NotoSansGurmukhi-Minimal.ttf", "NotoSansGurmukhi-Regular.ttf", GURMUKHI_MINIMAL)):
        data, covered = cut(verified_source(sources, source), characters | SEPARATORS)
        faces.append((name, data, covered))
    return faces


# ---------------------------------------------------------------- commands


def estimate_packed(data, cff, cjk_factor):
    if cff:
        return int(xz_size(data) * cjk_factor)
    return int(xz_size(data) * PACK_OVER_XZ_TRUETYPE)


def report_row(label, characters, data, cff, cjk_factor):
    return (f"| {label} | {characters:,} | {len(data):,} | {zlib_size(data):,} | {xz_size(data):,} | "
            f"{estimate_packed(data, cff, cjk_factor):,} |")


def command_study(arguments):
    if fonttools_version != FONTTOOLS_VERSION:
        print(f"note: fontTools {fonttools_version}, the recorded study used {FONTTOOLS_VERSION}")
    sets = CharacterSets()
    sources = arguments.sources
    cjk_path = verified_source(sources, "NotoSansCJKjp-Regular.otf")
    with open(cjk_path, "rb") as stream:
        cjk_data = stream.read()
    cjk_xz = xz_size(cjk_data)
    cjk_factor = CJK_GROUP_PACKED_BYTES / cjk_xz
    print(f"Calibration: full JP face {len(cjk_data):,} bytes, xz -9e {cjk_xz:,}; "
          f"recorded packed {CJK_GROUP_PACKED_BYTES:,.0f} gives CFF factor {cjk_factor:.3f}; "
          f"TrueType factor {PACK_OVER_XZ_TRUETYPE:.3f}.")
    print()
    print("Character sets (from Python codecs):")
    print(f"- JIS X 0208: non-kanji {len(sets.jis_non_kanji)}, level 1 {len(sets.jis_level_1)}, "
          f"level 2 {len(sets.jis_level_2)}; JIS X 0213 level 3 {len(sets.jis_level_3)}, level 4 {len(sets.jis_level_4)}")
    print(f"- KS X 1001: other {len(sets.ks_other)}, Hangul {len(sets.ks_hangul)}, hanja {len(sets.ks_hanja)}")
    print(f"- GB 2312: level 1 {len(sets.gb_level_1)}, level 2 {len(sets.gb_level_2)}")

    full = TTFont(cjk_path, lazy=True)
    full_cmap = best_cmap(full)
    for label, wanted in (("JIS X 0208 + kana", sets.japanese(False)), ("+ JIS X 0213 L3/L4", sets.japanese(True)),
                          ("KS X 1001 + all Hangul", sets.korean(True)), ("GB 2312 L1", sets.gb_level_1)):
        missing = wanted - set(full_cmap)
        print(f"- the full JP face lacks {len(missing)} of {len(wanted)} characters of {label}")
    korean_forms = locl_substitution(full, "KOR ")
    hangul_changed = sum(1 for code in HANGUL_SYLLABLES | HANGUL_JAMO
                         if code in full_cmap and full_cmap[code] in korean_forms)
    hanja_changed = sum(1 for code in sets.ks_hanja if code in full_cmap and full_cmap[code] in korean_forms)
    chinese_forms = locl_substitution(full, "ZHS ")
    hanzi_changed = sum(1 for code in sets.gb_level_1 if code in full_cmap and full_cmap[code] in chinese_forms)
    print(f"- Korean forms (locl KOR) change {hangul_changed} Hangul and jamo and {hanja_changed} of "
          f"{len(sets.ks_hanja)} KS X 1001 hanja; Chinese forms (locl ZHS) change {hanzi_changed} of "
          f"{len(sets.gb_level_1)} GB 2312 level 1 hanzi")
    print()

    header = "| Option | Characters | Raw bytes | zlib -9 | xz -9e | Packed (estimate) |\n|---|---|---|---|---|---|"
    print(header)
    print(report_row("Today: Noto Sans CJK JP, whole", len(full_cmap), cjk_data, True, cjk_factor))
    japanese_bytes, japanese = cut(cjk_path, sets.japanese(False), "JAN ", ("JP", "jp"))
    print(report_row("J1 Japanese: kana + JIS X 0208 L1/L2, JP forms", len(japanese), japanese_bytes, True, cjk_factor))
    japanese_0213_bytes, japanese_0213 = cut(cjk_path, sets.japanese(True), "JAN ", ("JP", "jp"))
    print(report_row("J2 J1 + JIS X 0213 L3/L4", len(japanese_0213), japanese_0213_bytes, True, cjk_factor))
    korean_bytes, korean = cut(cjk_path, sets.korean(True), "KOR ", ("KR", "kr"))
    print(report_row("K1 Korean: 11,172 Hangul + jamo + KS X 1001 with hanja, KR forms", len(korean),
                     korean_bytes, True, cjk_factor))
    korean_plain_bytes, korean_plain = cut(cjk_path, sets.korean(False), "KOR ", ("KR", "kr"))
    print(report_row("K2 K1 without hanja", len(korean_plain), korean_plain_bytes, True, cjk_factor))
    gb_in_jk = sets.gb_level_1 & (japanese | korean)
    chinese_only = sets.gb_level_1 - japanese - korean
    chinese_rest_bytes, chinese_rest = cut(cjk_path, chinese_only, "ZHS ", ("SC", "sc"))
    print(report_row("C1 Chinese: GB 2312 L1 hanzi J1 and K1 lack, SC forms", len(chinese_rest), chinese_rest_bytes,
                     True, cjk_factor))
    chinese_all_bytes, chinese_all = cut(cjk_path, sets.gb_level_1, "ZHS ", ("SC", "sc"))
    print(report_row("C2 Chinese: all GB 2312 L1 hanzi, SC forms", len(chinese_all), chinese_all_bytes, True,
                     cjk_factor))
    print(f"| C0 Chinese: nothing; J1 + K1 cover {len(gb_in_jk):,} of {len(sets.gb_level_1):,} GB 2312 L1 "
          f"hanzi ({100.0 * len(gb_in_jk) / len(sets.gb_level_1):.1f}%), JP or KR forms | 0 | 0 | 0 | 0 | 0 |")
    for name, characters in (("NotoSansArabic-Regular.ttf", ARABIC_MINIMAL),
                             ("NotoSansDevanagari-Regular.ttf", DEVANAGARI_MINIMAL),
                             ("NotoSansBengali-Regular.ttf", BENGALI_MINIMAL),
                             ("NotoSansGurmukhi-Regular.ttf", GURMUKHI_MINIMAL)):
        path = verified_source(sources, name)
        with open(path, "rb") as stream:
            original = stream.read()
        original_count = len(best_cmap(TTFont(path, lazy=True)))
        print(report_row(f"Today: {name}", original_count, original, False, cjk_factor))
        data, covered = cut(path, characters | SEPARATORS)
        print(report_row(f"Minimal {name.split('-')[0]}", len(covered), data, False, cjk_factor))
    hebrew = os.path.join(FONT_DIRECTORY, "NotoSansHebrew-Regular.ttf")
    with open(hebrew, "rb") as stream:
        hebrew_data = stream.read()
    print(report_row("Hebrew, unchanged", len(best_cmap(TTFont(hebrew, lazy=True))), hebrew_data, False, cjk_factor))

    if arguments.corpus:
        study_corpus(sets, full_cmap, japanese, korean, chinese_rest)


def corpus_scalars():
    scalars = {}
    for top in ("demo", "lab", "examples", "src/controls", "assets/locales"):
        root = os.path.join(REPOSITORY, top)
        for directory, _, files in os.walk(root):
            for name in files:
                if not name.endswith((".cpp", ".hpp", ".dml", ".xml", ".json", ".txt", ".md", ".mm")):
                    continue
                path = os.path.join(directory, name)
                try:
                    with open(path, encoding="utf-8") as stream:
                        text = stream.read()
                except (UnicodeDecodeError, OSError):
                    continue
                for character in text:
                    code = ord(character)
                    if code > 0x7F:
                        scalars.setdefault(code, os.path.relpath(path, REPOSITORY))
    return scalars


def study_corpus(sets, full_cmap, japanese, korean, chinese):
    primaries = set()
    for name in os.listdir(FONT_DIRECTORY):
        if name.startswith(("Portsmouth", "Carlito", "Cousine", "Selawik", "Arimo", "Tinos")) and name.endswith(".ttf"):
            primaries |= set(best_cmap(TTFont(os.path.join(FONT_DIRECTORY, name), lazy=True)))
    lost = []
    for code, where in sorted(corpus_scalars().items()):
        if code in primaries or code not in full_cmap:
            continue
        if code in japanese or code in korean or code in chinese:
            continue
        lost.append((code, where))
    print()
    print(f"Corpus: {len(lost)} first-party characters the whole CJK face drew and no primary or new CJK face does:")
    for code, where in lost:
        print(f"- U+{code:04X} {chr(code)} ({where})")


def command_write(arguments):
    if fonttools_version != FONTTOOLS_VERSION:
        raise SystemExit(f"fontTools {fonttools_version}; the checked-in faces were written with {FONTTOOLS_VERSION}")
    sets = CharacterSets()
    for name, data, covered in recommended_faces(sets, arguments.sources):
        path = os.path.join(arguments.output, name)
        with open(path, "wb") as stream:
            stream.write(data)
        print(f"{name}: {len(covered):,} characters, {len(data):,} bytes, sha256 {hashlib.sha256(data).hexdigest()}")


def manifest_digests():
    digests = {}
    with open(os.path.join(FONT_DIRECTORY, "bundled-fonts.sha256"), encoding="utf-8") as stream:
        for line in stream:
            digest, name = line.split()
            digests[name] = digest
    return digests


def coverage_ranges(path):
    scalars = sorted(best_cmap(TTFont(path, lazy=True)))
    result = []
    for code in scalars:
        if result and result[-1][1] + 1 == code:
            result[-1][1] = code
        else:
            result.append([code, code])
    return result


def command_coverage(arguments):
    digests = manifest_digests()
    lines = [
        "// Generated by `gui_forms_bundled_font_coverage_tests --write <this file>`; do not edit.",
        "// Each table: the Unicode scalars the face's Unicode character map maps to a glyph (STXlib glyph_of).",
    ]
    for index, name in enumerate(FALLBACK_ORDER):
        path = os.path.join(FONT_DIRECTORY, name)
        if sha256_of(path) != digests[name]:
            raise SystemExit(f"{name} does not match bundled-fonts.sha256")
        table = coverage_ranges(path)
        lines.append(f"// {name}, {len(table)} ranges")
        lines.append(f"constexpr CodepointRange coverage_{index}[] = {{")
        row = ""
        for position, (first, last) in enumerate(table):
            row += ("    " if position % 4 == 0 else " ") + f"{{0x{first:X}U, 0x{last:X}U}},"
            if position % 4 == 3 or position + 1 == len(table):
                lines.append(row)
                row = ""
        lines.append("};")
    lines.append(f"constexpr std::array<BundledFaceCoverage, {len(FALLBACK_ORDER)}> generated_coverages{{{{")
    for index, name in enumerate(FALLBACK_ORDER):
        lines.append(f"    {{\"{name}\", \"{digests[name]}\", coverage_{index}}},")
    lines.append("}};")
    with open(arguments.output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    study = commands.add_parser("study", help="measure every option")
    study.add_argument("--sources", required=True)
    study.add_argument("--corpus", action="store_true", help="list first-party characters the slim faces lose")
    write = commands.add_parser("write", help="write the recommended faces")
    write.add_argument("--sources", required=True)
    write.add_argument("--output", default=FONT_DIRECTORY)
    coverage = commands.add_parser("coverage", help="regenerate bundled_font_coverage.inc")
    coverage.add_argument("--output", default=COVERAGE_FILE)
    arguments = parser.parse_args()
    if arguments.command == "study":
        command_study(arguments)
    elif arguments.command == "write":
        command_write(arguments)
    else:
        command_coverage(arguments)
    return 0


if __name__ == "__main__":
    sys.exit(main())
