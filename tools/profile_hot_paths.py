#!/usr/bin/env python3
"""Sample GUI.Forms' recorded workloads with perf and list their hot paths.

Phase E, E5's dynamic half. On Linux this builds the workloads in their own
tree (the toolkit's Release configuration, plus frame pointers and line
tables so samples resolve to functions), runs each one under `perf record`,
and turns `perf report` into a hot-path list: every function's share of the
workload's samples, by itself (self) and with its callees (inclusive), and
the share of each area (GUI.Forms, Skia, HarfBuzz, FreeType, the C and C++
runtimes, the kernel).

A share is a fraction of samples, not a time and not a claim that anything
is fast or slow. Comparing two runs needs the same workload, host class and
build options; each report records them.

Commands:
  run --output DIR [--jobs N] [--frequency HZ]
      build, sample every workload, and write DIR/hot-paths.json,
      DIR/hot-paths.md and each workload's own output under DIR/<workload>/.
  summarize --report FILE --workload NAME [--top N]
      print the hot-path table for one saved `perf report` listing (the
      format `run` reads: --stdio --no-children -t TAB --sort dso,symbol).
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

from toolkit_configure import host_options, prepare_host, toolkit_options

ROOT: Path = Path(__file__).resolve().parents[2]
SOURCE: Path = ROOT / 'gui_forms'
# perf report's column separator: symbols (demangled C++) never hold a tab.
SEPARATOR: str = '\t'
# Frame pointers let perf walk GUI.Forms' own stacks (inclusive shares); line
# tables name the functions. stx_skia/ (from the stx checkout) is built in this tree, so it takes
# the same flags and its frames reach their GUI.Forms callers.
PROFILE_FLAGS: str = '-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -gline-tables-only'
# CMake's own Release flags for clang, which a CMAKE_<LANG>_FLAGS_RELEASE
# option replaces, so profile_flag_options repeats them.
RELEASE_FLAGS: str = '-O3 -DNDEBUG'
# The languages the toolkit compiles: C and C++, and Objective-C++ in the
# macOS host.
PROFILE_LANGUAGES: tuple[str, ...] = ('C', 'CXX', 'OBJCXX')
DEFAULT_TOP: int = 40
# Below this many samples a workload's shares are listed but not ranked:
# one sample is more than a tenth of a percent.
MINIMUM_RANKED_SAMPLES: int = 1000


@dataclass(frozen=True)
class Workload:
    """One recorded workload: the build target and its arguments.

    {output} in an argument is the workload's own output directory; {fonts}
    is the bundled font directory. runs is how many times one recording runs
    the program back to back, so that a short benchmark still collects
    MINIMUM_RANKED_SAMPLES samples.
    """
    name: str
    target: str
    arguments: tuple[str, ...]
    about: str
    runs: int = 1


WORKLOADS: tuple[Workload, ...] = (
    Workload('paint', 'gui_forms_paint_profile_workload', ('{output}',),
             'the Gallery through the CPU Skia adapter: M2d damage matrix and every page '
             '(benchmarks/paint_profile_workload.cpp)'),
    # 15 passes collected about 800 samples at 999 Hz; 180 collect about ten thousand.
    Workload('text_engine', 'gui_forms_text_engine_benchmark', ('{fonts}', '180'),
             '100 caption labels through the HarfBuzz/FreeType engine, 180 passes '
             '(benchmarks/text_engine_benchmark.cpp)'),
    # One run collected about 80 samples; 100 runs collect about eight thousand.
    Workload('document_view', 'gui_forms_document_view_benchmark', (),
             'the renderer-neutral DocumentView D1 workload, run 100 times in one recording '
             '(benchmarks/document_view_benchmark.cpp)', 100),
)

# Configure options that add the workloads to the toolkit build.
WORKLOAD_OPTIONS: tuple[str, ...] = (
    '-DGUI_FORMS_BUILD_PAINT_PROFILE=ON',
    '-DGUI_FORMS_BUILD_TEXT_ENGINE_BENCHMARK=ON',
    '-DGUI_FORMS_BUILD_DOCUMENT_VIEW_BENCHMARK=ON',
)


@dataclass(frozen=True)
class HotFunction:
    """One row of perf report: a function and its share of the samples."""
    symbol: str
    dso: str
    # Percent of the workload's samples in this function itself.
    self_percent: float
    # Percent with its callees; equal to self_percent when no call graph was read.
    inclusive_percent: float
    area: str


# Areas, tested in order; the first whose pattern matches the symbol (or,
# for the runtimes and the kernel, the object) names the row's area.
AREA_SYMBOLS: tuple[tuple[str, re.Pattern[str]], ...] = (
    # The workloads' own drivers (benchmarks/*.cpp): setup, timing and the
    # paint workload's pixel hash, kept apart so they do not count as the
    # library's cost.
    ('workload', re.compile(r'^(?:main|\(anonymous namespace\)::(?:measure|paint_once|pixel_hash|prepare_scale|'
                            r'run_damage_phase|run_page_phase|register_bundled_faces|case_record|write_record)\()')),
    ('skia', re.compile(r'^(?:[\w:<>, ]*\s)?(?:Sk|sk_|skcms|skvm|SkOpts|portable::|hsw::|skx::|sse2::|sse41::|neon::|'
                        r'lowp::|aaa_|blit_trapezoid|walk_edges|walk_convex_edges)')),
    # A standard container or algorithm instantiated for a GUI.Forms type is
    # GUI.Forms' cost, so the name may appear anywhere in the symbol.
    ('gui_forms', re.compile(r'gui_forms::')),
    ('harfbuzz', re.compile(r'^(?:[\w:<>, ]*\s)?(?:hb_|OT::|AAT::|CFF::)')),
    ('freetype', re.compile(r'^(?:FT_|ft_|tt_|TT_|cff_|ps_|af_|psh_|gray_|t1_|pfr_)')),
)
AREA_OBJECTS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ('kernel', re.compile(r'^\[kernel')),
    ('runtime', re.compile(r'^(?:libc|libm|libstdc\+\+|libc\+\+|libgcc|ld-linux|libpthread)')),
)


def area_of(symbol: str, dso: str) -> str:
    """The area a sampled function belongs to, by its name, then its object."""
    name: str
    pattern: re.Pattern[str]
    for name, pattern in AREA_SYMBOLS:
        if pattern.search(symbol) is not None:
            return name
    for name, pattern in AREA_OBJECTS:
        if pattern.search(dso) is not None:
            return name
    return 'other'


def parse_percent(text: str) -> float | None:
    """A perf report percentage column ("12.34%"), or None for another column."""
    stripped: str = text.strip()
    if not stripped.endswith('%'):
        return None
    try:
        return float(stripped[:-1])
    except ValueError:
        return None


def parse_report(text: str) -> list[HotFunction]:
    """The rows of a `perf report --stdio -t TAB --sort dso,symbol` listing.

    A row is one or two percentages (with --children: inclusive, then self;
    without: self), the object and the symbol, which perf prefixes with
    "[.] " (user) or "[k] " (kernel) and pads with spaces. perf 6.17 adds
    an "IPC [IPC Coverage]" column after the symbol ("-" when the hardware
    gives none); columns after the symbol are ignored. Comment lines (#),
    blank lines and call-tree lines (no percentage column) are skipped.
    Rows keep perf's order.
    """
    rows: list[HotFunction] = []
    line: str
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        columns: list[str] = line.split(SEPARATOR)
        percents: list[float] = []
        index: int = 0
        while index < len(columns):
            value: float | None = parse_percent(columns[index])
            if value is None:
                break
            percents.append(value)
            index += 1
        if not percents or len(columns) < index + 2:
            continue
        dso: str = columns[index].strip()
        symbol: str = columns[index + 1].strip()
        symbol = re.sub(r'^\[[.kgu]\]\s*', '', symbol)
        self_percent: float = percents[-1]
        inclusive_percent: float = percents[0]
        rows.append(HotFunction(symbol, dso, self_percent, inclusive_percent, area_of(symbol, dso)))
    return rows


def merge_inclusive(self_rows: list[HotFunction], children_rows: list[HotFunction]) -> list[HotFunction]:
    """The self rows, each given its inclusive share from the --children listing when it has one.

    The --children listing can split one function name into several rows
    (perf keeps entries apart that the self listing merges), so the
    inclusive shares of one name are summed. The sum is held between the
    function's self share and 100 %.
    """
    inclusive: dict[tuple[str, str], float] = {}
    row: HotFunction
    for row in children_rows:
        key: tuple[str, str] = (row.symbol, row.dso)
        inclusive[key] = inclusive.get(key, 0.0) + row.inclusive_percent
    merged: list[HotFunction] = []
    for row in self_rows:
        share: float = inclusive.get((row.symbol, row.dso), row.self_percent)
        share = min(100.0, max(share, row.self_percent))
        merged.append(HotFunction(row.symbol, row.dso, row.self_percent, round(share, 2), row.area))
    return merged


def sample_count(text: str) -> int:
    """The sample count in a perf report header ("# Samples: 81" or "6K"), or 0 when it has none."""
    match: re.Match[str] | None = re.search(r'^# Samples:\s*([0-9.]+)([KMG]?)', text, re.MULTILINE)
    if match is None:
        return 0
    scale: dict[str, int] = {'': 1, 'K': 1000, 'M': 1000000, 'G': 1000000000}
    return int(float(match.group(1)) * scale[match.group(2)])


def area_shares(rows: list[HotFunction]) -> dict[str, float]:
    """Each area's summed self share, largest first."""
    totals: dict[str, float] = {}
    row: HotFunction
    for row in rows:
        totals[row.area] = totals.get(row.area, 0.0) + row.self_percent
    ordered: list[tuple[str, float]] = sorted(totals.items(), key=area_sort_key)
    result: dict[str, float] = {}
    name: str
    share: float
    for name, share in ordered:
        result[name] = round(share, 2)
    return result


def area_sort_key(item: tuple[str, float]) -> tuple[float, str]:
    return (-item[1], item[0])


def markdown_table(workload: str, rows: list[HotFunction], top: int, samples: int = 0) -> str:
    """The hot-path table for one workload: its areas, then its top functions by self share."""
    lines: list[str] = [f'### {workload}', '']
    if samples:
        lines.append(f'About {samples} samples.')
        if samples < MINIMUM_RANKED_SAMPLES:
            lines.append(f'Fewer than {MINIMUM_RANKED_SAMPLES} samples: listed, too few to rank.')
        lines.append('')
    lines.extend(['| Area | Self share |', '|---|---:|'])
    name: str
    share: float
    for name, share in area_shares(rows).items():
        lines.append(f'| {name} | {share:.2f}% |')
    lines.extend(['', '| # | Self | Inclusive | Area | Function | Object |', '|---:|---:|---:|---|---|---|'])
    index: int
    for index in range(min(top, len(rows))):
        row: HotFunction = rows[index]
        symbol: str = row.symbol.replace('|', '\\|')
        if len(symbol) > 160:
            symbol = symbol[:157] + '...'
        lines.append(f'| {index + 1} | {row.self_percent:.2f}% | {row.inclusive_percent:.2f}% | {row.area} | '
                     f'`{symbol}` | {Path(row.dso).name} |')
    lines.append('')
    return '\n'.join(lines)


def command_output(command: list[str]) -> str:
    """A command's standard output, or "unavailable" when it cannot run."""
    try:
        completed: subprocess.CompletedProcess[str] = subprocess.run(
            command, capture_output=True, text=True, check=False)
    except OSError:
        return 'unavailable'
    if completed.returncode != 0:
        return 'unavailable'
    return completed.stdout.strip()


def host_description() -> dict[str, str]:
    """The host facts a reader needs to compare two reports."""
    cpu: str = 'unknown'
    cpuinfo: Path = Path('/proc/cpuinfo')
    if cpuinfo.is_file():
        match: re.Match[str] | None = re.search(r'^model name\s*:\s*(.+)$', cpuinfo.read_text(encoding='utf-8'),
                                                re.MULTILINE)
        if match is not None:
            cpu = match.group(1).strip()
    return {
        'system': platform.platform(),
        'machine': platform.machine(),
        'cpu': cpu,
        'cpus': str(os.cpu_count()),
        'runner': os.environ.get('GUI_FORMS_RUNNER', os.environ.get('RUNNER_NAME', 'local')),
        'image': os.environ.get('ImageOS', '') + ' ' + os.environ.get('ImageVersion', ''),
        'perf': command_output(['perf', 'version']),
        'compiler': command_output(['clang++', '--version']).splitlines()[0] if shutil.which('clang++') else 'unknown',
        'commit': command_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD']),
    }


def run_command(command: list[str], output: Path | None = None) -> None:
    print(' '.join(command), flush=True)
    if output is None:
        subprocess.run(command, check=True)
        return
    with output.open('w', encoding='utf-8') as stream:
        subprocess.run(command, check=True, stdout=stream)


def profile_flag_options() -> list[str]:
    """The configure options that add PROFILE_FLAGS to the Release build.

    They go in the build-type flags, not CMAKE_<LANG>_FLAGS: an option for
    the latter replaces the toolchain's CMAKE_<LANG>_FLAGS_INIT, which
    cmake/llvm22.cmake uses on macOS to select the pinned libc++ runtime
    (without it the build compiles against the SDK's headers and fails).
    """
    options: list[str] = []
    language: str
    for language in PROFILE_LANGUAGES:
        options.append(f'-DCMAKE_{language}_FLAGS_RELEASE={RELEASE_FLAGS} {PROFILE_FLAGS}')
    return options


def build_workloads(build: Path, jobs: int) -> Path:
    """Configure and build the workloads; returns the build tree."""
    tree: Path = build / 'gui-forms-profile'
    prepare_host(SOURCE, True)
    configure: list[str] = ['cmake', '-S', str(SOURCE), '-B', str(tree), '-G', 'Ninja',
                            *toolkit_options(SOURCE, build / 'gui-forms-profile-sdk', True, False),
                            *host_options('linux'), *WORKLOAD_OPTIONS, *profile_flag_options()]
    # The miniaudio source the native jobs' dependency cache restores, as
    # tools/ci_build.py uses it; Audio.cmake checks its digest either way.
    miniaudio: Path = build / 'gui-forms' / '_deps' / 'gui_forms_miniaudio-src'
    if (miniaudio / 'miniaudio.h').is_file():
        configure.append(f'-DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO={miniaudio}')
    run_command(configure)
    targets: list[str] = []
    workload: Workload
    for workload in WORKLOADS:
        targets.append(workload.target)
    run_command(['cmake', '--build', str(tree), '--parallel', str(jobs), '--target', *targets])
    return tree


def report_listing(data: Path, children: bool, destination: Path) -> str:
    """Write and return one perf report listing of data."""
    # The self listing prints no call trees (-g none); the inclusive listing
    # needs the call graph, and parse_report skips its tree lines.
    mode: list[str] = ['--children'] if children else ['--no-children', '-g', 'none']
    run_command(['perf', 'report', '-i', str(data), '--stdio', *mode, '-t', SEPARATOR,
                 '--sort', 'dso,symbol', '--percent-limit', '0.05'], destination)
    return destination.read_text(encoding='utf-8')


def profile_workload(workload: Workload, tree: Path, output: Path, frequency: int) -> tuple[list[HotFunction], int]:
    """Run one workload under perf record and read its two listings."""
    directory: Path = output / workload.name
    directory.mkdir(parents=True, exist_ok=True)
    arguments: list[str] = []
    argument: str
    for argument in workload.arguments:
        arguments.append(argument.replace('{output}', str(directory)).replace('{fonts}',
                                                                              str(SOURCE / 'assets/fonts')))
    data: Path = directory / 'perf.data'
    program: list[str] = [str(tree / workload.target), *arguments]
    if workload.runs > 1:
        # perf follows the shell's children; the loop stops at the first failing run.
        loop: str = f'i=0; while [ "$i" -lt {workload.runs} ]; do "$0" "$@" || exit 1; i=$((i + 1)); done'
        program = ['sh', '-c', loop, *program]
    run_command(['perf', 'record', '-F', str(frequency), '-g', '-o', str(data), '--', *program],
                directory / 'stdout.txt')
    self_listing: str = report_listing(data, False, directory / 'report-self.txt')
    self_rows: list[HotFunction] = parse_report(self_listing)
    children_rows: list[HotFunction] = parse_report(report_listing(data, True, directory / 'report-children.txt'))
    # perf.data is large and machine-bound; the listings carry its content.
    data.unlink()
    return merge_inclusive(self_rows, children_rows), sample_count(self_listing)


def hot_path_record(workload: Workload, rows: list[HotFunction], top: int, samples: int) -> dict[str, object]:
    functions: list[dict[str, object]] = []
    index: int
    for index in range(min(top, len(rows))):
        row: HotFunction = rows[index]
        functions.append({'symbol': row.symbol, 'object': Path(row.dso).name, 'area': row.area,
                          'self_percent': row.self_percent, 'inclusive_percent': row.inclusive_percent})
    return {'workload': workload.name, 'about': workload.about, 'samples': samples, 'areas': area_shares(rows),
            'functions': functions}


def run(output: Path, jobs: int, frequency: int, top: int) -> int:
    if platform.system() != 'Linux':
        print('profile_hot_paths.py run samples with Linux perf; this host is ' + platform.system(),
              file=sys.stderr)
        return 2
    if shutil.which('perf') is None:
        print('perf is not installed', file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    build: Path = ROOT / '.build' / 'native-linux-x64'
    if platform.machine().lower() in ('arm64', 'aarch64'):
        build = ROOT / '.build' / 'native-linux-arm64'
    tree: Path = build_workloads(build, jobs)
    host: dict[str, str] = host_description()
    records: list[dict[str, object]] = []
    sections: list[str] = []
    workload: Workload
    for workload in WORKLOADS:
        rows: list[HotFunction]
        samples: int
        rows, samples = profile_workload(workload, tree, output, frequency)
        records.append(hot_path_record(workload, rows, top, samples))
        sections.append(markdown_table(workload.name, rows, top, samples))
    report: dict[str, object] = {'schema': 'gui_forms.hot_paths.v1', 'host': host, 'frequency_hz': frequency,
                                 'build_flags': PROFILE_FLAGS, 'workloads': records}
    (output / 'hot-paths.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    header: list[str] = ['# GUI.Forms hot paths', '',
                         'Shares of perf samples per workload (self: in the function itself; inclusive: with '
                         'its callees, where frame pointers allow). Not timings.', '',
                         f'- Commit: `{host["commit"]}`',
                         f'- Host: {host["cpu"]}, {host["cpus"]} CPUs, {host["system"]}',
                         f'- Runner: {host["runner"]} ({host["image"].strip()})',
                         f'- perf: {host["perf"]}; sampling at {frequency} Hz with call graphs (frame pointers)',
                         f'- Compiler: {host["compiler"]}; Release plus `{PROFILE_FLAGS}`', '']
    (output / 'hot-paths.md').write_text('\n'.join(header) + '\n' + '\n'.join(sections), encoding='utf-8')
    print(f'hot paths written to {output}', flush=True)
    return 0


def summarize(report: Path, workload: str, top: int) -> int:
    listing: str = report.read_text(encoding='utf-8')
    rows: list[HotFunction] = parse_report(listing)
    if not rows:
        print(f'{report}: no perf report rows', file=sys.stderr)
        return 1
    print(markdown_table(workload, rows, top, sample_count(listing)))
    return 0


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    commands: argparse._SubParsersAction = parser.add_subparsers(dest='command', required=True)
    run_parser: argparse.ArgumentParser = commands.add_parser('run')
    run_parser.add_argument('--output', type=Path, required=True)
    run_parser.add_argument('--jobs', type=int, default=4)
    run_parser.add_argument('--frequency', type=int, default=999,
                            help='samples per second (perf record -F); 999 avoids beating with 1 kHz timers')
    run_parser.add_argument('--top', type=int, default=DEFAULT_TOP)
    summarize_parser: argparse.ArgumentParser = commands.add_parser('summarize')
    summarize_parser.add_argument('--report', type=Path, required=True)
    summarize_parser.add_argument('--workload', required=True)
    summarize_parser.add_argument('--top', type=int, default=DEFAULT_TOP)
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.command == 'run':
        return run(arguments.output, arguments.jobs, arguments.frequency, arguments.top)
    return summarize(arguments.report, arguments.workload, arguments.top)


if __name__ == '__main__':
    sys.exit(main())
