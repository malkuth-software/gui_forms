#!/usr/bin/env python3
"""Clang's experimental lifetime-safety lint over the whole tree, report-only.

Stage 1 of the borrow sanitizer plan (notes planning/borrow_sanitizer/,
owner ruling 2026-11-11: report-only first, then gating). Configures a
build tree of its own, as tools/ci_build.py configures the plain tree
(Release, cmake/llvm22.cmake, gallery and tests on), with CXXFLAGS and
OBJCXXFLAGS set to the lint flags (CMake folds them into the toolchain
file's flags at the first configure), builds everything with `ninja -k 0`,
and reports the lifetime findings, deduplicated by file, line, column and
flag. It never fails: a finding, a failed edge or a failed configure is
reported and the exit status is 0, so the checkpoint verify can run it
beside the gates. Usage errors exit 2.

    python3 -B tools/lifetime_lint.py [--build-dir DIR] [--jobs N]
        [--stx-prefix DIR] [--json FILE] [--parse-only]

--stx-prefix links an stx package already built at stx.lock's commit
(GUI_FORMS_STX_PREFIX, read-only), so third-party code is not compiled or
analysed; without it the package is built into the tree. The default tree
is .build/native-<host>-<arch>/gui-forms-lifetime-lint beside the plain
tree. The full build output stays in <tree>/lifetime-lint-build.log and the
table in <tree>/lifetime-lint-findings.json (or --json).

The analysis in LLVM 22 is intra-procedural and covers stack lifetimes
(returned addresses of locals, use after scope). It does not model a member
call as invalidating a view (a span used after the vector's push_back is
not reported); the checked borrow types (gui_forms/borrow/borrowed_view.hpp,
the sanitizer tree) cover that.
"""
from __future__ import annotations
import argparse
import collections
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

TOOLS: Path = Path(__file__).resolve().parent
SOURCE: Path = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import toolkit_configure  # noqa: E402

FLAGS: str = ('-Xclang -fexperimental-lifetime-safety -Wexperimental-lifetime-safety '
              '-Wexperimental-lifetime-safety-strict')
WARNING: re.Pattern[str] = re.compile(r'^([^:\s][^:]*):(\d+):(\d+): warning: (.*?) \[(-W[^\]]+)\]$')

# The findings of the cp28 run (2026-11-11) that are not defects, by file,
# message and the shape of the flagged line. All six are one false positive
# of the LLVM 22 analysis: a pointer or reference taken through a local
# std::unique_ptr (get() or *), after which the unique_ptr moves into a
# container that owns the pointee from then on. The analysis treats the
# result as a borrow of the local unique_ptr object, not of its heap
# pointee, so the move looks like the end of the borrowed object's life.
KNOWN_FALSE_POSITIVES: tuple[tuple[str, str, str], ...] = (
    ('src/controls/rules/rule_set/rule_set.cpp', 'address of stack memory is returned later',
     r'=\s*made\.get\(\)'),
    ('src/controls/guidance/help_provider/help_provider.cpp', 'address of stack memory is returned later',
     r'Entry&\s+result\s*=\s*\*entry'),
    ('src/controls/guidance/error_provider/error_provider.cpp', 'address of stack memory is returned later',
     r'Entry&\s+result\s*=\s*\*entry'),
    ('src/controls/ribbon_tool_tip/ribbon_tool_tip.cpp', 'address of stack memory is returned later',
     r'Entry&\s+result\s*=\s*\*entry'),
    ('src/render/skia/raster/skia_raster.cpp', 'object whose reference is captured does not live long enough',
     r'memo\s*=\s*replacement\.get\(\)'),
)


def host_name() -> str:
    hosts: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}
    result: str = hosts.get(platform.system(), 'linux')
    return result


def default_tree() -> Path:
    host: str = host_name()
    architecture: str = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    result: Path = SOURCE.parent / '.build' / ('native-' + host + '-' + architecture) / 'gui-forms-lifetime-lint'
    return result


def configure(tree: Path, stx_prefix: str) -> bool:
    environment: dict[str, str] = dict(os.environ)
    environment['CXXFLAGS'] = FLAGS
    environment['OBJCXXFLAGS'] = FLAGS
    command: list[str] = ['cmake', '-S', str(SOURCE), '-B', str(tree), '-G', 'Ninja',
                          *toolkit_configure.toolkit_options(SOURCE, tree / 'sdk', True, True),
                          *toolkit_configure.host_options(host_name()),
                          *toolkit_configure.stx_options(stx_prefix, os.environ.get('GUI_FORMS_STX_PACKAGE_DIR', ''))]
    with open(tree / 'lifetime-lint-configure.log', 'w', encoding='utf-8') as log:
        completed: subprocess.CompletedProcess = subprocess.run(command, env=environment, stdout=log,
                                                                stderr=subprocess.STDOUT)
    return completed.returncode == 0


