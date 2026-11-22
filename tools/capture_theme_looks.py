#!/usr/bin/env python3
"""Capture the industrial-surfaces page, headless, for the owner to see.

The page is rebuilt around the owner's process-control dialog (owner,
2026-11-22: "your lamps are not my lamps"; the industrial-surfaces study,
notes:experiments/industrial-surfaces/demo.html): the blasted enamel, the
jewel status lamps and the CANCEL and OK keys lit from the shipped bakes,
with the industrial theme's preview (the lamp radio choice and the coolant
flow pinpoint) and a carbon fibre swatch. This tool runs a built Gallery
headless on that page in the windows-professional theme, with the
industrial theme on the preview group (``--check gallery.industrial.theme``),
and writes PNGs:

    industrial-surfaces-light-1x.png, industrial-surfaces-light-2x.png
    industrial-surfaces-dark-2x.png
    industrial-surfaces-carbon-2x.png        the carbon fibre swatch at the
                                             top of the page's viewport
    industrial-surfaces-carbon-32px-2x.png   the same swatch at a 32px tile,
                                             through a user sheet that
                                             retiles that swatch alone
    industrial-surfaces-beside-study.png     the 2x page beside the study's
                                             reference render of the dialog

Every capture is at scale 1 or 2, where the dialog's keys are drawn at
most at their bakes' size (240 x 105 device pixels) and the glint paints
from world tiles, so no part passes a bake budget. The last image needs
the study's reference render (--reference, by default the notes
repository's dialog_demo_idle.png, 1000 x 620: the dialog at scale 2); it
is skipped, with a note in the manifest, when that file is missing or is
not an 8-bit PNG this tool reads.

The process-control demonstrator (gui_forms_process_control, owner,
2026-11-22: the same dialog as a program of its own, its keys drawn by the
light-map primitive) is captured too, headless at its own size:

    process-control-1x.png, process-control-2x.png
    process-control-beside-study.png         its 2x window beside the
                                             study's render

skipped, with a note in the manifest, when the build holds no
demonstrator. Then manifest.json lists each image, its command and its
size.

    python3 tools/capture_theme_looks.py --build-dir <build> [--out <folder>] [--reference <png>]

The default folder is ~/Developer/work/theme/captures. Exit status: 0 every
image captured; 1 a capture failed (the rest are still made); 2 usage, or
the Gallery not found in the build.
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

PAGE: str = "industrial-surfaces"
THEME: str = "windows-professional"
INDUSTRIAL_THEME_CHECK: str = "gallery.industrial.theme"
CARBON_GROUP: str = "gallery.industrial.carbon"
TIMEOUT_SECONDS: int = 180
SIGNATURE: bytes = b"\x89PNG\r\n\x1a\n"
DEFAULT_OUT: Path = Path.home() / "Developer" / "work" / "theme" / "captures"
DEFAULT_REFERENCE: Path = (Path.home() / "Developer" / "work" / "coordinator" / "notes" / "experiments" /
                           "industrial-surfaces" / "dialog_demo_idle.png")
BESIDE_NAME: str = f"{PAGE}-beside-study.png"
BESIDE_SOURCE: str = f"{PAGE}-light-2x.png"
BESIDE_GAP: int = 16
BESIDE_GROUND: tuple[int, int, int] = (30, 30, 30)
GALLERY_PROGRAMS: tuple[str, ...] = (
    "GUI.Forms Gallery.app/Contents/MacOS/GUI.Forms Gallery",
    "gui_forms_gallery", "gui_forms_gallery.exe", "GUI.Forms Gallery.exe", "GUI.Forms Gallery",
)
DEMONSTRATOR: str = "process-control"
DEMONSTRATOR_BESIDE_NAME: str = f"{DEMONSTRATOR}-beside-study.png"
DEMONSTRATOR_BESIDE_SOURCE: str = f"{DEMONSTRATOR}-2x.png"
DEMONSTRATOR_PROGRAMS: tuple[str, ...] = (
    "GUI.Forms Process Control.app/Contents/MacOS/GUI.Forms Process Control",
    "gui-forms-process-control", "GUI.Forms Process Control.exe", "GUI.Forms Process Control",
)

# A user sheet that retiles the page's carbon fibre swatch alone, over the
# Gallery's own declaration (`!important`: a user sheet's ordinary
# declarations yield to the program's). The colours are the Gallery
# theme's resin and sheen.
CARBON_SHEET: str = (
    "/* Written by tools/capture_theme_looks.py: the carbon fibre swatch at another tile. */\n"
    ".pattern-carbon-fibre::part(face) {{ background: pattern(carbon-fibre, #1c1d20, #6a6e75, {tile}) !important; }}\n"
)


@dataclass
class Capture:
    """One image: its file name, the scale and scheme, staging and an optional sheet."""
    name: str
    scale: str
    scheme: str
    reveal: str = ""
    sheet: Path | None = None
    command: list[str] = field(default_factory=list)


def find_program(build_dir: Path, programs: tuple[str, ...]) -> Path | None:
    """The first of `programs` in `build_dir`, searched to any depth."""
    for relative in programs:
        direct = build_dir / relative
        if direct.is_file():
            return direct
    leaf_names = {Path(relative).name for relative in programs}
    for candidate in sorted(build_dir.rglob("*")):
        if candidate.name in leaf_names and candidate.is_file():
            return candidate
    return None


def write_carbon_sheet(out: Path) -> Path:
    """Writes the 32px carbon sheet into <out>/sheets and returns it."""
    folder = out / "sheets"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "carbon-32px.css"
    path.write_text(CARBON_SHEET.format(tile="32px"), encoding="utf-8")
    return path


def planned_captures(carbon_sheet: Path) -> list[Capture]:
    """Every image the Gallery makes, in order."""
    captures: list[Capture] = [
        Capture(name=f"{PAGE}-light-1x.png", scale="1", scheme="light"),
        Capture(name=BESIDE_SOURCE, scale="2", scheme="light"),
        Capture(name=f"{PAGE}-dark-2x.png", scale="2", scheme="dark"),
        Capture(name=f"{PAGE}-carbon-2x.png", scale="2", scheme="light", reveal=CARBON_GROUP),
        Capture(name=f"{PAGE}-carbon-32px-2x.png", scale="2", scheme="light", reveal=CARBON_GROUP,
                sheet=carbon_sheet),
    ]
    return captures


def gallery_command(gallery: Path, capture: Capture, image: Path) -> list[str]:
    """The Gallery's arguments for `capture`, writing `image`."""
    command: list[str] = [
        str(gallery), "--page", PAGE, "--capture", str(image), "--headless",
        "--theme-base", THEME, "--scheme", capture.scheme, "--scale", capture.scale,
        "--check", INDUSTRIAL_THEME_CHECK,
    ]
    if capture.reveal:
        command.extend(["--reveal", capture.reveal])
    if capture.sheet is not None:
        command.extend(["--theme", str(capture.sheet)])
    return command


