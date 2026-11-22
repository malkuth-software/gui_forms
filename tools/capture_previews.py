#!/usr/bin/env python3
"""Capture the documentation's previews, headless, into a folder by commit.

The owner's rules for every preview (decision 2026-11-21): captured headless,
never from a screen; the windows-professional theme, light; rendered at
scale 2 and shown at half size; PNG through GUI.Forms' own encoder, made a
palette image when it has 256 colours or fewer; at most 40 KiB each, which
this tool enforces; kept outside this repository, in the previews
repository, one folder per GUI.Forms commit.

    python3 tools/capture_previews.py --build-dir <build> --out <previews checkout> [--pin]

It runs two programs of a build of the commit:

- gui_forms_preview_capture (tools/preview_capture/) for one PNG per stock
  control, written as controls/<control id>.png;
- the Gallery, ``--page <id> --capture ... --headless --theme-base
  windows-professional --scheme light --scale 2``, for each page of
  demo/gallery_page_specs.hpp, written as pages/<page id>.png.

Then it re-encodes each image (palette where it fits, deflate level 9; the
smallest of the encoder's output and the re-encodings is kept), checks the
budget, and writes <out>/<commit>/manifest.json. Layout:

    <out>/<commit>/controls/<control id>.png
    <out>/<commit>/pages/<page id>.png
    <out>/<commit>/manifest.json

``--pin`` then writes docs/previews.json (the capture's commit and each
image's size), which docs/conf.py reads: pages show a preview only once the
pin lists it, from the address {{ gf_media }}. Commit the folder to the
previews repository first, then the pin here. Exit status: 0 every image
captured and within budget; 1 a capture failed or an image is over budget
(the pin is not written); 2 usage, or a program not found.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import zlib
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
BUDGET_BYTES: int = 40 * 1024
SCALE: str = "2"
THEME: str = "windows-professional"
PAGE_SPECS: Path = ROOT / "demo" / "gallery_page_specs.hpp"
PIN: Path = ROOT / "docs" / "previews.json"
SIGNATURE: bytes = b"\x89PNG\r\n\x1a\n"
CAPTURE_PROGRAMS: tuple[str, ...] = ("gui_forms_preview_capture", "gui_forms_preview_capture.exe")
GALLERY_PROGRAMS: tuple[str, ...] = (
    "GUI.Forms Gallery.app/Contents/MacOS/GUI.Forms Gallery",
    "gui_forms_gallery", "gui_forms_gallery.exe", "GUI.Forms Gallery.exe", "GUI.Forms Gallery",
)

Runner = Callable[[Sequence[str]], subprocess.CompletedProcess]


class PngError(ValueError):
    """A PNG this tool does not read: not 8-bit, interlaced, or damaged."""


@dataclass
class Image:
    """Decoded pixels: `rows` of `width` RGBA pixels, four bytes each."""
    width: int
    height: int
    rows: list[bytes]


# --- PNG: reading the encoder's output and writing it smaller -----------------------

def chunks(data: bytes) -> list[tuple[bytes, bytes]]:
    if not data.startswith(SIGNATURE):
        raise PngError("not a PNG")
    found: list[tuple[bytes, bytes]] = []
    at = len(SIGNATURE)
    while at + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if len(body) != length or zlib.crc32(kind + body) != struct.unpack(">I", data[at + 8 + length:at + 12 + length])[0]:
            raise PngError(f"damaged {kind!r} chunk")
        found.append((kind, body))
        at += 12 + length
        if kind == b"IEND":
            break
    return found


def paeth(left: int, above: int, corner: int) -> int:
    estimate = left + above - corner
    to_left, to_above, to_corner = abs(estimate - left), abs(estimate - above), abs(estimate - corner)
    if to_left <= to_above and to_left <= to_corner:
        return left
    return above if to_above <= to_corner else corner


def unfilter(raw: bytes, width: int, height: int, channels: int) -> list[bytes]:
    stride = width * channels
    rows: list[bytes] = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        kind = raw[at]
        row = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        if len(row) != stride:
            raise PngError("short image data")
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            corner = previous[index - channels] if index >= channels else 0
            if kind == 1:
                row[index] = (row[index] + left) & 0xFF
            elif kind == 2:
                row[index] = (row[index] + above) & 0xFF
            elif kind == 3:
                row[index] = (row[index] + ((left + above) >> 1)) & 0xFF
            elif kind == 4:
                row[index] = (row[index] + paeth(left, above, corner)) & 0xFF
            elif kind != 0:
                raise PngError(f"filter type {kind}")
        rows.append(bytes(row))
        previous = row
    return rows


def decode(data: bytes) -> Image:
    """An 8-bit truecolour (2) or truecolour-with-alpha (6) PNG, as RGBA rows."""
    found = chunks(data)
    if not found or found[0][0] != b"IHDR":
        raise PngError("no IHDR")
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", found[0][1])
    if depth != 8 or colour not in (2, 6) or interlace != 0:
        raise PngError(f"depth {depth}, colour type {colour}, interlace {interlace}")
    channels = 4 if colour == 6 else 3
    raw = zlib.decompress(b"".join(body for kind, body in found if kind == b"IDAT"))
    rows = unfilter(raw, width, height, channels)
    if channels == 3:
        rows = [b"".join(row[at:at + 3] + b"\xff" for at in range(0, len(row), 3)) for row in rows]
    return Image(width, height, rows)


def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def png(width: int, height: int, colour: int, depth: int, rows: list[bytes], extra: bytes = b"") -> bytes:
    """A PNG of already-packed rows, each with filter type 0 (palette and
    flat interface pictures compress best unfiltered), deflate level 9."""
    header = struct.pack(">IIBBBBB", width, height, depth, colour, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    # sRGB, perceptual: the same chunk the encoder writes.
    return (SIGNATURE + chunk(b"IHDR", header) + chunk(b"sRGB", b"\x00") + extra
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def palette_png(image: Image) -> bytes | None:
    """`image` as a palette PNG (colour type 3, with tRNS when a colour is not
    opaque, at the smallest bit depth its colours allow), or None when it has
    more than 256 colours."""
    colours: dict[bytes, int] = {}
    for row in image.rows:
        for at in range(0, len(row), 4):
            pixel = row[at:at + 4]
            if pixel not in colours:
                if len(colours) == 256:
                    return None
                colours[pixel] = len(colours)
    depth = 1 if len(colours) <= 2 else 2 if len(colours) <= 4 else 4 if len(colours) <= 16 else 8
    per_byte = 8 // depth
    packed: list[bytes] = []
    for row in image.rows:
        indices = [colours[row[at:at + 4]] for at in range(0, len(row), 4)]
        out = bytearray()
        for start in range(0, len(indices), per_byte):
            value = 0
            group = indices[start:start + per_byte]
            for index in group:
                value = (value << depth) | index
            value <<= depth * (per_byte - len(group))
            out.append(value)
        packed.append(bytes(out))
    ordered = sorted(colours, key=colours.__getitem__)
    extra = chunk(b"PLTE", b"".join(colour[:3] for colour in ordered))
    alphas = bytes(colour[3] for colour in ordered).rstrip(b"\xff")
    if alphas:
        extra += chunk(b"tRNS", alphas)
    return png(image.width, image.height, 3, depth, packed, extra)


def truecolour_png(image: Image) -> bytes:
    if all(row[at + 3] == 0xFF for row in image.rows for at in range(0, len(row), 4)):
        rgb = [b"".join(row[at:at + 3] for at in range(0, len(row), 4)) for row in image.rows]
        return png(image.width, image.height, 2, 8, rgb)
    return png(image.width, image.height, 6, 8, image.rows)


@dataclass
class Optimized:
    data: bytes
    width: int
    height: int
    palette: bool


def optimize(data: bytes) -> Optimized:
    """The smallest of the encoder's PNG, its palette form and its truecolour
    form at deflate level 9; the pixels are the same in each."""
    image = decode(data)
    candidates: list[tuple[bytes, bool]] = [(data, False), (truecolour_png(image), False)]
    palette = palette_png(image)
    if palette is not None:
        candidates.append((palette, True))
    smallest, is_palette = min(candidates, key=lambda candidate: len(candidate[0]))
    return Optimized(smallest, image.width, image.height, is_palette)


# --- The capture run ----------------------------------------------------------------

def gallery_pages(specs: Path = PAGE_SPECS) -> list[str]:
    """The Gallery's page ids, in its navigation order (demo/gallery_page_specs.hpp)."""
    text = specs.read_text(encoding="utf-8")
    return re.findall(r'^\s*\{"([a-z0-9][a-z0-9-]*)",\s*"', text, re.M)


