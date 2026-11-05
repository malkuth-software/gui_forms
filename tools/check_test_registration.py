#!/usr/bin/env python3
"""Refuses test programs that the build never runs.

A test file whose program is not registered runs nowhere: it compiles in no
target, or its target is no ctest's command, and its checks pass silently by
never executing (tests/theme_font_tests.cpp was one, until T4 registered it).
This tool reads every tests/**/*.cpp that defines a main function, and every
add_executable and add_test in CMakeLists.txt and cmake/*.cmake, and fails
on a test program that:

- no add_executable lists, or
- whose executables no add_test names (by target name, $<TARGET_FILE:...>
  or a COMMAND word).

A file listed in SHARED with its reason is exempt (a program run by a script
or another harness rather than ctest). Exit status 0 when clean, 1 with one
line per finding.

Usage: python3 -B tools/check_test_registration.py [--root DIR]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Test programs registered some other way than add_test, with the reason.
SHARED: dict[str, str] = {
    'tests/frame_drag_probe.cpp': 'stage DK W0 measurement; puts windows on the desktop, run by hand',
    'tests/image/png_encode_benchmark.cpp': 'by-hand encoder size and time measurement, not a test',
    'tests/text_mask_cost_probe.cpp': 'by-hand text mask cost probe (EXCLUDE_FROM_ALL)',
    'tests/windows_message_queue_native_probe.cpp': 'local probe that needs an interactive desktop window',
    'tests/installed_feature_consumer/text_stack.cpp': 'installed-package consumer, built by tools/verify_install.py',
}

MAIN: re.Pattern[str] = re.compile(r'^\s*int\s+main\s*\(', re.MULTILINE)
COMMENT: re.Pattern[str] = re.compile(r'#[^\n]*')
EXECUTABLE: re.Pattern[str] = re.compile(r'\badd_executable\s*\(')
TEST: re.Pattern[str] = re.compile(r'\badd_test\s*\(')
TOKEN: re.Pattern[str] = re.compile(r'[A-Za-z0-9_.+\-/${}:<>]+')


def call_body(text: str, start: int) -> str:
    """The arguments of the CMake call whose '(' ends at start, up to its matching ')'."""
    depth: int = 1
    index: int = start
    while index < len(text) and depth > 0:
        character: str = text[index]
        if character == '(':
            depth += 1
        elif character == ')':
            depth -= 1
        index += 1
    body: str = text[start:index - 1]
    return body


def cmake_texts(root: Path) -> list[tuple[str, str]]:
    """Each build file's text without comments, with the directory its
    relative sources name: the root for the root's own files and the
    .cmake files it includes, the file's own directory for a subdirectory's
    CMakeLists.txt."""
    texts: list[tuple[str, str]] = []
    included: list[Path] = [root / 'CMakeLists.txt']
    included.extend(sorted((root / 'cmake').glob('*.cmake')))
    included.extend(sorted((root / 'demo').rglob('*.cmake')))
    for path in included:
        if path.is_file():
            texts.append((COMMENT.sub('', path.read_text(encoding='utf-8')), ''))
    for path in sorted((root / 'tests').rglob('CMakeLists.txt')):
        base: str = path.parent.relative_to(root).as_posix() + '/'
        texts.append((COMMENT.sub('', path.read_text(encoding='utf-8')), base))
    return texts


def source_path(word: str, base: str) -> str:
    """The repository path a source word names: from its tests/ component
    when it has one (a variable prefix such as ${_gui_audio_root}), else
    relative to the file's directory."""
    marker: int = word.find('tests/')
    if marker >= 0:
        return word[marker:]
    return base + word


def registrations(texts: list[tuple[str, str]]) -> tuple[dict[str, set[str]], set[str]]:
    """Each source path's executables, and every word any add_test uses."""
    sources: dict[str, set[str]] = {}
    tested: set[str] = set()
    for text, base in texts:
        for match in EXECUTABLE.finditer(text):
            words: list[str] = TOKEN.findall(call_body(text, match.end()))
            if not words:
                continue
            target: str = words[0]
            for word in words[1:]:
                path: str = source_path(word.strip('"'), base)
                if path.startswith('tests/'):
                    sources.setdefault(path, set()).add(target)
        for match in TEST.finditer(text):
            body: str = call_body(text, match.end())
            for word in re.findall(r'[A-Za-z0-9_]+', body):
                tested.add(word)
    return sources, tested


def test_programs(root: Path) -> list[str]:
    programs: list[str] = []
    for path in sorted((root / 'tests').rglob('*.cpp')):
        if MAIN.search(path.read_text(encoding='utf-8', errors='replace')):
            programs.append(path.relative_to(root).as_posix())
    return programs


def findings(root: Path, shared: dict[str, str] | None = None) -> list[str]:
    exempt: dict[str, str] = SHARED if shared is None else shared
    sources, tested = registrations(cmake_texts(root))
    found: list[str] = []
    for program in test_programs(root):
        if program in exempt:
            continue
        targets: set[str] = sources.get(program, set())
        if not targets:
            found.append(f'{program}: defines main but no add_executable lists it')
        elif not targets & tested:
            names: str = ', '.join(sorted(targets))
            found.append(f'{program}: its executable ({names}) is named by no add_test')
    for listed in sorted(exempt):
        if not (root / listed).is_file():
            found.append(f'{listed}: listed in SHARED but missing (remove the entry)')
    return found


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args(argv)
    found: list[str] = findings(arguments.root)
    for line in found:
        print(line)
    print(f'test registration: {len(test_programs(arguments.root))} test programs, {len(found)} findings')
    status: int = 1 if found else 0
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
