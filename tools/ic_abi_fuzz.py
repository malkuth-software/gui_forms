#!/usr/bin/env python3
"""Write the C ABI block table of gui_forms_tests' IC fuzz target (IC3).

tools/ic_declarations.py writes one block stub per call and admitted state
(legal) and one per refusal (misuse) into manifests/ic/blocks; each says
what its body must do but not how to call the slot. This tool joins the
stubs with the ABI manifest's signatures (manifests/abi/gf_abi.json) and
tools/ic/abi_fuzz.toml (the subject each table's member calls take, and the
slots never called) and writes tests/ic/gf_abi_ic_blocks.hpp in the
gui_forms_tests checkout: one invoke function per slot, which draws every
argument from the fuzz input through the engine's Arguments, and one Block
record per stub, with the statuses the declaration admits, the state the
block starts in and moves to, and for a misuse block the refusal expected
and the violations that make it (a parameter nulled, a count forced). The
engine (tests/ic/ic_engine.hpp there) runs the table in legal and misuse
modes with the state query as the per-step oracle; phase IC2's engine
replaces it.

    python3 -B tools/ic_abi_fuzz.py [--check] [--tests-root DIR]

--check fails (exit 1) when the table differs from what the inputs write.
With no gui_forms_tests checkout the tool says so and writes nothing.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import glob
import json
from pathlib import Path
import re
import sys
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tests_checkout  # noqa: E402

ROOT: Path = Path(__file__).resolve().parent.parent
MANIFEST: Path = ROOT / 'manifests' / 'abi' / 'gf_abi.json'
PROJECT: Path = ROOT / 'tools' / 'ic' / 'project.toml'
CONFIG: Path = ROOT / 'tools' / 'ic' / 'abi_fuzz.toml'
OUTPUT_NAME: str = 'tests/ic/gf_abi_ic_blocks.hpp'

# The engine's state models (ic_engine.hpp's Model), each with the states in
# the order the declarations list them; a declaration whose states match
# none is an error, so the engine and the declarations cannot drift apart.
MODELS: dict[str, tuple[str, ...]] = {
    'object': ('alive', 'disposing', 'disposed'),
    'window': ('created', 'running', 'ended'),
    'splashscreen': ('shown', 'handed_over'),
}
# Each slot of an argument's arena holds this many bytes (ic_engine.hpp's
# slot_bytes); a count beside a buffer is drawn below what fits.
SLOT_BYTES: int = 4096
MAXIMUM_PARAMETERS: int = 10
INTEGERS: dict[str, str] = {
    'uint64_t': 'u64', 'uint32_t': 'u32', 'int64_t': 'i64', 'int32_t': 'i32', 'double': 'real',
}
COUNT_NAME: re.Pattern[str] = re.compile(r'(^|_)(capacity|count|size|length)$')


class FuzzError(Exception):
    """An input the tool cannot join: the message names it."""


@dataclass
class Parameter:
    name: str
    type: str


@dataclass
class Slot:
    table: str
    name: str
    parameters: list[Parameter]
    skip: str = ''
    function: str = ''
    lines: list[str] = field(default_factory=list)


@dataclass
class OrderPart:
    """One misuse block of a call, kept to pair into refusal-order blocks."""
    refusal: str  # the clause's check, or "outside"
    state_name: str
    state: str
    expected: str
    violations: list[tuple[int, int]]
    declared: list[str]
    changing: list[str]


@dataclass
class Manifest:
    structs: dict[str, dict[str, object]]
    scalars: dict[str, str]
    constants: set[str]
    slots: dict[tuple[str, str], list[Parameter]]


def load_manifest() -> Manifest:
    data: dict[str, object] = json.loads(MANIFEST.read_text(encoding='utf-8'))
    structs: dict[str, dict[str, object]] = {}
    slots: dict[tuple[str, str], list[Parameter]] = {}
    record: dict[str, object]
    for record in data['structs']:
        structs[str(record['name'])] = record
        if record['kind'] != 'table':
            continue
        table: str = str(record['name'])[len('gf_'):-len('_table')]
        member: dict[str, object]
        for member in record['fields']:
            if 'signature' not in member:
                continue
            parameters: list[Parameter] = [Parameter(str(item['name']), str(item['type']))
                                           for item in member['signature']['parameters']]
            slots[(table, str(member['name']))] = parameters
    scalars: dict[str, str] = {str(item['name']): str(item['type']) for item in data['scalars']}
    constants: set[str] = {str(item['name']) for item in data['constants']}
    manifest: Manifest = Manifest(structs, scalars, constants, slots)
    return manifest


def pointee(type_name: str) -> tuple[str, bool, bool]:
    """(the pointed-to type, whether it is a pointer, whether it is const)."""
    if not type_name.endswith('*'):
        return type_name, False, False
    inner: str = type_name[:-1].strip()
    constant: bool = inner.startswith('const ')
    if constant:
        inner = inner[len('const '):].strip()
    return inner, True, constant


def carries_code(manifest: Manifest, type_name: str) -> bool:
    """Whether a type, or a record it holds or points to, has a function or a void* (iterative)."""
    pending: list[str] = [type_name]
    seen: set[str] = set()
    while pending:
        current: str = pointee(pending.pop())[0]
        if current in ('void', 'function'):
            return True
        if current in seen or current not in manifest.structs:
            continue
        seen.add(current)
        record: dict[str, object] = manifest.structs[current]
        member: dict[str, object]
        for member in record['fields']:
            member_type: str = str(member['type'])
            if member_type == 'function' or member_type.startswith('void') or member_type == 'void*':
                return True
            pending.append(member_type)
    return False


def sized(manifest: Manifest, type_name: str) -> bool:
    """Whether a record starts with struct_size (an extensible record)."""
    record: dict[str, object] | None = manifest.structs.get(type_name)
    if record is None:
        return False
    fields: list[dict[str, object]] = record['fields']
    starts: bool = bool(fields) and fields[0]['name'] == 'struct_size'
    return starts


def plain(manifest: Manifest, type_name: str) -> bool:
    """Whether a record holds only integers and doubles (any bytes are a value of it), iterative."""
    pending: list[str] = [type_name]
    while pending:
        current: str = pending.pop()
        if current in INTEGERS or current in manifest.scalars:
            continue
        record: dict[str, object] | None = manifest.structs.get(current)
        if record is None or record['kind'] not in ('record', 'extensible'):
            return False
        member: dict[str, object]
        for member in record['fields']:
            member_type: str = str(member['type'])
            if member_type.endswith('*') or member_type == 'function':
                return False
            pending.append(member_type)
    return True


def element_type(parameters: list[Parameter], index: int) -> str | None:
    """The pointee of the buffer a count parameter bounds: the nearest pointer before it, else after it."""
    position: int
    for position in range(index - 1, -1, -1):
        inner: str
        is_pointer: bool
        inner, is_pointer, _ = pointee(parameters[position].type)
        if is_pointer:
            return inner
    for position in range(index + 1, len(parameters)):
        inner, is_pointer, _ = pointee(parameters[position].type)
        if is_pointer:
            return inner
    return None


def cpp_size(type_name: str) -> str:
    if type_name in ('char', 'uint8_t'):
        return '1U'
    return 'sizeof(' + type_name + ')'


def argument_line(manifest: Manifest, slot: Slot, index: int, member: bool) -> str:
    """The C++ statement that draws parameter `index` of a slot."""
    parameter: Parameter = slot.parameters[index]
    type_name: str = parameter.type
    name: str = parameter.name
    position: str = str(index) + 'U'
    inner: str
    is_pointer: bool
    constant: bool
    inner, is_pointer, constant = pointee(type_name)
    if type_name == 'gf_object':
        source: str = 'drawn.subject()' if index == 0 and member else 'drawn.object(' + position + ')'
        return 'const gf_object ' + name + ' = ' + source + ';'
    if type_name == 'gf_utf8_view':
        return 'const gf_utf8_view ' + name + ' = drawn.text(' + position + ');'
    if type_name in INTEGERS:
        if type_name in ('uint64_t', 'uint32_t') and COUNT_NAME.search(name):
            bounded: str | None = element_type(slot.parameters, index)
            if bounded is not None:
                return ('const ' + type_name + ' ' + name + ' = static_cast<' + type_name + '>(drawn.extent('
                        + position + ', ' + cpp_size(bounded) + '));')
        return 'const ' + type_name + ' ' + name + ' = drawn.' + INTEGERS[type_name] + '(' + position + ');'
    if type_name in manifest.scalars:
        base: str = manifest.scalars[type_name]
        draw: str = INTEGERS.get(base, 'u32')
        return ('const ' + type_name + ' ' + name + ' = static_cast<' + type_name + '>(drawn.' + draw + '('
                + position + '));')
    if not is_pointer:
        if plain(manifest, type_name):
            return 'const ' + type_name + ' ' + name + ' = drawn.plain<' + type_name + '>(' + position + ');'
        return 'const ' + type_name + ' ' + name + ' = drawn.value<' + type_name + '>(' + position + ');'
    record_sized: str = 'true' if sized(manifest, inner) else 'false'
    if constant:
        if inner == 'gf_object':
            return 'const gf_object* const ' + name + ' = drawn.objects_input(' + position + ');'
        if inner in ('uint8_t', 'char'):
            return 'const ' + inner + '* const ' + name + ' = drawn.bytes_input<' + inner + '>(' + position + ');'
        return ('const ' + inner + '* const ' + name + ' = drawn.input<' + inner + '>(' + position + ', '
                + record_sized + ');')
    return inner + '* const ' + name + ' = drawn.output<' + inner + '>(' + position + ', ' + record_sized + ');'


def slot_function(manifest: Manifest, slot: Slot, member: bool) -> None:
    """Fills slot.function and slot.lines: the invoke function that draws the arguments and calls the slot."""
    slot.function = 'invoke_' + slot.table + '_' + slot.name
    # The draw source is `drawn` and the result `returned`: a slot's own
    # parameters may be called arguments or status.
    source: str = 'Arguments& drawn' if slot.parameters else '[[maybe_unused]] Arguments& drawn'
    lines: list[str] = ['// ' + slot.table + '.' + slot.name,
                        'inline gf_status ' + slot.function + '(const AbiTables& tables, ' + source + ') {']
    index: int
    for index in range(len(slot.parameters)):
        lines.append('    ' + argument_line(manifest, slot, index, member))
    names: str = ', '.join(parameter.name for parameter in slot.parameters)
    lines.append('    const gf_status returned = tables.' + slot.table + '.' + slot.name + '(' + names + ');')
    for index in range(len(slot.parameters)):
        if slot.parameters[index].type == 'gf_object*':
            lines.append('    drawn.adopt(returned, ' + slot.parameters[index].name + ');')
    lines.append('    return returned;')
    lines.append('}')
    slot.lines = lines


def status_of(manifest: Manifest, kind: str) -> str | None:
    constant: str = 'GF_STATUS_' + kind.upper()
    if constant in manifest.constants:
        return constant
    return None


def refusal_status(manifest: Manifest, expect: dict[str, object]) -> str | None:
    if expect.get('kind') == 'value':
        value: str = str(expect.get('value', ''))
        return value if value in manifest.constants else None
    if expect.get('kind') == 'failure':
        return status_of(manifest, str(expect.get('failure', '')))
    return None


def conjuncts(text: str) -> list[str]:
    """The top-level `and` operands of a clause."""
    parts: list[str] = []
    depth: int = 0
    current: str = ''
    words: list[str] = re.split(r'(\(|\)| and )', text)
    word: str
    for word in words:
        if word == '(':
            depth = depth + 1
        elif word == ')':
            depth = depth - 1
        if word == ' and ' and depth == 0:
            parts.append(current.strip())
            current = ''
            continue
        current = current + word
    if current.strip():
        parts.append(current.strip())
    return parts


def violations_of(clause: str, parameters: list[Parameter]) -> list[tuple[int, int]]:
    """(the parameter nulled, the parameter forced to 1, or -1), one per conjunct the engine can break."""
    names: dict[str, int] = {parameter.name: position for position, parameter in enumerate(parameters)}
    found: list[tuple[int, int]] = []
    part: str
    for part in conjuncts(clause):
        simple: re.Match[str] | None = re.fullmatch(r'not_null\((\w+)\)', part)
        if simple is not None and simple.group(1) in names:
            found.append((names[simple.group(1)], -1))
            continue
        paired: re.Match[str] | None = re.fullmatch(r'\(not_null\((\w+)\) or (\w+) == 0\)', part)
        if paired is not None and paired.group(1) in names and paired.group(2) in names:
            found.append((names[paired.group(1)], names[paired.group(2)]))
    return found


def load_declarations(project: dict[str, object]) -> list[tuple[str, dict[str, object]]]:
    """The C ABI declarations (those naming an ABI table), with their ids, sorted."""
    found: list[tuple[str, dict[str, object]]] = []
    pattern: str
    for pattern in project['declarations']:
        path: str
        for path in sorted(glob.glob(pattern, root_dir=ROOT, recursive=True)):
            declaration: dict[str, object] = tomllib.loads((ROOT / path).read_text(encoding='utf-8'))
            keys: dict[str, str] = declaration.get('keys', {})
            if 'abi' not in keys:
                continue
            identifier: str = Path(path).name[:-len('.ic.toml')]
            if all(identifier != known for known, _ in found):
                found.append((identifier, declaration))
    found.sort(key=sort_key)
    return found


def sort_key(item: tuple[str, dict[str, object]]) -> str:
    return item[0]


def cpp_string(text: str) -> str:
    escaped: str = text.replace('\\', '\\\\').replace('"', '\\"')
    return '"' + escaped + '"'


def generate() -> str:
    manifest: Manifest = load_manifest()
    project: dict[str, object] = tomllib.loads(PROJECT.read_text(encoding='utf-8'))
    config: dict[str, object] = tomllib.loads(CONFIG.read_text(encoding='utf-8'))
    subjects: dict[str, str] = config.get('subjects', {})
    skips: dict[str, str] = config.get('skip', {})
    known_slots: set[str] = {table + '.' + name for table, name in manifest.slots}
    unknown: list[str] = sorted(name for name in skips if name not in known_slots)
    if unknown:
        raise FuzzError('abi_fuzz.toml skips slots the manifest does not have: ' + ', '.join(unknown))
    declarations: list[tuple[str, dict[str, object]]] = load_declarations(project)
    blocks_root: Path = ROOT / str(project['blocks'])

    tables: list[tuple[str, str]] = []
    slots: dict[tuple[str, str], Slot] = {}
    block_lines: list[str] = []
    array_lines: list[str] = []
    counts: dict[str, int] = {'legal': 0, 'misuse': 0, 'ordered': 0, 'skipped': 0, 'unreachable': 0, 'unconcretized': 0}
    skipped_slots: dict[str, str] = {}

    def emit(name: str, mode: str, table_index: int, model: str, state_index: str, moves_index: str, slot: Slot,
             subject: str, declared: list[str], changing: list[str], expected: str,
             violations: list[tuple[int, int]], overruled: str = '') -> None:
        prefix: str = 'block_' + str(len(block_lines))
        array_lines.append('inline constexpr gf_status ' + prefix + '_declared[] = {' + ', '.join(declared) + '};')
        changing_span: str = 'std::span<const gf_status>{}'
        if changing:
            array_lines.append('inline constexpr gf_status ' + prefix + '_changing[] = {' + ', '.join(changing) + '};')
            changing_span = 'std::span<const gf_status>(' + prefix + '_changing)'
        violation_span: str = 'std::span<const Violation>{}'
        if violations:
            items: str = ', '.join('Violation{' + str(nulled) + 'U, ' + (str(forced) + 'U' if forced >= 0
                                                                          else 'no_parameter') + '}'
                                   for nulled, forced in violations)
            array_lines.append('inline constexpr Violation ' + prefix + '_violations[] = {' + items + '};')
            violation_span = 'std::span<const Violation>(' + prefix + '_violations)'
        block_lines.append('    Block{' + cpp_string(name) + ', Mode::' + mode + ', ' + str(table_index) + 'U, Model::'
                           + model + ', ' + state_index + ', ' + moves_index + ', &' + slot.function + ', '
                           + cpp_string(subject) + ', std::span<const gf_status>(' + prefix + '_declared), '
                           + changing_span + ', ' + expected + ', ' + violation_span
                           + (', ' + overruled if overruled else '') + '},')

    calls_of: dict[tuple[str, str], dict[str, object]] = {}
    identifier: str
    declaration: dict[str, object]
    for identifier, declaration in declarations:
        component: str = str(declaration['component'])
        table: str = component[len('gf_'):-len('_table')]
        table_index: int = len(tables)
        tables.append((table, str(declaration['keys']['abi'])))
        states: tuple[str, ...] = tuple(declaration.get('states', ()))
        model: str = 'none'
        if states:
            matched: list[str] = [name for name, model_states in MODELS.items() if model_states == states]
            if not matched:
                raise FuzzError(identifier + ': states ' + ', '.join(states) + ' match no engine model')
            model = matched[0]
        if table in subjects and model == 'none':
            model = 'object'
        order_parts: dict[tuple[str, str], list[OrderPart]] = {}
        stubs: dict[str, object] = json.loads((blocks_root / (identifier + '.blocks.json')).read_text(encoding='utf-8'))
        calls: dict[str, dict[str, object]] = {str(call['name']): call for call in declaration.get('call', [])}
        calls_of.update({(table, name): call for name, call in calls.items()})
        stub: dict[str, object]
        for stub in stubs['blocks']:
            call_name: str = str(stub['call'])
            key: tuple[str, str] = (table, call_name)
            if key not in manifest.slots:
                raise FuzzError(identifier + ': block ' + str(stub['name']) + ' names no slot of the manifest')
            call: dict[str, object] = calls.get(call_name, {})
            member: bool = call.get('kind') == 'member'
            if key not in slots:
                slot: Slot = Slot(table, call_name, manifest.slots[key])
                full_name: str = table + '.' + call_name
                if full_name in skips:
                    slot.skip = str(skips[full_name])
                elif any(carries_code(manifest, parameter.type) for parameter in slot.parameters):
                    slot.skip = 'takes code or a context pointer'
                elif len(slot.parameters) > MAXIMUM_PARAMETERS:
                    slot.skip = 'more parameters than the engine draws'
                elif member and table not in subjects and slot.parameters[0].type == 'gf_object':
                    slot.skip = 'no subject for the table (abi_fuzz.toml [subjects])'
                if not slot.skip:
                    slot_function(manifest, slot, member and slot.parameters[0].type == 'gf_object')
                else:
                    skipped_slots[full_name] = slot.skip
                slots[key] = slot
            slot = slots[key]
            if slot.skip:
                counts['skipped'] = counts['skipped'] + 1
                continue
            mode: str = str(stub['mode'])
            state_name: str | None = stub.get('state')
            if mode == 'misuse' and 'violates' in stub:
                state_name = str(declaration.get('initial')) if states else None
            if member and states and slot.parameters[0].type != 'gf_object':
                # A member of a handle that is not an object (a splashscreen, a paint endpoint): the engine draws
                # the handle, so it can start the block in no declared state.
                state_name = None
            if state_name is not None and model in MODELS and state_name not in MODELS[model]:
                raise FuzzError(identifier + ': block ' + str(stub['name']) + ' starts in an unknown state')
            reachable: tuple[str, ...] = {'object': ('alive', 'disposed'), 'window': ('created', 'ended')}.get(model, ())
            if state_name is not None and member and state_name not in reachable:
                counts['unreachable'] = counts['unreachable'] + 1
                continue
            state_index: str = 'no_state'
            if state_name is not None and member and model in MODELS:
                state_index = str(MODELS[model].index(state_name)) + 'U'
            moves_index: str = 'no_state'
            moves: str | None = stub.get('moves')
            if moves and model in MODELS and member:
                moves_index = str(MODELS[model].index(moves)) + 'U'
            declared: list[str] = ['GF_STATUS_OK']
            changing: list[str] = []
            failure: dict[str, object]
            for failure in stub.get('fails', []):
                status: str | None = status_of(manifest, str(failure['failure']))
                if status is None:
                    continue
                if status not in declared:
                    declared.append(status)
                if failure.get('leaves', 'unchanged') != 'unchanged' and status not in changing:
                    changing.append(status)
            for requirement in call.get('requires', []):
                status = refusal_status(manifest, {'kind': 'value', 'value': str(requirement['refuses'])[len('value:'):]})
                if status is not None and status not in declared:
                    declared.append(status)
            outside: object = call.get('outside')
            outside_values: list[str] = []
            if isinstance(outside, str):
                outside_values.append(outside)
            elif isinstance(outside, dict):
                outside_values.extend(str(value) for value in outside.values())
            identity: dict[str, str] = declaration.get('identity', {})
            if identity.get('stale'):
                outside_values.append(str(identity['stale']))
            value: str
            for value in outside_values:
                if value.startswith('value:') and value[len('value:'):] in manifest.constants:
                    if value[len('value:'):] not in declared:
                        declared.append(value[len('value:'):])
            expected: str = 'GF_STATUS_OK'
            violations: list[tuple[int, int]] = []
            if mode == 'misuse':
                status = refusal_status(manifest, stub['expect'])
                if status is None:
                    counts['unconcretized'] = counts['unconcretized'] + 1
                    continue
                expected = status
                if 'violates' in stub:
                    violations = violations_of(str(stub['violates']), slot.parameters)
                    if not violations:
                        counts['unconcretized'] = counts['unconcretized'] + 1
                        continue
                elif not member or state_index == 'no_state':
                    counts['unconcretized'] = counts['unconcretized'] + 1
                    continue
            counts[mode] = counts[mode] + 1
            if mode == 'misuse':
                # Kept for the refusal-order blocks below: a clause's violation or an outside state.
                part: str = str(stub['name']).rsplit('.', 1)[-1]
                record: OrderPart = OrderPart(part if 'violates' in stub else 'outside', str(state_name), state_index,
                                              expected, violations, declared, changing)
                order_parts.setdefault(key, []).append(record)
            emit(str(stub['name']), mode, table_index, model if member else 'none', state_index, moves_index, slot,
                 subjects.get(table, '') if member else '', declared, changing, expected, violations)
        for key in sorted(order_parts):
            # A refusal-order block: a clause broken while the subject is in an outside state, so both refusals
            # apply and the declared order (refusal_order) says which status the call reports.
            order: list[str] = [str(name) for name in calls_of[key].get('refusal_order', [])]
            if 'outside' not in order:
                continue
            parts: list[OrderPart] = order_parts[key]
            broken: OrderPart
            for broken in (item for item in parts if item.refusal != 'outside'):
                if broken.refusal not in order:
                    continue
                state_part: OrderPart
                for state_part in (item for item in parts if item.refusal == 'outside'):
                    clause_first: bool = order.index(broken.refusal) < order.index('outside')
                    winner: OrderPart = broken if clause_first else state_part
                    loser: OrderPart = state_part if clause_first else broken
                    counts['ordered'] = counts['ordered'] + 1
                    declared_both: list[str] = broken.declared + [status for status in state_part.declared
                                                                  if status not in broken.declared]
                    emit('gf_abi.' + key[0] + '.' + key[1] + '.order.' + broken.refusal + '.' + state_part.state_name,
                         'ordered', table_index, model, state_part.state, 'no_state', slots[key],
                         subjects.get(key[0], ''), declared_both, broken.changing, winner.expected, broken.violations,
                         loser.expected)


    out: list[str] = [
        '// The C ABI block table of the IC fuzz target (IC3), generated by gui_forms tools/ic_abi_fuzz.py from',
        "// gui_forms' manifests/ic/blocks (the declarations' block stubs), manifests/abi/gf_abi.json and",
        '// tools/ic/abi_fuzz.toml; do not edit. Regenerate with `python3 -B tools/ic_abi_fuzz.py` there.',
        '//',
        '// ' + str(counts['legal']) + ' legal and ' + str(counts['misuse']) + ' misuse blocks, ' + str(counts['ordered'])
        + ' refusal-order blocks;',
        '// ' + str(counts['skipped']) + ' stubs of slots the engine never calls,',
        '// ' + str(counts['unreachable']) + ' that start in a state the engine cannot reach, '
        + str(counts['unconcretized']) + ' misuse stubs it cannot break on purpose.',
        '#pragma once',
        '',
        '#include "gui_forms/c/gf_abi.h"',
        '#include "ic_engine.hpp"',
        '',
        '#include <array>',
        '#include <cstddef>',
        '#include <cstdint>',
        '#include <span>',
        '',
        'namespace gui_forms_tests::ic {',
        '',
        '// Every table of the ABI, at the header version.',
        'struct AbiTables final {',
    ]
    table_name: str
    for table_name, _ in tables:
        out.append('    gf_' + table_name + '_table ' + table_name + '{};')
    out.append('    // Whether each table negotiated (a host may lack one, such as the Windows paint endpoint).')
    out.append('    std::array<bool, ' + str(len(tables)) + 'U> present{};')
    out.append('};')
    out.append('')
    out.append('// Negotiates every table; returns whether the runtime and objects tables, which the engine needs, did.')
    out.append('[[nodiscard]] inline bool negotiate(AbiTables& tables) noexcept {')
    position: int
    table_id: str
    for position, (table_name, table_id) in enumerate(tables):
        out.append('    tables.present[' + str(position) + 'U] = gf_abi_get_table(' + table_id
                   + ', GF_ABI_MAJOR, GF_ABI_MINOR, &tables.' + table_name + ', sizeof(tables.' + table_name
                   + ')) == GF_STATUS_OK;')
    runtime_position: int = [name for name, _ in tables].index('runtime')
    objects_position: int = [name for name, _ in tables].index('objects')
    out.append('    const bool usable = tables.present[' + str(runtime_position) + 'U] && tables.present['
               + str(objects_position) + 'U];')
    out.append('    return usable;')
    out.append('}')
    out.append('')
    out.append('using Invoke = gf_status (*)(const AbiTables& tables, Arguments& arguments);')
    out.append('')
    out.append('// One block: the slot it calls, the state it starts in and moves to, and what the declaration admits.')
    out.append('struct Block final {')
    out.append('    const char* name{};')
    out.append('    Mode mode{Mode::legal};')
    out.append('    std::uint32_t table{};')
    out.append('    Model model{Model::none};')
    out.append('    std::uint32_t state{no_state};')
    out.append('    std::uint32_t moves{no_state};')
    out.append('    Invoke invoke{};')
    out.append('    // The schema type of the subject a member call takes; empty for a static call.')
    out.append('    const char* subject{};')
    out.append('    // OK, the declared failures, the requires refusals and the outside refusals.')
    out.append('    std::span<const gf_status> declared{};')
    out.append("    // The declared failures whose leaves are not unchanged: the subject's state may move.")
    out.append('    std::span<const gf_status> changing{};')
    out.append('    // A misuse block: the refusal it must see.')
    out.append('    gf_status expected{GF_STATUS_OK};')
    out.append('    // A misuse block of a requires clause: the ways to break it, one chosen per run.')
    out.append('    std::span<const Violation> violations{};')
    out.append("    // An ordered block: the refusal the declared order puts second, which must not be reported.")
    out.append('    gf_status overruled{GF_STATUS_OK};')
    out.append('};')
    out.append('')
    key: tuple[str, str]
    for key in sorted(slots):
        if slots[key].skip:
            continue
        out.extend(slots[key].lines)
        out.append('')
    out.append('// Slots no block calls, and why:')
    full: str
    for full in sorted(skipped_slots):
        out.append('//   ' + full + ': ' + skipped_slots[full])
    out.append('')
    out.extend(array_lines)
    out.append('')
    out.append('inline constexpr std::array<Block, ' + str(len(block_lines)) + 'U> abi_blocks{')
    out.extend(block_lines)
    out.append('};')
    out.append('')
    out.append('} // namespace gui_forms_tests::ic')
    out.append('')
    text: str = '\n'.join(out)
    return text


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--tests-root', type=Path, default=None)
    options: argparse.Namespace = parser.parse_args()
    tests: Path | None = tests_checkout.tests_root(options.tests_root)
    try:
        text: str = generate()
    except FuzzError as error:
        print('ic_abi_fuzz: ' + str(error), file=sys.stderr)
        return 2
    if tests is None:
        print('ic_abi_fuzz: no gui_forms_tests checkout; nothing written')
        return 0
    output: Path = tests / OUTPUT_NAME
    if options.check:
        current: str = output.read_text(encoding='utf-8') if output.exists() else ''
        if current != text:
            print('ic_abi_fuzz: ' + str(output) + ' is stale; run python3 -B tools/ic_abi_fuzz.py', file=sys.stderr)
            return 1
        print('ic_abi_fuzz: ' + str(output) + ' is current')
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding='utf-8')
    print('ic_abi_fuzz: wrote ' + str(output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