def find_program(build_dir: Path, names: Sequence[str]) -> Path | None:
    for name in names:
        for candidate in (build_dir / name, *build_dir.glob(f"**/{name}")):
            if candidate.is_file():
                return candidate
    return None


def resolve_commit(runner: Runner) -> str:
    completed = runner(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    commit = completed.stdout.strip() if completed.returncode == 0 else ""
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise SystemExit("capture_previews: cannot read the commit; name it with --commit")
    return commit


def capture_commands(capture: Path, gallery: Path | None, folder: Path, pages: Sequence[str],
                     only: Sequence[str]) -> list[tuple[str, list[str]]]:
    """(what, command) for each program run, in order."""
    controls = [str(capture), "--out", str(folder / "controls"), "--scale", SCALE]
    for control_id in only:
        controls += ["--only", control_id]
    commands: list[tuple[str, list[str]]] = [("controls", controls)]
    if gallery is not None:
        for page in pages:
            commands.append((f"pages/{page}", [
                str(gallery), "--page", page, "--capture", str(folder / "pages" / f"{page}.png"), "--headless",
                "--theme-base", THEME, "--scheme", "light", "--scale", SCALE]))
    return commands


def finish(folder: Path, commit: str, budget: int) -> tuple[dict, list[str]]:
    """Re-encodes every PNG under `folder`, returning the manifest and the
    images over `budget` (bytes)."""
    images: dict[str, dict] = {}
    over: list[str] = []
    for path in sorted(folder.glob("*/*.png")):
        name = path.relative_to(folder).with_suffix("").as_posix()
        result = optimize(path.read_bytes())
        path.write_bytes(result.data)
        images[name] = {"width": result.width, "height": result.height, "bytes": len(result.data),
                        "palette": result.palette}
        if len(result.data) > budget:
            over.append(f"{name}: {len(result.data)} bytes")
    manifest = {"commit": commit, "theme": THEME, "scheme": "light", "scale": int(SCALE), "budget": budget,
                "images": images}
    return manifest, over


def pin(manifest: dict, path: Path) -> None:
    images = {name: {key: entry[key] for key in ("width", "height", "bytes")}
              for name, entry in sorted(manifest["images"].items())}
    path.write_text(json.dumps({"capture": manifest["commit"], "images": images}, indent=1) + "\n",
                    encoding="utf-8")


def run_subprocess(command: Sequence[str]) -> subprocess.CompletedProcess:
    return subprocess.run(list(command), capture_output=True, text=True, check=False)


def main(argv: list[str] | None = None, runner: Runner = run_subprocess) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True, help="a build of the commit to capture")
    parser.add_argument("--out", type=Path, required=True, help="the previews repository's checkout")
    parser.add_argument("--commit", help="the commit the build is of (default: this checkout's HEAD)")
    parser.add_argument("--capture-program", type=Path, help="gui_forms_preview_capture (default: found in the build)")
    parser.add_argument("--gallery", type=Path, help="the Gallery program (default: found in the build)")
    parser.add_argument("--no-pages", action="store_true", help="capture the controls only")
    parser.add_argument("--page", action="append", default=[], help="only this Gallery page (repeatable)")
    parser.add_argument("--only", action="append", default=[], help="only this control id (repeatable)")
    parser.add_argument("--budget", type=int, default=BUDGET_BYTES, help=argparse.SUPPRESS)
    parser.add_argument("--pin", action="store_true", help="write docs/previews.json when every image passes")
    arguments = parser.parse_args(argv)

    capture = arguments.capture_program or find_program(arguments.build_dir, CAPTURE_PROGRAMS)
    gallery = None if arguments.no_pages else arguments.gallery or find_program(arguments.build_dir, GALLERY_PROGRAMS)
    if capture is None or (gallery is None and not arguments.no_pages):
        print("capture_previews: gui_forms_preview_capture or the Gallery is not in the build "
              "(build the Gallery targets, or name them)", file=sys.stderr)
        return 2
    commit = arguments.commit or resolve_commit(runner)
    folder = arguments.out / commit
    (folder / "pages").mkdir(parents=True, exist_ok=True)
    pages = arguments.page or gallery_pages()
    failed: list[str] = []
    for what, command in capture_commands(capture, gallery, folder, pages, arguments.only):
        completed = runner(command)
        if completed.returncode != 0:
            failed.append(f"{what}: exit {completed.returncode}: {completed.stderr.strip()[-400:]}")
    manifest, over = finish(folder, commit, arguments.budget)
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    print(f"capture_previews: {len(manifest['images'])} images in {folder}, "
          f"{sum(entry['bytes'] for entry in manifest['images'].values())} bytes")
    for line in failed:
        print(f"capture_previews: failed {line}", file=sys.stderr)
    for line in over:
        print(f"capture_previews: over the {arguments.budget}-byte budget: {line}", file=sys.stderr)
    if failed or over:
        return 1
    if arguments.pin:
        pin(manifest, PIN)
        print(f"capture_previews: pinned {commit} in {PIN}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
