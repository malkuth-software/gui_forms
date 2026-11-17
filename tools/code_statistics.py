#!/usr/bin/env python3
"""Counts the C++ and C that GUI.Forms provides, by component and provenance.

The count covers the files git tracks at HEAD of each checkout (git ls-tree
and git cat-file, so the working tree, build output and untracked files never
enter it, and a commit always gives the same numbers). It needs no network
and no build. The checkouts are this repository (gui_forms, whose root is the
default) and any others named on the command line: the stx package repository
at the commit stx.lock pins, and STXlib at the commit stx's stxlib.lock pins.

Languages:

- C++: .cpp .cc .cxx .hpp .hh .inc .ipp, Objective-C++ .mm, and every .h
  that is not C.
- C: .c, and every .h that belongs to C code: the sources nearest to it
  (the .c and C++ source files under its directory, else under the closest
  ancestor directory that has any) are mostly .c; or it uses no C++ (no
  C++-only keyword such as namespace, class, template or constexpr, and no
  scope operator ::, outside comments and strings) and sits in a directory
  with a C interface header (one that tests __cplusplus and uses no C++,
  itself included).

Per file: total lines; blank lines (only white space); comment lines (comment
text and no code, the inside of a /* */ block included); code lines (anything
else: code, preprocessor lines, string and raw-string contents); characters
(Unicode code points of the file decoded as UTF-8, each undecodable byte one
character); bytes. The scanner knows //, /* */, string and character
literals with escapes and prefixes, raw strings R"d(...)d", digit separators
and Objective-C's @"..." literals.

Components: each top-level directory, with src/<subsystem> and
third_party/<library> split; files at a checkout's root are "(root)".

Provenance, from the repositories' own records (each rule below names its
record):

- third_party: code from outside parties, copied in and not converted to the
  house style: every path under a third_party/ directory, and stx_skia files
  still listed in stx_skia/house_style_allowance.txt. The owner's own code is
  first-party wherever it is copied (BFFT in stx_vorbis, the C thread pool
  threadpool_atomic_fast in STXlib).
- converted: first-party code converted from an outside source, the files the
  PROVENANCE.md records name as derived from, rewritten from or restating
  another code base (stx_skia/skia/, stx_zlib's inflate, stx_raster3d,
  stx_conv's surfaces, STXlib's stx_btree).
- original: everything else, first-party code written for these
  repositories, ports between first-party repositories and copies of the
  owner's own code included.

The headline figure is first-party C++ (original and converted) over every
counted checkout.

STXlib is part of what GUI.Forms provides: when a counted stx checkout pins
STXlib (stxlib.lock) and no STXlib checkout is given, the report says so.

Usage:
  python3 -B tools/code_statistics.py [--root DIR] [CHECKOUT ...]
      [--json FILE] [--markdown FILE]

CHECKOUT is PATH or NAME=PATH. A checkout's name defaults to its kind,
recognized from its files (gui_forms has stx.lock, stx has stxlib.lock and
stx_skia/, STXlib has include/stxlib/), else its directory name. --json and
--markdown write those forms ("-" for standard output); the readable table
goes to standard output unless one of them already does. Exit status 0, or 2
when a checkout cannot be read.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import subprocess
import sys

SCHEMA_VERSION: int = 1
DEFAULT_ROOT: Path = Path(__file__).resolve().parents[1]

LANGUAGE_CPP: str = 'cpp'
LANGUAGE_C: str = 'c'
LANGUAGE_ORDER: tuple[str, ...] = (LANGUAGE_CPP, LANGUAGE_C)
LANGUAGE_NAMES: dict[str, str] = {LANGUAGE_CPP: 'C++', LANGUAGE_C: 'C'}

ORIGINAL: str = 'original'
CONVERTED: str = 'converted'
THIRD_PARTY: str = 'third_party'
PROVENANCE_ORDER: tuple[str, ...] = (ORIGINAL, CONVERTED, THIRD_PARTY)
PROVENANCE_NAMES: dict[str, str] = {
    ORIGINAL: 'first-party original',
    CONVERTED: 'first-party converted',
    THIRD_PARTY: 'third-party',
}

CPP_EXTENSIONS: frozenset[str] = frozenset(
    ('.cpp', '.cc', '.cxx', '.hpp', '.hh', '.inc', '.ipp', '.mm'))
C_EXTENSIONS: frozenset[str] = frozenset(('.c',))
HEADER_EXTENSION: str = '.h'
CXX_SOURCE_EXTENSIONS: frozenset[str] = frozenset(('.cpp', '.cc', '.cxx', '.mm'))
CPLUSPLUS_MACRO: str = '__cplusplus'

KIND_GUI_FORMS: str = 'gui_forms'
KIND_STX: str = 'stx'
KIND_STXLIB: str = 'stxlib'
KIND_OTHER: str = 'other'

# Identifiers that only C++ gives meaning to; one of them (or ::) in the code
# of a .h file that tests __cplusplus keeps it C++. Words a C program might use as plain names (new,
# this, try, using) are left out.
CXX_KEYWORDS: frozenset[str] = frozenset((
    'namespace', 'class', 'template', 'typename', 'public', 'private',
    'protected', 'virtual', 'constexpr', 'consteval', 'constinit', 'nullptr',
    'operator', 'noexcept', 'decltype', 'concept', 'requires', 'static_cast',
    'reinterpret_cast', 'const_cast', 'dynamic_cast'))
RAW_STRING_PREFIXES: frozenset[str] = frozenset(('R', 'u8R', 'uR', 'UR', 'LR'))
ENCODING_PREFIXES: frozenset[str] = frozenset(('u8', 'u', 'U', 'L'))
RAW_DELIMITER_LIMIT: int = 16


@dataclass(frozen=True)
class PathRule:
    """A provenance class for one file (prefix without a trailing slash) or a
    directory (prefix ending in a slash) of one kind of checkout."""
    kind: str
    prefix: str
    provenance: str
    record: str


# Copies of the owner's own code, first-party wherever they sit (owner,
# 2026-11-16); these come before every other rule, third_party/ included.
OWNER_COPIES: tuple[PathRule, ...] = (
    PathRule(KIND_STX, 'stx_vorbis/third_party/bfft/', ORIGINAL,
             'stx_vorbis/third_party/bfft/provenance.json: the owner\'s BFFT, falseywinchnet/bfft 0f75ca79'),
    PathRule(KIND_STXLIB, 'tests/reference/', ORIGINAL,
             'PROVENANCE.md, threadpool_atomic_fast: the owner\'s C original, kept unchanged'),
)

# The records' conversions that the generic rules (third_party/, the
# stx_skia allowance) do not see.
PATH_RULES: tuple[PathRule, ...] = (
    PathRule(KIND_STX, 'stx_skia/skia/', CONVERTED,
             'stx_skia/PROVENANCE.md: a subset of Skia chrome/m152 (2a9b593b), '
             'converted to the house style under stx_skia/house_style_allowance.txt'),
    PathRule(KIND_STX, 'stx_zlib/src/inflate.cpp', CONVERTED,
             'stx_zlib/PROVENANCE.md: tree construction and decoding derived from '
             'zlib contrib/puff 2.3'),
    PathRule(KIND_STX, 'stx_raster3d/src/', CONVERTED,
             'stx_raster3d/PROVENANCE.md: a house-style rewrite of conv-teapot fc0a609'),
    PathRule(KIND_STX, 'stx_raster3d/include/', CONVERTED,
             'stx_raster3d/PROVENANCE.md: a house-style rewrite of conv-teapot fc0a609'),
    PathRule(KIND_STX, 'stx_conv/include/stx_conv/surface.hpp', CONVERTED,
             'stx_conv/PROVENANCE.md, Surfaces: restates conv-teapot fc0a609'),
    PathRule(KIND_STX, 'stx_conv/src/surface.cpp', CONVERTED,
             'stx_conv/PROVENANCE.md, Surfaces: restates conv-teapot fc0a609'),
    PathRule(KIND_STX, 'stx_conv/src/detail/surface_kernel.hpp', CONVERTED,
             'stx_conv/PROVENANCE.md, Surfaces: restates conv-teapot fc0a609'),
    PathRule(KIND_STXLIB, 'src/algorithms/stx_btree/', CONVERTED,
             'PROVENANCE.md, batch 3c6: the C++ form of bbtree (Xiangpeng Hao)'),
)
STX_SKIA_ALLOWANCE: str = 'stx_skia/house_style_allowance.txt'
STX_SKIA_ROOT: str = 'stx_skia/'
THIRD_PARTY_DIRECTORY: str = 'third_party'
DISPLAY_NAMES: dict[str, str] = {KIND_STXLIB: 'STXlib'}
LOCK_COMMIT: re.Pattern[str] = re.compile(r'^commit = "([0-9a-f]{40})"$', re.MULTILINE)


@dataclass
class Counts:
    """Totals over a set of files."""
    files: int = 0
    lines: int = 0
    blank: int = 0
    comment: int = 0
    code: int = 0
    characters: int = 0
    bytes: int = 0

    def add(self, other: Counts) -> None:
        self.files += other.files
        self.lines += other.lines
        self.blank += other.blank
        self.comment += other.comment
        self.code += other.code
        self.characters += other.characters
        self.bytes += other.bytes

    def as_dictionary(self) -> dict[str, int]:
        return {
            'files': self.files, 'lines': self.lines, 'blank': self.blank,
            'comment': self.comment, 'code': self.code,
            'characters': self.characters, 'bytes': self.bytes,
        }


@dataclass(frozen=True)
class SourceScan:
    """Line classes of one file, and whether its code uses C++."""
    lines: int
    blank: int
    comment: int
    code: int
    uses_cxx: bool
    tests_cplusplus: bool


# Scanner states between characters and across line ends.
MODE_CODE: int = 0
MODE_BLOCK_COMMENT: int = 1
MODE_LINE_COMMENT: int = 2
MODE_STRING: int = 3
MODE_CHARACTER: int = 4
MODE_RAW_STRING: int = 5


def split_lines(text: str) -> list[str]:
    """The file's lines without their terminators (LF or CRLF); a last line
    without a terminator counts, an empty file has none."""
    if text == '':
        return []
    lines: list[str] = text.split('\n')
    if text.endswith('\n'):
        lines.pop()
    stripped: list[str] = []
    for line in lines:
        if line.endswith('\r'):
            stripped.append(line[:-1])
        else:
            stripped.append(line)
    return stripped


def skip_quoted(line: str, start: int, quote: str) -> tuple[int, bool]:
    """The index after the literal's closing quote from start, and True; or
    the line's length and False when the line ends inside the literal."""
    index: int = start
    length: int = len(line)
    while index < length:
        character: str = line[index]
        if character == '\\':
            index += 2
            continue
        if character == quote:
            return index + 1, True
        index += 1
    return length, False


