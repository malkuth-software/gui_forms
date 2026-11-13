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

It also writes each interaction payload in the C ABI's own vocabulary (audit
K2): the event fields a callback receives, as ``name: kind`` pairs
(``value: boolean``; ``cancel: boolean, row_id: unsigned_integer``), into
``src/abi/stable/schema/abi_interaction_fields.hpp``, which the schema table
publishes as ``gf_schema_interaction.payload``, and into
``manifests/abi/interaction_fields.json``, the public map from the C++
spelling to that text.

``python3 tools/abi_event_catalog.py`` writes the files; ``--check`` fails
when one is stale.
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
FIELDS_OUTPUT: Path = ROOT / 'src' / 'abi' / 'stable' / 'schema' / 'abi_interaction_fields.hpp'
FIELDS_MANIFEST: Path = ROOT / 'manifests' / 'abi' / 'interaction_fields.json'
ORDER_MANIFEST: Path = ROOT / 'manifests' / 'abi' / 'interaction_order.json'

# The gf_value kind each hand-written field carries, as the schema spells
# kinds (abi_event_fields.hpp's append_field overloads).
HAND_WRITTEN_KINDS: dict[str, str] = {
    'bool': 'boolean', 'int': 'signed_integer', 'double': 'number', 'float': 'number',
    'std::string': 'text', 'std::string_view': 'text', 'std::size_t': 'unsigned_integer',
    'std::ptrdiff_t': 'signed_integer', 'std::int8_t': 'signed_integer', 'std::int16_t': 'signed_integer',
    'std::int32_t': 'signed_integer', 'std::int64_t': 'signed_integer', 'std::uint8_t': 'unsigned_integer',
    'std::uint16_t': 'unsigned_integer', 'std::uint32_t': 'unsigned_integer',
    'std::uint64_t': 'unsigned_integer', 'Point': 'point', 'Size': 'size', 'Rect': 'rectangle',
    'Insets': 'insets', 'Color': 'color', 'BindingValue': 'any', 'Utf8Offset': 'unsigned_integer',
    'MaskPosition': 'unsigned_integer', 'Control::Ptr': 'object',
}

# Types the hand-written fields (abi_event_fields.hpp) carry, by spelling.
HAND_WRITTEN: frozenset[str] = frozenset({
    'bool', 'int', 'double', 'float', 'std::string', 'std::string_view', 'std::size_t', 'std::ptrdiff_t',
    'std::int8_t', 'std::int16_t', 'std::int32_t', 'std::int64_t',
    'std::uint8_t', 'std::uint16_t', 'std::uint32_t', 'std::uint64_t',
    'Point', 'Size', 'Rect', 'Insets', 'Color', 'BindingValue', 'Utf8Offset', 'MaskPosition',
    'Control::Ptr',
})

# Payload structs an event lends read-only but a raiser lends writable (the
# class tables' hook entries read the fields the entry wrote back): Control's
# mouse raisers take a MouseEventArgs whose handled ends the route.
LENT_WRITABLE: frozenset[str] = frozenset({'MouseEventArgs'})

# Types no event field carries yet, with the family that takes them.
DEFERRED: dict[str, str] = {
    'Painter&': 'owner-draw surfaces (C5d)',
    'std::exception_ptr': 'no portable form; the error message is a field',
    'FieldMap': 'keyed collections (C5c)',
    'ErrorMap': 'keyed collections (C5c)',
    'AnatomyStateSet': 'owner-draw surfaces (C5d)',
    'MaterialColourFilter': 'surface material values (C5c)',
    'DockDragOutcome': 'docking family (C5d)',
    'Theme': 'no field form; theme_changed is a notification',
}

# Declaring types that are not Components, reached through the ABI object
# that holds them: (holder class, the holder's header, the accessor giving
# the held object or null once it is gone). A window is a WindowHandle.
HOSTED: dict[str, tuple[str, str, str]] = {
    'Window': ('WindowHandle', '../windows/abi_window_handle.hpp', 'window'),
}

