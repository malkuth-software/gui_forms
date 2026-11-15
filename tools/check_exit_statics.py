#!/usr/bin/env python3
"""exit-releases-nothing: the termination rule for the C ABI's static state.

Rule 1 (owner): process termination releases none of our state. A static
object with a destructor is destroyed at exit, on the exiting thread, while
other threads may still run (Linux, macOS), or under the loader lock at
process detach (Windows); a C ABI table held that way releases client
callbacks and controls at exit (audit 2026-11-15, A-gf-abi-03-F2 and
A-gf-abi-01-F5). The stable registry shows the accepted form:

    static HandleRegistry* const instance = new HandleRegistry();

The rule, over src/abi/: every object of static storage duration is
either never destroyed (a `* const` pointer to an object made with `new`),
or of a type the source shows is trivially destructible: a scalar, a
`std::atomic`, a C record of the ABI (`gf_*`, `gd_*`), or a type named in a
`static_assert(std::is_trivially_destructible_v<Type>)` in the same file.
Two forms are read: a function-local `static` (an indented declaration),
and a namespace-scope object of a standard type that owns memory or a
lock (an unindented declaration of `std::mutex`, `std::vector<...>`,
`std::unordered_map<...>` and the like).

Run by tools/check_lifetime_rules.py after the harness's rules (the shim
there), on the same paths; files outside src/abi/ are skipped. A heuristic
over source with comments and literals masked; review still required.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import re

RULE: str = 'exit-releases-nothing'

LOCAL_STATIC: re.Pattern[str] = re.compile(
    r'^(?P<indent>[ \t]+)static[ \t]+(?P<type>(?:const[ \t]+)?[A-Za-z_][\w:]*(?:[ \t]*<[^;{}()\n]*>)?(?:[ \t]*(?:const[ \t]*)?\*[ \t]*(?:const[ \t]*)?)?)'
    r'[ \t]*(?P<name>[A-Za-z_]\w*)[ \t]*(?P<init>\{[^;\n]*\}|=[^;\n]*|\([^;\n]*\))?[ \t]*;[ \t]*$',
    re.MULTILINE)
NAMESPACE_OBJECT: re.Pattern[str] = re.compile(
    r'^(?:static[ \t]+)?(?:const[ \t]+)?(?P<type>std[ \t]*::[ \t]*(?:mutex|shared_mutex|recursive_mutex|condition_variable(?:_any)?'
    r'|string|vector|unordered_map|unordered_set|map|set|deque|list|unique_ptr|shared_ptr|function|optional)'
    r'\b(?:[ \t]*<[^;{}()\n]*>)?)[ \t]+(?P<name>[A-Za-z_]\w*)[ \t]*(?:\{[^;\n]*\})?[ \t]*;[ \t]*$',
    re.MULTILINE)
TRIVIAL_TYPE: re.Pattern[str] = re.compile(
    r'^(?:const\s+)?(?:bool|char|signed char|unsigned char|short|unsigned short|int|unsigned|unsigned int|long|unsigned long'
    r'|long long|unsigned long long|float|double|std::size_t|std::ptrdiff_t|std::u?int(?:8|16|32|64)_t|std::uintptr_t'
    r'|std::atomic\s*<[^;{}()]*>|std::atomic_\w+|std::thread::id|std::once_flag|(?:gf|gd)_\w+)$')
ASSERTED: re.Pattern[str] = re.compile(
    r'static_assert\s*\(\s*std\s*::\s*is_trivially_destructible_v\s*<\s*(?P<type>[\w:]+)\s*>')
CONSTEXPR: re.Pattern[str] = re.compile(r'^\s*static\s+(?:inline\s+)?(?:constexpr|constinit|thread_local)\b')


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str


def in_scope(path: Path) -> bool:
    parts: tuple[str, ...] = path.as_posix().split('/')
    for index in range(len(parts) - 1):
        if parts[index] == 'src' and parts[index + 1] == 'abi':
            return True
    return False


def line_of(text: str, offset: int) -> int:
    result: int = text.count('\n', 0, offset) + 1
    return result


def never_destroyed(type_text: str, init: str) -> bool:
    pointer: bool = re.search(r'\*\s*const\s*$', type_text) is not None
    made: bool = re.match(r'=\s*new\b', init.strip()) is not None
    return pointer and made


def trivially_destructible(type_text: str, asserted: set[str]) -> bool:
    bare: str = re.sub(r'\s+', ' ', type_text.strip())
    if TRIVIAL_TYPE.match(bare) is not None:
        return True
    if bare.endswith('*'):
        # A pointer itself is trivially destructible; what it points to is
        # released only by an explicit delete, which this rule does not see.
        return True
    name: str = re.sub(r'^const\s+', '', bare)
    result: bool = name in asserted or name.split('::')[-1] in asserted
    return result


def check_exit_statics(path: Path, text: str) -> list[Finding]:
    """Findings for one file; `text` is its source with comments and literals masked."""
    if not in_scope(path):
        return []
    asserted: set[str] = set()
    for match in ASSERTED.finditer(text):
        asserted.add(match.group('type'))
        asserted.add(match.group('type').split('::')[-1])
    findings: list[Finding] = []
    for match in LOCAL_STATIC.finditer(text):
        whole: str = match.group(0)
        if CONSTEXPR.match(whole) is not None:
            continue
        type_text: str = match.group('type')
        init: str = match.group('init') or ''
        if never_destroyed(type_text, init) or trivially_destructible(type_text, asserted):
            continue
        findings.append(Finding(str(path), line_of(text, match.start('name')), RULE,
                                f"static '{match.group('name')}' of type '{type_text.strip()}' is destroyed at exit; "
                                'make it a never-destroyed pointer (static T* const x = new T()), or assert its '
                                'type trivially destructible'))
    for match in NAMESPACE_OBJECT.finditer(text):
        findings.append(Finding(str(path), line_of(text, match.start('name')), RULE,
                                f"namespace-scope '{match.group('name')}' of type '{match.group('type').strip()}' is "
                                'destroyed at exit; reach it through a function returning a never-destroyed object'))
    return findings
