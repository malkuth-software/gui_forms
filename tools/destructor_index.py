#!/usr/bin/env python3
"""Whether a type is destroyed with work to do, read from first-party source.

tools/check_exit_statics.py (rule exit-releases-nothing) asks, for an
object of static storage duration, whether its type's destructor is
trivial. The compiler knows; this module answers from the source the check
already reads, without a compile database, so the check runs where the
other static checks run (the laptop, CI's static job) with no clang:

- A first-party record (struct, class or union defined in the files
  indexed) owns, meaning its destructor has work to do, when it declares a
  destructor that is not `= default`, or a base or data member owns. It is
  trivial when every base and member is trivial. A first-party enum is
  trivial. A `using` or `typedef` alias is followed.
- A standard type is classified from a fixed table: pointers, references,
  scalars, atomics, views and spans are trivial; strings, containers, smart
  pointers, functions, mutexes, threads and paths own; std::array,
  std::optional, std::pair, std::tuple and std::variant follow their
  arguments.
- Anything else (a third-party type, a template parameter, a name defined
  in no indexed file, or a name with conflicting definitions in other files)
  is unknown.

The reading is a heuristic over masked source (comments and literals
blanked), for the house style's layout: a record's members are declared
one per statement at the record's top level. Where it is wrong, the
compiler's answer wins: a static_assert(std::is_trivially_destructible_v<T>)
in the file states it, and check_exit_statics accepts it. A reading from
clang's AST (destructor triviality as the compiler sees it, the
definitionData of each CXXRecordDecl) needs the harness's AST reader and a
compile database, which the static jobs do not have.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
import re

TRIVIAL: str = 'trivial'
OWNING: str = 'owning'
UNKNOWN: str = 'unknown'

RECORD: re.Pattern[str] = re.compile(
    r'\b(?P<keyword>struct|class|union)\s+(?:alignas\s*\([^)]*\)\s*)?(?:\[\[[^\]]*\]\]\s*)?(?:[A-Z][A-Z0-9_]*_API\s+)?'
    r'(?P<name>[A-Za-z_]\w*)\s*(?:final\s*)?(?::\s*(?P<bases>[^{;()]*))?\{')
ENUM: re.Pattern[str] = re.compile(r'\benum\s+(?:class\s+|struct\s+)?(?:[A-Z][A-Z0-9_]*_API\s+)?(?P<name>[A-Za-z_]\w*)\s*[:{]')
ALIAS: re.Pattern[str] = re.compile(r'\busing\s+(?P<name>[A-Za-z_]\w*)\s*=\s*(?P<type>[^;]+);')
TYPEDEF: re.Pattern[str] = re.compile(r'\btypedef\s+(?P<type>[^;()]+?)\s+(?P<name>[A-Za-z_]\w*)\s*;')
ACCESS: re.Pattern[str] = re.compile(r'^\s*(?:public|private|protected)\s*:')
ATTRIBUTE: re.Pattern[str] = re.compile(r'\[\[[^\]]*\]\]')
SKIPPED_STATEMENT: re.Pattern[str] = re.compile(
    r'^(?:using|typedef|friend|static|template|enum|struct|class|union|static_assert|explicit|virtual|operator|constexpr'
    r'|consteval|inline|public|private|protected)\b')
SCALAR: re.Pattern[str] = re.compile(
    r'^(?:bool|char|signed char|unsigned char|char8_t|char16_t|char32_t|wchar_t|short|unsigned short|int|unsigned'
    r'|unsigned int|long|unsigned long|long long|unsigned long long|float|double|long double|void'
    r'|std::size_t|size_t|std::ptrdiff_t|std::u?int(?:_least|_fast)?(?:8|16|32|64)_t|u?int(?:8|16|32|64)_t'
    r'|std::u?intptr_t|std::u?intmax_t|std::byte|std::nullptr_t|std::max_align_t|std::thread::id|std::once_flag'
    r'|std::memory_order|std::errc|std::strong_ordering|std::partial_ordering|std::weak_ordering'
    r'|BOOL|BYTE|WORD|DWORD|UINT|LONG|ULONG|LONG_PTR|ULONG_PTR|UINT_PTR|WPARAM|LPARAM|LRESULT|HRESULT|ATOM|COLORREF'
    r'|H[A-Z]+|GType|gboolean|gint|guint|gsize|FT_\w+|hb_codepoint_t|(?:gf|gd)_\w+)$')
STANDARD_TRIVIAL: frozenset[str] = frozenset({
    'atomic', 'atomic_flag', 'atomic_uint64_t', 'atomic_uint32_t', 'atomic_size_t', 'atomic_bool', 'atomic_int', 'atomic_ref', 'span', 'string_view', 'basic_string_view', 'u8string_view',
    'u16string_view', 'u32string_view', 'wstring_view', 'reference_wrapper', 'bitset', 'source_location',
    'chrono::duration', 'chrono::time_point', 'chrono::nanoseconds', 'chrono::microseconds', 'chrono::milliseconds',
    'chrono::seconds', 'chrono::minutes', 'chrono::hours', 'chrono::steady_clock::time_point',
    'chrono::system_clock::time_point', 'in_place_t', 'monostate', 'nullopt_t', 'integral_constant',
    'from_chars_result', 'to_chars_result', 'numeric_limits', 'complex'})
STANDARD_FOLLOWING: frozenset[str] = frozenset({'array', 'optional', 'pair', 'tuple', 'variant'})
STANDARD_OWNING: frozenset[str] = frozenset({
    'string', 'basic_string', 'wstring', 'u8string', 'u16string', 'u32string', 'pmr::string', 'vector', 'pmr::vector',
    'deque', 'list', 'forward_list', 'map', 'multimap', 'set', 'multiset', 'unordered_map', 'unordered_multimap',
    'unordered_set', 'unordered_multiset', 'flat_map', 'flat_set', 'queue', 'priority_queue', 'stack',
    'unique_ptr', 'shared_ptr', 'weak_ptr', 'function', 'move_only_function', 'any', 'mutex', 'timed_mutex',
    'recursive_mutex', 'recursive_timed_mutex', 'shared_mutex', 'shared_timed_mutex', 'condition_variable',
    'condition_variable_any', 'thread', 'jthread', 'stop_source', 'stop_token', 'stop_callback', 'promise', 'future',
    'shared_future', 'packaged_task', 'filesystem::path', 'filesystem::directory_iterator', 'exception_ptr', 'locale',
    'regex', 'ostringstream', 'istringstream', 'stringstream', 'ofstream', 'ifstream', 'fstream', 'unique_lock',
    'shared_lock', 'lock_guard', 'scoped_lock', 'counting_semaphore', 'binary_semaphore', 'latch', 'barrier',
    'valarray', 'error_code', 'error_category', 'runtime_error', 'logic_error', 'exception'})


@dataclass
class Record:
    path: str
    name: str
    bases: list[str] = field(default_factory=list)
    members: list[str] = field(default_factory=list)
    user_destructor: bool = False
    # A class template's parameter names, in order: substituted by the
    # arguments a use names, and unknown where a use names none.
    parameters: list[str] = field(default_factory=list)


@dataclass
class TypeIndex:
    """First-party records, enums and aliases by unqualified name, with their files."""
    records: dict[str, list[Record]] = field(default_factory=dict)
    enums: set[str] = field(default_factory=set)
    aliases: dict[str, list[tuple[str, str]]] = field(default_factory=dict)

    def add(self, path: Path, text: str) -> None:
        """Index one file's definitions; `text` is its source with comments and literals masked."""
        where: str = path.as_posix()
        for match in ENUM.finditer(text):
            self.enums.add(match.group('name'))
        for match in ALIAS.finditer(text):
            self.aliases.setdefault(match.group('name'), []).append((where, match.group('type').strip()))
        for match in TYPEDEF.finditer(text):
            self.aliases.setdefault(match.group('name'), []).append((where, match.group('type').strip()))
        for match in RECORD.finditer(text):
            opening: int = match.end() - 1
            body: str = body_of(text, opening)
            record: Record = Record(where, match.group('name'))
            record.bases = base_types(match.group('bases') or '')
            record.parameters = template_parameters(text, match.start())
            read_body(record, body)
            self.records.setdefault(record.name, []).append(record)

    def classify(self, type_text: str, path: Path) -> str:
        """TRIVIAL, OWNING or UNKNOWN for a type as written in `path`."""
        result: str = Classifier(self, path.as_posix()).classify(type_text)
        return result


