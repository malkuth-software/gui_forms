#!/usr/bin/env python3
"""Export annotations of the public headers (CP29 handoff section 2.2).

The shared libraries are built with hidden visibility (ELF, Mach-O) and with
dllexport/dllimport (MinGW), so a symbol leaves its library only when its
declaration carries the library's export macro:

- GUI_FORMS_API (gui_forms/export/application_export.hpp): the Application
  library and the shared test runtime, which hold the same archives;
- GUI_FORMS_THREADING_API (gui_forms/export/threading_export.hpp): the
  Threading library, whose one header (threading.hpp) is annotated by hand.

This tool places GUI_FORMS_API in every public header under
include/gui_forms/ that belongs to those archives:

    python3 -B tools/export_annotations.py apply [HEADER...]
        Inserts the macro and the export header's include, and reports what
        it changed. Rerunning changes nothing.
    python3 -B tools/export_annotations.py check [HEADER...]
        Lists every declaration that needs the macro and lacks it; exits 1
        when there is one.
    python3 -B tools/export_annotations.py count [HEADER...]
        Prints the number of annotated classes and functions.

What it marks, outside templates:
- every class, struct or union definition at namespace scope, and each one
  nested in a public or protected section of a marked class (dllexport on
  MinGW does not reach nested classes; on ELF and Mach-O they inherit the
  enclosing class's visibility, so the mark is redundant but harmless);
- every function declaration without a body at namespace scope that is not
  inline, constexpr, consteval, static or extern "C";
- every `extern` variable declaration at namespace scope;
- every friend function declaration without a body and with an unqualified
  name in a marked class (a friend is not a member, so the class's mark does
  not reach it).

A class's mark covers its members, static data members, vtable and typeinfo,
which classes thrown, caught or dynamic_cast across the boundary need.

Not annotated: include/gui_forms/c/ and drawing_c_api.h (the C interfaces
carry their own macros and allowlists), include/gui_forms/audio/ (a static
library outside the Application archives), threading.hpp (its own library),
the export headers themselves, and class templates (their members are
instantiated in each consumer; none is explicitly instantiated in a library).
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / 'include'
PUBLIC = INCLUDE / 'gui_forms'
MACRO = 'GUI_FORMS_API'
EXPORT_INCLUDE = '#include "gui_forms/export/application_export.hpp"'

EXCLUDED_DIRECTORIES = ('c', 'audio', 'export')
EXCLUDED_FILES = ('drawing_c_api.h', 'threading.hpp')

CLASS_KEYS = ('class', 'struct', 'union')
ACCESS_KEYS = ('public', 'protected', 'private')
# Leading specifiers that make a namespace-scope function declaration need no
# export: it is defined in every unit that uses it, or has internal linkage.
NO_EXPORT_SPECIFIERS = ('inline', 'constexpr', 'consteval', 'static', 'typedef', 'using',
                        'static_assert', 'template', 'friend', 'namespace', 'enum',
                        'return', 'concept', 'requires', 'operator')
# Tokens after a closing parenthesis that still belong to a function head;
# a brace after them opens the function's body.
FUNCTION_TAIL = ('const', 'noexcept', 'override', 'final', 'volatile', '&', '&&')


class Token:
    """One token of a header: its text and its byte offset in the file."""

    def __init__(self, text, offset):
        self.text = text
        self.offset = offset


IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
NUMBER = re.compile(r"[0-9][0-9A-Za-z_'.]*")


def tokenize(source):
    """Returns the header's tokens without comments, literals' contents and
    preprocessor lines. String and character literals become one token "\"\""."""
    tokens = []
    position = 0
    length = len(source)
    line_start = True
    while position < length:
        character = source[position]
        if character == '\n':
            line_start = True
            position += 1
            continue
        if character in ' \t\r\f\v':
            position += 1
            continue
        if line_start and character == '#':
            # A preprocessor line, with its continuations.
            while position < length:
                end = source.find('\n', position)
                if end < 0:
                    position = length
                    break
                if source[end - 1] == '\\':
                    position = end + 1
                    continue
                position = end
                break
            continue
        line_start = False
        if source.startswith('//', position):
            end = source.find('\n', position)
            position = length if end < 0 else end
            continue
        if source.startswith('/*', position):
            end = source.find('*/', position + 2)
            position = length if end < 0 else end + 2
            continue
        raw = re.match(r'(?:u8|u|U|L)?R"([^(\s]*)\(', source[position:position + 32])
        if raw:
            terminator = ')' + raw.group(1) + '"'
            end = source.find(terminator, position + raw.end())
            tokens.append(Token('""', position))
            position = length if end < 0 else end + len(terminator)
            continue
        if character in '"\'':
            start = position
            position += 1
            while position < length and source[position] != character:
                if source[position] == '\\':
                    position += 1
                position += 1
            position += 1
            tokens.append(Token('""', start))
            continue
        identifier = IDENTIFIER.match(source, position)
        if identifier:
            tokens.append(Token(identifier.group(0), position))
            position = identifier.end()
            continue
        number = NUMBER.match(source, position)
        if number:
            tokens.append(Token(number.group(0), position))
            position = number.end()
            continue
        if source.startswith('::', position):
            tokens.append(Token('::', position))
            position += 2
            continue
        if source.startswith('[[', position):
            tokens.append(Token('[[', position))
            position += 2
            continue
        if source.startswith('&&', position):
            tokens.append(Token('&&', position))
            position += 2
            continue
        tokens.append(Token(character, position))
        position += 1
    return tokens


class Scope:
    """One open brace: what opened it and, for a class, its access."""

    def __init__(self, kind, marked=False, access='public', template=False):
        self.kind = kind          # 'namespace', 'linkage', 'class', 'enum' or 'opaque'
        self.marked = marked      # a class that carries (or receives) the macro
        self.access = access
        self.template = template  # a class template, or anything inside one


def skip_attributes(statement, index):
    """Returns the index of the first token after `[[...]]` and alignas(...)
    groups starting at `index`."""
    while index < len(statement):
        text = statement[index].text
        if text == '[[':
            depth = 1
            index += 1
            while index < len(statement) and depth > 0:
                if statement[index].text == '[[':
                    depth += 1
                elif statement[index].text == ']' and index + 1 < len(statement) \
                        and statement[index + 1].text == ']':
                    depth -= 1
                    index += 1
                index += 1
            continue
        if text in ('alignas', '__attribute__', '__declspec'):
            index += 1
            index = skip_group(statement, index, '(', ')')
            continue
        break
    return index


def skip_group(statement, index, opening, closing):
    """Returns the index after the balanced group opening at `index`."""
    if index >= len(statement) or statement[index].text != opening:
        return index
    depth = 0
    while index < len(statement):
        if statement[index].text == opening:
            depth += 1
        elif statement[index].text == closing:
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return index


def strip_template(statement):
    """Returns (is_template, index after `template <...>`)."""
    if statement and statement[0].text == 'template':
        return True, skip_group(statement, 1, '<', '>')
    return False, 0


class Finding:
    """A place that needs the macro: the offset to insert at and a description."""

    def __init__(self, offset, kind, name, annotated):
        self.offset = offset
        self.kind = kind          # 'class' or 'function'
        self.name = name
        self.annotated = annotated


def class_head(statement):
    """For a statement ending at a `{`: (is_template, key index, name) when it
    is a class head, else None."""
    is_template, index = strip_template(statement)
    index = skip_attributes(statement, index)
    if index >= len(statement) or statement[index].text not in CLASS_KEYS:
        return None
    key_index = index
    index += 1
    if index < len(statement) and statement[index].text == MACRO:
        index += 1
    index = skip_attributes(statement, index)
    if index >= len(statement) or not IDENTIFIER.fullmatch(statement[index].text):
        return None  # an unnamed class
    name = statement[index].text
    for token in statement[index:]:
        if token.text in ('=', '('):
            # `struct Foo x = {`, a function returning a struct, or a base
            # whose arguments hold a call: not a plain class head.
            if token.text == '=':
                return None
    return is_template, key_index, name


def function_declaration(statement):
    """For a statement ending at `;` at namespace scope (or a friend in a
    class): (insert index, name, annotated) when it declares a function or an
    extern variable that needs the macro, else None."""
    if not statement:
        return None
    index = skip_attributes(statement, 0)
    if index >= len(statement):
        return None
    first = statement[index].text
    if first == 'extern':
        if index + 1 < len(statement) and statement[index + 1].text == '""':
            return None  # extern "C"
        if index + 1 < len(statement) and statement[index + 1].text == 'template':
            return None
        annotated = index + 1 < len(statement) and statement[index + 1].text == MACRO
        name = last_identifier_before(statement, len(statement))
        return index + 1, name, annotated
    if first in NO_EXPORT_SPECIFIERS or first in CLASS_KEYS or first in ACCESS_KEYS:
        return None
    annotated = first == MACRO
    # The first '(' must come before any '=' or '{': a declarator, not an
    # initialised variable.
    parenthesis = None
    position = index
    while position < len(statement):
        text = statement[position].text
        if text == 'operator':
            # operator(), or a symbol spelled with '=' or '<': the parameters
            # follow the symbol.
            position += 1
            if position + 1 < len(statement) and statement[position].text == '(' \
                    and statement[position + 1].text == ')':
                position += 2
            while position < len(statement) and statement[position].text != '(':
                position += 1
            parenthesis = position if position < len(statement) else None
            break
        if text in ('=', '{'):
            return None
        if text == '(':
            parenthesis = position
            break
        position += 1
    if parenthesis is None or parenthesis == index:
        return None
    name_token = statement[parenthesis - 1].text
    if name_token in ('decltype', 'alignas', 'sizeof', 'noexcept', '__attribute__'):
        return None
    if not IDENTIFIER.fullmatch(name_token) and not is_operator_name(statement, parenthesis):
        return None
    # A leading specifier anywhere before the name also disqualifies.
    for position in range(index, parenthesis):
        if statement[position].text in ('inline', 'constexpr', 'consteval', 'static', 'typedef'):
            return None
    # A function body or `= default`/`= delete` after the parameters is a
    # definition.
    closing = skip_group(statement, parenthesis, '(', ')')
    for position in range(closing, len(statement)):
        if statement[position].text in ('{',):
            return None
        if statement[position].text == '-' and position + 1 < len(statement) \
                and statement[position + 1].text == '>':
            return None  # a deduction guide
        if statement[position].text == '=' and position + 1 < len(statement) \
                and statement[position + 1].text in ('default', 'delete', '0'):
            return None
    name = name_token
    if not IDENTIFIER.fullmatch(name_token):
        name = 'operator' + name_token
    return index, name, annotated


_CORPUS = []


def corpus():
    """The text of every public header, read once."""
    if not _CORPUS:
        _CORPUS.append('\n'.join(path.read_text() for path in sorted(PUBLIC.rglob('*.hpp'))))
    return _CORPUS[0]


def defined_inline(source, statement, headers_text):
    """Whether `headers_text` holds an inline definition of the friend
    function `statement` (in `source`) declares: one spelled
    `inline ... <name>(<first parameter>`."""
    opening = None
    for position, token in enumerate(statement):
        if token.text == '(':
            opening = position
            break
    if opening is None or opening + 1 >= len(statement):
        return False
    begin = statement[opening].offset
    end = source.find(',', begin)
    closing = source.find(')', begin)
    if end < 0 or (0 <= closing < end):
        end = closing
    parameter = ' '.join(source[begin + 1:end].split()[:-1])
    name = source[statement[0].offset:begin].split()[-1]
    pattern = (r'\binline\b[^;{}]*' + re.escape(name) + r'\s*\(\s*' +
               r'\s+'.join(re.escape(word) for word in parameter.split()))
    return re.search(pattern, headers_text) is not None


def qualified_name(statement):
    """Whether the declarator name before the first '(' is qualified."""
    for position, token in enumerate(statement):
        if token.text == '(':
            return position >= 2 and statement[position - 2].text == '::'
    return False


def is_operator_name(statement, parenthesis):
    """Whether the tokens before `parenthesis` spell `operator<symbol>`."""
    for back in range(1, 4):
        if parenthesis - back >= 0 and statement[parenthesis - back].text == 'operator':
            return True
    return False


def last_identifier_before(statement, end):
    for position in range(end - 1, -1, -1):
        if IDENTIFIER.fullmatch(statement[position].text):
            return statement[position].text
    return '?'


def scan(source):
    """Returns the findings of one header."""
    tokens = tokenize(source)
    findings = []
    scopes = [Scope('namespace')]
    statement = []
    parenthesis_depth = 0
    pending_class = None  # the Scope a class head about to open will push

    def at_namespace_scope():
        return all(scope.kind in ('namespace', 'linkage') for scope in scopes)

    def current():
        return scopes[-1]

    for token in tokens:
        text = token.text
        if text == '(':
            parenthesis_depth += 1
            statement.append(token)
            continue
        if text == ')':
            parenthesis_depth -= 1
            statement.append(token)
            continue
        if parenthesis_depth > 0:
            statement.append(token)
            continue
        scope = current()
        if scope.kind == 'class' and len(statement) == 1 and text == ':' \
                and statement[0].text in ACCESS_KEYS:
            scope.access = statement[0].text
            statement = []
            continue
        if text == '{':
            head = class_head(statement) if scope.kind in ('namespace', 'linkage', 'class') else None
            first = statement[0].text if statement else ''
            if scope.kind == 'opaque' or scope.kind == 'enum':
                scopes.append(Scope('opaque', template=scope.template))
                statement.append(token)
                continue
            if any(entry.text == 'namespace' for entry in statement[:3]):
                scopes.append(Scope('namespace', template=scope.template))
                statement = []
                continue
            if first == 'extern' and len(statement) == 2 and statement[1].text == '""':
                scopes.append(Scope('linkage', template=scope.template))
                statement = []
                continue
            if head is not None:
                is_template, key_index, name = head
                inside_template = scope.template or is_template
                key = statement[key_index].text
                default_access = 'private' if key == 'class' else 'public'
                wants_mark = not inside_template and (
                    at_namespace_scope()
                    or (scope.kind == 'class' and scope.marked and scope.access != 'private'))
                if wants_mark:
                    annotated = key_index + 1 < len(statement) \
                        and statement[key_index + 1].text == MACRO
                    key_token = statement[key_index]
                    findings.append(Finding(key_token.offset + len(key_token.text), 'class',
                                            name, annotated))
                scopes.append(Scope('class', marked=wants_mark, access=default_access,
                                    template=inside_template))
                statement = []
                continue
            if first == 'enum' or (len(statement) > 1 and statement[0].text == 'enum'):
                scopes.append(Scope('enum', template=scope.template))
                statement = []
                continue
            # A function body or a brace initialiser.
            previous = statement[-1].text if statement else ''
            is_body = previous in (')', '}') or previous in FUNCTION_TAIL or not statement
            scopes.append(Scope('opaque', template=scope.template or
                                (statement != [] and statement[0].text == 'template')))
            scopes[-1].body = is_body
            statement.append(token)
            continue
        if text == '}':
            closed = scopes.pop() if len(scopes) > 1 else scopes[0]
            if closed.kind == 'opaque':
                statement.append(token)
                if getattr(closed, 'body', False) and current().kind != 'opaque':
                    statement = []
                continue
            if closed.kind in ('namespace', 'linkage'):
                statement = []
                continue
            # A class or enum: its statement ends at the following ';'.
            statement = [Token('<type>', token.offset)]
            continue
        if text == ';':
            scope = current()
            if scope.kind == 'opaque':
                statement.append(token)
                continue
            if statement and statement[0].text != '<type>':
                if at_namespace_scope() and not scope.template:
                    result = function_declaration(statement)
                    if result is not None:
                        insert_index, name, annotated = result
                        findings.append(Finding(statement[insert_index].offset
                                                if insert_index < len(statement)
                                                else token.offset,
                                                'function', name, annotated))
                elif scope.kind == 'class' and scope.marked and statement[0].text == 'friend' \
                        and not scope.template:
                    rest = statement[1:]
                    if rest and rest[0].text not in CLASS_KEYS and rest[0].text != 'template':
                        result = function_declaration(rest)
                        if result is not None and qualified_name(rest):
                            # A friend naming a function of another
                            # namespace: that function's own declaration
                            # carries the macro.
                            result = None
                        if result is not None and defined_inline(source, rest, corpus()):
                            result = None  # an inline definition follows in the header
                        if result is not None:
                            insert_index, name, annotated = result
                            findings.append(Finding(rest[insert_index].offset, 'function',
                                                    name, annotated))
            statement = []
            continue
        statement.append(token)
    return findings


def headers(arguments):
    if arguments:
        return [pathlib.Path(argument).resolve() for argument in arguments]
    selected = []
    for path in sorted(PUBLIC.rglob('*')):
        if path.suffix not in ('.hpp', '.h') or not path.is_file():
            continue
        relative = path.relative_to(PUBLIC)
        if relative.parts[0] in EXCLUDED_DIRECTORIES or str(relative) in EXCLUDED_FILES:
            continue
        selected.append(path)
    return selected


def apply(path):
    source = path.read_text()
    findings = [finding for finding in scan(source) if not finding.annotated]
    if not findings:
        return 0
    pieces = []
    previous = 0
    for finding in sorted(findings, key=offset_of):
        pieces.append(source[previous:finding.offset])
        if finding.kind == 'class':
            pieces.append(' ' + MACRO)
        else:
            pieces.append(MACRO + ' ')
        previous = finding.offset
    pieces.append(source[previous:])
    result = ''.join(pieces)
    if EXPORT_INCLUDE not in result:
        result = add_include(result)
    path.write_text(result)
    return len(findings)


def offset_of(finding):
    return finding.offset


def add_include(source):
    """Inserts the export header's include as the first include of the
    header, or after #pragma once when it has none."""
    lines = source.split('\n')
    for index, line in enumerate(lines):
        if line.startswith('#include '):
            lines.insert(index, EXPORT_INCLUDE)
            return '\n'.join(lines)
    for index, line in enumerate(lines):
        if line.strip() == '#pragma once':
            lines[index + 1:index + 1] = ['', EXPORT_INCLUDE]
            return '\n'.join(lines)
    return EXPORT_INCLUDE + '\n' + source


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('mode', choices=('apply', 'check', 'count'))
    parser.add_argument('headers', nargs='*')
    arguments = parser.parse_args()
    paths = headers(arguments.headers)
    if arguments.mode == 'apply':
        changed = 0
        for path in paths:
            count = apply(path)
            if count:
                changed += 1
                print(f'{path.relative_to(ROOT)}: {count}')
        print(f'{changed} headers changed')
        return 0
    missing = 0
    classes = 0
    functions = 0
    for path in paths:
        source = path.read_text()
        for finding in scan(source):
            if finding.kind == 'class':
                classes += 1
            else:
                functions += 1
            if not finding.annotated:
                missing += 1
                if arguments.mode == 'check':
                    line = source.count('\n', 0, finding.offset) + 1
                    print(f'{path.relative_to(ROOT)}:{line}: {finding.kind} {finding.name} '
                          f'lacks {MACRO}')
    if arguments.mode == 'count':
        print(f'classes {classes} functions {functions} missing {missing}')
        return 0
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
