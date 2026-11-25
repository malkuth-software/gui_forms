#!/usr/bin/env python3
"""Check that a program's library allocator opt-in is live, without MCP.

    python3 tools/check_allocator_startup.py <program> [<program> ...] [--seconds 5]

Each program is started in its own directory with
GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1 and no arguments. Application::run
then ends a program whose operator new is not STXlib's allocator with status
70 before its window opens (gui_forms_use_library_allocator,
gui_forms::detail::program_allocator). The check passes when the program is
still running after the given seconds, and stops it; it fails when the
program exits before then, naming status 70 as the allocator. Prints one
line per program and exits 1 when any program fails.

For programs that do not take --mcp (Plan Paint, Pocket Reference);
tools/check_mcp_launch.py --require-library-allocator checks the ones that
do. A windowed program needs a window server: on Linux CI run this under
xvfb-run.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

NOT_THE_LIBRARY_ALLOCATOR: int = 70


def check(program: Path, seconds: float) -> str:
    """Runs program for seconds; returns an empty string when it was still
    running, otherwise what went wrong."""
    environment: dict[str, str] = dict(os.environ)
    environment['GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR'] = '1'
    process: subprocess.Popen[bytes] = subprocess.Popen(
        [str(program)], cwd=str(program.parent), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, env=environment)
    try:
        status: int = process.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        return ''
    errors: str = process.communicate()[1].decode('utf-8', 'replace').strip()
    if status == NOT_THE_LIBRARY_ALLOCATOR:
        return f'exited with status 70: its operator new is not STXlib\'s allocator ({errors})'
    return f'exited with status {status} within {seconds:g} s ({errors})'


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('programs', nargs='+', type=Path)
    parser.add_argument('--seconds', type=float, default=5.0,
                        help='how long each program must keep running (default 5)')
    arguments: argparse.Namespace = parser.parse_args()
    failures: int = 0
    program: Path
    for program in arguments.programs:
        program = program.resolve()
        failure: str = check(program, arguments.seconds)
        if failure:
            failures += 1
            print(f'FAIL {program.name}: {failure}')
        else:
            print(f'ok {program.name}: the library allocator is live')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
