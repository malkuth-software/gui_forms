#!/usr/bin/env python3
"""The GOST paint palette (skeudoindustrial surfaces, phase 4) as a theme package.

The colours and their evidence come from the industrial-surfaces study's
research (notes:experiments/industrial-surfaces/research/soviet_paints.md),
kept beside the package in demo/themes/gost_paints/SOURCES.md. Every colour
is named by its card number in the Soviet colour card index (the
"Kartoteka") and by its evidence class:

  --gost-a-<card>  class a: CIE x, y and reflectance printed by ГОСТ 22133-86
                   (Table 7, illuminant C), converted to sRGB by the method
                   below; this script recomputes each from its xyY.
  --gost-b-<card>  class b: a secondary source's RGB (the laz-legend.ru card
                   list, method unstated) for the card's first swatch.

Class c colours (name or card number only) have no value and are not in the
sheet.

    python3 -B tools/industrial_surfaces/gost_palette.py           # write
    python3 -B tools/industrial_surfaces/gost_palette.py --check   # verify

It also writes the industrial theme's manifest (demo/themes/industrial),
whose sheet is hand-written: the sheet's hash and the palette's pin.

Conversion (the research's section 6): XYZ from x, y and Y = reflectance /
100; Bradford adaptation from illuminant C (x .31006, y .31616) to D65
(x .31271, y .32902); the IEC 61966-2-1 sRGB matrix and transfer curve;
rounded to bytes.
"""

import argparse
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "demo" / "themes" / "gost_paints"
SHEET_NAME = "gost-paints.theme.css"
# The industrial theme (phase 5) imports the palette: its sheet is written by
# hand and its manifest here, pinning the palette's manifest by its hash.
INDUSTRIAL = ROOT / "demo" / "themes" / "industrial"
INDUSTRIAL_SHEET_NAME = "industrial.theme.css"

# card, English gloss, x, y, reflectance %, the research's hex, sources
CLASS_A = [
    (224, "ivory", 0.335, 0.350, 56.8, "#CCC7AC", "S1 S2"),
    (661, "light beige", 0.352, 0.351, 34.2, "#AE9B84", "S1 S2"),
    (619, "brown", 0.357, 0.339, 10.0, "#68554C", "S1 S2"),
    (319, "light grey-green (pale mint)", 0.311, 0.339, 52.4, "#B4C4B2", "S1 S2 S5"),
    (365, "grey-green", 0.300, 0.333, 27.5, "#80948A", "S1 S2 S5"),
    (397, "blue-green (industrial teal)", 0.269, 0.300, 12.4, "#4D676D", "S1 S2"),
    (858, "white night", 0.319, 0.333, 48.8, "#B9BBAE", "S1 S2"),
    (840, "grey", 0.317, 0.324, 28.8, "#94928D", "S1 S2"),
    (810, "grey-silver", 0.302, 0.318, 29.7, "#8D9695", "S1 S2"),
    (832, "dark grey (Table 3 gives 822: conflict 1)", 0.292, 0.305, 15.3, "#656F74", "S1 S2"),
    (270, "cream (electrical cabinet interiors)", 0.374, 0.376, 61.5, "#E7CA96", "S1 S2 S5"),
    (121, "orange (robotic complexes)", 0.506, 0.385, 29.5, "#E0783A", "S1 S6 S8"),
]

# card, English gloss, hex, sources
CLASS_B = [
    (491, "grey-blue (PF-115, ML-12, ML-152; tropical panels)", "#437180", "S3 S7 S8 S9 S13"),
    (463, "grey-blue (NTs-132: conflict 2)", "#5A94A0", "S5 S13"),
    (495, "light grey-blue", "#8CB5B7", "S7 S8 S13"),
    (564, "bluish grey", "#69878F", "S7 S8 S13"),
    (498, "green-blue (deep teal-blue)", "#186575", "S7 S8 S13"),
    (330, "sea-wave", "#318084", "S6 S8 S13"),
    (387, "sea-wave (second swatch)", "#17707E", "S6 S8 S13"),
    (320, "turquoise", "#1E9E8F", "S8 S13"),
    (388, "pistachio", "#6F9B77", "S3 S5 S6 S13"),
    (381, "pistachio (ML-12: conflict 4)", "#7AA27D", "S8 S13"),
    (309, "pistachio 309", "#7DAC80", "S5 S13"),
    (340, "pale green", "#A0CFAD", "S5 S8 S13"),
    (356, "dark blue-green", "#294948", "S5 S13"),
    (343, "green", "#3A7944", "S3 S13"),
    (324, "bright green (a signal-green sample)", "#45A063", "S8 S11 S13"),
    (423, "light blue (a signal-blue sample)", "#295C89", "S3 S6 S11 S13"),
    (451, "light blue 451", "#2B8CBF", "S3 S13"),
    (448, "pale blue", "#88C2D8", "S5 S13"),
    (505, "light grey 505 (panels)", "#9DB4AE", "S5 S7 S8 S9 S13"),
    (583, "light grey 583", "#ADB1A2", "S3 S5 S9 S13"),
    (522, "grey (PF-115 panels)", "#929F8E", "S3 S7 S9 S13"),
    (881, "grey (KhV-124 panels)", "#708479", "S4 S9 S13"),
    (526, "dark grey (PF-133, ML-152)", "#546666", "S6 S7 S13"),
    (894, "dark grey 894", "#566868", "S3 S13"),
    (732, "protective (olive)", "#444B21", "S7 S8 S9 S13"),
    (753, "protective (NTs-132, KhV-124)", "#787734", "S4 S5 S9 S13"),
]