def skip_number(line: str, start: int) -> int:
    """The index after the preprocessing number starting at start (digits,
    letters, '.', digit separators and exponent signs)."""
    index: int = start + 1
    length: int = len(line)
    while index < length:
        character: str = line[index]
        if character.isalnum() or character == '_' or character == '.':
            index += 1
        elif character == "'" and index + 1 < length and line[index + 1].isalnum():
            index += 1
        elif character in '+-' and line[index - 1] in 'eEpP':
            index += 1
        else:
            break
    return index


def skip_identifier(line: str, start: int) -> int:
    """The index after the identifier starting at start."""
    index: int = start + 1
    length: int = len(line)
    while index < length:
        character: str = line[index]
        if character.isalnum() or character == '_' or character == '$':
            index += 1
        else:
            break
    return index


def scan_source(text: str) -> SourceScan:
    """Classifies every line of a C, C++ or Objective-C++ source text."""
    blank: int = 0
    comment: int = 0
    code: int = 0
    uses_cxx: bool = False
    tests_cplusplus: bool = False
    mode: int = MODE_CODE
    raw_terminator: str = ''
    lines: list[str] = split_lines(text)
    for line in lines:
        has_code: bool = False
        has_comment: bool = False
        index: int = 0
        length: int = len(line)
        while index < length:
            if mode == MODE_BLOCK_COMMENT:
                end: int = line.find('*/', index)
                if end < 0:
                    if line[index:].strip() != '':
                        has_comment = True
                    index = length
                else:
                    has_comment = True
                    index = end + 2
                    mode = MODE_CODE
                continue
            if mode == MODE_LINE_COMMENT:
                if line[index:].strip() != '':
                    has_comment = True
                index = length
                continue
            if mode == MODE_RAW_STRING:
                has_code = True
                end_raw: int = line.find(raw_terminator, index)
                if end_raw < 0:
                    index = length
                else:
                    index = end_raw + len(raw_terminator)
                    mode = MODE_CODE
                continue
            if mode == MODE_STRING or mode == MODE_CHARACTER:
                has_code = True
                quote: str = '"' if mode == MODE_STRING else "'"
                closed: bool
                index, closed = skip_quoted(line, index, quote)
                if closed:
                    mode = MODE_CODE
                continue
            character: str = line[index]
            following: str = line[index + 1] if index + 1 < length else ''
            if character.isspace():
                index += 1
                continue
            if character == '/' and following == '/':
                has_comment = True
                mode = MODE_LINE_COMMENT
                index += 2
                continue
            if character == '/' and following == '*':
                has_comment = True
                mode = MODE_BLOCK_COMMENT
                index += 2
                continue
            has_code = True
            if character == '"':
                mode = MODE_STRING
                index += 1
                continue
            if character == "'":
                mode = MODE_CHARACTER
                index += 1
                continue
            if character.isdigit() or (character == '.' and following.isdigit()):
                index = skip_number(line, index)
                continue
            if character.isalpha() or character == '_' or character == '$':
                end_word: int = skip_identifier(line, index)
                word: str = line[index:end_word]
                index = end_word
                next_character: str = line[index] if index < length else ''
                if next_character == '"' and word in RAW_STRING_PREFIXES:
                    open_parenthesis: int = line.find('(', index + 1)
                    delimiter: str = line[index + 1:open_parenthesis] if open_parenthesis >= 0 else ''
                    if open_parenthesis >= 0 and len(delimiter) <= RAW_DELIMITER_LIMIT \
                            and '"' not in delimiter and ')' not in delimiter:
                        raw_terminator = ')' + delimiter + '"'
                        mode = MODE_RAW_STRING
                        index = open_parenthesis + 1
                    continue
                if next_character in ('"', "'") and word in ENCODING_PREFIXES:
                    mode = MODE_STRING if next_character == '"' else MODE_CHARACTER
                    index += 1
                    continue
                if word in CXX_KEYWORDS:
                    uses_cxx = True
                elif word == CPLUSPLUS_MACRO:
                    tests_cplusplus = True
                continue
            if character == ':' and following == ':':
                uses_cxx = True
                index += 2
                continue
            index += 1
        # A backslash at the very end of a line continues a // comment or a
        # quoted literal onto the next line; otherwise the line ends both.
        continued: bool = line.endswith('\\')
        if mode == MODE_LINE_COMMENT and not continued:
            mode = MODE_CODE
        if (mode == MODE_STRING or mode == MODE_CHARACTER) and not continued:
            mode = MODE_CODE
        if line.strip() == '':
            blank += 1
        elif has_code:
            code += 1
        elif has_comment:
            comment += 1
        else:
            code += 1
    return SourceScan(len(lines), blank, comment, code, uses_cxx, tests_cplusplus)