def build(tree: Path, jobs: int) -> int:
    with open(tree / 'lifetime-lint-build.log', 'w', encoding='utf-8') as log:
        completed: subprocess.CompletedProcess = subprocess.run(
            ['cmake', '--build', str(tree), '--parallel', str(jobs), '--', '-k', '0'],
            stdout=log, stderr=subprocess.STDOUT)
    return completed.returncode


def relative_to_source(tree: Path, reported: str) -> str:
    resolved: str = os.path.realpath(os.path.join(str(tree), reported))
    root: str = os.path.realpath(str(SOURCE)) + os.sep
    if resolved.startswith(root):
        return resolved[len(root):].replace(os.sep, '/')
    return resolved


def known(finding: dict) -> bool:
    entry: tuple[str, str, str]
    for entry in KNOWN_FALSE_POSITIVES:
        if finding['path'] != entry[0] or finding['message'] != entry[1]:
            continue
        source: Path = SOURCE / finding['path']
        lines: list[str] = source.read_text(encoding='utf-8', errors='replace').split('\n')
        if 0 < finding['line'] <= len(lines) and re.search(entry[2], lines[finding['line'] - 1]) is not None:
            return True
    return False


def parse(tree: Path) -> list[dict]:
    seen: set[tuple[str, int, int, str]] = set()
    findings: list[dict] = []
    line: str
    with open(tree / 'lifetime-lint-build.log', encoding='utf-8', errors='replace') as log:
        for line in log:
            match: re.Match[str] | None = WARNING.match(line.rstrip('\n'))
            if match is None or 'lifetime-safety' not in match.group(5):
                continue
            path: str = relative_to_source(tree, match.group(1))
            key: tuple[str, int, int, str] = (path, int(match.group(2)), int(match.group(3)), match.group(5))
            if key in seen:
                continue
            seen.add(key)
            finding: dict = {'path': path, 'line': key[1], 'column': key[2], 'message': match.group(4),
                             'flag': key[3]}
            finding['known_false_positive'] = known(finding)
            findings.append(finding)
    return findings


def report(findings: list[dict]) -> None:
    by_flag: collections.Counter = collections.Counter()
    by_area: collections.Counter = collections.Counter()
    fresh: int = 0
    finding: dict
    for finding in findings:
        by_flag[finding['flag']] += 1
        parts: list[str] = finding['path'].split('/')
        by_area['/'.join(parts[:2]) if len(parts) > 2 else parts[0]] += 1
        if not finding['known_false_positive']:
            fresh += 1
    print(f'lifetime-lint: {len(findings)} findings, {fresh} not known false positives (report-only)')
    flag: str
    count: int
    for flag, count in sorted(by_flag.items()):
        print(f'  {flag}: {count}')
    area: str
    for area, count in sorted(by_area.items()):
        print(f'  {area}: {count}')
    for finding in findings:
        mark: str = 'known' if finding['known_false_positive'] else 'NEW'
        print(f"  {mark} {finding['path']}:{finding['line']}:{finding['column']}: {finding['message']}"
              f" [{finding['flag']}]")


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', default='')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--stx-prefix', default=os.environ.get('GUI_FORMS_STX_PREFIX', ''))
    parser.add_argument('--json', default='')
    parser.add_argument('--parse-only', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    tree: Path = Path(arguments.build_dir).resolve() if arguments.build_dir else default_tree()
    tree.mkdir(parents=True, exist_ok=True)
    if not arguments.parse_only:
        if not configure(tree, arguments.stx_prefix):
            print(f'lifetime-lint: configure failed ({tree / "lifetime-lint-configure.log"}); nothing reported')
            return 0
        status: int = build(tree, arguments.jobs)
        log_text: str = (tree / 'lifetime-lint-build.log').read_text(encoding='utf-8', errors='replace')
        print(f'lifetime-lint: build exit {status}, {log_text.count("FAILED:")} failed edges')
    findings: list[dict] = parse(tree)
    destination: Path = Path(arguments.json) if arguments.json else tree / 'lifetime-lint-findings.json'
    destination.write_text(json.dumps(findings, indent=1) + '\n', encoding='utf-8')
    report(findings)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
