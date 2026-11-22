#!/usr/bin/env python3
"""Tests for the preview capture driver (tools/capture_previews.py): palette
images where they fit with the same pixels, the 40 KiB budget enforced, the
folder laid out by commit and the pin written only when every image passes."""
from __future__ import annotations

import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_previews as capture  # noqa: E402

COMMIT = "0123456789abcdef0123456789abcdef01234567"


def rgba_png(width: int, height: int, pixel_at) -> bytes:
    rows = [b"".join(pixel_at(x, y) for x in range(width)) for y in range(height)]
    return capture.png(width, height, 6, 8, rows)


def palette_pixels(data: bytes) -> list[bytes]:
    """The RGBA rows of a palette PNG this tool wrote (filter type 0)."""
    found = dict(capture.chunks(data))
    width, height, depth, colour = struct.unpack(">IIBB", found[b"IHDR"][:10])
    assert colour == 3
    palette = found[b"PLTE"]
    alphas = found.get(b"tRNS", b"")
    raw = zlib.decompress(found[b"IDAT"])
    stride = (width * depth + 7) // 8
    rows = []
    for y in range(height):
        packed = raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)]
        row = b""
        for x in range(width):
            bit = x * depth
            index = (packed[bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
            alpha = alphas[index] if index < len(alphas) else 255
            row += palette[index * 3:index * 3 + 3] + bytes([alpha])
        rows.append(row)
    return rows


def few_colours(x: int, y: int) -> bytes:
    return bytes([200, 200, 200, 255]) if (x // 4 + y // 4) % 2 else bytes([20, 60, 120, 128])


def many_colours(x: int, y: int) -> bytes:
    return bytes([x, y, (x * y) & 0xFF, 255])


class PngTests(unittest.TestCase):
    def test_a_picture_of_few_colours_becomes_a_palette_image_with_the_same_pixels(self) -> None:
        original = rgba_png(30, 20, few_colours)
        result = capture.optimize(original)
        self.assertTrue(result.palette)
        self.assertLess(len(result.data), len(original))
        self.assertEqual(palette_pixels(result.data), capture.decode(original).rows)
        self.assertEqual((result.width, result.height), (30, 20))

    def test_a_picture_of_many_colours_stays_truecolour_with_the_same_pixels(self) -> None:
        original = rgba_png(64, 64, many_colours)
        result = capture.optimize(original)
        self.assertFalse(result.palette)
        self.assertEqual(capture.decode(result.data).rows, capture.decode(original).rows)

    def test_every_filter_type_is_read(self) -> None:
        image = capture.decode(rgba_png(9, 3, many_colours))
        rows = image.rows
        raw = bytearray()
        previous = bytes(len(rows[0]))
        for kind, row in zip((1, 2, 4), rows):
            raw.append(kind)
            for index, value in enumerate(row):
                left = row[index - 4] if index >= 4 else 0
                corner = previous[index - 4] if index >= 4 else 0
                predictor = {1: left, 2: previous[index], 4: capture.paeth(left, previous[index], corner)}[kind]
                raw.append((value - predictor) & 0xFF)
            previous = row
        header = struct.pack(">IIBBBBB", 9, 3, 8, 6, 0, 0, 0)
        data = (capture.SIGNATURE + capture.chunk(b"IHDR", header) + capture.chunk(b"IDAT", zlib.compress(bytes(raw)))
                + capture.chunk(b"IEND", b""))
        self.assertEqual(capture.decode(data).rows, rows)

    def test_damaged_or_unsupported_files_are_refused(self) -> None:
        good = rgba_png(2, 2, few_colours)
        with self.assertRaises(capture.PngError):
            capture.decode(good[:20] + b"x" + good[21:])
        with self.assertRaises(capture.PngError):
            capture.decode(b"GIF89a")


class RunTests(unittest.TestCase):
    def test_the_gallery_pages_are_read_from_their_specs(self) -> None:
        pages = capture.gallery_pages()
        self.assertIn("control-spectrum", pages)
        self.assertEqual(len(pages), len(set(pages)))
        self.assertGreaterEqual(len(pages), 37)

    def test_the_commands_capture_in_the_theme_at_scale_two(self) -> None:
        commands = capture.capture_commands(Path("/b/cap"), Path("/b/gal"), Path("/o/c"), ["basics"], ["button"])
        self.assertEqual(commands[0], ("controls", ["/b/cap", "--out", "/o/c/controls", "--scale", "2",
                                                    "--only", "button"]))
        self.assertEqual(commands[1][1], ["/b/gal", "--page", "basics", "--capture", "/o/c/pages/basics.png",
                                          "--headless", "--theme-base", "windows-professional", "--scheme", "light",
                                          "--scale", "2"])

    def run_capture(self, out: Path, sizes: dict[str, int], pin_path: Path) -> tuple[int, list[list[str]]]:
        ran: list[list[str]] = []

        def runner(command: Sequence[str]) -> subprocess.CompletedProcess:
            ran.append(list(command))
            if "--out" in command:
                folder = Path(command[command.index("--out") + 1])
                folder.mkdir(parents=True, exist_ok=True)
                for name, side in sizes.items():
                    (folder / f"{name}.png").write_bytes(rgba_png(side, side, many_colours))
            return subprocess.CompletedProcess(command, 0, "", "")

        original = capture.PIN
        capture.PIN = pin_path
        try:
            status = capture.main(["--build-dir", str(out), "--out", str(out), "--commit", COMMIT,
                                   "--capture-program", "/b/cap", "--no-pages", "--pin"], runner)
        finally:
            capture.PIN = original
        return status, ran

    def test_a_capture_within_budget_is_laid_out_by_commit_and_pinned(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out = Path(scratch)
            pin_path = out / "previews.json"
            status, ran = self.run_capture(out, {"button": 16, "label": 8}, pin_path)
            self.assertEqual(status, 0)
            self.assertEqual(ran[0][:3], ["/b/cap", "--out", str(out / COMMIT / "controls")])
            manifest = json.loads((out / COMMIT / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(sorted(manifest["images"]), ["controls/button", "controls/label"])
            self.assertEqual(manifest["theme"], "windows-professional")
            pinned = json.loads(pin_path.read_text(encoding="utf-8"))
            self.assertEqual(pinned["capture"], COMMIT)
            self.assertEqual(pinned["images"]["controls/button"]["width"], 16)

    def test_an_image_over_budget_fails_the_run_and_is_not_pinned(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out = Path(scratch)
            pin_path = out / "previews.json"
            # 256 x 256 pixels of distinct colours: well over 40 KiB as a PNG.
            status, _ = self.run_capture(out, {"plot_view": 256}, pin_path)
            self.assertEqual(status, 1)
            self.assertFalse(pin_path.exists())


if __name__ == "__main__":
    unittest.main()