@dataclass
class SourceCensus:
    """For each directory ('' for the root), how many .c and how many C++
    source files (not headers) lie anywhere under it; and the directories
    holding a header that declares a C interface for C++ (tests __cplusplus
    and uses no C++)."""
    c_sources: dict[str, int] = field(default_factory=dict)
    cxx_sources: dict[str, int] = field(default_factory=dict)
    c_interfaces: set[str] = field(default_factory=set)


def parent_directories(path: str) -> list[str]:
    """The directories holding path, nearest first, ending with the root ''."""
    parts: list[str] = path.split('/')[:-1]
    directories: list[str] = []
    while parts:
        directories.append('/'.join(parts))
        parts.pop()
    directories.append('')
    return directories


def take_census(paths: list[str]) -> SourceCensus:
    census: SourceCensus = SourceCensus()
    for path in paths:
        suffix: str = Path(path).suffix
        if suffix in C_EXTENSIONS:
            table: dict[str, int] = census.c_sources
        elif suffix in CXX_SOURCE_EXTENSIONS:
            table = census.cxx_sources
        else:
            continue
        for directory in parent_directories(path):
            table[directory] = table.get(directory, 0) + 1
    return census


def nearest_sources_are_c(path: str, census: SourceCensus) -> bool:
    """True when the closest directory around path that holds any sources
    holds more .c than C++ sources."""
    for directory in parent_directories(path):
        c_count: int = census.c_sources.get(directory, 0)
        cxx_count: int = census.cxx_sources.get(directory, 0)
        if c_count + cxx_count > 0:
            return c_count > cxx_count
    return False


