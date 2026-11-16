"""module_exports writes the gui_forms C++20 module interface unit.

`import gui_forms;` is the main consumer path: the module is compiled from
its own translation unit, so no macro of the consumer's sources can reach
GUI.Forms' headers, and no macro of ours reaches the consumer (modules export
no macros). modules/gui_forms.cppm includes the consumer headers in its
global module fragment and exports the consumer surface with export-using
declarations, as libc++'s own std module does:

    module;
    #include "gui_forms/gui_forms.hpp"
    export module gui_forms;
    export namespace gui_forms {
    using gui_forms::Button;
    ...
    }

The names are every namespace-scope declaration of the headers the
umbrellas reach, except:

- internals: the areas in INTERNAL_AREAS (the generic-name audit's
  candidates for a private tree) and namespaces named detail or internal;
- entities with internal linkage, which a module may not export: static
  functions and variables, const or constexpr variables that are not
  inline, anything in an unnamed namespace;
- friend declarations, deduction guides, explicit instantiations and
  using-declarations.

Types reached through exported declarations stay usable (their members can be
named through them); only their own names stay unexported. Macros are not
exported: a consumer who needs a GF_ macro includes the C ABI header, which
carries the C shield.

    python3 -B tools/module_exports.py generate --root .
    python3 -B tools/module_exports.py check --root .

The parse is lexical (tools/consumer_macros.py's tokens): it reads
namespace-scope statements and skips class, function and initializer
bodies. It writes what it finds; a declaration it misreads shows up as a
compile error in the module, never as a silent export of an internal.
"""
from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path

import consumer_macros

MODULE_PATH: str = 'modules/gui_forms.cppm'
UMBRELLAS: tuple[str, ...] = ('gui_forms/gui_forms.hpp', 'gui_forms/c/gf_abi.hpp')

# Areas the module does not export (relative to include/), until the
# consumer header set is decided. A consumer reaches their types only
# through exported declarations.
INTERNAL_AREAS: tuple[str, ...] = (
    'gui_forms/allocation/', 'gui_forms/anatomy/', 'gui_forms/borrow/', 'gui_forms/capture/',
    'gui_forms/detail/', 'gui_forms/diagnostics/', 'gui_forms/engine/', 'gui_forms/export/',
    'gui_forms/host/', 'gui_forms/inspection/', 'gui_forms/integrity/', 'gui_forms/metrics/',
    'gui_forms/prepared_text/', 'gui_forms/scheduler/', 'gui_forms/schema/', 'gui_forms/surface_material/',
    'gui_forms/surface_root/', 'gui_forms/text_mask/', 'gui_forms/text_shaping/',
    'gui_forms/visual_inspection/', 'gui_forms/xml/')
INTERNAL_NAMESPACES: frozenset[str] = frozenset(('detail', 'internal', 'impl'))
MACRO_SPELLING = re.compile(r'[A-Z][A-Z0-9_]*')


@dataclasses.dataclass(frozen=True)
class Declaration:
    namespace: tuple[str, ...]
    name: str
    # 'class X;' alone: names the entity without defining it.
    forward: bool = dataclasses.field(default=False, compare=False)


def code_tokens(source: str) -> list[consumer_macros.Token]:
    """The tokens of source outside preprocessor lines."""
    tokens: list[consumer_macros.Token] = consumer_macros.lex(source)
    code: list[consumer_macros.Token] = []
    index: int = 0
    at_line_start: bool = True
    while index < len(tokens):
        token: consumer_macros.Token = tokens[index]
        if token.kind == 'newline':
            at_line_start = True
        elif token.kind == 'directive':
            at_line_start = False
        elif at_line_start and token.text == '#':
            while index < len(tokens) and tokens[index].kind != 'newline':
                index += 1
            continue
        else:
            at_line_start = False
            code.append(token)
        index += 1
    return code


def skip_balanced(tokens: list[consumer_macros.Token], index: int, opening: str, closing: str) -> int:
    """The index after the bracket closing the one at index."""
    depth: int = 0
    while index < len(tokens):
        text: str = tokens[index].text
        if text == opening:
            depth += 1
        elif text == closing:
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return index


def skip_template_parameters(tokens: list[consumer_macros.Token], index: int) -> int:
    """The index after the '>' closing the '<' at index (parentheses nest)."""
    depth: int = 0
    while index < len(tokens):
        text: str = tokens[index].text
        if text == '(':
            index = skip_balanced(tokens, index, '(', ')')
            continue
        if text == '<':
            depth += 1
        elif text == '>':
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return index


