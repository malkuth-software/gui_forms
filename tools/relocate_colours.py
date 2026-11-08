#!/usr/bin/env python3
"""Move the colours written in form documents into a theme sheet (stage T4,
theme variants stage B).

Colour belongs to the theme (owner ruling, 2026-11-03): a form document holds
structure and behaviour, and the project theme styles it through classes and
escaped stable IDs. This instrument relocates what older documents still
write inline. For each control whose block in a `.form` document sets
`fore-color` or `back-color` to a literal hexadecimal colour, it

- writes a rule keyed by the control's escaped stable ID into the theme sheet,
  `#gallery\\.ranges\\.heading { color: #182d42 }` (`fore-color` becomes
  `color`, `back-color` becomes `background-color`), the light value as
  written;
- writes the same rule under `@media (prefers-color-scheme: dark)` with a dark
  value seeded by the dark colour rule (gui_forms_dark_colour: a foreground as
  ink, a background as a stock surface), a starting point to edit;
- declares `:root { color-scheme: light dark }` when the sheet declares no
  colour scheme;
- and removes the statement from the form document.

By default nothing is written: the changes are printed as a unified diff.
`--write` applies them. A colour that is not a literal hexadecimal colour (a
role, a named colour, a function) is reported and left in place, and so is a
control whose ID the sheet already selects (the sheet's rule is not
overwritten).

The form documents are read with a small scanner, not the form compiler: it
knows strings, comments, blocks (`<type> <stable-id> {`) and statements
(`<property>: <value>;`), which is all the relocation needs. A document the
scanner cannot follow (an unbalanced brace) is refused, and nothing is
written.

C++-built control trees (colours set in code) are not read yet: they need
the control schema's tree dump, which is a later addition.

Usage:
    python3 tools/relocate_colours.py --sheet THEME.css --dark-tool PATH FORM...
    python3 tools/relocate_colours.py --sheet THEME.css --no-dark FORM...
    ... --write    apply instead of printing the diff
Exit status: 0 when the run completed (with or without changes), 1 on a
refusal (a document the scanner cannot follow, a dark tool that fails).
"""

import argparse
import dataclasses
import difflib
import functools
import operator
import pathlib
import re
import subprocess
import sys

# Form property -> (theme property, dark colour class).
COLOUR_PROPERTIES = {
    'fore-color': ('color', 'ink'),
    'back-color': ('background-color', 'stock_surface'),
}
HEX_COLOUR = re.compile(r'#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})')
BLOCK_HEAD = re.compile(r'([a-z][a-z0-9-]*)\s+([A-Za-z_][A-Za-z0-9_.\-]*)\s*$')
COLOR_SCHEME = re.compile(r':root\s*\{[^}]*color-scheme\s*:', re.S)
MARKER = '/* relocate_colours.py: colours moved from {} (light as written; dark seeded by the dark colour rule, a starting point to edit) */'


class Refusal(Exception):
    """A document or tool the relocation cannot use; nothing is written."""


@dataclasses.dataclass
class Statement:
    """One `<property>: <value>;` statement directly inside a control's block."""
    stable_id: str
    property: str
    value: str
    # Offsets of the statement in the document: from the first character of
    # the property to just past the `;`.
    start: int
    end: int


def scan(text, name):
    """The statements of every control block in `text`, in document order.

    A statement belongs to the innermost block around it. Raises Refusal for
    an unterminated string or comment or an unbalanced brace."""
    statements = []
    stack = []
    index = 0
    segment_start = 0
    length = len(text)
    while index < length:
        character = text[index]
        if text.startswith('/*', index):
            close = text.find('*/', index + 2)
            if close < 0:
                raise Refusal(f'{name}: unterminated comment')
            index = close + 2
            continue
        if character == '"':
            index = index + 1
            while index < length and text[index] != '"':
                index = index + (2 if text[index] == '\\' else 1)
            if index >= length:
                raise Refusal(f'{name}: unterminated string')
            index = index + 1
            continue
        if character == '{':
            head = strip_comments(text[segment_start:index]).strip()
            match = BLOCK_HEAD.search(head)
            stack.append(match.group(2) if match else None)
            index = index + 1
            segment_start = index
            continue
        if character == '}':
            if not stack:
                raise Refusal(f'{name}: unbalanced closing brace')
            stack.pop()
            index = index + 1
            segment_start = index
            continue
        if character == ';':
            raw = text[segment_start:index]
            statement = statement_of(raw, segment_start, index + 1, stack)
            if statement is not None:
                statements.append(statement)
            index = index + 1
            segment_start = index
            continue
        index = index + 1
    if stack:
        raise Refusal(f'{name}: unclosed block')
    return statements


def strip_comments(text):
    return re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)


def statement_of(raw, raw_start, end, stack):
    """The colour statement `raw` holds, or None."""
    if not stack or stack[-1] is None:
        return None
    cleaned = strip_comments(raw)
    colon = cleaned.find(':')
    if colon < 0:
        return None
    prop = cleaned[:colon].strip()
    if prop not in COLOUR_PROPERTIES:
        return None
    # The statement starts at its property's first character, after any
    # comment before it.
    match = re.search(r'(?:/\*.*?\*/\s*)*' + re.escape(prop), raw, re.S)
    offset = match.end() - len(prop) if match else raw.find(prop)
    return Statement(stable_id=stack[-1], property=prop, value=cleaned[colon + 1:].strip(),
                     start=raw_start + offset, end=end)


def escaped_id(stable_id):
    """`stable_id` as a CSS ID selector: every character that is not a name
    character escaped with a backslash."""
    out = []
    for character in stable_id:
        if character.isalnum() or character in '-_':
            out.append(character)
        else:
            out.append('\\' + character)
    return '#' + ''.join(out)


