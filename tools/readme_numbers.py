#!/usr/bin/env python3
"""Write, or check, the numbers in the GitHub README from docs/conf.py's table.

The site reads every version and ABI number at build time (docs/conf.py,
read_numbers); the README cannot run code, so its numbers are written here
and checked in CI (the house-style job), and cannot drift from the source.

    python3 tools/readme_numbers.py           # rewrite README.md in place
    python3 tools/readme_numbers.py --check   # exit 1 when README.md is stale

What it owns in README.md:

- every region between ``<!-- numbers:start -->`` and ``<!-- numbers:end -->``
  (at least one): within them, the number after "package version is",
  "C ABI", "host protocol", "drawing ABI" (or "drawing (`gd_*`) ABI") and
  "find_package(GUIForms" is written from the table (gf_version, gf_c_abi,
  gf_host_protocol, gf_drawing_abi). The regions together must name the
  package version and the C ABI. Other text in them is left alone;
- the platform table (the one under the "| Platform | Host | Notes |"
  header), from docs/conf.py's PLATFORMS and the minimum macOS and Windows
  versions CMake builds for;
- the Platforms badge, naming the same platforms.

The same phrases with a number outside a region ("C ABI 1.3" in a new
paragraph) are refused, so a sentence a writer adds cannot drift either.
Exit status: 0 current (or rewritten), 1 stale under --check,
2 a missing or broken marker, or a typed number.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from collections.abc import Mapping
from pathlib import Path
from types import ModuleType
from urllib.parse import quote

ROOT: Path = Path(__file__).resolve().parents[1]
README: Path = ROOT / "README.md"
REGION = re.compile(r"(<!-- numbers:start -->)(.*?)(<!-- numbers:end -->)", re.S)
# Each phrase a region may carry a number in: (pattern, table name). The
# pattern's first group is kept, its second is the number.
PHRASES: tuple[tuple[re.Pattern[str], str], ...] = (
    (re.compile(r"(\bpackage version is )(\d+(?:\.\d+)*)"), "gf_version"),
    (re.compile(r"(\bfind_package\(GUIForms )(\d+(?:\.\d+)*)"), "gf_version"),
    (re.compile(r"(\bC ABI )(\d+(?:\.\d+)*)"), "gf_c_abi"),
    (re.compile(r"(\bhost protocol )(\d+(?:\.\d+)*)"), "gf_host_protocol"),
    (re.compile(r"(\bdrawing (?:\(`gd_\*`\) )?ABI )(\d+(?:\.\d+)*)"), "gf_drawing_abi"),
)
REQUIRED: tuple[str, ...] = ("gf_version", "gf_c_abi")
TABLE = re.compile(r"^\| Platform \| Host \| Notes \|\n(?:\|.*\|\n)*", re.M)
BADGE = re.compile(r"!\[Platforms\]\(https://img\.shields\.io/badge/platforms-[^)]*\)")


class MarkerError(ValueError):
    """A span or block missing, unknown or unclosed, or a number typed outside one."""


def load_conf(repository: Path = ROOT) -> ModuleType:
    """docs/conf.py as a module: its table is the one source of every number."""
    path = repository / "docs" / "conf.py"
    spec = importlib.util.spec_from_file_location("gf_docs_conf", path)
    if spec is None or spec.loader is None:
        raise MarkerError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def platform_rows(platforms: tuple[tuple[str, str, str], ...], minimums: Mapping[str, str]) -> list[tuple[str, str, str]]:
    return [(name.format_map(minimums), host, notes) for name, host, notes in platforms]


def platform_table(rows: list[tuple[str, str, str]]) -> str:
    lines = ["| Platform | Host | Notes |", "|---|---|---|"]
    lines += [f"| {name} | {host} | {notes} |" for name, host, notes in rows]
    return "\n".join(lines) + "\n"


def platform_badge(rows: list[tuple[str, str, str]]) -> str:
    names = " | ".join(name.split()[0] for name, _, _ in rows)
    return f"![Platforms](https://img.shields.io/badge/platforms-{quote(names)}-lightgrey.svg)"


def fill_region(region: str, numbers: Mapping[str, str], named: set[str]) -> str:
    for pattern, name in PHRASES:
        if pattern.search(region):
            named.add(name)
        region = pattern.sub(lambda match, name=name: match.group(1) + numbers[name], region)
    return region


def render(text: str, numbers: Mapping[str, str], rows: list[tuple[str, str, str]]) -> str:
    """`text` with every region's numbers, the platform table and the badge
    rewritten. MarkerError for no region, a region without a required number,
    a number typed outside a region, or no single table or badge."""
    if text.count("<!-- numbers:start -->") != text.count("<!-- numbers:end -->"):
        raise MarkerError("README.md: a <!-- numbers:start --> region is not closed (or closed twice)")
    if not REGION.search(text):
        raise MarkerError("README.md: no <!-- numbers:start --> ... <!-- numbers:end --> region")
    outside = REGION.sub("", text)
    for pattern, name in PHRASES:
        typed = pattern.search(outside)
        if typed is not None:
            raise MarkerError(f"README.md: {typed.group(0)!r} types the {name} number outside a "
                              f"<!-- numbers:start --> region")
    if len(TABLE.findall(text)) != 1:
        raise MarkerError("README.md: needs exactly one platform table (| Platform | Host | Notes |)")
    if len(BADGE.findall(text)) != 1:
        raise MarkerError("README.md: needs exactly one Platforms badge")
    named: set[str] = set()
    result = REGION.sub(lambda match: match.group(1) + fill_region(match.group(2), numbers, named) + match.group(3),
                        text)
    missing = [name for name in REQUIRED if name not in named]
    if missing:
        raise MarkerError(f"README.md: no region names {missing[0]}")
    result = TABLE.sub(lambda match: platform_table(rows), result)
    result = BADGE.sub(lambda match: platform_badge(rows), result)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="exit 1 when README.md differs; write nothing")
    parser.add_argument("--readme", type=Path, default=README, help=argparse.SUPPRESS)
    arguments = parser.parse_args(argv)
    try:
        conf = load_conf()
        rows = platform_rows(conf.PLATFORMS, conf.read_minimum_platforms(ROOT))
        text = arguments.readme.read_text(encoding="utf-8")
        wanted = render(text, conf.GF_NUMBERS, rows)
    except (MarkerError, ValueError, OSError) as error:
        print(f"readme_numbers: {error}", file=sys.stderr)
        return 2
    if wanted == text:
        print("readme_numbers: README.md is current")
        return 0
    if arguments.check:
        print("readme_numbers: README.md is stale; run python3 tools/readme_numbers.py", file=sys.stderr)
        return 1
    arguments.readme.write_text(wanted, encoding="utf-8")
    print("readme_numbers: README.md rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