def body_of(text: str, opening: int) -> str:
    """The text between the brace at `opening` and its match (the rest of the text if unmatched)."""
    depth: int = 0
    index: int = opening
    while index < len(text):
        character: str = text[index]
        if character == '{':
            depth += 1
        elif character == '}':
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
        index += 1
    return text[opening + 1:]


TEMPLATE_HEAD: re.Pattern[str] = re.compile(r'template\s*<(?P<parameters>[^<>]*(?:<[^<>]*>[^<>]*)*)>\s*$')


def template_parameters(text: str, start: int) -> list[str]:
    """The parameter names of the template head just before `start`, if any."""
    head: re.Match[str] | None = TEMPLATE_HEAD.search(text[max(start - 400, 0):start])
    if head is None:
        return []
    names: list[str] = []
    for parameter in split_top_level(head.group('parameters'), ','):
        declared: str = parameter.split('=')[0].strip()
        found: re.Match[str] | None = re.search(r'([A-Za-z_]\w*)\s*(?:\.\.\.)?\s*$', declared)
        if found is not None:
            names.append(found.group(1))
    return names


def base_types(clause: str) -> list[str]:
    bases: list[str] = []
    for part in split_top_level(clause, ','):
        cleaned: str = re.sub(r'\b(?:public|private|protected|virtual)\b', ' ', part).strip()
        if cleaned != '':
            bases.append(cleaned)
    return bases