def language_of(path: str, scan: SourceScan, census: SourceCensus) -> str:
    """The language a counted file belongs to."""
    suffix: str = Path(path).suffix
    if suffix in C_EXTENSIONS:
        return LANGUAGE_C
    if suffix != HEADER_EXTENSION:
        return LANGUAGE_CPP
    if nearest_sources_are_c(path, census):
        return LANGUAGE_C
    if not scan.uses_cxx and path.rpartition('/')[0] in census.c_interfaces:
        return LANGUAGE_C
    return LANGUAGE_CPP


def is_counted(path: str) -> bool:
    suffix: str = Path(path).suffix
    return suffix in CPP_EXTENSIONS or suffix in C_EXTENSIONS or suffix == HEADER_EXTENSION


def component_of(path: str) -> str:
    """The component a path belongs to: its top-level directory, with
    src/<subsystem> and third_party/<library> split."""
    parts: list[str] = path.split('/')
    if len(parts) == 1:
        return '(root)'
    if parts[0] in ('src', THIRD_PARTY_DIRECTORY) and len(parts) > 2:
        return parts[0] + '/' + parts[1]
    return parts[0]


@dataclass
class Tree:
    """The regular files of one commit: path to blob identity."""
    blobs: dict[str, str] = field(default_factory=dict)


def run_git(checkout: Path, arguments: list[str], payload: bytes | None = None) -> bytes:
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        ['git', '-C', str(checkout)] + arguments, input=payload,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if completed.returncode != 0:
        message: str = completed.stderr.decode('utf-8', 'replace').strip()
        raise RuntimeError('git ' + ' '.join(arguments) + ' in ' + str(checkout) + ': ' + message)
    return completed.stdout


def read_tree(checkout: Path) -> Tree:
    """Every regular file of HEAD (symbolic links and submodules left out)."""
    listing: bytes = run_git(checkout, ['ls-tree', '-r', '-z', '--full-tree', 'HEAD'])
    tree: Tree = Tree()
    for entry in listing.split(b'\0'):
        if entry == b'':
            continue
        header, path = entry.split(b'\t', 1)
        fields: list[bytes] = header.split(b' ')
        if fields[0] not in (b'100644', b'100755') or fields[1] != b'blob':
            continue
        tree.blobs[path.decode('utf-8', 'surrogateescape')] = fields[2].decode('ascii')
    return tree


