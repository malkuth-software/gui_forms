#!/usr/bin/env python3
"""Generates the System.Drawing facade's native bridge, DrawingBridge.g.cs.

``generated/facade-v1/System.Drawing.Common/DrawingBridge.g.cs`` is the
facade's one binding of the drawing ABI: it negotiates the gd_* table through
``gd_get_api_v0``, installs the raster service through the table's
``raster_initialize`` (gd 1.1; there is no ``gdr_initialize`` export), and
wraps the table's entries for the facade's Bitmap, Graphics, Pen, Brush, Font
and Region types.

Provenance: the retired Windows Forms facade generator
(``tools/facade_generator/Program.cs``, removed by 7c3e91fda with the 0.x C
ABI, owner ruling D5 a) emitted this file from a fixed template, its
``DrawingBridgeSource()``. The bridge outlived that generator; the 1.0 seal
(8eee05d42) edited the generated file by hand. This tool takes that template
back as ``tools/drawing_bridge/DrawingBridge.cs.in``, carrying the seal's
edits, with one substitution: ``@NATIVE_LIBRARY@``, the library both imports
load. Since the one-module decision (owner, 2026-11-15) the drawing ABI and its
raster service are part of the C ABI library, so the name is that library's
logical name, ``gui_forms_abi<GF_ABI_MAJOR>`` with the major read from
``include/gui_forms/c/gf_abi.h`` (bindings/csharp's NativeLibraryName maps the
same logical name to a file per platform). The facade is Windows-only, where
that name is the DLL's.

    python3 -B tools/drawing_bridge.py           writes the file
    python3 -B tools/drawing_bridge.py --check   fails when it is stale
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent
TEMPLATE: Path = ROOT / "tools" / "drawing_bridge" / "DrawingBridge.cs.in"
HEADER: Path = ROOT / "include" / "gui_forms" / "c" / "gf_abi.h"
OUTPUT: Path = ROOT / "generated" / "facade-v1" / "System.Drawing.Common" / "DrawingBridge.g.cs"
PLACEHOLDER: str = "@NATIVE_LIBRARY@"
# The import that names the library: gd_get_api_v0 (the raster is installed
# through the table's raster_initialize slot).
PLACEHOLDER_COUNT: int = 1
MAJOR_DEFINITION: re.Pattern[str] = re.compile(r"^#define GF_ABI_MAJOR (\d+)u?\s*$", re.MULTILINE)


class BridgeError(Exception):
    """The template or the header does not give a bridge."""


def abi_major(header_text: str) -> int:
    """GF_ABI_MAJOR as gf_abi.h defines it; BridgeError when it is not defined once."""
    matches: list[str] = MAJOR_DEFINITION.findall(header_text)
    if len(matches) != 1:
        raise BridgeError(f"gf_abi.h defines GF_ABI_MAJOR {len(matches)} times; expected once")
    return int(matches[0])


def native_library(major: int) -> str:
    """The logical name of the C ABI library for one ABI major."""
    return "gui_forms_abi" + str(major)


def render(template_text: str, library: str) -> str:
    """The bridge's text: the template with each placeholder replaced by `library`."""
    count: int = template_text.count(PLACEHOLDER)
    if count != PLACEHOLDER_COUNT:
        raise BridgeError(f"the template names {PLACEHOLDER} {count} times; expected {PLACEHOLDER_COUNT}")
    rendered: str = template_text.replace(PLACEHOLDER, library)
    leftover: re.Match[str] | None = re.search(r"@[A-Z_]+@", rendered)
    if leftover is not None:
        raise BridgeError("unfilled placeholder " + leftover.group(0))
    return rendered


def expected_bridge() -> str:
    """The bridge the checked-in template and header give."""
    template_text: str = TEMPLATE.read_text(encoding="utf-8")
    header_text: str = HEADER.read_text(encoding="utf-8")
    return render(template_text, native_library(abi_major(header_text)))


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail when the checked-in bridge is stale")
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        text: str = expected_bridge()
    except BridgeError as error:
        print("drawing_bridge: " + str(error), file=sys.stderr)
        return 1
    current: str = OUTPUT.read_text(encoding="utf-8") if OUTPUT.is_file() else ""
    if arguments.check:
        if current != text:
            print(f"drawing_bridge: {OUTPUT.relative_to(ROOT)} is stale; run python3 -B tools/drawing_bridge.py",
                  file=sys.stderr)
            return 1
        print("drawing_bridge: up to date")
        return 0
    if current != text:
        # newline="" keeps the template's line endings as they are.
        with OUTPUT.open("w", encoding="utf-8", newline="") as stream:
            stream.write(text)
        print("drawing_bridge: wrote " + str(OUTPUT.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