def find_gallery(build_dir: Path) -> Path | None:
    """The Gallery program in `build_dir`, searched to any depth."""
    return find_program(build_dir, GALLERY_PROGRAMS)


def demonstrator_command(program: Path, capture: Capture, image: Path) -> list[str]:
    """The demonstrator's arguments for `capture`, writing `image`."""
    command: list[str] = [str(program), "--capture", str(image), "--headless", "--scale", capture.scale]
    return command


def demonstrator_captures() -> list[Capture]:
    """Every image the demonstrator makes, in order."""
    captures: list[Capture] = [
        Capture(name=f"{DEMONSTRATOR}-1x.png", scale="1", scheme="light"),
        Capture(name=DEMONSTRATOR_BESIDE_SOURCE, scale="2", scheme="light"),
    ]
    return captures


def run_capture(gallery: Path, capture: Capture, out: Path, demonstrator: bool = False) -> dict[str, object]:
    """Runs one capture; its manifest entry, with `ok` false on any failure."""
    image = out / capture.name
    if image.exists():
        image.unlink()
    capture.command = (demonstrator_command(gallery, capture, image) if demonstrator
                       else gallery_command(gallery, capture, image))
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


# -- the side by side ---------------------------------------------------------------
#
# A minimal PNG reader and writer, so that the tool needs nothing beyond the
# standard library: 8-bit greyscale, RGB, grey with alpha and RGBA, not
# interlaced, read into RGB rows (alpha over the ground).