def read_blobs(checkout: Path, identities: list[str]) -> dict[str, bytes]:
    """The contents of the named blobs, through one git cat-file --batch."""
    contents: dict[str, bytes] = {}
    if not identities:
        return contents
    unique: list[str] = sorted(set(identities))
    payload: bytes = ('\n'.join(unique) + '\n').encode('ascii')
    output: bytes = run_git(checkout, ['cat-file', '--batch'], payload)
    position: int = 0
    for identity in unique:
        header_end: int = output.index(b'\n', position)
        header: list[bytes] = output[position:header_end].split(b' ')
        if len(header) != 3 or header[1] != b'blob':
            raise RuntimeError('unexpected cat-file header for ' + identity)
        size: int = int(header[2])
        start: int = header_end + 1
        contents[identity] = output[start:start + size]
        position = start + size + 1
    return contents


def checkout_kind(tree: Tree) -> str:
    if 'stx.lock' in tree.blobs:
        return KIND_GUI_FORMS
    if 'stxlib.lock' in tree.blobs and STX_SKIA_ALLOWANCE in tree.blobs:
        return KIND_STX
    for path in tree.blobs:
        if path.startswith('include/stxlib/'):
            return KIND_STXLIB
    return KIND_OTHER


def read_allowance(text: str) -> dict[str, int]:
    """stx_skia/house_style_allowance.txt as repository path to finding count."""
    allowance: dict[str, int] = {}
    for line in text.splitlines():
        stripped: str = line.strip()
        if stripped == '' or stripped.startswith('#'):
            continue
        fields: list[str] = stripped.split(None, 1)
        if len(fields) == 2 and fields[0].isdigit():
            allowance[STX_SKIA_ROOT + fields[1]] = int(fields[0])
    return allowance


@dataclass(frozen=True)
class ProvenanceRules:
    kind: str
    allowance: dict[str, int]


def rule_matches(rule: PathRule, path: str, kind: str) -> bool:
    if rule.kind != kind:
        return False
    if rule.prefix.endswith('/'):
        return path.startswith(rule.prefix)
    return path == rule.prefix


def provenance_of(path: str, rules: ProvenanceRules) -> str:
    """The provenance class of one counted file."""
    for rule in OWNER_COPIES:
        if rule_matches(rule, path, rules.kind):
            return rule.provenance
    if THIRD_PARTY_DIRECTORY in path.split('/')[:-1]:
        return THIRD_PARTY
    if rules.allowance.get(path, 0) > 0:
        return THIRD_PARTY
    for rule in PATH_RULES:
        if rule_matches(rule, path, rules.kind):
            return rule.provenance
    return ORIGINAL


@dataclass
class Checkout:
    name: str
    kind: str
    commit: str
    commit_date: str
    lock_commits: dict[str, str]
    pinned_commit: str = ''
    rows: dict[tuple[str, str, str], Counts] = field(default_factory=dict)
    extensions: dict[str, int] = field(default_factory=dict)
    undecodable_files: int = 0


def lock_commit(contents: bytes) -> str:
    match: re.Match[str] | None = LOCK_COMMIT.search(contents.decode('utf-8', 'replace'))
    return match.group(1) if match is not None else ''


def count_checkout(path: Path, name: str) -> Checkout:
    """Reads HEAD of one checkout and counts its C++ and C files."""
    tree: Tree = read_tree(path)
    kind: str = checkout_kind(tree)
    commit: str = run_git(path, ['rev-parse', 'HEAD']).decode('ascii').strip()
    commit_date: str = run_git(path, ['log', '-1', '--format=%cs', 'HEAD']).decode('ascii').strip()
    counted: list[str] = sorted(candidate for candidate in tree.blobs if is_counted(candidate))
    extra: list[str] = [lock for lock in ('stx.lock', 'stxlib.lock', STX_SKIA_ALLOWANCE) if lock in tree.blobs]
    identities: list[str] = [tree.blobs[candidate] for candidate in counted + extra]
    contents: dict[str, bytes] = read_blobs(path, identities)
    lock_commits: dict[str, str] = {}
    if 'stx.lock' in tree.blobs:
        lock_commits[KIND_STX] = lock_commit(contents[tree.blobs['stx.lock']])
    if 'stxlib.lock' in tree.blobs:
        lock_commits[KIND_STXLIB] = lock_commit(contents[tree.blobs['stxlib.lock']])
    allowance: dict[str, int] = {}
    if STX_SKIA_ALLOWANCE in tree.blobs:
        allowance = read_allowance(contents[tree.blobs[STX_SKIA_ALLOWANCE]].decode('utf-8', 'replace'))
    rules: ProvenanceRules = ProvenanceRules(kind, allowance)
    census: SourceCensus = take_census(counted)
    result: Checkout = Checkout(name if name != '' else (kind if kind != KIND_OTHER else path.resolve().name),
                                kind, commit, commit_date, lock_commits)
    scans: dict[str, SourceScan] = {}
    characters: dict[str, int] = {}
    for source_path in counted:
        data: bytes = contents[tree.blobs[source_path]]
        text: str
        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError:
            text = data.decode('utf-8', 'surrogateescape')
            result.undecodable_files += 1
        scans[source_path] = scan_source(text)
        characters[source_path] = len(text)
        if Path(source_path).suffix == HEADER_EXTENSION and scans[source_path].tests_cplusplus \
                and not scans[source_path].uses_cxx:
            census.c_interfaces.add(source_path.rpartition('/')[0])
    for source_path in counted:
        scan: SourceScan = scans[source_path]
        language: str = language_of(source_path, scan, census)
        key: tuple[str, str, str] = (component_of(source_path), language, provenance_of(source_path, rules))
        file_counts: Counts = Counts(1, scan.lines, scan.blank, scan.comment, scan.code,
                                     characters[source_path], len(contents[tree.blobs[source_path]]))
        if key not in result.rows:
            result.rows[key] = Counts()
        result.rows[key].add(file_counts)
        suffix: str = Path(source_path).suffix
        result.extensions[suffix] = result.extensions.get(suffix, 0) + 1
    return result


