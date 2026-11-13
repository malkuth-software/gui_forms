#!/usr/bin/env python3
"""Flatten the control headers to include/gui_forms/controls/<name>.hpp (audit F2).

The control headers were nested by base chain
(include/gui_forms/controls/panel/card/card.hpp); the chain no longer exists
and the compatibility policy fixes the layout at 1.0. This tool moves every
tracked header under include/gui_forms/controls/ to that directory, under its
own file name, and rewrites every reference in the tree. There are no
forwarding headers and no aliases (the names rule): an old spelling stops
compiling.

    python3 -B tools/flatten_control_headers.py plan
        Prints the moves and fails when two headers share a file name or a
        destination already exists. Changes nothing.
    python3 -B tools/flatten_control_headers.py apply [--regenerate]
        Writes the mapping (tools/control_header_moves.json), moves each
        header with `git mv` so its history follows, rewrites the references
        and prints the counts. With --regenerate it then reruns the source
        generators whose outputs list headers in sorted order (the control
        registry's outputs, the ABI event catalog, the stock specimens, the
        property-family test and the API pages), so they are byte-current.
    python3 -B tools/flatten_control_headers.py check
        Fails when a tracked file still names a header in the mapping's old
        spelling (the mapping must exist).

A reference is any spelling `gui_forms/controls/<path>.hpp` whose path is a
moved header: an include line, a repository path (`include/` before it), a
relative include that reaches it through `include/`, a CMake list entry, a
generator's path table, a property-family config or a documentation page.
Spellings that name no moved header are left alone and listed. The warning
baseline (tools/warning_baseline.json) counts warnings per directory, so its
directories under include/gui_forms/controls/ fold into that directory, with
their counts summed.

The mapping file is what the product repins (workstream I6) replay: each
entry is a repository path before and after the move.

Standard library only; Python 3.12+. Run from any directory; the repository is
the parent of this file's directory.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

ROOT: Path = Path(__file__).resolve().parent.parent
CONTROLS_DIRECTORY: PurePosixPath = PurePosixPath("include/gui_forms/controls")
MAPPING_PATH: Path = ROOT / "tools" / "control_header_moves.json"
WARNING_BASELINE_PATH: Path = ROOT / "tools" / "warning_baseline.json"
# Tracked paths never rewritten: third-party sources and binary assets, and
# this tool, its test and its mapping, whose old spellings are data.
EXCLUDED_PREFIXES: tuple[str, ...] = ("third_party/", "assets/", "stx_skia/")
EXCLUDED_PATHS: frozenset[str] = frozenset({
    "tools/flatten_control_headers.py",
    "tools/test_flatten_control_headers.py",
    "tools/control_header_moves.json",
})
# Any spelling of a header path under gui_forms/controls/.
REFERENCE: re.Pattern[str] = re.compile(r"gui_forms/controls/[A-Za-z0-9_./-]+?\.hpp")
INCLUDE_LINE: re.Pattern[str] = re.compile(r"^\s*#\s*(include|import)\b")
# The generators rerun by `apply --regenerate`, in dependency order.
GENERATORS: tuple[tuple[str, ...], ...] = (
    ("tools/control_registry.py", "render"),
    ("tools/control_registry.py", "readme-table", "--write", "README.md"),
    ("tools/control_registry.py", "emit-cpp"),
    ("tools/control_registry.py", "emit-abi"),
    ("tools/control_registry.py", "emit-forms"),
    ("tools/abi_event_catalog.py",),
    ("tools/stock_specimens.py",),
    ("tools/property_codemod.py", "tests"),
    ("tools/generate_library_docs.py",),
)


@dataclass(frozen=True)
class Move:
    """One header's repository path before and after the move."""

    source: PurePosixPath
    destination: PurePosixPath


@dataclass
class RewriteCounts:
    """What a rewrite changed: files, include lines and other references."""

    files: int = 0
    include_lines: int = 0
    other_references: int = 0


def git(*arguments: str) -> str:
    """Runs git in the repository and returns its standard output."""
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ("git", *arguments), cwd=ROOT, check=True, capture_output=True, text=True)
    output: str = completed.stdout
    return output


def tracked_files() -> list[str]:
    """Every tracked path, repository-relative with forward slashes."""
    listing: str = git("ls-files", "-z")
    paths: list[str] = [path for path in listing.split("\0") if path]
    return paths