def remove_statements(text, statements):
    """`text` without `statements`; a line left blank by a removal goes too."""
    pieces = []
    cursor = 0
    for statement in sorted(statements, key=operator.attrgetter('start')):
        line_start = text.rfind('\n', 0, statement.start) + 1
        line_end = text.find('\n', statement.end)
        line_end = len(text) if line_end < 0 else line_end
        alone = not text[line_start:statement.start].strip() and not text[statement.end:line_end].strip()
        if alone:
            pieces.append(text[cursor:line_start])
            cursor = min(line_end + 1, len(text))
        else:
            # The blanks before an inline statement go with it.
            start = statement.start
            while start > line_start and text[start - 1] in ' 	':
                start = start - 1
            pieces.append(text[cursor:start])
            cursor = statement.end
    pieces.append(text[cursor:])
    return ''.join(pieces)


def run_dark_tool(tool, requests):
    """The dark colour of each (class, colour) in `requests`, from the filter."""
    if not requests:
        return []
    feed = ''.join(f'{kind} {colour}\n' for kind, colour in requests)
    try:
        result = subprocess.run([tool], input=feed, capture_output=True, text=True, check=False)
    except OSError as error:
        raise Refusal(f'dark tool {tool}: {error}') from error
    lines = result.stdout.split()
    if result.returncode != 0 or len(lines) != len(requests):
        raise Refusal(f'dark tool {tool} failed: {result.stderr.strip()}')
    return lines


def no_dark_colours(requests):
    """No dark values: the dark rules are left out (--no-dark)."""
    return None


def relocate(forms, sheet_text, dark_colours):
    """The new sheet text, the new form texts and the report lines.

    `forms` is a list of (name, text); `dark_colours(requests)` returns the
    dark colour of each (class, colour) request, or None to leave the dark
    rules out."""
    report = []
    rules = []
    removals = {}
    selected = sheet_text
    for name, text in forms:
        moved = []
        for statement in scan(text, name):
            selector = escaped_id(statement.stable_id)
            theme_property, kind = COLOUR_PROPERTIES[statement.property]
            if not HEX_COLOUR.fullmatch(statement.value):
                report.append(f'{name}: {statement.stable_id} {statement.property}: '
                              f'{statement.value} is not a literal colour; left in place')
                continue
            if re.search(re.escape(selector) + r'\s*[{,:]', selected):
                report.append(f'{name}: {statement.stable_id}: the sheet already selects {selector}; left in place')
                continue
            moved.append(statement)
            rules.append((name, selector, theme_property, statement.value.lower(), kind))
        removals[name] = moved
    darks = dark_colours([(kind, value) for _, _, _, value, kind in rules]) if rules else []
    blocks = []
    for name, _ in forms:
        mine = [(index, rule) for index, rule in enumerate(rules) if rule[0] == name]
        if not mine:
            continue
        lines = [MARKER.format(name)]
        for _, (_, selector, theme_property, value, _) in mine:
            lines.append(f'{selector} {{ {theme_property}: {value} }}')
        if darks is not None:
            lines.append('@media (prefers-color-scheme: dark) {')
            for index, (_, selector, theme_property, _, _) in mine:
                lines.append(f'  {selector} {{ {theme_property}: {darks[index]} }}')
            lines.append('}')
        blocks.append('\n'.join(lines) + '\n')
    new_sheet = sheet_text
    if blocks:
        if not COLOR_SCHEME.search(sheet_text):
            new_sheet = ':root { color-scheme: light dark }\n' + new_sheet
            report.append('the sheet now declares color-scheme: light dark')
        if new_sheet and not new_sheet.endswith('\n'):
            new_sheet = new_sheet + '\n'
        new_sheet = new_sheet + ('\n' if new_sheet.strip() else '') + '\n'.join(blocks)
    new_forms = [(name, remove_statements(text, removals[name])) for name, text in forms]
    report.append(f'{len(rules)} colour(s) moved')
    return new_sheet, new_forms, report


def diff(name, before, after):
    return ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                        fromfile=f'a/{name}', tofile=f'b/{name}'))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('forms', nargs='+', type=pathlib.Path, help='form documents to relocate from')
    parser.add_argument('--sheet', required=True, type=pathlib.Path, help='theme sheet to write the rules into')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--dark-tool', type=pathlib.Path, help='the gui_forms_dark_colour executable')
    group.add_argument('--no-dark', action='store_true', help='write no dark rules')
    parser.add_argument('--write', action='store_true', help='apply the changes (default: print a diff)')
    arguments = parser.parse_args(argv)

    forms = [(path.as_posix(), path.read_text(encoding="utf-8")) for path in arguments.forms]
    sheet_text = arguments.sheet.read_text(encoding='utf-8') if arguments.sheet.exists() else ''

    dark_colours = no_dark_colours
    if not arguments.no_dark:
        dark_colours = functools.partial(run_dark_tool, str(arguments.dark_tool))
    try:
        new_sheet, new_forms, report = relocate(forms, sheet_text, dark_colours)
    except Refusal as refusal:
        print(f'relocate_colours: {refusal}; nothing written', file=sys.stderr)
        return 1
    for line in report:
        print(f'relocate_colours: {line}', file=sys.stderr)
    if not arguments.write:
        sys.stdout.write(diff(arguments.sheet.as_posix(), sheet_text, new_sheet))
        for (name, before), (_, after) in zip(forms, new_forms):
            sys.stdout.write(diff(name, before, after))
        return 0
    if new_sheet != sheet_text:
        arguments.sheet.write_text(new_sheet, encoding='utf-8')
    for (name, before), (_, after) in zip(forms, new_forms):
        if after != before:
            pathlib.Path(name).write_text(after, encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
