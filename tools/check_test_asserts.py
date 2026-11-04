#!/usr/bin/env python3
"""Refuses NDEBUG-sensitive checks in first-party test code.

A Release build defines NDEBUG, which compiles every assert() out: a test
that checks with bare assert() checks nothing there, and one whose assert
calls something that matters (a create, a dispatch, an exchange) also
behaves differently. Tests check with their own require()/expect() helper,
which reports and fails in every build. This tool fails on:

- a call of assert( outside comments and literals (static_assert is fine);
- #include <cassert> or <assert.h>;
- #define NDEBUG or #undef NDEBUG (a file may not opt out of Release).

It scans tests/ and every stx_*/tests/ under --root (default: this
checkout). Exit status 0 when clean, 1 with one line per finding.

Usage: python3 -B tools/check_test_asserts.py [--root DIR] [PATH...]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SUFFIXES: tuple[str, ...] = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inc', '.m', '.mm')
ASSERT_CALL: re.Pattern[str] = re.compile(r'(?<![\w.:>])assert\s*\(')
ASSERT_INCLUDE: re.Pattern[str] = re.compile(r'^\s*#\s*include\s*<(cassert|assert\.h)>')
NDEBUG_SWITCH: re.Pattern[str] = re.compile(r'^\s*#\s*(define|undef)\s+NDEBUG\b')
# Comments, raw strings (with their delimiter), strings and character
# literals, leftmost first, so a quote inside a comment opens nothing.
NON_CODE: re.Pattern[str] = re.compile(
    r'//[^\n]*'
    r'|/\*.*?(?:\*/|\Z)'
    r'|R"([^(\s]{0,16})\(.*?\)\1"'
    r'|"(?:\\.|[^"\\\n])*"?'
    r"|'(?:\\.|[^'\\\n])*'?",
    re.S)
NOT_NEWLINE: re.Pattern[str] = re.compile(r'[^\n]')

# Files whose bare asserts another lane is replacing (C ABI worker, S2.1).
# A listed file that is already clean fails as stale, so the entry goes
# when the fix lands.
PENDING: dict[str, str] = {
    'tests/live_surface_tests.cpp': 'C ABI lane S2.1',
    'tests/windows_live_surface_endpoint_tests.cpp': 'C ABI lane S2.1',
}


def code_only(text: str) -> str:
    """The text with comments and string and character literals blanked,
    newlines kept, so line numbers still match."""
    blanked: str = NON_CODE.sub(blank, text)
    return blanked


def blank(match: re.Match[str]) -> str:
    """A comment or literal as spaces, keeping its newlines."""
    spaces: str = NOT_NEWLINE.sub(' ', match.group(0))
    return spaces


def findings_of(path: Path) -> list[tuple[int, str]]:
    """The (line, what) findings of one source file."""
    text: str = path.read_text(encoding='utf-8', errors='replace')
    found: list[tuple[int, str]] = []
    # Most files name neither; only those that do are lexed.
    if 'assert' not in text and 'NDEBUG' not in text:
        return found
    number: int
    line: str
    for number, line in enumerate(text.splitlines(), start=1):
        if ASSERT_INCLUDE.match(line):
            found.append((number, 'includes the assert header'))
        if NDEBUG_SWITCH.match(line):
            found.append((number, 'switches NDEBUG'))
    for number, line in enumerate(code_only(text).splitlines(), start=1):
        if ASSERT_CALL.search(line):
            found.append((number, 'checks with bare assert(), which NDEBUG removes'))
    found.sort()
    return found


def test_trees(root: Path) -> list[Path]:
    trees: list[Path] = [root / 'tests']
    trees.extend(sorted(path for path in root.glob('stx_*/tests') if path.is_dir()))
    return trees


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument('paths', nargs='*', help='files or directories instead of the test trees')
    arguments: argparse.Namespace = parser.parse_args()
    root: Path = Path(arguments.root).resolve()
    starts: list[Path] = [Path(path).resolve() for path in arguments.paths] or test_trees(root)
    files: list[Path] = []
    start: Path
    for start in starts:
        if start.is_file():
            files.append(start)
        elif start.is_dir():
            files.extend(sorted(path for path in start.rglob('*') if path.suffix in SUFFIXES and path.is_file()))
    failures: int = 0
    pending_seen: set[str] = set()
    path: Path
    for path in files:
        relative: str = path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()
        found: list[tuple[int, str]] = findings_of(path)
        if relative in PENDING:
            pending_seen.add(relative)
            if not found:
                print(f'{relative}: clean, but still listed as pending ({PENDING[relative]}); remove the entry')
                failures += 1
            continue
        number: int
        what: str
        for number, what in found:
            print(f'{relative}:{number}: {what}')
            failures += 1
    checked: str = f'{len(files)} files; {failures} findings'
    if pending_seen:
        checked += f'; {len(pending_seen)} pending files skipped'
    print(checked)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
