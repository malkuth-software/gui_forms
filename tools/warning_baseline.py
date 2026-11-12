#!/usr/bin/env python3
"""The warning baseline of first-party code (owner decision D11, 2026-11-10).

First-party code compiles with -Wall -Wextra (cmake/Hardening.cmake) and no
warning fails the build. This tool reads a build's compiler output and counts
the distinct warning sites (file, line, column and flag) in each directory of
this repository: a header's warning counts once however many sources include
it, and code outside the repository (third-party, system and generated
headers) is not counted. tools/warning_baseline.json records the counts per
host.

  check  --log <build output>   fails when a directory reports more sites
                                 than the baseline records for the host
  update --log <build output>   records the counts the log reports

A build that compiles only part of the tree (an incremental or narrowed
build) reports a subset of the sites, so it can only pass a check that the
whole build passes; the baseline itself is updated from a whole build. A
host with no recorded baseline is reported and not checked.
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import re
import sys
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
BASELINE: Path = ROOT / 'tools' / 'warning_baseline.json'
# path:line:column: warning: text [-Wflag], as Clang and GCC print it; the
# path may begin with a drive letter.
WARNING: re.Pattern[str] = re.compile(
    r'^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): warning: (?P<text>.*?)(?: \[(?P<flag>-W[^\]]+)\])?\s*$')


def host_name() -> str:
    names: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}
    name: str = names.get(platform.system(), platform.system().lower())
    return name


def repository_path(path: str, build_directory: Path) -> str | None:
    """The path relative to the repository, with forward slashes, or None
    for a path outside it (or inside a build or dependency directory)."""
    candidate: Path = Path(path.replace('\\', '/'))
    if not candidate.is_absolute():
        candidate = build_directory / candidate
    resolved: str = os.path.normcase(os.path.normpath(str(candidate)))
    root: str = os.path.normcase(os.path.normpath(str(ROOT)))
    if not resolved.startswith(root + os.sep):
        return None
    relative: str = os.path.relpath(resolved, root).replace('\\', '/')
    if relative.startswith(('.build/', 'build/', '_deps/', 'third_party/')) or '/_deps/' in relative:
        return None
    return relative


def count_sites(log: Path, build_directory: Path) -> dict[str, int]:
    """Distinct warning sites per repository directory."""
    sites: set[tuple[str, str, str, str]] = set()
    line: str
    with log.open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            match: re.Match[str] | None = WARNING.match(line.rstrip('\r\n'))
            if match is None:
                continue
            relative: str | None = repository_path(match.group('path'), build_directory)
            if relative is None:
                continue
            flag: str = match.group('flag') or match.group('text')
            sites.add((relative, match.group('line'), match.group('column'), flag))
    counts: dict[str, int] = {}
    site: tuple[str, str, str, str]
    for site in sites:
        directory: str = site[0].rpartition('/')[0] or '.'
        counts[directory] = counts.get(directory, 0) + 1
    return counts


def load_baseline() -> dict:
    if not BASELINE.is_file():
        return {'about': '', 'hosts': {}}
    baseline: dict = json.loads(BASELINE.read_text(encoding='utf-8'))
    return baseline


def check(counts: dict[str, int], host: str) -> int:
    baseline: dict = load_baseline()
    recorded: dict[str, int] | None = baseline.get('hosts', {}).get(host)
    total: int = sum(counts.values())
    if recorded is None:
        print(f'warning baseline: no baseline for {host}; {total} warning sites, not checked')
        return 0
    rises: list[str] = []
    directory: str
    for directory in sorted(counts):
        allowed: int = recorded.get(directory, 0)
        if counts[directory] > allowed:
            rises.append(f'  {directory}: {counts[directory]} warning sites, baseline {allowed}')
    if rises:
        print(f'warning baseline: {len(rises)} directories report more warning sites than the {host} baseline '
              '(fix the new warnings; tools/warning_baseline.py update records a lower count):')
        print('\n'.join(rises))
        return 1
    print(f'warning baseline: {total} warning sites, within the {host} baseline of {sum(recorded.values())}')
    return 0


def update(counts: dict[str, int], host: str) -> int:
    baseline: dict = load_baseline()
    baseline['about'] = ('Distinct first-party warning sites per directory and host, from a whole build '
                         '(tools/warning_baseline.py, cmake/Hardening.cmake). A count may only go down.')
    hosts: dict = baseline.setdefault('hosts', {})
    ordered: dict[str, int] = {}
    directory: str
    for directory in sorted(counts):
        ordered[directory] = counts[directory]
    hosts[host] = ordered
    sorted_hosts: dict = {}
    name: str
    for name in sorted(hosts):
        sorted_hosts[name] = hosts[name]
    baseline['hosts'] = sorted_hosts
    with BASELINE.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(baseline, stream, indent=1)
        stream.write('\n')
    print(f'warning baseline: recorded {sum(counts.values())} warning sites in {len(counts)} directories for {host}')
    return 0


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=('check', 'update'))
    parser.add_argument('--log', required=True, type=Path, help="the build's compiler output")
    parser.add_argument('--build-dir', type=Path, default=Path.cwd(),
                        help='the directory the build ran in, for relative paths (default: here)')
    parser.add_argument('--host', default=host_name(), help='the host the baseline is for (default: this one)')
    args: argparse.Namespace = parser.parse_args()
    counts: dict[str, int] = count_sites(args.log, args.build_dir)
    if args.action == 'check':
        return check(counts, args.host)
    return update(counts, args.host)


if __name__ == '__main__':
    sys.exit(main())
