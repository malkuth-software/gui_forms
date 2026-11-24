#!/usr/bin/env python3
"""state-agreement: state whose agreement is held only by statement order.

The 2026-11-23 state-agreement hunt (checkpoint 37) found one family of
defects across many classes: two facts that must agree (a member and the
child list, a flag and the value beside it, a cache and what it caches) are
kept in step only by the order of statements, so a call that refuses, throws
or runs a handler part-way leaves them apart. This tool reports candidates of
three shapes in GUI.Forms' own C++, each with the question a reviewer
answers:

  a  write before a refusing call: a member (`name_`) is written, and later
     in the same function a call that can refuse, throw or run other code
     (add_child, remove_child, insert_child, request_focus,
     begin_focus_scope, open_overlay, set_pointer_capture(true), attach)
     follows, with no try block in the function; constructors and
     destructors are skipped. The house rule is
     commit-last: do the step that can fail, then write the members.
  b  teardown paths that disagree: two teardown functions of one class
     (dispose, detach, close, hide, dismiss, release, cancel, revoke, the
     destructor) each clear at least two members, share at least one, and
     each clears one the other leaves.
  c  cached early return: an apply, arrange, sync, update or refresh
     function returns at once when a member equals the wanted value. The
     member is then the only record of what was applied; a change made
     elsewhere is never repaired.

A heuristic over source text with comments and string contents blanked; each
candidate needs a reader. A line, or the line above, holding
`state-agreement: reviewed <reason>` admits a candidate that was read and
holds. Report-only: the exit status is 0 unless --strict is given, so it
never fails CI by default.

Usage: check_state_agreement.py [--strict] [--json OUT] [--rule a|b|c] [paths...]
(paths default to src/).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys

SUFFIXES: tuple[str, ...] = ('.cpp', '.cc', '.mm')
SKIPPED_PARTS: tuple[str, ...] = ('third_party', 'external', 'vendor', 'generated')
REVIEWED: str = 'state-agreement: reviewed '

# Calls that can refuse, throw or run other code before they return.
# set_pointer_capture counts only when it takes the capture (`true`):
# releasing it is the usual last step of a press. subscribe is left out: it
# fails only for memory and runs nothing, and counting it buried the rest.
REFUSING_CALLS: tuple[str, ...] = (
    'add_child', 'insert_child', 'remove_child', 'request_focus', 'begin_focus_scope',
    'open_overlay', r'set_pointer_capture(?=\s*\(\s*true)', 'attach',
)
REFUSING_CALL: re.Pattern[str] = re.compile(r'\b(' + '|'.join(REFUSING_CALLS) + r')\s*\(')
# A write to a member: `name_ = ...` (not `==`), `name_ += ...`, or a
# changing member call on it.
MEMBER_WRITE: re.Pattern[str] = re.compile(
    r'(?<![\w.])(?:\(\*this\)\.)?([a-z]\w*_)\s*(?:(?:[+\-*/|&]?=)(?!=)|\.(?:set|reset|clear|push_back|emplace_back|'
    r'insert|erase|assign|take)\s*\()')
# A teardown function's name.
TEARDOWN_NAME: re.Pattern[str] = re.compile(
    r'^(?:~\w+|on_dispose|on_detached_from_window|on_overlay_revoked|\w*(?:dispose|detach|close|hide|dismiss|'
    r'release|cancel|revoke|teardown|tear_down)\w*)$')
# A member cleared: assigned an empty value, or reset or cleared.
MEMBER_CLEARED: re.Pattern[str] = re.compile(
    r'(?<![\w.])(?:\(\*this\)\.)?([a-z]\w*_)\s*(?:=\s*(?:\{\s*\}|nullptr|false|0U?|0\.0|\w+\{\s*\}|'
    r'[\w:]+::none\(\))\s*;|\.(?:reset|clear)\s*\(\s*\))')
# An apply-like function's name, for rule c.
APPLY_NAME: re.Pattern[str] = re.compile(r'^(?:apply|arrange|sync|synchronize|update|refresh)\w*$')
# `if (member_ == value) return` or `if (value == member_) return`, either order.
CACHED_RETURN: re.Pattern[str] = re.compile(
    r'\bif\s*\(\s*(?:([a-z]\w*_)\s*==\s*[\w.:()*]+|[\w.:()*]+\s*==\s*([a-z]\w*_))\s*\)\s*(?:\{\s*)?return\b')
# A member function definition's head: `Class::name(` (or `Class::~Class(`).
DEFINITION: re.Pattern[str] = re.compile(r'\b([A-Z]\w*)::(~?[A-Za-z_]\w*)\s*\(')


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    function: str
    detail: str
    question: str


@dataclass(frozen=True)
class Function:
    class_name: str
    name: str
    body_start: int
    body_end: int


def blank(source: str) -> str:
    """Comments and string or character literal contents become spaces;
    offsets and newlines are kept."""
    out: list[str] = list(source)
    index: int = 0
    length: int = len(source)
    while index < length:
        character: str = source[index]
        following: str = source[index + 1] if index + 1 < length else ''
        if character == '/' and following == '/':
            end: int = source.find('\n', index)
            end = length if end < 0 else end
            for position in range(index, end):
                out[position] = ' '
            index = end
        elif character == '/' and following == '*':
            end = source.find('*/', index + 2)
            end = length if end < 0 else end + 2
            for position in range(index, end):
                if source[position] != '\n':
                    out[position] = ' '
            index = end
        elif character == '"' or (character == "'" and not (index > 0 and source[index - 1].isalnum())):
            position = index + 1
            while position < length and source[position] != character and source[position] != '\n':
                if source[position] == '\\':
                    position += 1
                position += 1
            for blanked in range(index + 1, min(position, length)):
                if source[blanked] != '\n':
                    out[blanked] = ' '
            index = position + 1
        else:
            index += 1
    return ''.join(out)


def matching(text: str, start: int, opening: str, closing: str) -> int:
    """The index of the bracket closing the one at `start`, or -1."""
    depth: int = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    return -1


def functions_of(text: str) -> list[Function]:
    """The member function definitions in blanked text, with their bodies."""
    found: list[Function] = []
    position: int = 0
    while True:
        head: re.Match[str] | None = DEFINITION.search(text, position)
        if head is None:
            break
        position = head.end()
        close: int = matching(text, head.end() - 1, '(', ')')
        if close < 0:
            continue
        # The body opens after the parameter list, qualifiers and an
        # initializer list; a ';' first means a declaration or a call.
        index: int = close + 1
        while index < len(text) and text[index] not in '{;':
            if text[index] == '(':
                inner: int = matching(text, index, '(', ')')
                if inner < 0:
                    break
                index = inner
            index += 1
        if index >= len(text) or text[index] != '{':
            continue
        # An initializer list may hold braces (`member_{}`): the body is the
        # last brace group before the next definition at depth 0, so walk
        # brace groups until one is followed by neither ',' nor another
        # initializer.
        body_start: int = index
        body_end: int = matching(text, body_start, '{', '}')
        while body_end > 0:
            after: str = text[body_end + 1:body_end + 40].lstrip()
            if not after.startswith(','):
                break
            next_open: int = text.find('{', body_end + 1)
            if next_open < 0:
                break
            body_start = next_open
            body_end = matching(text, body_start, '{', '}')
        if body_end < 0:
            continue
        found.append(Function(head.group(1), head.group(2), body_start, body_end))
        position = body_end
    return found


def line_of(text: str, offset: int) -> int:
    return text.count('\n', 0, offset) + 1


def reviewed(lines: list[str], line: int) -> bool:
    """Whether the line, or the one above, admits the candidate."""
    for number in (line, line - 1):
        if 1 <= number <= len(lines) and REVIEWED in lines[number - 1]:
            return True
    return False


def encloses(body: str, write: int, call: int) -> bool:
    """Whether the block holding `write` is still open at `call`: a write in
    a branch that closed before the call (another case, an early return) is
    not on the call's path."""
    depth: int = 0
    for character in body[write:call]:
        if character == '{':
            depth += 1
        elif character == '}':
            depth -= 1
            if depth < 0:
                return False
    return True