def attach_pins(checkouts: list[Checkout]) -> None:
    """Records, for stx and STXlib checkouts, the commit another checkout's
    lock pins (gui_forms' stx.lock, stx's stxlib.lock)."""
    for checkout in checkouts:
        for other in checkouts:
            pinned: str = other.lock_commits.get(checkout.kind, '')
            if pinned != '' and other is not checkout:
                checkout.pinned_commit = pinned


@dataclass
class Report:
    checkouts: list[Checkout]

    def total(self) -> Counts:
        total: Counts = Counts()
        for checkout in self.checkouts:
            for counts in checkout.rows.values():
                total.add(counts)
        return total

    def first_party_cpp(self) -> Counts:
        """The headline figure: first-party C++, original and converted, in
        every counted checkout."""
        result: Counts = self.by(LANGUAGE_CPP, ORIGINAL, None)
        result.add(self.by(LANGUAGE_CPP, CONVERTED, None))
        return result

    def headline_label(self) -> str:
        names: list[str] = []
        for checkout in self.checkouts:
            names.append(DISPLAY_NAMES.get(checkout.name, checkout.name))
        joined: str = names[0] if len(names) == 1 else ', '.join(names[:-1]) + ' and ' + names[-1]
        return 'first-party C++ (' + joined + ')'

    def uncounted_pins(self) -> list[tuple[str, str]]:
        """(kind, pinned commit) for each checkout a counted lock pins that
        was not given."""
        counted_kinds: set[str] = set()
        for checkout in self.checkouts:
            counted_kinds.add(checkout.kind)
        missing: list[tuple[str, str]] = []
        for checkout in self.checkouts:
            for kind in sorted(checkout.lock_commits):
                if kind not in counted_kinds and checkout.lock_commits[kind] != '':
                    missing.append((kind, checkout.lock_commits[kind]))
        return missing

    def by(self, language: str | None, provenance: str | None, checkout_name: str | None) -> Counts:
        result: Counts = Counts()
        for checkout in self.checkouts:
            if checkout_name is not None and checkout.name != checkout_name:
                continue
            for key, counts in checkout.rows.items():
                if language is not None and key[1] != language:
                    continue
                if provenance is not None and key[2] != provenance:
                    continue
                result.add(counts)
        return result

    def component_rows(self) -> list[tuple[str, str, str, Counts]]:
        """(checkout, component, language, counts) in checkout order, then by
        component and language."""
        rows: list[tuple[str, str, str, Counts]] = []
        for checkout in self.checkouts:
            merged: dict[tuple[str, str], Counts] = {}
            for key, counts in checkout.rows.items():
                component_key: tuple[str, str] = (key[0], key[1])
                if component_key not in merged:
                    merged[component_key] = Counts()
                merged[component_key].add(counts)
            for component_key in sorted(merged, key=component_sort_key):
                rows.append((checkout.name, component_key[0], component_key[1], merged[component_key]))
        return rows


def component_sort_key(key: tuple[str, str]) -> tuple[str, int]:
    return (key[0], LANGUAGE_ORDER.index(key[1]))


def row_sort_key(key: tuple[str, str, str]) -> tuple[str, int, int]:
    return (key[0], LANGUAGE_ORDER.index(key[1]), PROVENANCE_ORDER.index(key[2]))


