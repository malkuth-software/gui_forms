#!/usr/bin/env python3
"""The blend and opaque study runner (presentation and blending programme,
2026-11-16).

Builds and runs the Gallery study workload (benchmarks/blend_study_workload.cpp)
in an existing configured build tree, and writes to build/blend-study/<label>/:

- blend-study.jsonl: one line per case, as the workload writes it: the page,
  size, scale, damage band and frame counts (the exact workload), the blend
  counters summed over the counting pass (gui_forms/diagnostics/blend_study),
  and the timing pass's frame times and pixel hash;
- heat-depth-<page>.png and heat-blend-<page>.png: each page's heat maps at
  scale 1 over its own frame;
- manifest.json: the commands run, the revision, host, CPU, compiler and
  build type;
- summary.md: per case, per frame: copied and blended megapixels, the share
  of copies that hid earlier writes (overdraw), mean depth, opaque fills
  issued source-over, opaque images drawn blended, and median frame time;
- PRODUCTS.md: the procedure for the workloads in other repositories.

The integrator runs it on the M4, from the checkout's root in the mirror,
serialized with every other mirror and build operation:

    python3 -B tools/blend_study.py --build-dir <configured tree> --label base
    python3 -B tools/blend_study.py --build-dir <configured tree> --label branch \\
        --compare build/blend-study/base

--compare writes compare.md: for every case in both runs, whether the pixel
hashes are equal and how each counter moved; it exits 1 when any hash
differs, so a change promised to keep pixels identical is checked.

Products are in other repositories (PlaySuite, SwiftEdit, Plan Paint, the
Airspy map): no script here reaches them. They are measured through the
study's log instead (GUI_FORMS_BLEND_STUDY_LOG), with no product code, by
the procedure PRODUCTS.md states; then

    python3 -B tools/blend_study.py --summarize-log <log.jsonl> --label <product>

summarizes a log into build/blend-study/<product>/summary.md.

Timings are evidence only on the M4 (or another dedicated host), not over a
remote desktop.
"""

import argparse
import datetime
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET = 'gui_forms_blend_study_workload'

PRODUCTS = """# Product workloads for the blend study

The products live in other repositories, so this runner cannot drive them.
Each is measured through the study's log, which needs no product code:

1. Build the product against an SDK built from the branch under study (the
   product's own SDK procedure; the products' build scripts take the SDK
   prefix). Record the SDK revision and the product revision.
2. Run it on the M4 with the study on and its log named:

       GUI_FORMS_DEBUG_BLEND=1 GUI_FORMS_BLEND_STUDY_LOG=$PWD/build/blend-study/<product>.jsonl <product binary>

   GUI_FORMS_DEBUG_BLEND=depth or =blend also shows the heat map in the
   window (macOS host), for screenshots; the counts are the same.
3. Perform the workload below for about the stated count of frames, then
   quit. Every frame any window paints is one JSON line.
4. Summarize:

       python3 -B tools/blend_study.py --summarize-log build/blend-study/<product>.jsonl --label <product>

5. For frame times, repeat steps 2 and 3 with GUI_FORMS_DEBUG_BLEND unset
   and the product's own timing output (or the window's MetricsSnapshot
   present durations); counting costs about as much as painting.

Workloads (state any deviation in the summary):

- PlaySuite: its default window at its default size, opaque mode as it runs
  today; play one track through the transport and open each panel once
  (about 600 frames).
- SwiftEdit scrolling: a 10 000-line source file, scrolled from top to
  bottom with the scroll bar at its default speed, then by wheel (about
  600 frames).
- Plan Paint: a new 2048 x 2048 canvas at 100 %, 50 freehand strokes with
  the default brush, then a pan across the canvas (about 600 frames).
- Airspy map: the map demo at its default view, panned in a circle and
  zoomed in and out twice (about 600 frames).
"""


def run(command, log):
    """Runs `command` from the checkout's root, recording it; raises on failure."""
    log.append(' '.join(str(part) for part in command))
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True)


def capture(command):
    """Returns `command`'s standard output, or '' when it cannot run."""
    try:
        completed = subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError):
        return ''
    return completed.stdout.strip()


def cache_value(build_dir, name):
    """A CMakeCache.txt entry of the build tree, or ''."""
    cache = Path(build_dir) / 'CMakeCache.txt'
    if not cache.is_file():
        return ''
    prefix = name + ':'
    for line in cache.read_text(errors='replace').splitlines():
        if line.startswith(prefix):
            return line.split('=', 1)[1] if '=' in line else ''
    return ''


