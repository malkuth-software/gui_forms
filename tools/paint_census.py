#!/usr/bin/env python3
"""Count the control code that paints without the theme engine (stage T4).

Stage T4's gate is that the Gallery renders every stock control with no
control-specific paint code: every part goes through `paint_part`, which
hands it to the theme's engine. This census finds the code that does not:
calls under src/controls/ to a `Painter` drawing method, or to
`paint_surface_material` or `paint_part_from_recipe`, which draw a recipe
without the engine's rules.

The drawing methods are read from the `Painter` class itself
(include/gui_forms/types/painter/painter.hpp): every virtual member but the
state ones (save, restore, translate, clipping, groups, framebuffers and
measurement), so a new drawing method is counted without editing this file.
A call is `.name(` on a code line; comments are skipped. Definitions (an
override in a Painter subclass) are not calls and are not counted.

Each file is classed by its path:
- `demo`: the Gallery's own code (src/controls/gallery*), which is not a
  stock control;
- `exempt`: a control that draws the application's content, listed in the
  baseline's `exempt` table with the reason (an owner decision, T4-4);
- `stock`: everything else. The gate is zero stock calls.

Commands:
- `scan [--json PATH]` prints the census (one line per file with calls,
  then the totals) and optionally writes it as JSON.
- `check --baseline PATH` fails (exit 1) when any file has more calls than
  its baseline entry, or calls and no entry: the census only falls. It
  names the files that fell, so the baseline can be lowered with `update`.
- `update --baseline PATH` rewrites the baseline from the tree, keeping its
  `exempt` table.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
PAINTER_HEADER: Path = ROOT / "include" / "gui_forms" / "types" / "painter" / "painter.hpp"
CONTROLS: Path = ROOT / "src" / "controls"
DEFAULT_BASELINE: Path = ROOT / "manifests" / "paint_census.json"

# Painter members that set state or measure rather than draw.
STATE_METHODS: frozenset[str] = frozenset({
    "save", "restore", "translate", "clip_rect", "clip_rounded_rect",
    "begin_group", "end_group", "create_framebuffer", "measure_text_utf8",
})
# Free functions that draw a material or recipe without the engine's rules.
RECIPE_FUNCTIONS: tuple[str, ...] = ("paint_surface_material", "paint_part_from_recipe")
SOURCE_SUFFIXES: frozenset[str] = frozenset({".cpp", ".hpp", ".mm"})
VIRTUAL_METHOD: re.Pattern[str] = re.compile(r"\bvirtual\s+[^;{(]*?\b([a-z_][a-z0-9_]*)\s*\(")


@dataclass
class FileCensus:
    """One file's direct drawing calls, by callee name."""
    path: str
    category: str
    calls: dict[str, int] = field(default_factory=dict)

    def total(self) -> int:
        count = 0
        for value in self.calls.values():
            count += value
        return count


def painter_drawing_methods(header: Path) -> list[str]:
    """The Painter's drawing methods: its virtual members less the state ones."""
    text = header.read_text(encoding="utf-8")
    start = text.find("class Painter ")
    if start < 0:
        raise SystemExit(f"paint_census: no `class Painter` in {header}")
    names: set[str] = set()
    for match in VIRTUAL_METHOD.finditer(text, start):
        name = match.group(1)
        if name not in STATE_METHODS:
            names.add(name)
    if not names:
        raise SystemExit(f"paint_census: no drawing methods found in {header}")
    return sorted(names)


def call_pattern(methods: list[str]) -> re.Pattern[str]:
    """A member call `.name(` of a drawing method, or a call of a recipe function."""
    members = "|".join(re.escape(name) for name in methods)
    functions = "|".join(re.escape(name) for name in RECIPE_FUNCTIONS)
    return re.compile(rf"\.\s*({members})\s*\(|(?<![\w.])({functions})\s*\(")


def strip_comment(line: str) -> str:
    """The code before a `//` comment. String literals holding `//` are rare
    enough in paint code that a literal match is accepted."""
    position = line.find("//")
    if position < 0:
        return line
    return line[:position]


def classify(relative: str, exempt: dict[str, str]) -> str:
    if relative in exempt:
        return "exempt"
    first = relative.split("/", 1)[0]
    if first == "gallery" or first.startswith("gallery_"):
        return "demo"
    return "stock"


