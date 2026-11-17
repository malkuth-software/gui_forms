#!/usr/bin/env python3
"""stable-id-literal: a stable ID written in a test uses no '_'.

A stable ID is '.'-separated segments of lowercase letters and digits joined
by single '-' (StableId's rule). C++ code that names one through StableId's
literal constructor is checked when it compiles; the C ABI's tests and
helpers pass IDs as run-time strings, which the ABI refuses with
GF_STATUS_INVALID_ARGUMENT only when the test runs. Three audit tests slipped
that way in one round (2026-11-15: "cpp.self_owned", "fields.element_button",
"inspection...tab_index"), so this rule reads the positions that carry an ID:

- the last string argument of a call whose name ends in `create`
  (`gf_kit_create(kit, "type", "id")`, `create(tables, "type", "id")`,
  `objects.create(text_view("type"), text_view("id"), ...)`): any '_';
- `StableId("...")`: any '_';
- `gf_kit_text("...")`, and an assignment to a member or variable ending in
  `row_id` or `stable_id`: a '_' in a dotted string (type names and property
  names are not dotted).

A line, or the line above, holding `stable-id: allow <reason>` admits a
deliberately invalid ID (a refusal test). A heuristic over raw text with line
comments removed; review still required. Run by tools/check_lifetime_rules.py
on its paths under tests/.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import re


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str


CREATE_CALL: re.Pattern[str] = re.compile(r'\b\w*create\s*\(')
STABLE_ID_CALL: re.Pattern[str] = re.compile(r'\bStableId\s*\(\s*"((?:[^"\\]|\\.)*)"')
KIT_TEXT_CALL: re.Pattern[str] = re.compile(r'\bgf_kit_text\s*\(\s*"((?:[^"\\]|\\.)*)"')
ID_ASSIGNMENT: re.Pattern[str] = re.compile(r'\b\w*row_id\s*=\s*"((?:[^"\\]|\\.)*)"')
WRAPPED: re.Pattern[str] = re.compile(r'^\s*(?:\w+::)*(?:text_view|gf_kit_text)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)\s*$')
LITERAL: re.Pattern[str] = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"\s*$')
ALLOW: str = 'stable-id: allow '
SUFFIXES: tuple[str, ...] = ('.c', '.cc', '.cpp', '.h', '.hpp', '.mm', '.m')


def without_line_comment(line: str) -> str:
    """The line up to a `//` that is outside a string literal."""
    inside: bool = False
    escaped: bool = False
    index: int = 0
    while index < len(line):
        character: str = line[index]
        if escaped:
            escaped = False
        elif character == '\\':
            escaped = True
        elif character == '"':
            inside = not inside
        elif not inside and line.startswith('//', index):
            return line[:index]
        index += 1
    return line


def call_arguments(code: str, open_index: int) -> str:
    """The text between a call's '(' at open_index and its matching ')' on this line."""
    depth: int = 0
    index: int = open_index
    while index < len(code):
        if code[index] == '(':
            depth += 1
        elif code[index] == ')':
            depth -= 1
            if depth == 0:
                return code[open_index + 1:index]
        index += 1
    return code[open_index + 1:]


def direct_strings(arguments: str) -> list[str]:
    """The call's top-level arguments that are a string, or a string wrapped in text_view or gf_kit_text."""
    parts: list[str] = []
    depth: int = 0
    inside: bool = False
    escaped: bool = False
    start: int = 0
    index: int = 0
    while index < len(arguments):
        character: str = arguments[index]
        if escaped:
            escaped = False
        elif character == '\\':
            escaped = True
        elif character == '"':
            inside = not inside
        elif not inside and character in '([{':
            depth += 1
        elif not inside and character in ')]}':
            depth -= 1
        elif not inside and character == ',' and depth == 0:
            parts.append(arguments[start:index])
            start = index + 1
        index += 1
    parts.append(arguments[start:])
    strings: list[str] = []
    for part in parts:
        literal: re.Match[str] | None = LITERAL.match(part)
        wrapped: re.Match[str] | None = WRAPPED.match(part)
        if literal is not None:
            strings.append(literal.group(1))
        elif wrapped is not None:
            strings.append(wrapped.group(1))
    return strings


def check_stable_id_literals(path: Path, text: str) -> list[Finding]:
    findings: list[Finding] = []
    if path.suffix not in SUFFIXES or 'tests' not in path.parts:
        return findings
    lines: list[str] = text.splitlines()
    for number, raw in enumerate(lines, 1):
        previous: str = lines[number - 2] if number > 1 else ''
        if ALLOW in raw or ALLOW in previous:
            continue
        code: str = without_line_comment(raw)
        flagged: list[tuple[str, str]] = []
        for match in CREATE_CALL.finditer(code):
            strings: list[str] = direct_strings(call_arguments(code, match.end() - 1))
            if len(strings) >= 2 and '_' in strings[-1]:
                flagged.append((strings[-1], 'the ID argument of ' + match.group(0).rstrip('( ')))
        for match in STABLE_ID_CALL.finditer(code):
            if '_' in match.group(1):
                flagged.append((match.group(1), 'StableId'))
        for match in KIT_TEXT_CALL.finditer(code):
            if '_' in match.group(1) and '.' in match.group(1):
                flagged.append((match.group(1), 'gf_kit_text'))
        for match in ID_ASSIGNMENT.finditer(code):
            if '_' in match.group(1) and '.' in match.group(1) and '/' not in match.group(1):
                flagged.append((match.group(1), 'an ID assignment'))
        for value, place in flagged:
            findings.append(Finding(str(path), number, 'stable-id-literal',
                                    f'"{value}" ({place}) has a \'_\'; stable IDs join words with \'-\''))
    return findings
