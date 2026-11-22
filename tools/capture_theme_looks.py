#!/usr/bin/env python3
"""Capture carbon fibre and the lamp looks, headless, for the owner to see.

The night of 2026-11-22 brought a carbon-fibre procedural pattern
(ProceduralPattern::carbon_fibre, ``pattern(carbon-fibre, ...)``), a radio
group drawn with lamps (RadioButton's ``indicator_style`` lamp) and the
pinpoint lamp (IndicatorLamp's ``lamp_style`` pinpoint). The Gallery's
industrial-surfaces page shows all three: the carbon fibre and brushed
metal swatches beside the glints, and in the preview group the feed choice
drawn with lamps and the Coolant flow pinpoint beside the lens lamps. This
tool runs a built Gallery headless on that page in the windows-professional
theme and writes PNGs:

    industrial-surfaces-light-1x.png, industrial-surfaces-light-2x.png
    industrial-surfaces-dark-2x.png
    industrial-surfaces-carbon-16px-2x.png   every panel's face carbon fibre,
    industrial-surfaces-carbon-32px-2x.png   one or two twill repeats a tile

The last two layer a user sheet (written beside the images, in sheets/)
over the Gallery's theme with ``--theme``, so that carbon fibre covers a
large surface. Then manifest.json lists each image, its command and its
size.

    python3 tools/capture_theme_looks.py --build-dir <build> [--out <folder>]

The default folder is ~/Developer/work/theme/captures. Exit status: 0 every
image captured; 1 a capture failed (the rest are still made); 2 usage, or
the Gallery not found in the build.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

PAGE: str = "industrial-surfaces"
THEME: str = "windows-professional"
TIMEOUT_SECONDS: int = 180
SIGNATURE: bytes = b"\x89PNG\r\n\x1a\n"
DEFAULT_OUT: Path = Path.home() / "Developer" / "work" / "theme" / "captures"
GALLERY_PROGRAMS: tuple[str, ...] = (
    "GUI.Forms Gallery.app/Contents/MacOS/GUI.Forms Gallery",
    "gui_forms_gallery", "gui_forms_gallery.exe", "GUI.Forms Gallery.exe", "GUI.Forms Gallery",
)

# A user sheet that puts carbon fibre on every panel's face, over the
# Gallery's own declarations (`!important`: a user sheet's ordinary
# declarations yield to the program's). The colours are the Gallery
# theme's swatch resin and sheen.
CARBON_SHEET: str = (
    "/* Written by tools/capture_theme_looks.py: carbon fibre on every panel. */\n"
    "panel::part(face) {{ background: pattern(carbon-fibre, #1c1d20, #6a6e75, {tile}) !important; }}\n"
)


@dataclass
class Capture:
    """One image: its file name, the scale and scheme, and an optional sheet."""
    name: str
    scale: str
    scheme: str
    sheet: Path | None = None
    command: list[str] = field(default_factory=list)


def find_gallery(build_dir: Path) -> Path | None:
    """The Gallery program in `build_dir`, searched to any depth."""
    for relative in GALLERY_PROGRAMS:
        direct = build_dir / relative
        if direct.is_file():
            return direct
    leaf_names = {Path(relative).name for relative in GALLERY_PROGRAMS}
    for candidate in sorted(build_dir.rglob("*")):
        if candidate.name in leaf_names and candidate.is_file():
            return candidate
    return None


def write_sheets(out: Path) -> dict[str, Path]:
    """Writes the carbon sheets into <out>/sheets and returns them by tile."""
    folder = out / "sheets"
    folder.mkdir(parents=True, exist_ok=True)
    sheets: dict[str, Path] = {}
    for tile in ("16px", "32px"):
        path = folder / f"carbon-{tile}.css"
        path.write_text(CARBON_SHEET.format(tile=tile), encoding="utf-8")
        sheets[tile] = path
    return sheets


def planned_captures(sheets: dict[str, Path]) -> list[Capture]:
    """Every image this tool makes, in order."""
    captures: list[Capture] = [
        Capture(name=f"{PAGE}-light-1x.png", scale="1", scheme="light"),
        Capture(name=f"{PAGE}-light-2x.png", scale="2", scheme="light"),
        Capture(name=f"{PAGE}-dark-2x.png", scale="2", scheme="dark"),
    ]
    for tile, sheet in sheets.items():
        captures.append(Capture(name=f"{PAGE}-carbon-{tile}-2x.png", scale="2", scheme="light", sheet=sheet))
    return captures


def gallery_command(gallery: Path, capture: Capture, image: Path) -> list[str]:
    """The Gallery's arguments for `capture`, writing `image`."""
    command: list[str] = [
        str(gallery), "--page", PAGE, "--capture", str(image), "--headless",
        "--theme-base", THEME, "--scheme", capture.scheme, "--scale", capture.scale,
    ]
    if capture.sheet is not None:
        command.extend(["--theme", str(capture.sheet)])
    return command


def run_capture(gallery: Path, capture: Capture, out: Path) -> dict[str, object]:
    """Runs one capture; its manifest entry, with `ok` false on any failure."""
    image = out / capture.name
    if image.exists():
        image.unlink()
    capture.command = gallery_command(gallery, capture, image)
    entry: dict[str, object] = {"image": capture.name, "command": capture.command, "ok": False}
    try:
        finished = subprocess.run(capture.command, capture_output=True, text=True, timeout=TIMEOUT_SECONDS,
                                  check=False)
    except subprocess.TimeoutExpired:
        entry["error"] = f"timed out after {TIMEOUT_SECONDS} s"
        return entry
    entry["exit_status"] = finished.returncode
    if finished.returncode != 0:
        entry["error"] = finished.stderr.strip()[-2000:]
        return entry
    if not image.is_file() or not image.read_bytes().startswith(SIGNATURE):
        entry["error"] = "no PNG was written"
        return entry
    entry["bytes"] = image.stat().st_size
    entry["ok"] = True
    return entry


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a build of this commit, holding the Gallery")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"where the images go (default {DEFAULT_OUT})")
    options = parser.parse_args(arguments)
    gallery = find_gallery(options.build_dir)
    if gallery is None:
        print(f"capture_theme_looks: no Gallery program in {options.build_dir}", file=sys.stderr)
        return 2
    out: Path = options.out
    out.mkdir(parents=True, exist_ok=True)
    sheets = write_sheets(out)
    entries: list[dict[str, object]] = []
    for capture in planned_captures(sheets):
        entry = run_capture(gallery, capture, out)
        entries.append(entry)
        status = f"{entry.get('bytes', 0)} bytes" if entry["ok"] else f"FAILED: {entry.get('error', '')}"
        print(f"capture_theme_looks: {capture.name}: {status}")
    manifest = {"gallery": str(gallery), "page": PAGE, "theme": THEME, "images": entries}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    failed = sum(1 for entry in entries if not entry["ok"])
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