def report_json(report: Report) -> dict[str, object]:
    """The report with stable keys; counts are plain integers."""
    checkouts: list[dict[str, object]] = []
    rows: list[dict[str, object]] = []
    for checkout in report.checkouts:
        description: dict[str, object] = {
            'name': checkout.name, 'kind': checkout.kind, 'commit': checkout.commit,
            'commit_date': checkout.commit_date, 'pinned_commit': checkout.pinned_commit,
            'at_pin': checkout.pinned_commit == checkout.commit if checkout.pinned_commit != '' else None,
            'extensions': dict(sorted(checkout.extensions.items())),
            'undecodable_files': checkout.undecodable_files,
            'totals': report.by(None, None, checkout.name).as_dictionary(),
        }
        checkouts.append(description)
        for key in sorted(checkout.rows, key=row_sort_key):
            row: dict[str, object] = {'checkout': checkout.name, 'component': key[0],
                                      'language': key[1], 'provenance': key[2]}
            row.update(checkout.rows[key].as_dictionary())
            rows.append(row)
    by_language: dict[str, object] = {}
    for language in LANGUAGE_ORDER:
        by_language[language] = report.by(language, None, None).as_dictionary()
    by_provenance: dict[str, object] = {}
    for provenance in PROVENANCE_ORDER:
        per_language: dict[str, object] = {}
        for language in LANGUAGE_ORDER:
            per_language[language] = report.by(language, provenance, None).as_dictionary()
        per_language['all'] = report.by(None, provenance, None).as_dictionary()
        by_provenance[provenance] = per_language
    return {
        'schema': SCHEMA_VERSION,
        'checkouts': checkouts,
        'total': report.total().as_dictionary(),
        'headline': {'label': report.headline_label(),
                     'first_party_cpp': report.first_party_cpp().as_dictionary()},
        'uncounted_pins': [{'kind': kind, 'commit': commit} for kind, commit in report.uncounted_pins()],
        'by_language': by_language,
        'by_provenance': by_provenance,
        'rows': rows,
    }


COLUMNS: tuple[str, ...] = ('files', 'lines', 'blank', 'comment', 'code', 'characters', 'bytes')
COLUMN_TITLES: tuple[str, ...] = ('Files', 'Lines', 'Blank', 'Comment', 'Code', 'Characters', 'Bytes')


def grouped(value: int) -> str:
    return format(value, ',')


def count_cells(counts: Counts) -> list[str]:
    values: dict[str, int] = counts.as_dictionary()
    cells: list[str] = []
    for column in COLUMNS:
        cells.append(grouped(values[column]))
    return cells


def summary_rows(report: Report) -> list[list[str]]:
    """Language by provenance, with per-language and overall totals."""
    rows: list[list[str]] = []
    for language in LANGUAGE_ORDER:
        for provenance in PROVENANCE_ORDER:
            counts: Counts = report.by(language, provenance, None)
            if counts.files == 0:
                continue
            rows.append([LANGUAGE_NAMES[language], PROVENANCE_NAMES[provenance]] + count_cells(counts))
        rows.append([LANGUAGE_NAMES[language], 'all'] + count_cells(report.by(language, None, None)))
    for provenance in PROVENANCE_ORDER:
        rows.append(['C++ and C', PROVENANCE_NAMES[provenance]] + count_cells(report.by(None, provenance, None)))
    rows.append(['C++ and C', 'all'] + count_cells(report.total()))
    return rows


def checkout_rows(report: Report) -> list[list[str]]:
    rows: list[list[str]] = []
    for checkout in report.checkouts:
        for language in LANGUAGE_ORDER:
            counts: Counts = report.by(language, None, checkout.name)
            if counts.files > 0:
                rows.append([checkout.name, LANGUAGE_NAMES[language]] + count_cells(counts))
    return rows


def component_table_rows(report: Report) -> list[list[str]]:
    rows: list[list[str]] = []
    for checkout_name, component, language, counts in report.component_rows():
        rows.append([checkout_name, component, LANGUAGE_NAMES[language]] + count_cells(counts))
    return rows


def checkout_line(checkout: Checkout) -> str:
    line: str = checkout.name + ' ' + checkout.commit[:12] + ' (' + checkout.commit_date + ')'
    if checkout.pinned_commit != '':
        if checkout.pinned_commit == checkout.commit:
            line += ', the pinned commit'
        else:
            line += ', not the pinned ' + checkout.pinned_commit[:12]
    return line


def text_table(titles: list[str], rows: list[list[str]], left: int) -> list[str]:
    """Columns padded to their widths; the first left columns align left."""
    widths: list[int] = [len(title) for title in titles]
    for row in rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(cell))
    lines: list[str] = []
    for row in [titles] + rows:
        cells: list[str] = []
        for index, cell in enumerate(row):
            cells.append(cell.ljust(widths[index]) if index < left else cell.rjust(widths[index]))
        lines.append('  '.join(cells).rstrip())
    return lines


def markdown_table(titles: list[str], rows: list[list[str]], left: int) -> list[str]:
    alignment: list[str] = []
    for index in range(len(titles)):
        alignment.append('---' if index < left else '---:')
    lines: list[str] = ['| ' + ' | '.join(titles) + ' |', '|' + '|'.join(alignment) + '|']
    for row in rows:
        lines.append('| ' + ' | '.join(row) + ' |')
    return lines