def declarations_of(source: str) -> list[Declaration]:
    """The namespace-scope declarations of one header that a module may export."""
    tokens: list[consumer_macros.Token] = code_tokens(source)
    found: list[Declaration] = []
    # Each entry: the namespace path, or None for a level that exports nothing.
    stack: list[tuple[str, ...] | None] = [()]
    head: list[consumer_macros.Token] = []
    index: int = 0
    while index < len(tokens):
        token: consumer_macros.Token = tokens[index]
        text: str = token.text
        if text == 'template' and index + 1 < len(tokens) and tokens[index + 1].text == '<':
            head.append(token)
            index = skip_template_parameters(tokens, index + 1)
            continue
        if text == '[' and index + 1 < len(tokens) and tokens[index + 1].text == '[':
            index = skip_balanced(tokens, index, '[', ']')
            continue
        if text == '(':
            end: int = skip_balanced(tokens, index, '(', ')')
            head.extend(tokens[index:end])
            index = end
            continue
        if text == ';':
            record(head, stack[-1], found, False)
            head = []
            index += 1
            continue
        if text == '}':
            if len(stack) > 1:
                stack.pop()
            head = []
            index += 1
            continue
        if text == '{':
            words: list[str] = [part.text for part in head]
            if 'namespace' in words and '=' not in words:
                stack.append(namespace_entry(head, stack[-1]))
                head = []
                index += 1
                continue
            if words[:1] == ['extern'] and len(head) == 2 and head[1].kind == 'literal':
                stack.append(stack[-1])
                head = []
                index += 1
                continue
            if is_initializer(head):
                end = skip_balanced(tokens, index, '{', '}')
                head.extend(tokens[index:end])
                index = end
                continue
            record(head, stack[-1], found, True)
            index = skip_balanced(tokens, index, '{', '}')
            head = []
            continue
        head.append(token)
        index += 1
    return found


def namespace_entry(head: list[consumer_macros.Token], enclosing: tuple[str, ...] | None) -> tuple[str, ...] | None:
    if enclosing is None:
        return None
    names: list[str] = []
    after: bool = False
    for token in head:
        if token.text == 'namespace':
            after = True
        elif after and token.kind == 'identifier' and token.text != 'inline':
            names.append(token.text)
    if not names:
        return None  # an unnamed namespace: internal linkage
    for name in names:
        if name in INTERNAL_NAMESPACES:
            return None
    return enclosing + tuple(names)


def is_initializer(head: list[consumer_macros.Token]) -> bool:
    """True when a '{' after head starts a variable's initializer."""
    words: list[str] = [token.text for token in head]
    if '=' in words:
        return True
    for keyword in ('class', 'struct', 'union', 'enum', 'concept'):
        if keyword in words:
            return False
    if ')' in words or not head:
        return False
    return head[-1].kind == 'identifier' and head[-1].text not in ('const', 'noexcept', 'override', 'final')


def record(head: list[consumer_macros.Token], namespace: tuple[str, ...] | None, found: list[Declaration],
           has_body: bool) -> None:
    if namespace is None or not head:
        return
    name: str | None = declared_name(head)
    if name is None:
        return
    words: list[str] = [token.text for token in head]
    forward: bool = (not has_body and len(words) >= 2 and words[0] in ('class', 'struct', 'union', 'enum')
                     and words[-1] == name and '=' not in words and ':' not in words)
    declaration: Declaration = Declaration(namespace, name, forward)
    if declaration not in found:
        found.append(declaration)


def declared_name(head: list[consumer_macros.Token]) -> str | None:
    """The exportable name head declares, or None."""
    words: list[str] = [token.text for token in head]
    first: str = words[0]
    if first in ('friend', 'static_assert', 'namespace') or 'friend' in words or 'static' in words:
        return None
    if first == 'using':
        if len(head) >= 3 and head[1].kind == 'identifier' and words[2] == '=':
            return words[1]
        return None
    if first == 'extern' and 'template' in words:
        return None
    if 'concept' in words:
        return identifier_after(head, words.index('concept'))
    if 'typedef' in words:
        for position in range(len(head) - 2):
            if words[position] == '(' and words[position + 1] == '*' and head[position + 2].kind == 'identifier':
                return words[position + 2]
        identifiers: list[str] = [token.text for token in head if token.kind == 'identifier']
        return identifiers[-1] if identifiers else None
    for keyword in ('class', 'struct', 'union', 'enum'):
        if keyword in words and '(' not in words[:words.index(keyword)]:
            return identifier_after(head, words.index(keyword))
    opening: int = function_parenthesis(head)
    if opening >= 0:
        leading: list[str] = [word for word in words if word not in ('template', 'explicit')]
        # The lexer spells '->' and '::' as two punctuators each.
        if has_pair(words, opening, '-', '>') and leading[:1] == [words[opening - 1]]:
            return None  # a deduction guide
        name_start: int = operator_start(head, opening)
        name_start = opening - 1 if name_start < 0 else name_start
        if name_start > 1 and words[name_start - 1] == ':' and words[name_start - 2] == ':':
            return None  # an out-of-class member definition
        return operator_or_name(head, opening)
    # A variable: const and constexpr variables have internal linkage unless inline.
    if ('const' in words or 'constexpr' in words) and 'inline' not in words and 'extern' not in words:
        return None
    end: int = len(head)
    for position, word in enumerate(words):
        if word in ('=', '[', '{'):
            end = position
            break
    if end > 0 and words[end - 1] == '>':
        return None  # a variable template's specialization: the primary is exported
    for token in reversed(head[:end]):
        if token.kind == 'identifier' and token.text not in consumer_macros.NOT_DEFINABLE:
            return token.text
    return None


