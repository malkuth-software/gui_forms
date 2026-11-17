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

Two tables follow the phases: the slowest edges, and the quiet stretches,
spans of at least QUIET_MINIMUM_SECONDS in which no edge finished, with the
edges that ran through each. A job log shows such a stretch as a silence in
the build's output (the Windows job's 30 to 70 minute silences on #483,
2026-11-16 and 17); the table names what ran through it. The summary is
printed to the job log as well as appended to --out.

With --samples (tools/windows_resource_sampler.ps1's log) and the tree's
build-clock.json (tools/ci_build.py: the build's wall-clock end), a third
table lines each quiet stretch up with the resource samples taken during it:
the lowest free memory, the highest commit charge, the peak page faults and
hard-fault pages, and the largest and busiest processes at the sample with
the least free memory.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import sys

STX_OUTPUT: re.Pattern[str] = re.compile(r'(^|/)(stx_skia/|third_party/|_deps/gui_forms_stx-src/|stx-package/)')
OBJECT_SUFFIXES: tuple[str, ...] = ('.o', '.obj')
PHASES: tuple[str, ...] = ('stx or third-party compiles', 'test compiles', 'other compiles', 'test links',
                           'other links', 'other edges')
# The rows of the slowest-edges table.
SLOWEST_COUNT: int = 15
# The shortest span without a finished edge that the quiet-stretch table lists.
QUIET_MINIMUM_SECONDS: float = 120.0
# The most quiet stretches listed, the longest first.
QUIET_COUNT: int = 10


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


def edge_duration(edge: Edge) -> int:
    """An edge's duration in milliseconds (the sort key of the slowest-edges table)."""
    return edge.end - edge.start


def edge_end(edge: Edge) -> int:
    """An edge's end in milliseconds (the order in which ninja finished the edges)."""
    return edge.end


class QuietStretch:
    """A span of the build, in milliseconds, in which no edge finished, and the edges running through it."""

    def __init__(self, start: int, end: int, running: list[Edge]) -> None:
        self.start: int = start
        self.end: int = end
        self.running: list[Edge] = running


def quiet_stretch_length(stretch: QuietStretch) -> int:
    """A quiet stretch's length in milliseconds (its sort key)."""
    return stretch.end - stretch.start


def quiet_stretches(edges: list[Edge], minimum_milliseconds: int) -> list[QuietStretch]:
    """The spans of at least minimum_milliseconds between consecutive edge ends
    (or from the build's first start to its first end) in which no edge
    finished, the longest first, each with the edges that ran through all of it."""
    if not edges:
        return []
    by_end: list[Edge] = sorted(edges, key=edge_end)
    begin: int = edges[0].start
    edge: Edge
    for edge in edges:
        begin = min(begin, edge.start)
    stretches: list[QuietStretch] = []
    previous_end: int = begin
    for edge in by_end:
        if edge.end - previous_end >= minimum_milliseconds:
            running: list[Edge] = []
            candidate: Edge
            for candidate in edges:
                if candidate.start <= previous_end and candidate.end >= edge.end:
                    running.append(candidate)
            stretches.append(QuietStretch(previous_end, edge.end, running))
        previous_end = max(previous_end, edge.end)
    stretches.sort(key=quiet_stretch_length, reverse=True)
    return stretches


class ResourceSample:
    """One row of the resource sampler's log; the counters are NaN when not read."""

    def __init__(self, epoch_ms: int, available_mb: float, committed_mb: float, commit_limit_mb: float,
                 page_faults_per_s: float, pages_per_s: float, top_working_set: str, top_cpu: str) -> None:
        self.epoch_ms: int = epoch_ms
        self.available_mb: float = available_mb
        self.committed_mb: float = committed_mb
        self.commit_limit_mb: float = commit_limit_mb
        self.page_faults_per_s: float = page_faults_per_s
        self.pages_per_s: float = pages_per_s
        self.top_working_set: str = top_working_set
        self.top_cpu: str = top_cpu


# The resource sampler's columns, in its header's order.
SAMPLE_COLUMNS: tuple[str, ...] = ('time_utc', 'epoch_ms', 'available_mb', 'committed_mb', 'commit_limit_mb',
                                   'page_faults_per_s', 'pages_per_s', 'cpu_percent', 'disk_percent',
                                   'top_working_set', 'top_cpu')


def sample_number(text: str) -> float:
    """A sampler column's value, or NaN when it is empty or not a number."""
    try:
        return float(text)
    except ValueError:
        return float('nan')


def read_samples(text: str) -> list[ResourceSample]:
    """The successful samples of a resource sampler log (its header and `error` rows are skipped)."""
    samples: list[ResourceSample] = []
    line: str
    for line in text.splitlines():
        fields: list[str] = line.lstrip('\ufeff').split('\t')
        if len(fields) != len(SAMPLE_COLUMNS) or not fields[1].isdigit() or fields[2] == 'error':
            continue
        samples.append(ResourceSample(int(fields[1]), sample_number(fields[2]), sample_number(fields[3]),
                                      sample_number(fields[4]), sample_number(fields[5]),
                                      sample_number(fields[6]), fields[9], fields[10]))
    return samples


def finite_values(values: list[float]) -> list[float]:
    """The values that are numbers (NaN marks a counter that was not read)."""
    kept: list[float] = []
    value: float
    for value in values:
        if value == value:
            kept.append(value)
    return kept


def resource_rows(edges: list[Edge], stretches: list[QuietStretch], samples: list[ResourceSample],
                  finished_ms: int) -> list[str]:
    """The table lining each quiet stretch up with the samples taken during it.

    The ninja log's times are milliseconds after ninja began; finished_ms is
    the wall-clock end of its last edge (build-clock.json), which places them
    on the samples' clock."""
    last_end: int = 0
    edge: Edge
    for edge in edges:
        last_end = max(last_end, edge.end)
    begin: int = edges[0].start
    for edge in edges:
        begin = min(begin, edge.start)
    offset: int = finished_ms - last_end
    rows: list[str] = ['', 'Resources during the quiet stretches (tools/windows_resource_sampler.ps1):', '',
                       '| From (s) | Samples | Lowest free MB | Highest commit MB / limit | Peak faults/s '
                       '| Peak hard-fault pages/s | Largest working sets (name:pid:MB) | Busiest since the '
                       'previous sample (name:pid:s) |',
                       '|---:|---:|---:|---:|---:|---:|---|---|']
    stretch: QuietStretch
    for stretch in stretches:
        inside: list[ResourceSample] = []
        sample: ResourceSample
        for sample in samples:
            if stretch.start + offset <= sample.epoch_ms <= stretch.end + offset:
                inside.append(sample)
        from_seconds: str = f'{(stretch.start - begin) / 1000.0:.1f}'
        if not inside:
            rows.append(f'| {from_seconds} | 0 | | | | | | |')
            continue
        lowest: ResourceSample = inside[0]
        for sample in inside:
            if sample.available_mb < lowest.available_mb:
                lowest = sample
        committed: list[float] = []
        limits: list[float] = []
        faults: list[float] = []
        pages: list[float] = []
        for sample in inside:
            committed.append(sample.committed_mb)
            limits.append(sample.commit_limit_mb)
            faults.append(sample.page_faults_per_s)
            pages.append(sample.pages_per_s)
        committed_kept: list[float] = finite_values(committed)
        limits_kept: list[float] = finite_values(limits)
        faults_kept: list[float] = finite_values(faults)
        pages_kept: list[float] = finite_values(pages)
        commit_text: str = ''
        if committed_kept and limits_kept:
            commit_text = f'{max(committed_kept):.0f} / {max(limits_kept):.0f}'
        faults_text: str = f'{max(faults_kept):.0f}' if faults_kept else ''
        pages_text: str = f'{max(pages_kept):.0f}' if pages_kept else ''
        rows.append(f'| {from_seconds} | {len(inside)} | {lowest.available_mb:.0f} | {commit_text} | {faults_text} '
                    f'| {pages_text} | {lowest.top_working_set} | {lowest.top_cpu} |')
    return rows


def summary(edges: list[Edge], title: str, samples: list[ResourceSample] | None = None,
            finished_ms: int | None = None) -> str:
    """The Markdown summary of edges; with samples and the build's wall-clock
    end (finished_ms), also the resources during each quiet stretch."""
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
    slowest: list[Edge] = sorted(edges, key=edge_duration, reverse=True)[:SLOWEST_COUNT]
    rows.extend(['', f'Slowest {len(slowest)} edges:', '', '| Edge | Seconds | Started (s) |', '|---|---:|---:|'])
    for edge in slowest:
        rows.append(f'| {edge.output} | {edge_duration(edge) / 1000.0:.1f} | {(edge.start - begin) / 1000.0:.1f} |')
    stretches: list[QuietStretch] = quiet_stretches(edges, int(QUIET_MINIMUM_SECONDS * 1000.0))[:QUIET_COUNT]
    rows.append('')
    if not stretches:
        rows.append(f'No quiet stretch: an edge finished at least every {QUIET_MINIMUM_SECONDS:.0f} s.')
    else:
        rows.extend([f'Quiet stretches (no edge finished for {QUIET_MINIMUM_SECONDS:.0f} s or more):', '',
                     '| From (s) | Minutes | Edges running through it |', '|---:|---:|---|'])
        stretch: QuietStretch
        for stretch in stretches:
            names: list[str] = []
            for edge in stretch.running:
                names.append(edge.output)
            running_text: str = ', '.join(names) if names else 'none: no edge was running'
            rows.append(f'| {(stretch.start - begin) / 1000.0:.1f} | {quiet_stretch_length(stretch) / 60000.0:.1f} '
                        f'| {running_text} |')
        if samples is not None:
            if finished_ms is None:
                rows.extend(['', 'Resource samples were given, but the tree has no build-clock.json to place '
                             'the ninja log on their clock.'])
            elif not samples:
                rows.extend(['', 'The resource sampler log has no successful sample.'])
            else:
                rows.extend(resource_rows(edges, stretches, samples, finished_ms))
    return '\n'.join(rows) + '\n'


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('tree', type=Path, help='a ninja build tree')
    parser.add_argument('--title', default='', help='the heading (default: the tree)')
    parser.add_argument('--out', type=Path, default=None, help='also append the summary here')
    parser.add_argument('--samples', type=Path, default=None,
                        help="tools/windows_resource_sampler.ps1's log, lined up with the quiet stretches")
    args: argparse.Namespace = parser.parse_args(argv)
    log: Path = args.tree / '.ninja_log'
    edges: list[Edge] = []
    if log.is_file():
        edges = last_build(log.read_text(encoding='utf-8', errors='replace'))
    samples: list[ResourceSample] | None = None
    finished_ms: int | None = None
    if args.samples is not None:
        samples = []
        if args.samples.is_file():
            samples = read_samples(args.samples.read_text(encoding='utf-8', errors='replace'))
        clock_path: Path = args.tree / 'build-clock.json'
        if clock_path.is_file():
            clock: dict = json.loads(clock_path.read_text(encoding='utf-8'))
            finished_ms = int(clock['finished_ms'])
    text: str = summary(edges, args.title or str(args.tree), samples, finished_ms)
    print(text, flush=True)
    if args.out is not None:
        with args.out.open('a', encoding='utf-8') as stream:
            stream.write(text + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
