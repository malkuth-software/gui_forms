#!/usr/bin/env python3
"""Count the standard vocabulary at GUI.Forms' C++ surface (D15, stage V0).

D15 (owner, 2026-11-15) replaces std::vector, std::string and the standard
ownership pointers at the C++ surface with STXlib's owned vocabulary before
1.0; Q2 adds std::optional, std::string_view and std::span. The migration
plan (STXlib notes, planning/M2b-owned-vocabulary-migration.md) moves them
in stages V1 to V6, and V6 ends with every count at zero.

The STXlib shield (2026-11-22) adds the thread, lock, stop and file names:
GUI.Forms reaches the host's threads and files only through STXlib
(tools/std_rules.toml's std-threads, std-stop-token and std-files rules
hold the whole tree; this census holds the public surface).

The surface is what a C++ client compiles against: include/gui_forms/,
except include/gui_forms/c/ (the C ABI, which names no C++ type). A name is
counted where it is qualified (`std::optional`, `::std::span`) on code:
comments and literals are blanked first, with check_std_rules.py's reading
(tools/check_std_rules.py, strip_comments_and_literals), so the two checks
read a header the same way. An unqualified name (after a using-declaration)
is not counted; the house style spells types, and V6's check_std_rules
configuration for the surface checks the same qualified names.

The baseline (manifests/std_surface_census.json) holds each name's
occurrences and the number of headers naming it. It is a ratchet:
- `check` fails (exit 1) when any name's occurrences rise above the
  baseline, and also when one has fallen below it, so the baseline is
  lowered in the same fold that lowered the count (as check_std_rules'
  allowance reports a stale line);
- `update` rewrites the baseline from the tree, and refuses (exit 1) when
  that would raise a count;
- `scan [--json PATH]` prints the census.

Header counts are reported, not ratcheted: moving a name between headers
is not a regression.

Usage: python3 -I tools/std_surface_census.py check|update|scan
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType

ROOT: Path = Path(__file__).resolve().parents[1]
SURFACE: Path = ROOT / "include" / "gui_forms"
EXCLUDED: tuple[str, ...] = ("c",)
DEFAULT_BASELINE: Path = ROOT / "manifests" / "std_surface_census.json"
SOURCE_SUFFIXES: frozenset[str] = frozenset({".hpp", ".h", ".inl", ".ipp"})

# The D15 families, then Q2's. Each is matched as a qualified name ending at a
# word boundary, so `std::string` does not count `std::string_view`.
NAMES: tuple[str, ...] = (
    "string",
    "vector",
    "shared_ptr",
    "weak_ptr",
    "make_shared",
    "enable_shared_from_this",
    "static_pointer_cast",
    "dynamic_pointer_cast",
    "unique_ptr",
    "make_unique",
    "optional",
    "nullopt",
    "string_view",
    "span",
    # The STXlib shield (2026-11-22): the host's threads and files reach
    # GUI.Forms only through STXlib, so none of these names a public type.
    # What remains: std::stop_token in the public stop parameters (a
    # copyable stop token waits on STXlib) and std::filesystem (FilePath,
    # std::filesystem::path, the public path type until a decision).
    "thread",
    "jthread",
    "mutex",
    "recursive_mutex",
    "shared_mutex",
    "condition_variable",
    "unique_lock",
    "lock_guard",
    "scoped_lock",
    "this_thread",
    "stop_token",
    "stop_source",
    "stop_callback",
    "filesystem",
    "ifstream",
    "ofstream",
    "fstream",
)
QUALIFIED: re.Pattern[str] = re.compile(r"(?<![A-Za-z0-9_])(?:::\s*)?std\s*::\s*(" + "|".join(NAMES) +
                                        r")(?![A-Za-z0-9_])")


def load_std_rules() -> ModuleType:
    """check_std_rules.py, beside this file, imported by path (the tool runs
    with -I, so its directory is not on the import path)."""
    path: Path = Path(__file__).resolve().parent / "check_std_rules.py"
    specification = importlib.util.spec_from_file_location("check_std_rules", path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module: ModuleType = importlib.util.module_from_spec(specification)
    # Registered before it runs: its dataclasses look their module up in
    # sys.modules (Python 3.14 fails on a module that is not there).
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


STD_RULES: ModuleType = load_std_rules()


@dataclass
class NameCount:
    """One name's occurrences on the surface and the headers naming it."""
    occurrences: int
    headers: int