def rule_a(path: str, text: str, function: Function) -> list[Finding]:
    # A constructor builds an object no one else sees yet; a destructor
    # leaves one no one will see again.
    if function.name == function.class_name or function.name.startswith('~'):
        return []
    body: str = text[function.body_start:function.body_end]
    if re.search(r'\btry\s*\{', body) is not None:
        return []
    found: list[Finding] = []
    reported: set[str] = set()
    for call in REFUSING_CALL.finditer(body):
        # Writes in an earlier statement only: `token_ = event.subscribe(...)`
        # stores the call's own result.
        statement_start: int = max(body.rfind(';', 0, call.start()), body.rfind('{', 0, call.start()),
                                   body.rfind('}', 0, call.start())) + 1
        written: list[str] = []
        for write in MEMBER_WRITE.finditer(body, 0, statement_start):
            if not encloses(body, write.start(), call.start()):
                continue
            member: str = write.group(1)
            if member not in written:
                written.append(member)
        fresh: list[str] = [member for member in written if member not in reported]
        if not fresh:
            continue
        reported.update(fresh)
        offset: int = function.body_start + call.start()
        members: str = ', '.join(fresh)
        found.append(Finding(
            path, line_of(text, offset), 'a', function.class_name + '::' + function.name,
            members + ' written before ' + call.group(1) + '()',
            'If ' + call.group(1) + ' refuses, throws or runs a handler that reads them, do ' + members +
            ' still agree with the object? (commit-last: write them after the call)'))
    return found


