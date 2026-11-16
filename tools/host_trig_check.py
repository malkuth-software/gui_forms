#!/usr/bin/env python3
"""Refuse the host's trigonometry in the engine, controls and renderers.

The host libraries' sin, cos, tan, atan2 and hypot differ in their last
bits between macOS, glibc and the Windows runtime, so a pixel or a
control point computed with them can differ by machine. GUI.Forms' own
engine is the reference: src/core, src/controls and src/render call the
deterministic functions of include/gui_forms/engine/engine_math/
engine_math.hpp instead (engine_sine_cosine, engine_sine_cosine_degrees,
engine_tangent, engine_arctangent2, engine_hypotenuse).

A call is the name, optionally qualified by `std::` or `::`, with an
optional `f` or `l` suffix (the C forms), followed by `(`; a member
(`.sin(`), another namespace's function (`math::sin(`) and a longer
identifier (`engine_sine_cosine(`) are not calls. Comments, string and
character literals are skipped.

The allowance (manifests/host_trig_allowance.json) lists files that may
still call the host, with their counts. It only shrinks:
- `check` fails (exit 1) when a file has more calls than its allowance,
  or calls and no entry, and also when an entry is larger than the file's
  count (lower it with `update`), so a moved call cannot come back;
- `update` rewrites the allowance to the tree's counts, and refuses to
  raise or add an entry;
- `scan` prints the counts.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
DEFAULT_ALLOWANCE: Path = ROOT / "manifests" / "host_trig_allowance.json"
SCANNED: tuple[str, ...] = ("src/core", "src/controls", "src/render")
SOURCE_SUFFIXES: frozenset[str] = frozenset({".c", ".cpp", ".h", ".hpp", ".mm"})
DESCRIPTION: str = (
    "Files under src/core, src/controls and src/render that may still call the host's sin, cos, tan, "
    "atan2 or hypot (tools/host_trig_check.py), with their counts. Shrink only: new code calls "
    "include/gui_forms/engine/engine_math/engine_math.hpp."
)

# The name may follow `std::` or a bare `::`; nothing before it may be a
# word character, a member access or another scope's `::`.
CALL: re.Pattern[str] = re.compile(r"(?<![\w.>:])(?:(?:std)?::)?(?:sin|cos|tan|atan2|hypot)[fl]?\s*\(")


def code_of(text: str) -> str:
    """The text with comments and string and character literals blanked
    (newlines kept, so line numbers stay)."""
    output: list[str] = []
    index: int = 0
    length: int = len(text)
    while index < length:
        character: str = text[index]
        following: str = text[index + 1] if index + 1 < length else ""
        if character == "/" and following == "/":
            end: int = text.find("\n", index)
            index = length if end < 0 else end
            continue
        if character == "/" and following == "*":
            end = text.find("*/", index + 2)
            stop: int = length if end < 0 else end + 2
            output.append("\n" * text.count("\n", index, stop))
            index = stop
            continue
        if character in "\"'":
            quote: str = character
            index += 1
            while index < length and text[index] != quote and text[index] != "\n":
                index += 2 if text[index] == "\\" else 1
            index += 1
            output.append(" ")
            continue
        output.append(character)
        index += 1
    return "".join(output)


def count_calls(text: str) -> int:
    calls: int = len(CALL.findall(code_of(text)))
    return calls


def scan(root: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    for directory in SCANNED:
        base: Path = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            calls: int = count_calls(path.read_text(encoding="utf-8", errors="replace"))
            if calls > 0:
                counts[path.relative_to(root).as_posix()] = calls
    return counts


def read_allowance(path: Path) -> dict[str, int]:
    if not path.exists():
        return {}
    data: dict = json.loads(path.read_text(encoding="utf-8"))
    allowance: dict[str, int] = {str(name): int(value) for name, value in data.get("allowance", {}).items()}
    return allowance


def write_allowance(path: Path, allowance: dict[str, int]) -> None:
    data: dict = {"description": DESCRIPTION, "allowance": dict(sorted(allowance.items()))}
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def problems_of(counts: dict[str, int], allowance: dict[str, int]) -> list[str]:
    problems: list[str] = []
    for name in sorted(counts):
        allowed: int = allowance.get(name, 0)
        if counts[name] > allowed:
            problems.append(f"{name}: {counts[name]} host trigonometry calls, allowance {allowed}; "
                            "use engine_math.hpp")
    for name in sorted(allowance):
        found: int = counts.get(name, 0)
        if found < allowance[name]:
            problems.append(f"{name}: allowance {allowance[name]} but {found} calls; lower it with `update`")
    return problems


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("scan", "check", "update"))
    parser.add_argument("--allowance", type=Path, default=DEFAULT_ALLOWANCE)
    parser.add_argument("--root", type=Path, default=ROOT)
    options: argparse.Namespace = parser.parse_args(arguments)
    counts: dict[str, int] = scan(options.root)
    if options.command == "scan":
        for name in sorted(counts):
            print(f"{counts[name]:4d} {name}")
        print(f"host trigonometry: {sum(counts.values())} calls in {len(counts)} files")
        return 0
    allowance: dict[str, int] = read_allowance(options.allowance)
    if options.command == "update":
        raised: list[str] = [name for name in counts if counts[name] > allowance.get(name, 0)]
        if raised:
            for name in sorted(raised):
                print(f"{name}: {counts[name]} calls above allowance {allowance.get(name, 0)}; "
                      "the allowance only shrinks", file=sys.stderr)
            return 1
        write_allowance(options.allowance, counts)
        print(f"host trigonometry allowance: {sum(counts.values())} calls in {len(counts)} files")
        return 0
    problems: list[str] = problems_of(counts, allowance)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"host trigonometry: ok; {sum(counts.values())} allowed calls in {len(counts)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