def split_top_level(text: str, separator: str) -> list[str]:
    """`text` split at `separator` outside angle brackets, parentheses and braces."""
    parts: list[str] = []
    depth: int = 0
    start: int = 0
    for index, character in enumerate(text):
        if character in '<({[':
            depth += 1
        elif character in '>)}]':
            depth = max(depth - 1, 0)
        elif character == separator and depth == 0:
            parts.append(text[start:index])
            start = index + 1
    parts.append(text[start:])
    return parts


def top_level_statements(body: str) -> list[str]:
    """The body's statements at its own level: nested blocks (function bodies, nested
    records, brace initializers) are left out, and a function body ends its statement."""
    statements: list[str] = []
    current: list[str] = []
    depth: int = 0
    for character in body:
        if character == '{':
            if depth == 0:
                current.append(' {} ')
            depth += 1
            continue
        if character == '}':
            depth = max(depth - 1, 0)
            if depth == 0:
                text: str = ''.join(current)
                # A function body (a `)` before the block, or a const/noexcept/override
                # after it) ends the definition; a brace initializer waits for its `;`.
                if re.search(r'\)\s*(?:const|noexcept|override|final|&|&&|\s)*\{\}\s*$', text) is not None:
                    current = []
            continue
        if depth > 0:
            continue
        if character == ';':
            statements.append(''.join(current))
            current = []
            continue
        current.append(character)
    return statements


def read_body(record: Record, body: str) -> None:
    destructor: re.Pattern[str] = re.compile(r'~\s*' + re.escape(record.name) + r'\s*\(')
    for raw in top_level_statements(body):
        statement: str = ' '.join(ACCESS.sub(' ', raw).split())
        statement = ' '.join(ATTRIBUTE.sub(' ', statement).split())
        while ACCESS.match(statement) is not None:
            statement = ACCESS.sub(' ', statement).strip()
        if statement == '':
            continue
        if destructor.search(statement) is not None:
            if re.search(r'=\s*default\s*$', statement) is None:
                record.user_destructor = True
            continue
        if SKIPPED_STATEMENT.match(statement) is not None:
            continue
        declarator: str = re.sub(r'\s*(?:\{\}|=.*)$', '', statement).strip()
        declarator = re.sub(r'\s*:\s*\d+$', '', declarator)
        if '(' in declarator or declarator == '':
            continue
        declarator = re.sub(r'^mutable\s+', '', declarator)
        member: re.Match[str] | None = re.match(r'^(?P<type>.+?)\s*\b(?P<name>[A-Za-z_]\w*)\s*(?P<extent>(?:\[[^\]]*\])*)$',
                                                declarator)
        if member is None:
            continue
        type_text: str = member.group('type').strip()
        if type_text == '' or type_text.endswith(','):
            continue
        record.members.append(type_text)