def find_binary(build_dir):
    """The workload binary under the build tree."""
    for candidate in Path(build_dir).rglob(TARGET):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise SystemExit(f'blend study: {TARGET} not found under {build_dir}')


def read_records(path):
    """The JSON lines of `path`, in order."""
    records = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line:
            records.append(json.loads(line))
    return records


def case_key(record):
    """What identifies a case across runs."""
    return (record.get('phase'), record.get('page'), record.get('scale'), record.get('damage_percent'))


def mean_depth(histogram):
    """The mean writes per painted pixel, the last bin taken at its index."""
    pixels = sum(histogram)
    if pixels == 0:
        return 0.0
    weighted = sum(index * count for index, count in enumerate(histogram))
    return weighted / pixels


def summary_row(label, counters, frames, median_ns=None):
    """One Markdown row of per-frame figures."""
    frames = max(frames, 1)
    copied = counters.get('copied_pixels', 0)
    blended = counters.get('blended_pixels', 0)
    overdraw = counters.get('overdraw_pixels', 0)
    share = overdraw / copied if copied else 0.0
    cells = [
        label,
        f'{copied / frames / 1e6:.3f}',
        f'{blended / frames / 1e6:.3f}',
        f'{share * 100.0:.1f} %',
        f'{mean_depth(counters.get("depth_histogram", [])):.2f}',
        f'{counters.get("opaque_fills_blended", 0) / frames:.1f}',
        f'{counters.get("opaque_images_blended", 0) / frames:.1f}',
        '' if median_ns is None else f'{median_ns / 1e6:.3f}',
    ]
    return '| ' + ' | '.join(cells) + ' |'


SUMMARY_HEADER = ('| case | copied Mpx/frame | blended Mpx/frame | overdraw / copied | mean depth | '
                  'opaque fills source-over /frame | opaque images blended /frame | median ms (study off) |\n'
                  '|---|---|---|---|---|---|---|---|')


def write_summary(output, records, title):
    lines = [f'# {title}', '', SUMMARY_HEADER]
    totals = {}
    frames = 0
    for record in records:
        counters = record['counters']
        label = f"{record['phase']} {record['page']} x{record['scale']:g} {record['damage_percent']:g} %"
        lines.append(summary_row(label, counters, record.get('counted_frames', 0),
                                 record.get('frame_ns', {}).get('median')))
        frames += record.get('counted_frames', 0)
        for name, value in counters.items():
            if isinstance(value, list):
                merged = totals.setdefault(name, [0] * len(value))
                for index, count in enumerate(value):
                    merged[index] += count
            else:
                totals[name] = totals.get(name, 0) + value
    lines.append(summary_row('all cases', totals, frames))
    lines.append('')
    lines.append('Counts are by device bounding box (blend_study.hpp); timings come from the pass with the '
                 'study off.')
    (Path(output) / 'summary.md').write_text('\n'.join(lines) + '\n')


def write_comparison(output, records, baseline_dir):
    baseline = {case_key(record): record for record in read_records(Path(baseline_dir) / 'blend-study.jsonl')}
    lines = [f'# Comparison with {baseline_dir}', '',
             '| case | pixels | copied | blended | overdraw | opaque fills | opaque images | median ms |',
             '|---|---|---|---|---|---|---|---|']
    differing = 0
    for record in records:
        before = baseline.get(case_key(record))
        if before is None:
            continue
        same = before.get('pixel_hash') == record.get('pixel_hash')
        differing += 0 if same else 1
        cells = [f"{record['phase']} {record['page']} x{record['scale']:g} {record['damage_percent']:g} %",
                 'same' if same else 'DIFFER']
        for name in ('copied_pixels', 'blended_pixels', 'overdraw_pixels', 'opaque_fills_blended',
                     'opaque_images_blended'):
            cells.append(f"{before['counters'].get(name, 0)} -> {record['counters'].get(name, 0)}")
        cells.append(f"{before['frame_ns']['median'] / 1e6:.3f} -> {record['frame_ns']['median'] / 1e6:.3f}")
        lines.append('| ' + ' | '.join(cells) + ' |')
    lines.append('')
    lines.append(f'{differing} case(s) with different pixels.')
    (Path(output) / 'compare.md').write_text('\n'.join(lines) + '\n')
    return differing


