#!/usr/bin/env python3
"""Move the colours written in form documents into a theme sheet (stage T4,
theme variants stage B).

Colour belongs to the theme (owner ruling, 2026-11-03): a form document holds
structure and behaviour, and the project theme styles it through classes and
escaped stable IDs. This instrument relocates what older documents still
write inline. For each control whose element in a `.form` document sets
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
- and removes the attribute from the form document.

By default nothing is written: the changes are printed as a unified diff.
`--write` applies them. A colour that is not a literal hexadecimal colour (a
role, a named colour, a function) is reported and left in place, and so is a
control whose ID the sheet already selects (the sheet's rule is not
overwritten).

The form documents are read with a small scanner, not the form compiler: it
knows comments, declarations and start tags with their quoted attributes
(`<label id="page.heading" fore-color="#182d42"/>`), which is all the
relocation needs. A document the scanner cannot follow (an unterminated tag
or value) is refused, and nothing is written.

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
START_TAG_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_.\-]*')
# One attribute; `value` is None when its quote is not closed.
ATTRIBUTE = re.compile(r'\s+(?P<name>[A-Za-z_:][A-Za-z0-9_.:\-]*)\s*=\s*(?P<value>"[^"]*"|\'[^\']*\')?')
TAG_CLOSE = re.compile(r'\s*/?>')
COLOR_SCHEME = re.compile(r':root\s*\{[^}]*color-scheme\s*:', re.S)
MARKER = '/* relocate_colours.py: colours moved from {} (light as written; dark seeded by the dark colour rule, a starting point to edit) */'


class Refusal(Exception):
    """A document or tool the relocation cannot use; nothing is written."""


@dataclasses.dataclass
class Statement:
    """One colour attribute, `<property>="<value>"`, of an object element."""
    stable_id: str
    property: str
    value: str
    # Offsets of the attribute in the document: from the first character of
    # its name to just past its closing quote.
    start: int
    end: int


def scan(text, name):
    """The colour attributes of every object element in `text`, in document
    order.

    Raises Refusal for an unterminated comment, declaration, start tag or
    attribute value."""
    statements = []
    index = 0
    length = len(text)
    while index < length:
        open_at = text.find('<', index)
        if open_at < 0:
            break
        if text.startswith('<!--', open_at):
            close = text.find('-->', open_at + 4)
            if close < 0:
                raise Refusal(f'{name}: unterminated comment')
            index = close + 3
            continue
        if text.startswith('<?', open_at) or text.startswith('<!', open_at):
            close = text.find('>', open_at + 2)
            if close < 0:
                raise Refusal(f'{name}: unterminated declaration')
            index = close + 1
            continue
        if text.startswith('</', open_at):
            close = text.find('>', open_at + 2)
            if close < 0:
                raise Refusal(f'{name}: unterminated end tag')
            index = close + 1
            continue
        index = start_tag_statements(text, open_at, name, statements)
    return statements


def start_tag_statements(text, open_at, name, statements):
    """Adds the colour attributes of the start tag at `open_at` to
    `statements`; returns the offset past the tag."""
    tag = START_TAG_NAME.match(text, open_at + 1)
    if tag is None:
        raise Refusal(f'{name}: a malformed tag at offset {open_at}')
    index = tag.end()
    stable_id = None
    found = []
    while True:
        attribute = ATTRIBUTE.match(text, index)
        if attribute is None:
            break
        if attribute.group('value') is None:
            raise Refusal(f'{name}: an unterminated attribute value at offset {attribute.start("name")}')
        prop = attribute.group('name')
        value = attribute.group('value')[1:-1]
        if prop == 'id':
            stable_id = value
        elif prop in COLOUR_PROPERTIES:
            found.append(Statement(stable_id='', property=prop, value=value.strip(),
                                   start=attribute.start('name'), end=attribute.end()))
        index = attribute.end()
    close = TAG_CLOSE.match(text, index)
    if close is None:
        raise Refusal(f'{name}: an unterminated start tag at offset {open_at}')
    # The colours of an element without an ID have no selector to move to.
    if stable_id is not None:
        for statement in found:
            statement.stable_id = stable_id
            statements.append(statement)
    return close.end()


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
    """`text` without `statements` (attributes); a line left blank by a
    removal goes too."""
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
            # The blanks before an inline attribute go with it.
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