def template_parts(type_text: str) -> tuple[str, list[str]]:
    """A type's name and its template arguments (empty when it has none)."""
    opening: int = type_text.find('<')
    if opening < 0 or not type_text.endswith('>'):
        return type_text, []
    name: str = type_text[:opening].strip()
    arguments: list[str] = [argument.strip() for argument in split_top_level(type_text[opening + 1:-1], ',')]
    return name, arguments


class Classifier:
    def __init__(self, index: TypeIndex, path: str) -> None:
        self.index: TypeIndex = index
        self.path: str = path
        self.visiting: set[str] = set()

    def classify(self, type_text: str) -> str:
        bare: str = ' '.join(re.sub(r'\b(?:const|volatile|mutable|typename|struct|class|enum)\b', ' ', type_text).split())
        if bare == '':
            return UNKNOWN
        if bare.endswith('*') or bare.endswith('&') or re.search(r'\(\s*\*', bare) is not None:
            return TRIVIAL
        bare = re.sub(r'^::', '', bare)
        if SCALAR.match(bare) is not None:
            return TRIVIAL
        name, arguments = template_parts(bare)
        if name.startswith('std::'):
            return self.standard(name[5:], arguments)
        last: str = name.split('::')[-1]
        if last in self.index.enums and last not in self.index.records:
            return TRIVIAL
        if last in self.index.records:
            return self.record(last, arguments)
        if last in self.index.aliases:
            return self.alias(last)
        return UNKNOWN

    def standard(self, name: str, arguments: list[str]) -> str:
        if name in STANDARD_OWNING or name.startswith('pmr::'):
            return OWNING
        if name in STANDARD_TRIVIAL or name.startswith('chrono::') or name.startswith('ranges::'):
            return TRIVIAL
        if name in STANDARD_FOLLOWING:
            followed: list[str] = arguments[:1] if name == 'array' else arguments
            return combine([self.classify(argument) for argument in followed])
        return UNKNOWN

    def chosen(self, entries: list, path_of) -> list:
        """The definitions this file sees: its own when it has one, else all of them."""
        own: list = [entry for entry in entries if path_of(entry) == self.path]
        result: list = own if own else entries
        return result

    def record(self, name: str, arguments: list[str]) -> str:
        if name in self.visiting:
            # A record reaching itself does so through a pointer or a container,
            # which the member's own classification decides.
            return TRIVIAL
        self.visiting.add(name)
        definitions: list[Record] = self.chosen(self.index.records[name], record_path)
        results: list[str] = []
        for definition in definitions:
            if definition.user_destructor:
                results.append(OWNING)
                continue
            written: list[str] = definition.bases + definition.members
            parts: list[str] = [self.classify(substituted(text, definition.parameters, arguments)) for text in written]
            results.append(combine(parts))
        self.visiting.discard(name)
        result: str = agree(results)
        return result

    def alias(self, name: str) -> str:
        if name in self.visiting:
            return UNKNOWN
        self.visiting.add(name)
        definitions: list[tuple[str, str]] = self.chosen(self.index.aliases[name], alias_path)
        results: list[str] = [self.classify(target) for _, target in definitions]
        self.visiting.discard(name)
        result: str = agree(results)
        return result


def substituted(type_text: str, parameters: list[str], arguments: list[str]) -> str:
    """`type_text` with each template parameter replaced by its argument; a parameter with
    no argument becomes a name no index defines, so it reads as unknown."""
    result: str = type_text
    for position, parameter in enumerate(parameters):
        argument: str = arguments[position] if position < len(arguments) else '__dependent__'
        result = re.sub(r'\b' + re.escape(parameter) + r'\b', argument, result)
    return result


def record_path(record: Record) -> str:
    return record.path


def alias_path(alias: tuple[str, str]) -> str:
    return alias[0]


def combine(parts: list[str]) -> str:
    """A whole's class from its parts': owning if any part owns, unknown if any is unknown."""
    if OWNING in parts:
        return OWNING
    if UNKNOWN in parts:
        return UNKNOWN
    return TRIVIAL


def agree(results: list[str]) -> str:
    """One answer for several definitions of a name: theirs when they agree, else unknown."""
    if not results:
        return UNKNOWN
    if all(result == results[0] for result in results):
        return results[0]
    return UNKNOWN
