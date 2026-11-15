#!/usr/bin/env python3
"""exit-releases-nothing: the termination rule for the C ABI's static state.

Rule 1 (owner): process termination releases none of our state. A static
object with a destructor is destroyed at exit, on the exiting thread, while
other threads may still run (Linux, macOS), or under the loader lock at
process detach (Windows); a C ABI table held that way releases client
callbacks and controls at exit (audit 2026-11-15, A-gf-abi-03-F2 and
A-gf-abi-01-F5). The stable registry shows the accepted form:

    static HandleRegistry* const instance = new HandleRegistry();

The rule, over src/ (the C ABI first, A-gf-abi-03-F2; all of src/ since,
as the rule holds everywhere): every object of static storage duration is
either never destroyed (a `* const` pointer to an object made with `new`),
or of a type the source shows is trivially destructible: a scalar, a
`std::atomic`, a C record of the ABI (`gf_*`, `gd_*`), or a type named in a
`static_assert(std::is_trivially_destructible_v<Type>)` in the same file.
A Windows scalar or handle (`DWORD`, `LONG`, `HWND`, ...) and GLib's
`GType` count as scalars. A `static` with a parenthesised list is read as
a function declaration and skipped.
Two forms are read: a function-local `static` (an indented declaration),
and a namespace-scope object (an unindented declaration). Whether a type's
destructor has work to do comes from tools/destructor_index.py, which reads
the first-party records, enums and aliases of every file in the run: a
record owns when it declares a destructor or a base or member owns, and
standard types come from a fixed table. A local static passes when its type
reads as trivial; a namespace-scope object is a finding when its type reads
as owning (an unknown type, third-party or undefined in the run, passes
there, since most of those are C records and plain tables).

Run by tools/check_lifetime_rules.py after the harness's rules (the shim
there), on the same paths; files outside src/ are skipped. A heuristic
over source with comments and literals masked; review still required.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import re

import destructor_index

RULE: str = 'exit-releases-nothing'

LOCAL_STATIC: re.Pattern[str] = re.compile(
    r'^(?P<indent>[ \t]+)static[ \t]+(?P<type>(?:const[ \t]+)?[A-Za-z_][\w:]*(?:[ \t]*<[^;{}()\n]*>)?(?:[ \t]*(?:const[ \t]*)?\*[ \t]*(?:const[ \t]*)?)?)'
    r'[ \t]*(?P<name>[A-Za-z_]\w*)[ \t]*(?P<init>\{[^;\n]*\}|=[^;\n]*|\([^;\n]*\))?[ \t]*;[ \t]*$',
    re.MULTILINE)
# An unindented declaration of an object: a namespace-scope object in the
# house style's layout (record members and function bodies are indented).
NAMESPACE_OBJECT: re.Pattern[str] = re.compile(
    r'^(?:(?:static|inline)[ \t]+)*(?:const[ \t]+)?(?P<type>(?:::)?[A-Za-z_][\w:]*(?:[ \t]*<[^;{}()\n]*>)?'
    r'(?:[ \t]*(?:const[ \t]*)?\*[ \t]*(?:const[ \t]*)?)?)[ \t]+(?P<name>[A-Za-z_]\w*)[ \t]*'
    r'(?P<init>\{[^;\n]*\}|=[^;\n]*)?[ \t]*;[ \t]*$',
    re.MULTILINE)
# First words that make an unindented line something other than an object.
NOT_AN_OBJECT: re.Pattern[str] = re.compile(
    r'^(?:return|using|typedef|extern|friend|template|namespace|constexpr|constinit|thread_local|struct|class|union|enum'
    r'|goto|delete|throw|co_return|static_assert|case|default|public|private|protected|else|do)\b')
TRIVIAL_TYPE: re.Pattern[str] = re.compile(
    r'^(?:const\s+)?(?:bool|char|signed char|unsigned char|short|unsigned short|int|unsigned|unsigned int|long|unsigned long'
    r'|long long|unsigned long long|float|double|std::size_t|std::ptrdiff_t|std::u?int(?:8|16|32|64)_t|std::uintptr_t'
    r'|std::atomic\s*<[^;{}()]*>|std::atomic_\w+|std::thread::id|std::once_flag|(?:gf|gd)_\w+'
    r'|BOOL|BYTE|WORD|DWORD|UINT|LONG|ULONG|LONG_PTR|ULONG_PTR|H[A-Z]+|GType)$')
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
    if 'testdata' in parts:
        # This check's own fixtures (tools/testdata/exit_statics) hold
        # findings on purpose; its tests check them under a src/ path.
        return False
    for index in range(len(parts) - 1):
        if parts[index] == 'src':
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
    if re.search(r'\*(?: ?const)?$', bare) is not None:
        # A pointer itself is trivially destructible; what it points to is
        # released only by an explicit delete, which this rule does not see.
        return True
    name: str = re.sub(r'^const\s+', '', bare)
    result: bool = name in asserted or name.split('::')[-1] in asserted
    return result


def check_exit_statics(path: Path, text: str, index: destructor_index.TypeIndex | None = None) -> list[Finding]:
    """Findings for one file; `text` is its source with comments and literals masked.

    `index` holds the first-party types the run has read (the shim indexes
    every selected file first); without one, the file's own definitions are
    indexed. A type the index reads as trivially destructible passes; at
    namespace scope, a type it reads as owning is a finding, and an unknown
    one (a third-party type, or one defined in no indexed file) passes."""
    if not in_scope(path):
        return []
    if index is None:
        index = destructor_index.TypeIndex()
        index.add(path, text)
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
        if init.startswith('('):
            # A parenthesised list reads as a function declaration (a class's
            # static member function, or a declaration in a body); the house
            # style initializes objects with braces or `=`.
            continue
        if never_destroyed(type_text, init) or trivially_destructible(type_text, asserted):
            continue
        if index.classify(type_text, path) == destructor_index.TRIVIAL:
            continue
        findings.append(Finding(str(path), line_of(text, match.start('name')), RULE,
                                f"static '{match.group('name')}' of type '{type_text.strip()}' is destroyed at exit; "
                                'make it a never-destroyed pointer (static T* const x = new T()), or assert its '
                                'type trivially destructible'))
    for match in NAMESPACE_OBJECT.finditer(text):
        if NOT_AN_OBJECT.match(match.group(0)) is not None:
            continue
        type_text = match.group('type')
        if trivially_destructible(type_text, asserted):
            continue
        if index.classify(type_text, path) != destructor_index.OWNING:
            continue
        findings.append(Finding(str(path), line_of(text, match.start('name')), RULE,
                                f"namespace-scope '{match.group('name')}' of type '{type_text.strip()}' is "
                                'destroyed at exit with work to do; reach it through a function returning a '
                                'never-destroyed object'))
    return findings