def cleared_members(text: str, function: Function) -> set[str]:
    body: str = text[function.body_start:function.body_end]
    members: set[str] = {match.group(1) for match in MEMBER_CLEARED.finditer(body)}
    return members


def rule_b(path: str, text: str, functions: list[Function]) -> list[Finding]:
    by_class: dict[str, list[tuple[Function, set[str]]]] = {}
    for function in functions:
        if TEARDOWN_NAME.match(function.name) is None:
            continue
        members: set[str] = cleared_members(text, function)
        if len(members) < 2:
            continue
        by_class.setdefault(function.class_name, []).append((function, members))
    found: list[Finding] = []
    for class_name in sorted(by_class):
        paths: list[tuple[Function, set[str]]] = by_class[class_name]
        for first_index in range(len(paths)):
            for second_index in range(first_index + 1, len(paths)):
                first: tuple[Function, set[str]] = paths[first_index]
                second: tuple[Function, set[str]] = paths[second_index]
                if first[0].name == second[0].name or not (first[1] & second[1]):
                    continue
                only_first: list[str] = sorted(first[1] - second[1])
                only_second: list[str] = sorted(second[1] - first[1])
                # Each path leaves something the other clears; a path that
                # clears a subset of another's is the narrower teardown.
                if not only_first or not only_second:
                    continue
                detail: str = (first[0].name + ' alone clears ' + ', '.join(only_first) + '; ' +
                               second[0].name + ' alone clears ' + ', '.join(only_second))
                found.append(Finding(
                    path, line_of(text, second[0].body_start), 'b', class_name + '::' + second[0].name, detail,
                    'After either path, is every member the other clears still meaningful, or should one '
                    'teardown (a shared reset) clear the whole set?'))
    return found


def rule_c(path: str, text: str, function: Function) -> list[Finding]:
    if APPLY_NAME.match(function.name) is None:
        return []
    body: str = text[function.body_start:function.body_end]
    found: list[Finding] = []
    for match in CACHED_RETURN.finditer(body):
        member: str = match.group(1) or match.group(2)
        offset: int = function.body_start + match.start()
        found.append(Finding(
            path, line_of(text, offset), 'c', function.class_name + '::' + function.name,
            'returns early when ' + member + ' already equals the wanted value',
            'Is ' + member + ' the only record of what was applied? If the applied state can change '
            'elsewhere, the early return skips the repair: compare against the state itself.'))
    return found


def finding_order(finding: Finding) -> tuple[int, str]:
    order: tuple[int, str] = (finding.line, finding.rule)
    return order


def check_state_agreement(path: Path, source: str, rules: str = 'abc') -> list[Finding]:
    """The candidates in one source file."""
    text: str = blank(source)
    lines: list[str] = source.split('\n')
    functions: list[Function] = functions_of(text)
    candidates: list[Finding] = []
    for function in functions:
        if 'a' in rules:
            candidates.extend(rule_a(str(path), text, function))
        if 'c' in rules:
            candidates.extend(rule_c(str(path), text, function))
    if 'b' in rules:
        candidates.extend(rule_b(str(path), text, functions))
    kept: list[Finding] = [finding for finding in candidates if not reviewed(lines, finding.line)]
    kept.sort(key=finding_order)
    return kept


def sources_under(paths: list[Path]) -> list[Path]:
    found: list[Path] = []
    for root in paths:
        candidates: list[Path] = [root] if root.is_file() else sorted(root.rglob('*'))
        for candidate in candidates:
            if candidate.suffix not in SUFFIXES or not candidate.is_file():
                continue
            if any(part in SKIPPED_PARTS for part in candidate.parts):
                continue
            found.append(candidate)
    return found


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('paths', nargs='*', type=Path, default=[Path('src')])
    parser.add_argument('--strict', action='store_true', help='exit 1 when there are candidates')
    parser.add_argument('--json', type=Path, help='also write the candidates as JSON')
    parser.add_argument('--rule', default='abc', help='the rules to run (letters of abc)')
    options: argparse.Namespace = parser.parse_args(arguments)
    findings: list[Finding] = []
    for source in sources_under(options.paths):
        findings.extend(check_state_agreement(source, source.read_text(encoding='utf-8', errors='replace'),
                                              options.rule))
    counts: dict[str, int] = {'a': 0, 'b': 0, 'c': 0}
    for finding in findings:
        counts[finding.rule] += 1
        print(f'{finding.path}:{finding.line}: [{finding.rule}] {finding.function}: {finding.detail}\n'
              f'    review: {finding.question}')
    print(f'state-agreement: {len(findings)} candidates (a {counts["a"]}, b {counts["b"]}, c {counts["c"]}); '
          'report-only, review each')
    if options.json is not None:
        records: list[dict[str, object]] = [finding.__dict__ for finding in findings]
        options.json.write_text(json.dumps(records, indent=1) + '\n', encoding='utf-8')
    if options.strict and findings:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
