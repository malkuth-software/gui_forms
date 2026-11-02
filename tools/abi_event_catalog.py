#!/usr/bin/env python3
"""Generate the C ABI's event catalog from the interaction declarations.

Every type declares the interactions it raises in its own
``append_interactions`` (an ``std::array<InteractionDeclaration, N>`` in its
source file); the C++ event is the accessor of the same name. This tool reads
those declarations and the payload structs and enumerations in
``include/gui_forms`` and writes ``src/abi/stable/events/abi_event_catalog.hpp``:

- one connector per (declaring type, interaction), which finds the type by
  ``dynamic_cast`` and subscribes a forwarder deduced from the event's
  argument types (abi_event_fields.hpp);
- one field writer per payload struct (members in declaration order, nested
  structs dotted) and one field reader per struct a mutable payload reaches;
- the choice names of every enumeration a payload reaches.

Members of a type the fields cannot carry are left out and listed in
``deferred_event_members``, which the C ABI track reports to the owner.

``python3 tools/abi_event_catalog.py`` writes the file; ``--check`` fails
when it is stale.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent
INCLUDE: Path = ROOT / 'include'
SOURCES: Path = ROOT / 'src'
OUTPUT: Path = ROOT / 'src' / 'abi' / 'stable' / 'events' / 'abi_event_catalog.hpp'

# Types the hand-written fields (abi_event_fields.hpp) carry, by spelling.
HAND_WRITTEN: frozenset[str] = frozenset({
    'bool', 'int', 'double', 'float', 'std::string', 'std::string_view', 'std::size_t', 'std::ptrdiff_t',
    'std::int8_t', 'std::int16_t', 'std::int32_t', 'std::int64_t',
    'std::uint8_t', 'std::uint16_t', 'std::uint32_t', 'std::uint64_t',
    'Point', 'Size', 'Rect', 'Insets', 'Color', 'BindingValue', 'Utf8Offset', 'MaskPosition',
    'Control::Ptr',
})

# Types no event field carries yet, with the family that takes them.
DEFERRED: dict[str, str] = {
    'Painter&': 'owner-draw surfaces (C5d)',
    'std::exception_ptr': 'no portable form; the error message is a field',
    'FieldMap': 'keyed collections (C5c)',
    'ErrorMap': 'keyed collections (C5c)',
    'AnatomyStateSet': 'owner-draw surfaces (C5d)',
    'MaterialColourFilter': 'surface material values (C5c)',
    'DockDragOutcome': 'docking family (C5d)',
}

ARRAY = re.compile(r'constexpr std::array<InteractionDeclaration,\s*\d+>\s+(\w+)\s*\{\{(.*?)\}\};', re.S)
ENTRY = re.compile(r'\{\s*\.name\s*=\s*"([^"]+)",\s*\.payload\s*=\s*"([^"]*)",\s*'
                   r'\.mutable_payload\s*=\s*(true|false)\s*\}')
APPEND = re.compile(r'void ([\w:]+)::append_interactions\(std::vector<InteractionDeclaration>& interactions\) '
                    r'const \{(.*?)\n\}', re.S)
CLASS = re.compile(r'^(?:class|struct) (\w+)(?: final)?\s*(?::\s*([^{;]*))?\{', re.M)
OPTIONAL = re.compile(r'^std::optional<(.+)>$')
VECTOR = re.compile(r'^std::(?:vector|span)<(?:const )?(.+)>$')
POINTER = re.compile(r'^(?:std::shared_ptr<(\w+)>|(\w+)\*)$')


@dataclass
class Declaration:
    owner: str
    name: str
    payload: str
    mutable: bool


@dataclass
class Struct:
    name: str
    header: str
    members: list[tuple[str, str]]


@dataclass
class Enumeration:
    name: str
    header: str
    choices: list[str]
    flags: bool


@dataclass
class Catalog:
    declarations: list[Declaration] = field(default_factory=list)
    class_headers: dict[str, str] = field(default_factory=dict)
    depth: dict[str, int] = field(default_factory=dict)
    structs: dict[str, Struct] = field(default_factory=dict)
    enums: dict[str, Enumeration] = field(default_factory=dict)
    readable: set[str] = field(default_factory=set)
    deferred: list[str] = field(default_factory=list)


def read_headers() -> dict[str, str]:
    headers: dict[str, str] = {}
    path: Path
    for path in sorted(INCLUDE.rglob('*.hpp')):
        headers[path.relative_to(INCLUDE).as_posix()] = path.read_text(encoding='utf-8')
    return headers


def read_declarations() -> list[Declaration]:
    declarations: list[Declaration] = []
    path: Path
    for path in sorted(SOURCES.rglob('*.cpp')):
        text: str = path.read_text(encoding='utf-8')
        arrays: dict[str, list[tuple[str, str, bool]]] = {}
        for match in ARRAY.finditer(text):
            arrays[match.group(1)] = [(entry.group(1), entry.group(2), entry.group(3) == 'true')
                                      for entry in ENTRY.finditer(match.group(2))]
        for match in APPEND.finditer(text):
            owner: str = match.group(1).split('::')[-1]
            for used in re.finditer(r'(\w+)\.begin\(\)', match.group(2)):
                for name, payload, mutable in arrays.get(used.group(1), []):
                    declarations.append(Declaration(owner, name, payload, mutable))
    return declarations


def find_classes(headers: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    """Each class defined at namespace scope: its header and its first base."""
    where: dict[str, str] = {}
    base: dict[str, str] = {}
    for header, text in headers.items():
        for match in CLASS.finditer(text):
            name: str = match.group(1)
            if name in where:
                continue
            where[name] = header
            bases: str = match.group(2) or ''
            first: re.Match[str] | None = re.search(r'public\s+([\w:]+)', bases)
            if first is not None:
                base[name] = first.group(1).split('::')[-1]
    return where, base


def class_depth(name: str, base: dict[str, str]) -> int:
    depth: int = 0
    current: str = name
    while current in base and depth < 32:
        current = base[current]
        depth += 1
    return depth


def struct_body(text: str, name: str) -> str | None:
    one_line: re.Match[str] | None = re.search(r'^struct ' + re.escape(name) + r'(?: final)? \{([^\n]*)\};$', text,
                                               re.M)
    if one_line is not None:
        return one_line.group(1)
    match: re.Match[str] | None = re.search(r'^struct ' + re.escape(name) + r'(?: final)? \{(.*?)\n\};', text,
                                            re.S | re.M)
    return None if match is None else match.group(1)


def enum_body(text: str, name: str) -> str | None:
    match: re.Match[str] | None = re.search(r'^enum class ' + re.escape(name) + r'\b[^{;]*\{(.*?)\};', text,
                                            re.S | re.M)
    return None if match is None else match.group(1)


def strip_comments(text: str) -> str:
    without_line: str = re.sub(r'//[^\n]*', '', text)
    return re.sub(r'/\*.*?\*/', '', without_line, flags=re.S)


def parse_members(body: str) -> list[tuple[str, str]]:
    """The data members of a struct body: (type, name), in order."""
    members: list[tuple[str, str]] = []
    braces: int = 0
    parentheses: int = 0
    statement: str = ''
    character: str
    for character in strip_comments(body):
        if character == '{':
            braces += 1
        elif character == '}':
            braces -= 1
        elif character == '(':
            parentheses += 1
        elif character == ')':
            parentheses -= 1
        if braces == 0 and parentheses == 0 and character == ';':
            members.extend(member_of(statement))
            statement = ''
            continue
        if braces == 0 and character not in '{}':
            statement += character
    return members


def member_of(statement: str) -> list[tuple[str, str]]:
    text: str = ' '.join(statement.split())
    if not text or '(' in text or text.startswith(('friend ', 'static ', 'using ', 'template', 'public:',
                                                   'private:', 'protected:')):
        return []
    text = re.sub(r'\{[^}]*\}$', '', text).split('=')[0].strip()
    match: re.Match[str] | None = re.match(r'^(.+?)\s*\b(\w+)$', text)
    if match is None:
        return []
    return [(match.group(1).strip(), match.group(2))]


def parse_choices(body: str) -> tuple[list[str], bool]:
    text: str = strip_comments(body)
    choices: list[str] = []
    part: str
    for part in text.split(','):
        name: str = part.split('=')[0].strip()
        if re.fullmatch(r'\w+', name):
            choices.append(name)
    return choices, '<<' in text


def split_arguments(payload: str) -> list[str]:
    arguments: list[str] = []
    depth: int = 0
    current: str = ''
    character: str
    for character in payload:
        if character == '<':
            depth += 1
        elif character == '>':
            depth -= 1
        if character == ',' and depth == 0:
            arguments.append(current.strip())
            current = ''
            continue
        current += character
    if current.strip():
        arguments.append(current.strip())
    return arguments


def resolve(catalog: Catalog, headers: dict[str, str], spelling: str, readable: bool, path: str) -> None:
    """Records the structs and enumerations a field of this spelling reaches."""
    text: str = spelling.replace('const ', '').strip()
    inner: re.Match[str] | None = OPTIONAL.match(text)
    if inner is not None:
        resolve(catalog, headers, inner.group(1), readable, path)
        return
    element: re.Match[str] | None = VECTOR.match(text)
    if element is not None:
        resolve(catalog, headers, element.group(1), False, path + '[]')
        return
    if POINTER.match(text) is not None or text in HAND_WRITTEN:
        return
    if text in DEFERRED:
        catalog.deferred.append(f'{path} ({text}): {DEFERRED[text]}')
        return
    if text in catalog.structs:
        if readable and text not in catalog.readable:
            catalog.readable.add(text)
            for member_type, member_name in catalog.structs[text].members:
                resolve(catalog, headers, member_type, True, text + '.' + member_name)
        return
    if text in catalog.enums:
        return
    for header, source in headers.items():
        body: str | None = struct_body(source, text)
        if body is not None:
            catalog.structs[text] = Struct(text, header, parse_members(body))
            if readable:
                catalog.readable.add(text)
            for member_type, member_name in catalog.structs[text].members:
                resolve(catalog, headers, member_type, readable, text + '.' + member_name)
            return
        choices_body: str | None = enum_body(source, text)
        if choices_body is not None:
            choices, flags = parse_choices(choices_body)
            catalog.enums[text] = Enumeration(text, header, choices, flags)
            return
    # A class the fields carry as an object (a Component), or a reference to
    # the sender, which the event already names.
    return


def build() -> Catalog:
    headers: dict[str, str] = read_headers()
    catalog: Catalog = Catalog()
    catalog.declarations = read_declarations()
    where, base = find_classes(headers)
    declaration: Declaration
    for declaration in catalog.declarations:
        if declaration.owner not in where:
            raise SystemExit(f'abi_event_catalog: no header defines {declaration.owner}')
        catalog.class_headers[declaration.owner] = where[declaration.owner]
        catalog.depth[declaration.owner] = class_depth(declaration.owner, base)
        argument: str
        for argument in split_arguments(declaration.payload):
            resolve(catalog, headers, argument, declaration.mutable, declaration.owner + '.' + declaration.name)
    catalog.deferred = sorted(set(catalog.deferred))
    return catalog


def member_is_field(member_type: str) -> bool:
    text: str = member_type.replace('const ', '').strip()
    return text not in DEFERRED and not text.endswith('&')


def render(catalog: Catalog) -> str:
    lines: list[str] = [
        '// Generated by `python3 tools/abi_event_catalog.py` from the interaction declarations',
        '// and the payload types in include/gui_forms. Do not edit; `--check` fails when stale.',
        '// Included only by abi_events.cpp (see abi_event_fields.hpp for the field rules).',
        '',
        '#pragma once',
        '',
        '#include "abi_event_fields.hpp"',
        '',
    ]
    includes: set[str] = set(catalog.class_headers.values())
    includes.update(item.header for item in catalog.structs.values())
    includes.update(item.header for item in catalog.enums.values())
    header: str
    for header in sorted(includes):
        lines.append(f'#include "{header}"')
    lines += ['', '#include <array>', '#include <string_view>', '', 'namespace gui_forms::abi1 {', '']

    lines.append('// The choices of every enumeration a payload reaches.')
    enumeration: Enumeration
    for enumeration in sorted(catalog.enums.values(), key=lambda item: item.name):
        lines.append('template <>')
        lines.append(f'struct EnumerationChoices<{enumeration.name}> {{')
        lines.append(f'    static constexpr bool flags = {"true" if enumeration.flags else "false"};')
        lines.append(f'    static constexpr std::array<EnumerationChoice, {len(enumeration.choices)}> choices{{{{')
        choice: str
        for choice in enumeration.choices:
            lines.append(f'        {{"{choice}", static_cast<std::int64_t>({enumeration.name}::{choice})}},')
        lines.append('    }};')
        lines.append('};')
        lines.append('')

    structs: list[Struct] = sorted(catalog.structs.values(), key=lambda item: item.name)
    lines.append('// Field writers and readers, declared first so they may call each other.')
    item: Struct
    for item in structs:
        lines.append(f'void append_field(EventFields& fields, std::string_view name, const {item.name}& value);')
        if item.name in catalog.readable:
            lines.append(f'void read_field(EventFields& fields, std::string_view name, {item.name}& value);')
    lines.append('')
    for item in structs:
        carried: list[tuple[str, str]] = [member for member in item.members if member_is_field(member[0])]
        single: bool = len(item.members) == 1 and item.members[0][1] == 'value'
        lines.append(f'inline void append_field(EventFields& fields, std::string_view name, const {item.name}& value) {{')
        if single:
            lines.append('    // An identity: the field is its one value.')
            lines.append('    append_field(fields, name, value.value);')
        elif not carried:
            lines.append('    fields.append_empty(name);')
        else:
            lines.append('    const EventFieldPrefix prefix = fields.enter(name);')
            for member_type, member_name in carried:
                lines.append(f'    append_field(fields, "{member_name}", value.{member_name});')
            lines.append('    fields.leave(prefix);')
        lines.append('}')
        lines.append('')
        if item.name in catalog.readable:
            lines.append(f'inline void read_field(EventFields& fields, std::string_view name, {item.name}& value) {{')
            if single:
                lines.append('    read_field(fields, name, value.value);')
            elif not carried:
                lines.append('    static_cast<void>(fields);')
                lines.append('    static_cast<void>(name);')
                lines.append('    static_cast<void>(value);')
            else:
                lines.append('    const EventFieldPrefix prefix = fields.enter(name);')
                for member_type, member_name in carried:
                    lines.append(f'    read_field(fields, "{member_name}", value.{member_name});')
                lines.append('    fields.leave(prefix);')
            lines.append('}')
            lines.append('')

    lines.append('// One connector per (declaring type, interaction).')
    ordered: list[Declaration] = sorted(catalog.declarations,
                                        key=lambda item: (item.name, -catalog.depth[item.owner], item.owner))
    seen: set[tuple[str, str]] = set()
    unique: list[Declaration] = []
    declaration: Declaration
    for declaration in ordered:
        key: tuple[str, str] = (declaration.owner, declaration.name)
        if key not in seen:
            seen.add(key)
            unique.append(declaration)
    for declaration in unique:
        function: str = connector_name(declaration)
        lines.append(f'inline SubscriptionToken {function}(Component& source, const EventSink& sink) {{')
        lines.append(f'    {declaration.owner}* typed = dynamic_cast<{declaration.owner}*>(&source);')
        lines.append('    if (typed == nullptr) {')
        lines.append('        return SubscriptionToken{};')
        lines.append('    }')
        lines.append(f'    SubscriptionToken token = subscribe_forwarded((*typed).{declaration.name}(), sink);')
        lines.append('    return token;')
        lines.append('}')
        lines.append('')
    lines.append('// By interaction name, then the most derived declaring type first.')
    lines.append(f'inline constexpr std::array<EventConnector, {len(unique)}> event_connectors{{{{')
    for declaration in unique:
        lines.append(f'    {{"{declaration.name}", "{declaration.owner}", &{connector_name(declaration)}}},')
    lines.append('}};')
    lines.append('')
    lines.append('// Payload members no field carries yet, with the family that takes them.')
    lines.append(f'inline constexpr std::array<std::string_view, {len(catalog.deferred)}> deferred_event_members{{{{')
    text: str
    for text in catalog.deferred:
        lines.append(f'    "{text}",')
    lines.append('}};')
    lines.append('')
    lines.append('} // namespace gui_forms::abi1')
    lines.append('')
    return '\n'.join(lines)


def connector_name(declaration: Declaration) -> str:
    owner: str = re.sub(r'(?<!^)(?=[A-Z])', '_', declaration.owner).lower()
    return f'connect_{owner}_{declaration.name}'


def main(argv: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--check', action='store_true', help='fail when the committed file is stale')
    parser.add_argument('--out', type=Path, default=OUTPUT)
    args: argparse.Namespace = parser.parse_args(argv)
    catalog: Catalog = build()
    text: str = render(catalog)
    if args.check:
        current: str = args.out.read_text(encoding='utf-8') if args.out.exists() else ''
        if current != text:
            print(f'abi_event_catalog: {args.out} is stale; run python3 tools/abi_event_catalog.py')
            return 1
        print(f'abi_event_catalog: {args.out} is current')
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding='utf-8', newline='\n')
    print(f'abi_event_catalog: {len(catalog.declarations)} declarations, {len(catalog.structs)} structs, '
          f'{len(catalog.enums)} enumerations, {len(catalog.deferred)} deferred members -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