def plan_moves(paths: Sequence[str]) -> list[Move]:
    """The move of every nested control header, sorted by source.

    Raises ValueError when two headers share a file name, or a destination is
    already a tracked file that is not itself moving.
    """
    tracked: set[str] = set(paths)
    moves: list[Move] = []
    by_name: dict[str, PurePosixPath] = {}
    for path in sorted(paths):
        candidate: PurePosixPath = PurePosixPath(path)
        if candidate.suffix != ".hpp":
            continue
        if CONTROLS_DIRECTORY not in candidate.parents:
            continue
        if candidate.parent == CONTROLS_DIRECTORY:
            continue
        name: str = candidate.name
        if name in by_name:
            raise ValueError(f"two control headers are named {name}: {by_name[name]} and {candidate}")
        by_name[name] = candidate
        destination: PurePosixPath = CONTROLS_DIRECTORY / name
        if str(destination) in tracked:
            raise ValueError(f"{destination} already exists ({candidate} would replace it)")
        moves.append(Move(source=candidate, destination=destination))
    return moves


def spelling(path: PurePosixPath) -> str:
    """A header's include spelling: its repository path without `include/`."""
    text: str = str(path)
    result: str = text[len("include/"):]
    return result


def spelling_map(moves: Sequence[Move]) -> dict[str, str]:
    """Old include spelling to new include spelling, for every move."""
    result: dict[str, str] = {}
    for move in moves:
        result[spelling(move.source)] = spelling(move.destination)
    return result


def write_mapping(moves: Sequence[Move]) -> None:
    """Writes tools/control_header_moves.json: the moves in source order."""
    document: dict[str, object] = {
        "about": ("Control headers flattened to include/gui_forms/controls/<name>.hpp (audit F2), "
                  "written by tools/flatten_control_headers.py. Each entry is a repository path "
                  "before and after the move; an include spelling drops the leading include/. "
                  "No forwarding headers exist: the old spellings do not compile."),
        "moves": [{"from": str(move.source), "to": str(move.destination)} for move in moves],
    }
    text: str = json.dumps(document, indent=2) + "\n"
    MAPPING_PATH.write_text(text, encoding="utf-8")


def read_mapping() -> list[Move]:
    """The moves recorded in tools/control_header_moves.json."""
    document: Mapping[str, object] = json.loads(MAPPING_PATH.read_text(encoding="utf-8"))
    entries: object = document["moves"]
    if not isinstance(entries, list):
        raise ValueError(f"{MAPPING_PATH}: 'moves' must be a list")
    moves: list[Move] = []
    for entry in entries:
        moves.append(Move(source=PurePosixPath(entry["from"]), destination=PurePosixPath(entry["to"])))
    return moves


def move_headers(moves: Sequence[Move]) -> None:
    """Moves each header with git mv, so its history follows."""
    for move in moves:
        git("mv", str(move.source), str(move.destination))


def path_depth(path: Path) -> int:
    """The number of components in `path`: deeper directories sort first."""
    depth: int = len(path.parts)
    return depth


def remove_empty_directories() -> None:
    """Removes the directories under include/gui_forms/controls the moves emptied."""
    base: Path = ROOT / CONTROLS_DIRECTORY
    directories: list[Path] = [path for path in base.rglob("*") if path.is_dir()]
    directories.sort(key=path_depth, reverse=True)
    for directory in directories:
        if not any(directory.iterdir()):
            directory.rmdir()


def rewrite_text(text: str, spellings: Mapping[str, str], counts: RewriteCounts,
                 unmatched: set[str]) -> str:
    """The text with every moved header's old spelling replaced.

    Counts each changed include line once and each other replaced reference
    once; spellings that name no moved header go to `unmatched`.
    """
    lines: list[str] = text.split("\n")
    for index, line in enumerate(lines):
        if "gui_forms/controls/" not in line:
            continue
        replaced: int = 0
        pieces: list[str] = []
        position: int = 0
        for match in REFERENCE.finditer(line):
            found: str = match.group(0)
            pieces.append(line[position:match.start()])
            replacement: str | None = spellings.get(found)
            if replacement is None:
                if found not in spellings.values():
                    unmatched.add(found)
                pieces.append(found)
            else:
                pieces.append(replacement)
                replaced += 1
            position = match.end()
        pieces.append(line[position:])
        if replaced == 0:
            continue
        lines[index] = "".join(pieces)
        if INCLUDE_LINE.match(line):
            counts.include_lines += 1
        else:
            counts.other_references += replaced
    result: str = "\n".join(lines)
    return result


def rewritten_path(path: str) -> bool:
    """Whether the rewrite and the check read `path`."""
    excluded: bool = path.startswith(EXCLUDED_PREFIXES) or path in EXCLUDED_PATHS
    return not excluded