# Declaring types published under another class's name. The surface root
# declares the window's interactions (S1.2 moved them from Window), and the
# ABI holds a surface root only as a window until sub-surfaces have handles
# of their own, so they stay Window's and are reached through WindowHandle.
PUBLISHED_AS: dict[str, str] = {
    'SurfaceRoot': 'Window',
}

ARRAY = re.compile(r'constexpr std::array<InteractionDeclaration,\s*\d+>\s+(\w+)\s*\{\{(.*?)\}\};', re.S)
ENTRY = re.compile(r'\{\s*\.name\s*=\s*"([^"]+)",\s*\.payload\s*=\s*"([^"]*)",\s*'
                   r'\.mutable_payload\s*=\s*(true|false)\s*\}')
APPEND = re.compile(r'void ([\w:]+)::append_interactions\(std::vector<InteractionDeclaration>& interactions\) '
                    r'(?:const )?\{(.*?)\n\}', re.S)
CLASS = re.compile(r'^(?:class|struct) (?:GUI_FORMS_API )?(\w+)(?: final)?\s*(?::\s*([^{;]*))?\{', re.M)
OPTIONAL = re.compile(r'^std::optional<(.+)>$')
# A collection: a vector, a span, or a borrowed span (a call-scope payload,
# gui_forms/borrow/borrowed_view/borrowed_view.hpp), whose element follows.
VECTOR = re.compile(r'^(?:std::(?:vector|span)|BorrowedSpan)<(?:const )?(.+)>$')
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
            declared: str = match.group(1).split('::')[-1]
            owner: str = PUBLISHED_AS.get(declared, declared)
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
    one_line: re.Match[str] | None = re.search(r'^struct (?:GUI_FORMS_API )?' + re.escape(name) + r'(?: final)? \{([^\n]*)\};$', text,
                                               re.M)
    if one_line is not None:
        return one_line.group(1)
    match: re.Match[str] | None = re.search(r'^struct (?:GUI_FORMS_API )?' + re.escape(name) + r'(?: final)? \{(.*?)\n\};', text,
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
    lent: str
    for lent in sorted(LENT_WRITABLE):
        resolve(catalog, headers, lent, True, 'raiser.' + lent)
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
    hosted: list[str] = sorted({HOSTED[item.owner][1] for item in catalog.declarations if item.owner in HOSTED})
    if hosted:
        lines.append('')
        for header in hosted:
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
        if declaration.owner in HOSTED:
            holder, _, accessor = HOSTED[declaration.owner]
            lines.append(f'    {holder}* holder = dynamic_cast<{holder}*>(&source);')
            lines.append('    if (holder == nullptr) {')
            lines.append('        return SubscriptionToken{};')
            lines.append('    }')
            lines.append(f'    {declaration.owner}* typed = (*holder).{accessor}();')
        else:
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


def field_entries(catalog: Catalog, spelling: str, name: str, members_unprefixed: bool) -> list[str]:
    """The ``name: kind`` entries one argument or member of this spelling adds."""
    text: str = spelling.replace('const ', '').strip()
    inner: re.Match[str] | None = OPTIONAL.match(text)
    if inner is not None:
        entries: list[str] = field_entries(catalog, inner.group(1), name, members_unprefixed)
        optional_entries: list[str] = []
        entry: str
        for entry in entries:
            optional_entries.append(entry + ' optional')
        return optional_entries
    if VECTOR.match(text) is not None:
        return [name + ': collection']
    if text in HAND_WRITTEN_KINDS:
        return [name + ': ' + HAND_WRITTEN_KINDS[text]]
    if POINTER.match(text) is not None or text.endswith('::Ptr'):
        return [name + ': object']
    if text in DEFERRED or text.endswith('&'):
        return []
    if text in catalog.enums:
        return [name + ': enumeration']
    if text in catalog.structs:
        entries = []
        member_type: str
        member_name: str
        for member_type, member_name in catalog.structs[text].members:
            if not member_is_field(member_type):
                continue
            member: str = member_name if members_unprefixed else name + '.' + member_name
            entries.extend(field_entries(catalog, member_type, member, False))
        return entries
    # A class spelled bare is a reference to the sender, which the event
    # names already: no field.
    return []


def payload_fields(catalog: Catalog, payload: str) -> str:
    """An interaction's payload as the event fields it gives, in the ABI's words."""
    arguments: list[str] = split_arguments(payload)
    count: int = len(arguments)
    entries: list[str] = []
    index: int
    argument: str
    for index, argument in enumerate(arguments):
        name: str = 'value' if count == 1 else 'value_' + str(index + 1)
        entries.extend(field_entries(catalog, argument, name, count == 1))
    return ', '.join(entries)


def payload_map(catalog: Catalog) -> dict[str, str]:
    mapped: dict[str, str] = {}
    declaration: Declaration
    for declaration in catalog.declarations:
        mapped[declaration.payload] = payload_fields(catalog, declaration.payload)
    return dict(sorted(mapped.items()))


def render_fields(mapped: dict[str, str]) -> str:
    lines: list[str] = [
        '// Generated by `python3 tools/abi_event_catalog.py` from the interaction declarations',
        '// and the payload types in include/gui_forms. Do not edit; `--check` fails when stale.',
        "// Each interaction payload (its C++ spelling, sorted) in the C ABI's own words: the",
        '// event fields a callback receives, as gf_schema_interaction.payload publishes them (K2).',
        '',
        '#pragma once',
        '',
        '#include <array>',
        '#include <string_view>',
        '',
        'namespace gui_forms::abi1 {',
        '',
        'struct InteractionFieldsEntry final {',
        '    std::string_view payload;',
        '    std::string_view fields;',
        '};',
        '',
        f'inline constexpr std::array<InteractionFieldsEntry, {len(mapped)}> interaction_fields{{{{',
    ]
    payload: str
    fields: str
    for payload, fields in mapped.items():
        lines.append(f'    {{"{payload}", "{fields}"}},')
    lines.extend(['}};', '', '} // namespace gui_forms::abi1', ''])
    return '\n'.join(lines)


def render_manifest(mapped: dict[str, str]) -> str:
    lines: list[str] = ['{', '  "format": 1,', '  "payloads": {']
    items: list[tuple[str, str]] = list(mapped.items())
    index: int
    payload: str
    fields: str
    for index, (payload, fields) in enumerate(items):
        separator: str = ',' if index + 1 < len(items) else ''
        lines.append(f'    "{payload}": "{fields}"{separator}')
    lines.extend(['  }', '}', ''])
    return '\n'.join(lines)


# The input order per interaction sequence (the owner's six rules, ADR-003
# amendment 2026-11-12). Each step names a declared interaction as
# Owner.name and its kind: physical (rule 1, causal order), repeated
# (physical, zero or more times) or derived (rule 2: after the completing
# physical event, on the same target only) or deferred (the capture's end,
# raised last whatever happened to the sequence) or activation (on_activate,
# after the derived events of a primary click or an activation key's release,
# cancelled and dropped with them; a button's raises ButtonBase.clicked).
# cancelled_by names the event
# and the field that cancels the derived steps when set (rule 3: suppress_click
# or suppress_key_press; handled only ends the route); every sequence's derived
# steps are dropped when a handler disposes the control, opens a modal or
# the capture is lost (rule 5). State is already updated at the completing
# physical event (rule 4).
ORDER_SEQUENCES: list[tuple[str, list[tuple[str, str]], str, str]] = [
    ('click', [('Control.pointer_preview_observed', 'physical'), ('Control.mouse_down', 'physical'),
               ('Control.mouse_move', 'repeated'), ('Control.mouse_up', 'physical'),
               ('Control.click', 'derived'), ('Control.mouse_click', 'derived'),
               ('ButtonBase.clicked', 'activation'), ('Control.mouse_capture_changed', 'deferred')],
     'Control.mouse_up:suppress_click',
     'the activation follows a primary click only; mouse_capture_changed ends a primary press'),
    ('double_click', [('Control.pointer_preview_observed', 'physical'), ('Control.mouse_down', 'physical'),
                      ('Control.mouse_move', 'repeated'), ('Control.mouse_up', 'physical'),
                      ('Control.double_click', 'derived'), ('Control.mouse_double_click', 'derived'),
                      ('Control.mouse_capture_changed', 'deferred')],
     'Control.mouse_up:suppress_click',
     'the second press of a double-click (after a click sequence): no click, mouse_click or on_activate'),
    ('key', [('Control.preview_key_down', 'physical'), ('Window.key_down', 'physical'),
             ('Control.key_down', 'physical'), ('Window.key_press', 'derived'),
             ('Control.key_press', 'derived'), ('Window.key_up', 'physical'), ('Control.key_up', 'physical')],
     'Control.key_down:suppress_key_press',
     'suppress_key_press on the window\'s or the control\'s key_down cancels key_press and the text, except an IME commit'),
    ('keyboard_activation', [('Control.preview_key_down', 'physical'), ('Control.key_down', 'physical'),
                             ('Control.key_up', 'physical'), ('ButtonBase.clicked', 'activation')],
     'Control.key_down:suppress_key_press',
     'Space or Enter on a button: the activation follows key_up, with no mouse events'),
]


def render_order(catalog: Catalog) -> str:
    declared: set[str] = {f'{declaration.owner}.{declaration.name}' for declaration in catalog.declarations}
    lines: list[str] = ['{', '  "format": 1,', '  "rules": "ADR-003 amendment 2026-11-12",', '  "sequences": {']
    index: int
    name: str
    steps: list[tuple[str, str]]
    cancelled_by: str
    note: str
    for index, (name, steps, cancelled_by, note) in enumerate(ORDER_SEQUENCES):
        missing: list[str] = [event for event, kind in steps if event not in declared]
        cancel_event: str = cancelled_by.partition(':')[0]
        cancel_field: str = cancelled_by.partition(':')[2]
        if cancel_event not in declared:
            missing.append(cancel_event)
        if missing:
            raise SystemExit(f'abi_event_catalog: the order of {name} names undeclared interactions: '
                             f'{", ".join(missing)}')
        rendered: list[str] = [f'{{"event": "{event}", "kind": "{kind}"}}' for event, kind in steps]
        separator: str = ',' if index + 1 < len(ORDER_SEQUENCES) else ''
        lines.append(f'    "{name}": {{')
        lines.append('      "steps": [')
        lines.extend(f'        {entry}{"," if position + 1 < len(rendered) else ""}'
                     for position, entry in enumerate(rendered))
        lines.append('      ],')
        lines.append(f'      "cancelled_by": {{"event": "{cancel_event}", "field": "{cancel_field}"}},')
        lines.append('      "dropped_when": ["control disposed", "modal opened", "capture lost"],')
        lines.append(f'      "note": "{note}"')
        lines.append(f'    }}{separator}')
    lines.extend(['  }', '}', ''])
    return '\n'.join(lines)


def main(argv: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--check', action='store_true', help='fail when the committed file is stale')
    parser.add_argument('--out', type=Path, default=OUTPUT)
    args: argparse.Namespace = parser.parse_args(argv)
    catalog: Catalog = build()
    text: str = render(catalog)
    mapped: dict[str, str] = payload_map(catalog)
    outputs: list[tuple[Path, str]] = [(args.out, text), (FIELDS_OUTPUT, render_fields(mapped)),
                                       (FIELDS_MANIFEST, render_manifest(mapped)),
                                       (ORDER_MANIFEST, render_order(catalog))]
    path: Path
    content: str
    if args.check:
        stale: bool = False
        for path, content in outputs:
            current: str = path.read_text(encoding='utf-8') if path.exists() else ''
            if current != content:
                print(f'abi_event_catalog: {path} is stale; run python3 tools/abi_event_catalog.py')
                stale = True
            else:
                print(f'abi_event_catalog: {path} is current')
        return 1 if stale else 0
    for path, content in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8', newline='\n')
    print(f'abi_event_catalog: {len(catalog.declarations)} declarations, {len(catalog.structs)} structs, '
          f'{len(catalog.enums)} enumerations, {len(catalog.deferred)} deferred members -> {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