def has_pair(words: list[str], start: int, first: str, second: str) -> bool:
    for position in range(start, len(words) - 1):
        if words[position] == first and words[position + 1] == second:
            return True
    return False


def identifier_after(head: list[consumer_macros.Token], position: int) -> str | None:
    following: list[consumer_macros.Token] = head[position + 1:]
    for offset, token in enumerate(following):
        if token.text in ('class', 'struct', 'alignas', 'final'):
            continue
        # An export macro before the name (class GUI_FORMS_API Button).
        if (MACRO_SPELLING.fullmatch(token.text) and offset + 1 < len(following)
                and following[offset + 1].kind == 'identifier' and following[offset + 1].text != 'final'):
            continue
        if token.kind == 'identifier':
            return token.text
        if token.text not in ('(', ')') and token.kind != 'number':
            return None
    return None


def function_parenthesis(head: list[consumer_macros.Token]) -> int:
    """The index of the declarator's '(' (after a name or an operator), or -1."""
    for position in range(1, len(head)):
        if head[position].text != '(':
            continue
        previous: consumer_macros.Token = head[position - 1]
        if previous.kind == 'identifier' and not MACRO_SPELLING.fullmatch(previous.text):
            return position
        if previous.kind == 'punctuator' and operator_start(head, position) >= 0:
            return position
    return -1


def operator_start(head: list[consumer_macros.Token], opening: int) -> int:
    for position in range(opening - 1, max(-1, opening - 4), -1):
        if head[position].text == 'operator':
            return position
    return -1


def operator_or_name(head: list[consumer_macros.Token], opening: int) -> str:
    start: int = operator_start(head, opening)
    if start >= 0:
        return 'operator' + ''.join(token.text for token in head[start + 1:opening])
    return head[opening - 1].text


def module_declarations(root: Path, extra_roots: list[Path]) -> list[Declaration]:
    include: Path = root / 'include'
    roots: list[Path] = [include.resolve()] + extra_roots
    seeds: list[consumer_macros.Include] = []
    for umbrella in UMBRELLAS:
        seeds.append(consumer_macros.Include(umbrella, False, 1))
    closure: consumer_macros.Closure = consumer_macros.compute_closure(seeds, roots, include)
    # Names an internal area defines stay unexported even where a consumer
    # header forward-declares them (an internal forward declaration of a
    # consumer type does not count).
    internal_names: set[Declaration] = set()
    for path in closure.headers:
        if path.is_relative_to(include.resolve()):
            if path.relative_to(include.resolve()).as_posix().startswith(INTERNAL_AREAS):
                for declaration in declarations_of(consumer_macros.read_source(path)):
                    if not declaration.forward:
                        internal_names.add(declaration)
    declarations: list[Declaration] = []
    for path in closure.headers:
        if not path.is_relative_to(include.resolve()):
            continue  # STXlib's headers: its own module exports them
        relative: str = path.relative_to(include.resolve()).as_posix()
        if relative.startswith(INTERNAL_AREAS):
            continue
        for declaration in declarations_of(consumer_macros.read_source(path)):
            if (declaration.namespace[:1] == ('gui_forms',) and declaration not in internal_names
                    and declaration not in declarations):
                declarations.append(declaration)
    return declarations


def render_module(declarations: list[Declaration]) -> str:
    lines: list[str] = [
        '// Generated by tools/module_exports.py from the consumer headers; do not edit.',
        '//',
        '// The gui_forms module: `import gui_forms;`. The headers are included in the',
        '// global module fragment, so they are read here, in this unit, and no',
        '// macro of an importing translation unit reaches them; the consumer',
        '// surface is exported by name below. Macros are not exported.',
        'module;',
    ]
    for umbrella in UMBRELLAS:
        lines.append(f'#include "{umbrella}"')
    lines.append('export module gui_forms;')
    by_namespace: dict[tuple[str, ...], list[str]] = {}
    for declaration in declarations:
        by_namespace.setdefault(declaration.namespace, []).append(declaration.name)
    for namespace in sorted(by_namespace):
        qualified: str = '::'.join(namespace)
        lines.append('')
        lines.append(f'export namespace {qualified} {{')
        for name in sorted(set(by_namespace[namespace])):
            lines.append(f'using {qualified}::{name};')
        lines.append(f'}} // namespace {qualified}')
    return '\n'.join(lines) + '\n'


def main(argument_list: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('command', choices=('generate', 'check'))
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--include-root', action='append', default=[])
    arguments: argparse.Namespace = parser.parse_args(argument_list)
    root: Path = arguments.root.resolve()
    extra_roots: list[Path] = [Path(directory).resolve() for directory in arguments.include_root]
    text: str = render_module(module_declarations(root, extra_roots))
    output: Path = root / MODULE_PATH
    current: str | None = output.read_text(encoding='utf-8') if output.is_file() else None
    if current == text:
        return 0
    if arguments.command == 'check':
        print(f'module_exports: {MODULE_PATH} is stale; run `python3 -B tools/module_exports.py generate`',
              file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding='utf-8', newline='\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