def rewrite_references(paths: Sequence[str], spellings: Mapping[str, str]) -> tuple[RewriteCounts, set[str]]:
    """Rewrites every tracked text file that names a moved header."""
    counts: RewriteCounts = RewriteCounts()
    unmatched: set[str] = set()
    for path in paths:
        if not rewritten_path(path):
            continue
        file: Path = ROOT / path
        if not file.is_file():
            continue
        try:
            text: str = file.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if "gui_forms/controls/" not in text:
            continue
        rewritten: str = rewrite_text(text, spellings, counts, unmatched)
        if rewritten != text:
            file.write_text(rewritten, encoding="utf-8")
            counts.files += 1
    return counts, unmatched


def fold_warning_baseline() -> int:
    """Folds the warning baseline's directories under the controls directory.

    Returns the number of directory keys folded away.
    """
    document: dict[str, object] = json.loads(WARNING_BASELINE_PATH.read_text(encoding="utf-8"))
    hosts: object = document["hosts"]
    if not isinstance(hosts, dict):
        raise ValueError(f"{WARNING_BASELINE_PATH}: 'hosts' must be an object")
    prefix: str = str(CONTROLS_DIRECTORY) + "/"
    folded: int = 0
    for host, counts in hosts.items():
        merged: dict[str, int] = {}
        for directory, count in counts.items():
            key: str = directory
            if directory.startswith(prefix):
                key = str(CONTROLS_DIRECTORY)
                folded += 1
            merged[key] = merged.get(key, 0) + count
        hosts[host] = dict(sorted(merged.items()))
    WARNING_BASELINE_PATH.write_text(json.dumps(document, indent=1) + "\n", encoding="utf-8")
    return folded


def regenerate() -> None:
    """Reruns the source generators so their sorted outputs are current."""
    for generator in GENERATORS:
        subprocess.run((sys.executable, "-B", *generator), cwd=ROOT, check=True, capture_output=True)


def remaining_references(paths: Sequence[str], spellings: Mapping[str, str]) -> list[str]:
    """Each `path:line` of a tracked file that still names an old spelling."""
    findings: list[str] = []
    for path in paths:
        if not rewritten_path(path):
            continue
        file: Path = ROOT / path
        if not file.is_file():
            continue
        try:
            text: str = file.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if "gui_forms/controls/" not in text:
            continue
        for number, line in enumerate(text.split("\n"), start=1):
            for match in REFERENCE.finditer(line):
                if match.group(0) in spellings:
                    findings.append(f"{path}:{number}: {match.group(0)}")
    return findings


def command_plan(arguments: argparse.Namespace) -> int:
    del arguments
    moves: list[Move] = plan_moves(tracked_files())
    for move in moves:
        print(f"{move.source} -> {move.destination}")
    print(f"flatten control headers: {len(moves)} moves")
    return 0


def command_apply(arguments: argparse.Namespace) -> int:
    moves: list[Move] = plan_moves(tracked_files())
    if not moves:
        print("flatten control headers: nothing to move")
        return 0
    write_mapping(moves)
    move_headers(moves)
    remove_empty_directories()
    spellings: dict[str, str] = spelling_map(moves)
    counts, unmatched = rewrite_references(tracked_files(), spellings)
    folded: int = fold_warning_baseline()
    if arguments.regenerate:
        regenerate()
    print(f"flatten control headers: {len(moves)} headers moved; {counts.include_lines} include lines and "
          f"{counts.other_references} other references rewritten in {counts.files} files; "
          f"{folded} warning-baseline directories folded")
    for found in sorted(unmatched):
        print(f"not a moved header, left alone: {found}")
    return 0


def command_check(arguments: argparse.Namespace) -> int:
    del arguments
    spellings: dict[str, str] = spelling_map(read_mapping())
    findings: list[str] = remaining_references(tracked_files(), spellings)
    for finding in findings:
        print(finding)
    if findings:
        print(f"flatten control headers: {len(findings)} references to old spellings")
        return 1
    print("flatten control headers: no old spelling remains")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    commands = parser.add_subparsers(dest="command", required=True)
    plan: argparse.ArgumentParser = commands.add_parser("plan", help="print the moves; change nothing")
    plan.set_defaults(run=command_plan)
    apply: argparse.ArgumentParser = commands.add_parser("apply", help="move the headers and rewrite references")
    apply.add_argument("--regenerate", action="store_true", help="rerun the source generators afterwards")
    apply.set_defaults(run=command_apply)
    check: argparse.ArgumentParser = commands.add_parser("check", help="fail on any remaining old spelling")
    check.set_defaults(run=command_check)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments: argparse.Namespace = build_parser().parse_args(argv)
    try:
        status: int = arguments.run(arguments)
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"flatten control headers: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
