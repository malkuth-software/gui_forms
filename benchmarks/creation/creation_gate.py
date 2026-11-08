#!/usr/bin/env python3
"""The creation-cost gate (APP_WINDOW_RULES_DESIGN K.2).

    creation_gate.py record BENCHMARK MANIFEST [--note TEXT]
    creation_gate.py check BENCHMARK MANIFEST

BENCHMARK is gui_forms_creation_benchmark, built in Release; run on the M4
with nothing else running. Both modes run it as separate processes, since the
time varies more between processes (placement, caches) than within one.

record: ten runs. Per case, median_ns is the median of their medians and
threshold_ns their spread (largest minus smallest); allocations and bytes
must be equal in every run.

Each run is two processes: windows without an application, and windows
joined to one (--joined, the *_joined cases).

check: five runs; per case the median of their medians. Fails when the
allocations exceed the baseline's, or the median exceeds median_ns plus
threshold_ns. Prints one line per case and the verdict.

The time rule (spread of ten runs, compared with a median of five) is this
gate's author's choice while Q-K1 is open.
"""
import argparse
import re
import statistics
import subprocess
import sys

LINE = re.compile(r'^(\w+)\s+median\s+(\d+) ns\s+p10-p90\s+(\d+) ns\s+allocations\s+(\d+)\s+bytes\s+(\d+)$')
ROUNDS = 1000
RECORD_RUNS = 10
CHECK_RUNS = 5


def measure(benchmark, runs):
    """Per case, in output order: (medians, allocation set, byte set)."""
    cases = {}
    for _ in range(runs):
        output = ''
        for mode in ([], ['--joined']):
            output += subprocess.run([benchmark, '--rounds', str(ROUNDS)] + mode, check=True,
                                     capture_output=True, text=True).stdout
        for line in output.splitlines():
            match = LINE.match(line.strip())
            if not match:
                continue
            medians, allocations, byte_counts = cases.setdefault(match.group(1), ([], set(), set()))
            medians.append(int(match.group(2)))
            allocations.add(int(match.group(4)))
            byte_counts.add(int(match.group(5)))
    return cases


def record(options):
    cases = measure(options.benchmark, RECORD_RUNS)
    lines = [
        '# gui_forms_creation_benchmark baseline (APP_WINDOW_RULES_DESIGN K.2), written by',
        f'# creation_gate.py record: {RECORD_RUNS} processes of {ROUNDS} interleaved rounds.',
        '# median_ns: median of the runs\' medians; threshold_ns: their spread.',
        '# Re-record only for a deliberate change, with a note.',
    ]
    if options.note:
        lines.append('# Note: ' + options.note)
    for name, (medians, allocations, byte_counts) in cases.items():
        if len(allocations) != 1 or len(byte_counts) != 1:
            print(f'{name}: allocations varied between runs: {sorted(allocations)} {sorted(byte_counts)}')
            return 1
        lines.append(f'case {name} median_ns {int(statistics.median(medians))} '
                     f'threshold_ns {max(medians) - min(medians)} '
                     f'allocations {min(allocations)} bytes {min(byte_counts)}')
        print(f'{name}: medians {min(medians)}..{max(medians)} ns, {min(allocations)} allocations')
    with open(options.manifest, 'w') as manifest:
        manifest.write('\n'.join(lines) + '\n')
    return 0


def read_manifest(path):
    baselines = {}
    with open(path) as manifest:
        for line in manifest:
            fields = line.split()
            if len(fields) == 10 and fields[0] == 'case':
                baselines[fields[1]] = {fields[i]: int(fields[i + 1]) for i in range(2, 10, 2)}
    return baselines


def check(options):
    baselines = read_manifest(options.manifest)
    cases = measure(options.benchmark, CHECK_RUNS)
    failures = 0
    for name, baseline in baselines.items():
        if name not in cases:
            print(f'FAIL {name}: not measured')
            failures += 1
            continue
        medians, allocations, _ = cases[name]
        median = int(statistics.median(medians))
        limit = baseline['median_ns'] + baseline['threshold_ns']
        verdict = []
        if max(allocations) > baseline['allocations']:
            verdict.append(f'{max(allocations)} allocations > {baseline["allocations"]}')
        if median > limit:
            verdict.append(f'median {median} ns > limit {limit} ns')
        failures += 1 if verdict else 0
        print(f'{"FAIL" if verdict else "ok  "} {name}: median {median} ns (baseline {baseline["median_ns"]}, '
              f'limit {limit}), {max(allocations)} allocations (baseline {baseline["allocations"]})'
              + (': ' + '; '.join(verdict) if verdict else ''))
    print('creation gate: ' + ('ok' if failures == 0 else 'FAILED'))
    return 0 if failures == 0 else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['record', 'check'])
    parser.add_argument('benchmark')
    parser.add_argument('manifest')
    parser.add_argument('--note', default='')
    options = parser.parse_args()
    return record(options) if options.mode == 'record' else check(options)


if __name__ == '__main__':
    sys.exit(main())