def converted(x, y, reflectance):
    """The research's conversion of ГОСТ 22133-86 xyY to an sRGB hex."""
    big_y = reflectance / 100.0
    xyz = [x * big_y / y, big_y, (1.0 - x - y) * big_y / y]
    bradford = [[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]]
    inverse = [[0.9869929, -0.1470543, 0.1599627], [0.4323053, 0.5183603, 0.0492912],
               [-0.0085287, 0.0400428, 0.9684867]]

    def times(matrix, vector):
        return [sum(matrix[row][column] * vector[column] for column in range(3)) for row in range(3)]

    def white(wx, wy):
        return [wx / wy, 1.0, (1.0 - wx - wy) / wy]

    source = times(bradford, white(0.31006, 0.31616))
    target = times(bradford, white(0.31271, 0.32902))
    cone = times(bradford, xyz)
    adapted = times(inverse, [cone[index] * target[index] / source[index] for index in range(3)])
    srgb = [[3.2406, -1.5372, -0.4986], [-0.9689, 1.8758, 0.0415], [0.0557, -0.2040, 1.0570]]
    linear = times(srgb, adapted)

    def encode(value):
        curved = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
        return round(max(0.0, min(1.0, curved)) * 255.0)

    return "#%02X%02X%02X" % tuple(encode(value) for value in linear)


def sheet_text():
    lines = [
        "/* The GOST paint palette (skeudoindustrial surfaces, phase 4). Generated by",
        "   tools/industrial_surfaces/gost_palette.py; do not edit. Sources, evidence classes",
        "   and conflicts: SOURCES.md beside this sheet.",
        "",
        "   --gost-a-<card>: class a, GOST 22133-86 colorimetry (xyY, illuminant C) converted",
        "   to sRGB; the first swatch of the card pair, specular component included, so a",
        "   glossy enamel looks slightly darker and more saturated.",
        "   --gost-b-<card>: class b, a hobbyist digitisation of the card (method unstated). */",
        ":root {",
    ]
    for card, gloss, x, y, reflectance, hex_value, sources in CLASS_A:
        lines.append("    --gost-a-%d: %s; /* %s; x %.3f y %.3f Y %.1f; %s */" %
                     (card, hex_value.lower(), gloss, x, y, reflectance, sources))
    for card, gloss, hex_value, sources in CLASS_B:
        lines.append("    --gost-b-%d: %s; /* %s; %s */" % (card, hex_value.lower(), gloss, sources))
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def manifest_text(sheet):
    digest = hashlib.sha256(sheet.encode("utf-8")).hexdigest()
    return ("{\n"
            "  \"format\": \"gui-forms-theme/1\",\n"
            "  \"name\": \"gost-paints\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"engine\": \">=0.3\",\n"
            "  \"sheet\": {\"path\": \"%s\", \"sha256\": \"%s\"}\n"
            "}\n" % (SHEET_NAME, digest))


def industrial_manifest_text(palette_manifest):
    sheet = (INDUSTRIAL / INDUSTRIAL_SHEET_NAME).read_bytes()
    digest = hashlib.sha256(sheet).hexdigest()
    pin = hashlib.sha256(palette_manifest.encode("utf-8")).hexdigest()
    return ("{\n"
            "  \"format\": \"gui-forms-theme/1\",\n"
            "  \"name\": \"industrial\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"engine\": \">=0.3\",\n"
            "  \"sheet\": {\"path\": \"%s\", \"sha256\": \"%s\"},\n"
            "  \"imports\": {\"gost-paints\": \"%s\"}\n"
            "}\n" % (INDUSTRIAL_SHEET_NAME, digest, pin))


def conversions_hold():
    held = True
    for card, _gloss, x, y, reflectance, hex_value, _sources in CLASS_A:
        computed = converted(x, y, reflectance)
        if computed != hex_value:
            print("gost_palette: card %d converts to %s, the research gives %s" % (card, computed, hex_value),
                  file=sys.stderr)
            held = False
    return held


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the package differs or a conversion fails")
    arguments = parser.parse_args()
    if not conversions_hold():
        return 1
    sheet = sheet_text()
    manifest = manifest_text(sheet)
    outputs = [(PACKAGE / SHEET_NAME, sheet), (PACKAGE / "theme.json", manifest),
               (INDUSTRIAL / "theme.json", industrial_manifest_text(manifest))]
    if arguments.check:
        stale = [path for path, text in outputs if not path.exists() or path.read_text(encoding="utf-8") != text]
        for path in stale:
            print("gost_palette: %s is stale; run python3 -B tools/industrial_surfaces/gost_palette.py" % path,
                  file=sys.stderr)
        return 1 if stale else 0
    PACKAGE.mkdir(parents=True, exist_ok=True)
    for path, text in outputs:
        path.write_text(text, encoding="utf-8")
    print("gost_palette: %d class a and %d class b colours -> %s" % (len(CLASS_A), len(CLASS_B), PACKAGE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