@dataclass
class Picture:
    """An RGB image: its width, height and rows of width * 3 bytes."""
    width: int
    height: int
    rows: list[bytearray]


def paeth(left: int, up: int, corner: int) -> int:
    estimate = left + up - corner
    to_left = abs(estimate - left)
    to_up = abs(estimate - up)
    to_corner = abs(estimate - corner)
    if to_left <= to_up and to_left <= to_corner:
        return left
    if to_up <= to_corner:
        return up
    return corner


def unfilter(data: bytes, width: int, height: int, channels: int) -> list[bytearray] | None:
    """The scanlines of `data` with their filters undone, or None when short."""
    stride = width * channels
    rows: list[bytearray] = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        if at + 1 + stride > len(data):
            return None
        kind = data[at]
        line = bytearray(data[at + 1:at + 1 + stride])
        at += 1 + stride
        for index in range(stride):
            left = line[index - channels] if index >= channels else 0
            up = previous[index]
            corner = previous[index - channels] if index >= channels else 0
            if kind == 1:
                line[index] = (line[index] + left) & 0xFF
            elif kind == 2:
                line[index] = (line[index] + up) & 0xFF
            elif kind == 3:
                line[index] = (line[index] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                line[index] = (line[index] + paeth(left, up, corner)) & 0xFF
            elif kind != 0:
                return None
        rows.append(line)
        previous = line
    return rows


def read_png(path: Path) -> Picture | None:
    """`path` as RGB, or None when it is not a PNG this reader takes."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if not data.startswith(SIGNATURE):
        return None
    at = len(SIGNATURE)
    header: tuple[int, ...] | None = None
    compressed = bytearray()
    while at + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            break
    if header is None:
        return None
    width, height, depth, colour, _, _, interlace = header
    channels_of = {0: 1, 2: 3, 4: 2, 6: 4}
    if depth != 8 or interlace != 0 or colour not in channels_of:
        return None
    channels = channels_of[colour]
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error:
        return None
    lines = unfilter(raw, width, height, channels)
    if lines is None:
        return None
    rows: list[bytearray] = []
    for line in lines:
        row = bytearray(width * 3)
        for x in range(width):
            pixel = line[x * channels:(x + 1) * channels]
            grey = channels <= 2
            rgb = (pixel[0], pixel[0], pixel[0]) if grey else (pixel[0], pixel[1], pixel[2])
            alpha = pixel[channels - 1] if channels in (2, 4) else 255
            for channel in range(3):
                ground = BESIDE_GROUND[channel]
                row[x * 3 + channel] = (rgb[channel] * alpha + ground * (255 - alpha) + 127) // 255
        rows.append(row)
    return Picture(width=width, height=height, rows=rows)


def write_png(path: Path, picture: Picture) -> None:
    """`picture` as an RGB PNG at `path`."""
    raw = bytearray()
    for row in picture.rows:
        raw.append(0)
        raw.extend(row)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", picture.width, picture.height, 8, 2, 0, 0, 0)
    path.write_bytes(SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
                     chunk(b"IEND", b""))


def beside(left: Picture, right: Picture) -> Picture:
    """`left` and `right` side by side, top-aligned, BESIDE_GAP apart."""
    width = left.width + BESIDE_GAP + right.width
    height = max(left.height, right.height)
    ground = bytes(BESIDE_GROUND)
    rows: list[bytearray] = []
    for y in range(height):
        row = bytearray(ground * width)
        if y < left.height:
            row[0:left.width * 3] = left.rows[y]
        if y < right.height:
            start = (left.width + BESIDE_GAP) * 3
            row[start:start + right.width * 3] = right.rows[y]
        rows.append(row)
    return Picture(width=width, height=height, rows=rows)


def make_beside(out: Path, reference: Path, name: str = BESIDE_NAME,
                source: str = BESIDE_SOURCE) -> dict[str, object]:
    """The 2x capture `source` beside the study's render, as `name`; its manifest entry."""
    entry: dict[str, object] = {"image": name, "reference": str(reference), "ok": False}
    page = read_png(out / source)
    if page is None:
        entry["error"] = f"{source} is missing or not a PNG this tool reads"
        return entry
    study = read_png(reference)
    if study is None:
        entry["skipped"] = True
        entry["error"] = "the study's reference render is missing or not a PNG this tool reads"
        return entry
    image = out / name
    write_png(image, beside(page, study))
    entry["bytes"] = image.stat().st_size
    entry["ok"] = True
    return entry


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a build of this commit, holding the Gallery")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"where the images go (default {DEFAULT_OUT})")
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE,
                        help=f"the study's render of the dialog at scale 2 (default {DEFAULT_REFERENCE})")
    options = parser.parse_args(arguments)
    gallery = find_gallery(options.build_dir)
    if gallery is None:
        print(f"capture_theme_looks: no Gallery program in {options.build_dir}", file=sys.stderr)
        return 2
    out: Path = options.out
    out.mkdir(parents=True, exist_ok=True)
    carbon_sheet = write_carbon_sheet(out)
    entries: list[dict[str, object]] = []
    for capture in planned_captures(carbon_sheet):
        entry = run_capture(gallery, capture, out)
        entries.append(entry)
        status = f"{entry.get('bytes', 0)} bytes" if entry["ok"] else f"FAILED: {entry.get('error', '')}"
        print(f"capture_theme_looks: {capture.name}: {status}")
    side = make_beside(out, options.reference)
    entries.append(side)
    side_status = f"{side.get('bytes', 0)} bytes" if side["ok"] else f"not made: {side.get('error', '')}"
    print(f"capture_theme_looks: {BESIDE_NAME}: {side_status}")
    demonstrator = find_program(options.build_dir, DEMONSTRATOR_PROGRAMS)
    if demonstrator is None:
        entries.append({"image": f"{DEMONSTRATOR}-*.png", "ok": False, "skipped": True,
                        "error": f"no process-control demonstrator in {options.build_dir}"})
        print("capture_theme_looks: process-control: skipped, no demonstrator in the build")
    else:
        for capture in demonstrator_captures():
            entry = run_capture(demonstrator, capture, out, demonstrator=True)
            entries.append(entry)
            status = f"{entry.get('bytes', 0)} bytes" if entry["ok"] else f"FAILED: {entry.get('error', '')}"
            print(f"capture_theme_looks: {capture.name}: {status}")
        demonstrator_side = make_beside(out, options.reference, DEMONSTRATOR_BESIDE_NAME, DEMONSTRATOR_BESIDE_SOURCE)
        entries.append(demonstrator_side)
        print(f"capture_theme_looks: {DEMONSTRATOR_BESIDE_NAME}: "
              f"{'made' if demonstrator_side['ok'] else 'not made: ' + str(demonstrator_side.get('error', ''))}")
    manifest = {"gallery": str(gallery), "demonstrator": str(demonstrator) if demonstrator else "",
                "page": PAGE, "theme": THEME, "images": entries}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    failed = sum(1 for entry in entries if not entry["ok"] and not entry.get("skipped", False))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
