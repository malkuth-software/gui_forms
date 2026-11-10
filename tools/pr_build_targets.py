#!/usr/bin/env python3
"""Print the build outputs the tests of a configured tree run, one per line (ci_build.test_build_targets).

  python tools/pr_build_targets.py --build-dir <tree> [--tests <ctest -R pattern>]

A pull request's programs job builds these instead of `all`: the tests it
runs, plus every shared and module library, and no program, bundle or
package no test runs. --tests narrows the listing to the matching tests
(the ones the native job did not pass); without it every test counts.
--changed-from narrows it further to the tests the paths changed since that
ref can affect (ci_build.pull_request_listing), and --pattern-out writes
the ctest -R pattern of the tests left (NONE when none is) for the test
step. The build-tree paths the tests name that ninja builds none of
(fetched sources, files the tests write) go to standard error.
"""
from __future__ import annotations
import argparse
import contextlib
from pathlib import Path
import sys

import change_selection
import ci_build


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build-dir', type=Path, required=True, help='a configured ninja build tree')
    parser.add_argument('--tests', default='', help='a ctest -R pattern choosing the tests (default: all)')
    parser.add_argument('--changed-from', default='',
                        help='keep only the tests the paths changed since this ref can affect, plus the smoke set')
    parser.add_argument('--pattern-out', type=Path, default=None,
                        help='write the ctest -R pattern of the tests left here (NONE: none)')
    args: argparse.Namespace = parser.parse_args(argv)
    toolkit: Path = args.build_dir.resolve()
    listing: dict = ci_build.test_listing(toolkit, args.tests, '')
    if args.changed_from:
        changed: list[str] = change_selection.changed_paths(ci_build.ROOT / 'gui_forms', args.changed_from)
        # The change selection says why each path selects what it does on standard output, which here
        # carries only the targets.
        with contextlib.redirect_stdout(sys.stderr):
            listing = ci_build.pull_request_listing(listing, changed)
    if args.pattern_out is not None:
        names: list[str] = []
        test: dict
        for test in listing.get('tests', []):
            names.append(test['name'])
        pattern: str = 'NONE'
        if names:
            pattern = change_selection.name_pattern(names)
        # Unix line ends on every host: the workflow's shell reads the file, and a
        # carriage return would end up in the pattern.
        args.pattern_out.write_text(pattern + '\n', encoding='utf-8', newline='\n')
    targets: list[str]
    unbuilt: list[str]
    targets, unbuilt = ci_build.test_build_targets(listing, ci_build.ninja_outputs(toolkit), toolkit)
    print(f"{len(listing.get('tests', []))} tests run {len(targets)} build outputs; "
          f"{len(unbuilt)} named paths need no build: {' '.join(unbuilt)}", file=sys.stderr)
    target: str
    # One target per line with Unix line ends on every host: xargs passes a
    # carriage return on as part of the name, which ninja does not know.
    sys.stdout.reconfigure(newline='\n')
    for target in targets:
        sys.stdout.write(target + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