def surface_headers(surface: Path) -> list[Path]:
    """Every header under surface except the excluded subdirectories, sorted."""
    found: list[Path] = []
    for path in sorted(surface.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative: Path = path.relative_to(surface)
        if relative.parts and relative.parts[0] in EXCLUDED:
            continue
        found.append(path)
    return found


def count_text(text: str) -> dict[str, int]:
    """Each name's qualified occurrences in one header's code."""
    stripped: str = STD_RULES.strip_comments_and_literals(text)
    counts: dict[str, int] = {name: 0 for name in NAMES}
    for match in QUALIFIED.finditer(stripped):
        name: str = match.group(1)
        counts[name] = counts[name] + 1
    return counts


def census(surface: Path) -> dict[str, NameCount]:
    """The whole surface's census, one entry per name in NAMES order."""
    result: dict[str, NameCount] = {name: NameCount(0, 0) for name in NAMES}
    for path in surface_headers(surface):
        counts: dict[str, int] = count_text(path.read_text(encoding="utf-8"))
        for name in NAMES:
            found: int = counts[name]
            if found == 0:
                continue
            entry: NameCount = result[name]
            entry.occurrences = entry.occurrences + found
            entry.headers = entry.headers + 1
    return result


def census_json(counts: dict[str, NameCount]) -> dict[str, object]:
    names: dict[str, dict[str, int]] = {}
    for name in NAMES:
        entry: NameCount = counts[name]
        names[name] = {"occurrences": entry.occurrences, "headers": entry.headers}
    document: dict[str, object] = {
        "description": ("Qualified std:: names at GUI.Forms' C++ surface (include/gui_forms/ without c/), "
                        "tools/std_surface_census.py. D15 and Q2, and the STXlib shield's thread, lock, stop and file names: "
                        "every count only falls, and V6 ends at zero. "
                        "Occurrences are ratcheted; headers are reported."),
        "names": names,
    }
    return document


def read_baseline(path: Path) -> dict[str, int]:
    """The baseline's occurrences per name; a name it lacks reads as zero."""
    document = json.loads(path.read_text(encoding="utf-8"))
    names = document.get("names", {})
    result: dict[str, int] = {}
    for name in NAMES:
        entry = names.get(name, {})
        result[name] = int(entry.get("occurrences", 0))
    return result


@dataclass
class Judgement:
    risen: list[str]
    fallen: list[str]


def judge(counts: dict[str, NameCount], baseline: dict[str, int]) -> Judgement:
    risen: list[str] = []
    fallen: list[str] = []
    for name in NAMES:
        found: int = counts[name].occurrences
        allowed: int = baseline[name]
        if found > allowed:
            risen.append(f"std::{name}: {found} occurrences, baseline {allowed}")
        elif found < allowed:
            fallen.append(f"std::{name}: {found} occurrences, baseline {allowed}")
    judgement: Judgement = Judgement(risen, fallen)
    return judgement


def print_census(counts: dict[str, NameCount]) -> None:
    for name in NAMES:
        entry: NameCount = counts[name]
        print(f"std::{name}: {entry.occurrences} occurrences in {entry.headers} headers")


def command_check(surface: Path, baseline_path: Path) -> int:
    counts: dict[str, NameCount] = census(surface)
    judgement: Judgement = judge(counts, read_baseline(baseline_path))
    for line in judgement.risen:
        print(f"risen: {line}")
    for line in judgement.fallen:
        print(f"fallen: {line}: lower it (tools/std_surface_census.py update)")
    if judgement.risen or judgement.fallen:
        print("std_surface_census: failed", file=sys.stderr)
        return 1
    print("std_surface_census: matches the baseline", file=sys.stderr)
    return 0


def command_update(surface: Path, baseline_path: Path) -> int:
    counts: dict[str, NameCount] = census(surface)
    if baseline_path.exists():
        judgement: Judgement = judge(counts, read_baseline(baseline_path))
        if judgement.risen:
            for line in judgement.risen:
                print(f"risen: {line}")
            print("std_surface_census: refused, the census only falls", file=sys.stderr)
            return 1
    text: str = json.dumps(census_json(counts), indent=2) + "\n"
    baseline_path.write_text(text, encoding="utf-8")
    print_census(counts)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="The std:: census of GUI.Forms' C++ surface (D15).")
    parser.add_argument("command", choices=("check", "update", "scan"))
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--surface", type=Path, default=SURFACE)
    parser.add_argument("--json", type=Path, default=None, help="scan: also write the census as JSON")
    arguments = parser.parse_args(argv)
    if arguments.command == "check":
        return command_check(arguments.surface, arguments.baseline)
    if arguments.command == "update":
        return command_update(arguments.surface, arguments.baseline)
    counts: dict[str, NameCount] = census(arguments.surface)
    print_census(counts)
    if arguments.json is not None:
        arguments.json.write_text(json.dumps(census_json(counts), indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