def headline_rows(report: Report) -> list[list[str]]:
    counts: Counts = report.first_party_cpp()
    return [[report.headline_label(), grouped(counts.files), grouped(counts.lines),
             grouped(counts.code), grouped(counts.characters)]]


HEADLINE_TITLES: list[str] = ['Figure', 'Files', 'Lines', 'Code', 'Characters']


def uncounted_lines(report: Report) -> list[str]:
    lines: list[str] = []
    for kind, commit in report.uncounted_pins():
        lines.append(DISPLAY_NAMES.get(kind, kind) + ' (pinned at ' + commit[:12] + ') is not counted: '
                     'give its checkout to count it.')
    return lines


def render_text(report: Report) -> str:
    lines: list[str] = ['C++ and C at HEAD of each checkout (tools/code_statistics.py)']
    for checkout in report.checkouts:
        lines.append('  ' + checkout_line(checkout))
    for note in uncounted_lines(report):
        lines.append('  ' + note)
    lines.append('')
    lines += text_table(HEADLINE_TITLES, headline_rows(report), 1)
    lines.append('')
    lines.append('By language and provenance')
    lines += text_table(['Language', 'Provenance'] + list(COLUMN_TITLES), summary_rows(report), 2)
    lines.append('')
    lines.append('By checkout')
    lines += text_table(['Checkout', 'Language'] + list(COLUMN_TITLES), checkout_rows(report), 2)
    lines.append('')
    lines.append('By component')
    lines += text_table(['Checkout', 'Component', 'Language'] + list(COLUMN_TITLES), component_table_rows(report), 3)
    return '\n'.join(lines) + '\n'


def render_markdown(report: Report) -> str:
    sources: list[str] = []
    for checkout in report.checkouts:
        sources.append(checkout.name + ' `' + checkout.commit[:12] + '` (' + checkout.commit_date + ')')
    lines: list[str] = [
        'Counted by `tools/code_statistics.py` at ' + ', '.join(sources) + '.',
        '',
    ]
    for note in uncounted_lines(report):
        lines += [note, '']
    lines += markdown_table(HEADLINE_TITLES, headline_rows(report), 1)
    lines += ['', '**By language and provenance**', '']
    lines += markdown_table(['Language', 'Provenance'] + list(COLUMN_TITLES), summary_rows(report), 2)
    lines += ['', '**By checkout**', '']
    lines += markdown_table(['Checkout', 'Language'] + list(COLUMN_TITLES), checkout_rows(report), 2)
    lines += ['', '**By component**', '']
    lines += markdown_table(['Checkout', 'Component', 'Language'] + list(COLUMN_TITLES),
                            component_table_rows(report), 3)
    return '\n'.join(lines) + '\n'


def parse_checkout(argument: str) -> tuple[str, Path]:
    """NAME=PATH or PATH."""
    if '=' in argument:
        name, path = argument.split('=', 1)
        return name, Path(path)
    return '', Path(argument)


def collect(root: Path, extra: list[str]) -> Report:
    """Counts the root checkout and each extra checkout once, in order."""
    requested: list[tuple[str, Path]] = [('', root)]
    for argument in extra:
        requested.append(parse_checkout(argument))
    seen: set[Path] = set()
    checkouts: list[Checkout] = []
    for name, path in requested:
        resolved: Path = path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        checkouts.append(count_checkout(resolved, name))
    attach_pins(checkouts)
    return Report(checkouts)


def write_output(destination: str, text: str) -> None:
    if destination == '-':
        sys.stdout.write(text)
    else:
        Path(destination).write_text(text, encoding='utf-8')


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description='Count the C++ and C at HEAD of GUI.Forms and the given checkouts.')
    parser.add_argument('--root', type=Path, default=DEFAULT_ROOT,
                        help='the gui_forms checkout (default: this repository)')
    parser.add_argument('checkouts', nargs='*', metavar='CHECKOUT',
                        help='another checkout, as PATH or NAME=PATH (stx, STXlib)')
    parser.add_argument('--json', metavar='FILE', help='write the JSON report ("-" for standard output)')
    parser.add_argument('--markdown', metavar='FILE', help='write the Markdown tables ("-" for standard output)')
    options: argparse.Namespace = parser.parse_args(arguments)
    report: Report
    try:
        report = collect(options.root, options.checkouts)
    except (RuntimeError, OSError) as error:
        sys.stderr.write('code_statistics: ' + str(error) + '\n')
        return 2
    if options.json is not None:
        write_output(options.json, json.dumps(report_json(report), indent=2, sort_keys=True) + '\n')
    if options.markdown is not None:
        write_output(options.markdown, render_markdown(report))
    if options.json != '-' and options.markdown != '-':
        sys.stdout.write(render_text(report))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
