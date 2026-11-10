#!/usr/bin/env python3
"""Summarise where a ninja build's time went, from the tree's .ninja_log, as a Markdown table.

  python tools/ninja_phases.py <build tree> [--title TEXT] [--out FILE]

Each edge of the log's last build is put into a phase by its output: stx or
third-party compiles (none when the stx package is linked), test compiles,
other compiles (the toolkit and programs), test links, other links, and the
rest (custom commands). For each phase it reports the edges, the summed
seconds (the work, which ninja spread over its jobs) and the share of the
total. The build's wall time is the span from the first edge's start to the
last edge's end. A missing or empty log writes a line saying so; it never
fails a job, because the summary is evidence, not a gate.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import sys

STX_OUTPUT: re.Pattern[str] = re.compile(r'(^|/)(stx_skia/|third_party/|_deps/gui_forms_stx-src/|stx-package/)')
OBJECT_SUFFIXES: tuple[str, ...] = ('.o', '.obj')
PHASES: tuple[str, ...] = ('stx or third-party compiles', 'test compiles', 'other compiles', 'test links',
                           'other links', 'other edges')


class Edge:
    """One edge of the log: its start and end in milliseconds since the build began, and its output."""

    def __init__(self, start: int, end: int, output: str) -> None:
        self.start: int = start
        self.end: int = end
        self.output: str = output


def last_build(text: str) -> list[Edge]:
    """The edges of the log's last build.

    A log lists every build since it was created (version line, then
    start, end, mtime, output, hash per line). Ninja writes a line as its
    edge finishes, so the end times rise within a build and restart with
    the next one: the last build begins after the last fall. A rebuilt
    output keeps only its newest line.
    """
    edges: list[Edge] = []
    line: str
    for line in text.splitlines():
        if line.startswith('#'):
            continue
        fields: list[str] = line.split('\t')
        if len(fields) < 4 or not fields[0].isdigit() or not fields[1].isdigit():
            continue
        edges.append(Edge(int(fields[0]), int(fields[1]), fields[3]))
    first: int = 0
    index: int
    for index in range(1, len(edges)):
        if edges[index].end < edges[index - 1].end:
            first = index
    newest: dict[str, Edge] = {}
    edge: Edge
    for edge in edges[first:]:
        newest[edge.output] = edge
    return list(newest.values())


def phase_of(output: str) -> str:
    """The phase an edge's output belongs to (PHASES)."""
    name: str = output.rsplit('/', 1)[-1]
    is_test: bool = '_tests.dir/' in output or '_test.dir/' in output or '/tests/' in output
    if output.endswith(OBJECT_SUFFIXES):
        if STX_OUTPUT.search(output):
            return 'stx or third-party compiles'
        if is_test:
            return 'test compiles'
        return 'other compiles'
    if name.endswith(('.a', '.lib', '.so', '.dylib', '.dll', '.exe')) or '.' not in name:
        if 'tests' in name or '_test' in name:
            return 'test links'
        return 'other links'
    return 'other edges'


def summary(edges: list[Edge], title: str) -> str:
    """The Markdown summary of edges."""
    if not edges:
        return f'### {title}\n\nNo ninja log: nothing was built.\n'
    seconds: dict[str, float] = {}
    counts: dict[str, int] = {}
    phase: str
    for phase in PHASES:
        seconds[phase] = 0.0
        counts[phase] = 0
    begin: int = edges[0].start
    finish: int = edges[0].end
    edge: Edge
    for edge in edges:
        phase = phase_of(edge.output)
        seconds[phase] += (edge.end - edge.start) / 1000.0
        counts[phase] += 1
        begin = min(begin, edge.start)
        finish = max(finish, edge.end)
    total: float = sum(seconds.values())
    rows: list[str] = [f'### {title}', '',
                       f'{len(edges)} edges; wall {(finish - begin) / 60000.0:.1f} min; '
                       f'work {total / 60.0:.1f} min summed over the jobs', '',
                       '| Phase | Edges | Work (min) | Share |', '|---|---:|---:|---:|']
    for phase in PHASES:
        share: float = 0.0
        if total > 0.0:
            share = 100.0 * seconds[phase] / total
        rows.append(f'| {phase} | {counts[phase]} | {seconds[phase] / 60.0:.1f} | {share:.0f}% |')
    return '\n'.join(rows) + '\n'


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('tree', type=Path, help='a ninja build tree')
    parser.add_argument('--title', default='', help='the heading (default: the tree)')
    parser.add_argument('--out', type=Path, default=None, help='append the summary here (default: print it)')
    args: argparse.Namespace = parser.parse_args(argv)
    log: Path = args.tree / '.ninja_log'
    edges: list[Edge] = []
    if log.is_file():
        edges = last_build(log.read_text(encoding='utf-8', errors='replace'))
    text: str = summary(edges, args.title or str(args.tree))
    if args.out is None:
        print(text)
    else:
        with args.out.open('a', encoding='utf-8') as stream:
            stream.write(text + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
