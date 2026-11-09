#!/usr/bin/env python3
"""Decide whether a pull request changes documentation only (owner, 2026-11-09).

  python3 tools/ci_docs_only.py --event <github event name> --base <base commit>

Prints `docs_only=true` or `docs_only=false` for $GITHUB_OUTPUT, and the
reason on standard error. native.yml's `changes` job runs it; with true the
four native jobs report success without building (their `build` job is
skipped), the engine gate passes without comparing, and the programs jobs do
not run, while house-style and the documentation site still check the change.

A pull request is documentation only when every path it changes is
documentation (documentation_path) and no test reads it (the test-area rules
of tools/test_areas.json give it no area: tools/change_selection.py).
Everything else builds in full: any other event (a merge group, a push to
main, the schedule, a dispatch), a pull request touching code, CMake, tests,
tools, workflows or anything a test reads (the API docstrings under
docs/library/ are compiled by gui_forms_doc_examples_tests), and any failure
to read the change, which never reports documentation only.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

import change_selection

ROOT: Path = Path(__file__).resolve().parent.parent

# Top-level directories whose files are source, build input or test data,
# whatever their suffix: a .md or .rst file there is not documentation here.
CODE_DIRECTORIES: frozenset[str] = frozenset({
    '.github', 'assets', 'benchmarks', 'bindings', 'cmake', 'code_integrity_checker', 'demo',
    'examples', 'file_manager_demoboard', 'fuzz', 'generated', 'include', 'manifests', 'packaging',
    'sdrsharp_reflection_system', 'src', 'tests', 'third_party', 'tools'})

# Documentation-looking files the build reads: the SDK installs the notices
# and tools/ci_build.py checks them, and the programs ship them.
BUILD_INPUTS: frozenset[str] = frozenset({'THIRD_PARTY_NOTICES.md'})

DOCUMENTATION_SUFFIXES: tuple[str, ...] = ('.md', '.rst')


def documentation_path(path: str) -> bool:
    """Whether path (relative, forward slashes) is documentation by its place and suffix.

    The documentation site (docs/, with the wiki under docs/wiki/) and
    Markdown or reStructuredText files outside the code directories, such as
    README.md, AGENTS.md and the planning text.
    """
    if path in BUILD_INPUTS:
        return False
    if path.startswith('docs/'):
        return True
    top: str = path.split('/', 1)[0]
    if '/' in path and top in CODE_DIRECTORIES:
        return False
    return path.endswith(DOCUMENTATION_SUFFIXES)


def classify(event: str, changed: list[str], rules: list[change_selection.PathRule]) -> tuple[bool, str]:
    """Whether the change is documentation only, with the reason.

    changed lists the paths a pull request changes; rules are the test-area
    path rules. Only a pull_request event with at least one changed path, all
    of them documentation that no test reads, is documentation only.
    """
    if event != 'pull_request':
        return False, f'a {event} event always builds in full'
    if not changed:
        return False, 'no changed path was found, so the change is built in full'
    path: str
    for path in changed:
        if not documentation_path(path):
            return False, f'{path} is not documentation'
        areas: tuple[str, ...] | None = change_selection.areas_for_path(path, rules)
        if areas is None or areas:
            return False, f'{path} is read by tests (tools/test_areas.json)'
    return True, f'all {len(changed)} changed paths are documentation that no test reads'


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--event', required=True, help="the GitHub event name (github.event_name)")
    parser.add_argument('--base', default='', help="a pull request's base commit")
    args: argparse.Namespace = parser.parse_args()
    docs_only: bool = False
    reason: str
    try:
        changed: list[str] = []
        if args.event == 'pull_request' and args.base:
            changed = change_selection.changed_paths(ROOT, args.base)
        docs_only, reason = classify(args.event, changed, change_selection.load_rules())
    except Exception as error:  # noqa: BLE001 - any failure builds in full, never skips
        reason = f'the change could not be classified ({error}), so it is built in full'
    print('docs_only=' + ('true' if docs_only else 'false'))
    print(reason, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