def summarize_log(log_path, output):
    """Summarizes a GUI_FORMS_BLEND_STUDY_LOG file, per window."""
    windows = {}
    for record in read_records(log_path):
        entry = windows.setdefault(record.get('window', ''), {'frames': 0, 'counters': {}, 'paint_ns': []})
        entry['frames'] += 1
        counters = record['counters']
        entry['paint_ns'].append(counters.get('paint_nanoseconds', 0))
        for name, value in counters.items():
            if isinstance(value, list):
                merged = entry['counters'].setdefault(name, [0] * len(value))
                for index, count in enumerate(value):
                    merged[index] += count
            else:
                entry['counters'][name] = entry['counters'].get(name, 0) + value
    lines = [f'# Blend study log {log_path}', '', SUMMARY_HEADER]
    for window, entry in sorted(windows.items()):
        lines.append(summary_row(f'window {window or "(unnamed)"}, {entry["frames"]} frames',
                                 entry['counters'], entry['frames']))
    lines.append('')
    lines.append('Paint times in a log include the counting pass; take frame times from a run with the study off.')
    Path(output).mkdir(parents=True, exist_ok=True)
    (Path(output) / 'summary.md').write_text('\n'.join(lines) + '\n')
    shutil.copyfile(log_path, Path(output) / 'blend-study-log.jsonl')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--build-dir', help='an existing configured build tree (Skia and the Gallery on)')
    parser.add_argument('--label', default=None, help='the output directory name under build/blend-study/')
    parser.add_argument('--cmake', default=shutil.which('cmake') or 'cmake')
    parser.add_argument('--jobs', default='8')
    parser.add_argument('--skip-build', action='store_true', help='run the workload already built')
    parser.add_argument('--revision', default=None,
                        help='the revision under study, when git cannot read it in the mirror')
    parser.add_argument('--compare', default=None, help='an earlier output directory to compare with')
    parser.add_argument('--summarize-log', default=None, help='a GUI_FORMS_BLEND_STUDY_LOG file to summarize')
    arguments = parser.parse_args()

    label = arguments.label or datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    output = ROOT / 'build' / 'blend-study' / label

    if arguments.summarize_log:
        summarize_log(arguments.summarize_log, output)
        print(f'blend study: wrote {output / "summary.md"}')
        return 0

    if not arguments.build_dir:
        parser.error('--build-dir is required unless --summarize-log is given')
    build_dir = Path(arguments.build_dir)
    if not (build_dir / 'CMakeCache.txt').is_file():
        raise SystemExit(f'blend study: {build_dir} is not a configured build tree')
    output.mkdir(parents=True, exist_ok=True)

    commands = []
    if not arguments.skip_build:
        run([arguments.cmake, '-DGUI_FORMS_BUILD_BLEND_STUDY=ON', str(build_dir)], commands)
        run([arguments.cmake, '--build', str(build_dir), '--target', TARGET, '-j', arguments.jobs], commands)
    binary = find_binary(build_dir)
    run([binary, output], commands)

    records = read_records(output / 'blend-study.jsonl')
    manifest = {
        'workload': 'benchmarks/blend_study_workload.cpp (Gallery pages at scales 1 and 2, 4 warm-up and 24 '
                    'measured frames per pass; first page at 900 x 620, scales 1 and 2, damage bands 1, 10 '
                    'and 100 %, 8 warm-up and 48 measured frames per pass)',
        'cases': len(records),
        'commands': commands,
        'revision': arguments.revision or capture(['git', 'rev-parse', 'HEAD']),
        'host': platform.node(),
        'platform': platform.platform(),
        'machine': platform.machine(),
        'cpu': capture(['sysctl', '-n', 'machdep.cpu.brand_string']) or platform.processor(),
        'compiler': cache_value(build_dir, 'CMAKE_CXX_COMPILER'),
        'build_type': cache_value(build_dir, 'CMAKE_BUILD_TYPE'),
        'cxx_flags': cache_value(build_dir, 'CMAKE_CXX_FLAGS'),
        'started': datetime.datetime.now().isoformat(timespec='seconds'),
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    write_summary(output, records, f'Blend study, {label}')
    (output / 'PRODUCTS.md').write_text(PRODUCTS)
    status = 0
    if arguments.compare:
        differing = write_comparison(output, records, arguments.compare)
        if differing:
            print(f'blend study: {differing} case(s) drew different pixels; see {output / "compare.md"}')
            status = 1
    print(f'blend study: wrote {output}')
    return status


if __name__ == '__main__':
    sys.exit(main())
