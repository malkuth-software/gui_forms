"""Draw the public roadmap image, docs/_static/roadmap.svg.

The chevron bar of releases toward 1.0 shown at the top of the Roadmap page,
in the house palette, with the
Portsmouth Rapids faces subset and embedded so the image renders identically
everywhere. Each release is described in its own section of docs/dev/roadmap.md; edit
STAGES here and those sections together when the plan changes, then run:

    python3 tools/make_roadmap_svg.py
"""
import base64
import html
import io
import re
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/PortsmouthRapids.ttf"
FONT_BOLD = ROOT / "assets/fonts/PortsmouthRapids-Bold.ttf"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs/_static/roadmap.svg"

WIDTH = 1240

# (version, name, kind, items). kind: shipped, stage, stable, after.
STAGES = [
    ("0.1", "Shipped", "shipped", [
        "Retained core, CPU painting",
        "One text engine everywhere",
        "70+ Forms controls",
        "Hosts: macOS, Windows, X11",
        "Installed SDK, cached builds",
    ]),
    ("0.2", "Verified", "shipped", [
        "Accessibility verified on every host",
        "Font substitution",
        "Articulated controls: named parts, one schema",
        "Owned compression and image codecs",
    ]),
    ("0.3", "Themeable", "shipped", [
        "Theme engine and theme language",
        "GUI.Forms theme, light and dark",
        "Familiar themes",
        "Compositor cooperation",
    ]),
    ("0.4", "Flexible", "shipped", [
        "Flex and grid layout",
        "Form documents that round-trip hand edits",
        "Damage-preserving painting",
        "More Windows Forms controls",
    ]),
    ("0.5", "ABI seal", "stage", [
        "C ABI 1.1, audited",
        "Properties as capabilities",
        "Application, windows and rules",
        "Form and theme languages",
    ]),
    ("1.0", "Backend", "stage", [
        "Final controls",
        "C# bindings",
    ]),
    ("Tools", "Designers", "after", [
        "Conversion program",
        "Visual editor and interface builder",
        "Visual Studio integration",
    ]),
    ("1.0", "Stable", "stable", [
        "Same traces on every host",
        "Native accessibility tested",
        "Reproducible packages, SBOM",
        "Docs generated from the control manifest",
        "API frozen",
    ]),
]

STYLE = {
    "shipped": {"fill": "#ffffff", "edge": "#2f8f5b", "head": "#2f8f5b", "ink": "#153765", "sub": "#4f6687"},
    "stage": {"fill": "#e3eefc", "edge": "#7c9cc8", "head": "#3d6fbd", "ink": "#153765", "sub": "#4f6687"},
    "stable": {"fill": "#fff3d0", "edge": "#c49332", "head": "#c49332", "ink": "#4a3208", "sub": "#7a5a20"},
    "after": {"fill": "#f4f4f5", "edge": "#b9c3d1", "head": "#7d8aa0", "ink": "#2b3b52", "sub": "#66748a"},
}


def subset_font(path: Path, characters: str) -> str:
    """Return a base64 TrueType subset of the face holding only the characters drawn."""
    options = subset.Options()
    options.layout_features = ["kern", "liga"]
    font = TTFont(str(path))
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=characters)
    subsetter.subset(font)
    buffer = io.BytesIO()
    font.save(buffer)
    return base64.b64encode(buffer.getvalue()).decode()


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def main() -> None:
    count = len(STAGES)
    margin = 2
    notch = 18
    bar_h = 54
    chevron_w = (WIDTH - 2 * margin + (count - 1) * notch) / count
    height = bar_h + 2 * margin
    out: list[str] = []
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {height}" width="{WIDTH}" height="{height}" role="img" aria-labelledby="t d">')
    out.append('<title id="t">GUI.Forms releases toward 1.0</title>')
    description = ", then ".join(f"{version} {name}" for version, name, _, _ in STAGES)
    out.append(f'<desc id="d">{esc(description)}.</desc>')
    out.append('<defs><style>'
               '@font-face{font-family:"PR";src:url(data:font/ttf;base64,@@REGULAR@@) format("truetype");font-weight:400}'
               '@font-face{font-family:"PR";src:url(data:font/ttf;base64,@@BOLD@@) format("truetype");font-weight:700}'
               'text{font-family:"PR","Segoe UI",Helvetica,Arial,sans-serif}'
               '</style>'
               '<linearGradient id="gold" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffedba"/><stop offset="1" stop-color="#ffcf66"/></linearGradient>'
               '</defs>')
    for index, (version, name, kind, _) in enumerate(STAGES):
        st = STYLE[kind]
        x = margin + index * (chevron_w - notch)
        y = margin
        h = bar_h
        left_notch = 0 if index == 0 else notch
        if index == count - 1:
            points = [(x, y), (x + chevron_w, y), (x + chevron_w, y + h), (x, y + h), (x + left_notch, y + h / 2)]
        else:
            points = [(x, y), (x + chevron_w - notch, y), (x + chevron_w, y + h / 2), (x + chevron_w - notch, y + h), (x, y + h), (x + left_notch, y + h / 2)]
        fill = "url(#gold)" if kind == "stable" else st["head"]
        ink = "#4a3208" if kind == "stable" else "#ffffff"
        out.append('<polygon points="' + " ".join(f"{px:.1f},{py:.1f}" for px, py in points) + f'" fill="{fill}" stroke="#ffffff" stroke-width="2.5" stroke-linejoin="round"/>')
        cx = x + left_notch / 2 + (chevron_w - notch) / 2
        out.append(f'<text x="{cx:.1f}" y="{y + 25}" text-anchor="middle" font-size="20" font-weight="700" fill="{ink}">{esc(version)}</text>')
        out.append(f'<text x="{cx:.1f}" y="{y + 43}" text-anchor="middle" font-size="13" fill="{ink}">{esc(name)}</text>')
    out.append("</svg>")
    document = "\n".join(out)
    drawn = "".join(html.unescape(m) for m in re.findall(r">([^<>]+)</text>", document)) + " 0123456789"
    document = document.replace("@@REGULAR@@", subset_font(FONT, drawn)).replace("@@BOLD@@", subset_font(FONT_BOLD, drawn))
    OUT.write_text(document, encoding="utf-8")
    print(OUT, round(OUT.stat().st_size / 1024), "KiB")


if __name__ == "__main__":
    main()