def scan(controls: Path, methods: list[str], exempt: dict[str, str]) -> list[FileCensus]:
    """Every file under `controls` with at least one direct drawing call."""
    pattern = call_pattern(methods)
    result: list[FileCensus] = []
    for path in sorted(controls.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        relative = path.relative_to(controls).as_posix()
        census = FileCensus(path=relative, category=classify(relative, exempt))
        in_block_comment = False
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw
            if in_block_comment:
                end = line.find("*/")
                if end < 0:
                    continue
                line = line[end + 2:]
                in_block_comment = False
            opening = line.find("/*")
            if opening >= 0 and line.find("*/", opening) < 0:
                in_block_comment = True
                line = line[:opening]
            code = strip_comment(line)
            for match in pattern.finditer(code):
                name = match.group(1) or match.group(2)
                census.calls[name] = census.calls.get(name, 0) + 1
        if census.calls:
            result.append(census)
    return result


def totals(census: list[FileCensus]) -> dict[str, dict[str, int]]:
    """Files and calls per category."""
    summary: dict[str, dict[str, int]] = {}
    for entry in census:
        bucket = summary.setdefault(entry.category, {"files": 0, "calls": 0})
        bucket["files"] += 1
        bucket["calls"] += entry.total()
    return summary


def load_baseline(path: Path) -> dict:
    if not path.is_file():
        return {"exempt": {}, "files": {}}
    data = json.loads(path.read_text(encoding="utf-8"))
    data.setdefault("exempt", {})
    data.setdefault("files", {})
    return data


def baseline_document(census: list[FileCensus], exempt: dict[str, str], methods: list[str]) -> dict:
    files: dict[str, dict] = {}
    for entry in census:
        files[entry.path] = {"category": entry.category, "calls": entry.total()}
    document = {
        "description": "Direct drawing calls under src/controls/ (tools/paint_census.py); "
                       "the stage T4 gate is zero stock calls. The census may only fall.",
        "drawing_methods": methods,
        "exempt": dict(sorted(exempt.items())),
        "totals": totals(census),
        "files": files,
    }
    return document


def check(census: list[FileCensus], baseline: dict) -> tuple[list[str], list[str]]:
    """(failures, fallen): files above their baseline, and files below it."""
    failures: list[str] = []
    fallen: list[str] = []
    recorded: dict[str, dict] = baseline["files"]
    seen: set[str] = set()
    for entry in census:
        seen.add(entry.path)
        allowed = 0
        if entry.path in recorded:
            allowed = int(recorded[entry.path]["calls"])
        if entry.total() > allowed:
            failures.append(f"{entry.path}: {entry.total()} direct drawing calls, baseline {allowed}")
        elif entry.total() < allowed:
            fallen.append(f"{entry.path}: {entry.total()} (baseline {allowed})")
    for path, record in sorted(recorded.items()):
        if path not in seen and int(record["calls"]) > 0:
            fallen.append(f"{path}: 0 (baseline {record['calls']})")
    return failures, fallen


def print_census(census: list[FileCensus]) -> None:
    for entry in census:
        names = ", ".join(f"{name} {count}" for name, count in sorted(entry.calls.items()))
        print(f"{entry.category:6} {entry.total():4}  {entry.path}  ({names})")
    for category, bucket in sorted(totals(census).items()):
        print(f"total {category}: {bucket['files']} files, {bucket['calls']} calls")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    scan_command = commands.add_parser("scan")
    scan_command.add_argument("--json", type=Path)
    scan_command.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    check_command = commands.add_parser("check")
    check_command.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    update_command = commands.add_parser("update")
    update_command.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    arguments = parser.parse_args(argv)

    baseline = load_baseline(arguments.baseline)
    exempt: dict[str, str] = baseline["exempt"]
    methods = painter_drawing_methods(PAINTER_HEADER)
    census = scan(CONTROLS, methods, exempt)
    if arguments.command == "scan":
        print_census(census)
        if arguments.json is not None:
            document = baseline_document(census, exempt, methods)
            arguments.json.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        return 0
    if arguments.command == "update":
        document = baseline_document(census, exempt, methods)
        arguments.baseline.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        for category, bucket in sorted(document["totals"].items()):
            print(f"total {category}: {bucket['files']} files, {bucket['calls']} calls")
        return 0
    failures, fallen = check(census, baseline)
    for line in failures:
        print(f"paint census: {line}")
    for line in fallen:
        print(f"paint census: fell: {line} (lower the baseline with `update`)")
    stock = totals(census).get("stock", {"files": 0, "calls": 0})
    print(f"paint census: {'FAILED' if failures else 'ok'}; stock {stock['files']} files, {stock['calls']} calls")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
