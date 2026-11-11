#!/usr/bin/env python3
"""Run a test program that is meant to end the process, for CTest.

The fail-fast and borrow-sanitizer tests end with std::abort. On macOS and
Linux that is SIGABRT, and CTest fails a test killed by a signal even when
its PASS_REGULAR_EXPRESSION matches (Windows reports exit code 3 instead).
This runs the program with the same standard streams, so CTest still reads
its report, and exits 0 when the program ended abnormally (a signal or a
non-zero status) and 1 when it returned 0, so the test's regular
expressions decide.

    python3 -B tools/run_expecting_abort.py <program> [arguments...]
"""
from __future__ import annotations
import subprocess
import sys


def main(arguments: list[str]) -> int:
    if not arguments:
        print('usage: run_expecting_abort.py <program> [arguments...]', file=sys.stderr)
        return 2
    sys.stdout.flush()
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(arguments, check=False)
    ended_abnormally: bool = completed.returncode != 0
    if not ended_abnormally:
        print('run_expecting_abort: the program returned 0', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
