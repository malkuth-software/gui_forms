#!/usr/bin/env python3
"""Compare a host's normative trace with the headless reference.

Release-1.0 condition 2 (tests/support/normative_trace.hpp): every host
replays one scripted scene and must reproduce its input order, focus,
activation, text, paint receipts and display commands. The references are
written by the headless run:

    gui_forms_normative_trace_tests --write <reference-dir>

which leaves normative-1.00.trace and normative-2.00.trace there. A host
driver (gui_forms_macos_normative_trace, gui_forms_linux_normative_trace)
writes its own trace; this tool compares it with the reference whose scale
matches the trace's header:

    normative_trace.py compare --reference-dir <dir> --trace <host.trace>
        [--strict-hover]

Rules, per step (the lines between one "step" line and the next):
  - pointer moves, enters and leaves are ignored, since a host may add
    hover the script did not make, unless --strict-hover;
  - the host's receipts must include every receipt of the reference (a
    host may repaint more, never less);
  - every other line (pointer presses, releases and wheels, keys, text,
    focus, activation, the display summary and its lines) must match the
    reference exactly and in order.
Exit status 0 when the traces agree; 1 with the first difference otherwise;
2 for unusable input.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
from dataclasses import dataclass, field

HOVER_ACTIONS = ("action=move", "action=enter", "action=leave")


@dataclass
class Step:
    """One step's lines: its ordered lines and its receipt set."""

    title: str
    lines: list[str] = field(default_factory=list)
    receipts: set[str] = field(default_factory=set)


def is_hover(line: str) -> bool:
    if not line.startswith("pointer "):
        return False
    return any(line.endswith(action) or (action + " ") in line for action in HOVER_ACTIONS)


def read_trace(path: pathlib.Path) -> tuple[str, list[Step]]:
    """The header's scale and the steps; the lines before the first step
    and after the last (the display trace, end) form a step titled
    "(header)" and "(finish)" respectively."""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if not lines or not lines[0].startswith("normative-trace version=1 "):
        raise ValueError(f"{path}: not a version 1 normative trace")
    scale = ""
    for word in lines[0].split():
        if word.startswith("scale="):
            scale = word[len("scale="):]
    if not scale:
        raise ValueError(f"{path}: the header names no scale")
    steps: list[Step] = [Step("(header)")]
    for line in lines[1:]:
        if line.startswith("step "):
            steps.append(Step(line))
            continue
        if line.startswith("display "):
            if steps[-1].title != "(finish)":
                steps.append(Step("(finish)"))
        current = steps[-1]
        if line.startswith("receipt "):
            current.receipts.add(line)
        else:
            current.lines.append(line)
    if not lines[-1] == "end":
        raise ValueError(f"{path}: the trace does not end with 'end' (the run stopped early)")
    return scale, steps


def compare_steps(reference: list[Step], host: list[Step], strict_hover: bool) -> list[str]:
    """The differences, first first; empty when the traces agree."""
    problems: list[str] = []
    reference_titles = [step.title for step in reference]
    host_titles = [step.title for step in host]
    if reference_titles != host_titles:
        problems.append("steps differ: reference " + repr(reference_titles) + ", host " + repr(host_titles))
        return problems
    for expected, actual in zip(reference, host):
        expected_lines = expected.lines if strict_hover else [line for line in expected.lines if not is_hover(line)]
        actual_lines = actual.lines if strict_hover else [line for line in actual.lines if not is_hover(line)]
        if expected_lines != actual_lines:
            for index in range(max(len(expected_lines), len(actual_lines))):
                want = expected_lines[index] if index < len(expected_lines) else "(nothing)"
                got = actual_lines[index] if index < len(actual_lines) else "(nothing)"
                if want != got:
                    problems.append(f"{expected.title}: line {index + 1}: expected '{want}', host '{got}'")
                    break
        missing = sorted(expected.receipts - actual.receipts)
        if missing:
            problems.append(f"{expected.title}: receipts missing on the host: {', '.join(missing)}")
    return problems


def command_compare(arguments: argparse.Namespace) -> int:
    trace_path = pathlib.Path(arguments.trace)
    try:
        scale, host = read_trace(trace_path)
        reference_path = pathlib.Path(arguments.reference_dir) / f"normative-{scale}.trace"
        if not reference_path.is_file():
            print(f"normative_trace: no reference for scale {scale} ({reference_path})", file=sys.stderr)
            return 2
        reference_scale, reference = read_trace(reference_path)
    except (OSError, ValueError) as error:
        print(f"normative_trace: {error}", file=sys.stderr)
        return 2
    if reference_scale != scale:
        print(f"normative_trace: {reference_path} is at scale {reference_scale}, not {scale}", file=sys.stderr)
        return 2
    problems = compare_steps(reference, host, arguments.strict_hover)
    if problems:
        print(f"normative_trace: {trace_path} differs from {reference_path}:")
        for problem in problems:
            print("  " + problem)
        return 1
    print(f"normative_trace: {trace_path} matches {reference_path} ({len(host) - 2} steps)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    compare = commands.add_parser("compare", help="compare a host trace with the matching reference")
    compare.add_argument("--reference-dir", required=True)
    compare.add_argument("--trace", required=True)
    compare.add_argument("--strict-hover", action="store_true")
    compare.set_defaults(handler=command_compare)
    arguments = parser.parse_args()
    return arguments.handler(arguments)


if __name__ == "__main__":
    sys.exit(main())
