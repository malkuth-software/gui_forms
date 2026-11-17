"""consumer_shield writes the consumer-macro shields and checks their handshake.

A shield is the fallback for consumers who cannot `import gui_forms;`: C
programs of the C ABI, and C++ programs built without modules. Each is two
generated headers: begin saves (push_macro) and removes (#undef) every
identifier of its headers that a consumer may define
(tools/consumer_macros.py says which: the open names), and end restores each
one (pop_macro). There are two:

- the C shield, gui_forms/c/gf_c_shield_begin.h and gf_c_shield_end.h,
  checked in: the open names of the C ABI headers (gf_abi.h,
  gf_abi_classes.h, drawing_c_api.h) and of the C++ binding over them
  (gf_abi.hpp), which carry it themselves;
- the full shield, gui_forms/gf_shield_begin.h and gf_shield_end.h, the open
  names of every header under include/gui_forms, generated at build time
  into the build tree and installed from there (cmake/ConsumerShield.cmake).
  A C++ consumer without modules wraps its GUI.Forms includes in it
  (docs/user/consume-the-sdk.md).

The C shield's names are a subset of the full shield's: both classify with
the whole surface's first-party macros and configuration names.

Both act only at the outermost level and share one handshake, so either may
enclose the other. A header that carries the handshake includes the C
shield's begin after its #pragma once (or inside its include guard) and its
end at its bottom. Begin opens the shield only when none is open, and end
closes it only at the level that opened it. The level is kept on the
preprocessor's own macro stack, so no counter and no per-header name is
needed:

    begin:  #pragma push_macro("GF_SHIELD_OPENER")   save this level's flag
            #undef GF_SHIELD_OPENER                  not the opener by default
            #ifndef GF_SHIELD_ACTIVE                 no shield yet:
            #define GF_SHIELD_ACTIVE                   open it here
            #define GF_SHIELD_OPENER                   and say so
            push_macro / #undef for every name
            #endif
    end:    #ifdef GF_SHIELD_OPENER                  this level opened it:
            pop_macro for every name, in reverse
            #undef GF_SHIELD_ACTIVE
            #endif
            #pragma pop_macro("GF_SHIELD_OPENER")    the enclosing level's flag

Inside an open full shield a C-shield header's begin and end do nothing; a
C shield opened by such a header closes before the header ends.

    python3 -B tools/consumer_shield.py c --root . [--check] \\
        [--include-root DIR ...] [--include-directories-file FILE]
    python3 -B tools/consumer_shield.py full --root . --output-dir DIR [--check] [...]

c rewrites the C shield in include/gui_forms/c; with --check it fails when
the C shield is stale or a handshake header lacks its two include lines.
full writes the full shield into DIR (the gui_forms directory of a build or
install tree); with --check it fails when the files in DIR are missing or
stale, which is how an install of a stale shield fails.
"""
from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path

import consumer_macros


@dataclasses.dataclass(frozen=True)
class Shield:
    """One shield's two headers, as include spellings, and its name source."""
    begin: str
    end: str
    source: str


C_SHIELD: Shield = Shield('gui_forms/c/gf_c_shield_begin.h', 'gui_forms/c/gf_c_shield_end.h',
                          'the C ABI headers and gf_abi.hpp')
FULL_SHIELD: Shield = Shield('gui_forms/gf_shield_begin.h', 'gui_forms/gf_shield_end.h',
                             'every header under include/gui_forms')

# The handshake: the C shield's two include lines.
BEGIN_LINE: str = f'#include "{C_SHIELD.begin}"'
END_LINE: str = f'#include "{C_SHIELD.end}"'

# The headers that carry the handshake, relative to include/, and the seeds
# of the C shield's names. The generated ones (gf_abi_classes.h, gf_abi.hpp)
# get it from their generators.
HANDSHAKE_HEADERS: tuple[str, ...] = (
    'gui_forms/c/gf_abi.h', 'gui_forms/c/gf_abi_classes.h', 'gui_forms/c/gf_abi.hpp', 'gui_forms/drawing_c_api.h')

PRAGMA_ONCE = re.compile(r'^[ \t]*#[ \t]*pragma[ \t]+once\b[^\n]*\n?', re.MULTILINE)
DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*(\w+)[ \t]*(\w*)', re.MULTILINE)


def surface_closure(include: Path, roots: list[Path]) -> consumer_macros.Closure:
    seeds: list[consumer_macros.Include] = consumer_macros.surface_seeds(include / 'gui_forms', include)
    return consumer_macros.compute_closure(seeds, roots, include)


def shield_names(root: Path, extra_roots: list[Path], standard_names: frozenset[str]) -> list[str]:
    """The full shield's names: every open identifier of the consumer surface, sorted."""
    include: Path = root / 'include'
    roots: list[Path] = [include.resolve()] + extra_roots
    closure: consumer_macros.Closure = surface_closure(include, roots)
    classification: consumer_macros.Classification = consumer_macros.classify(closure.headers, standard_names)
    return sorted(classification.hostile)


def c_shield_names(root: Path, extra_roots: list[Path], standard_names: frozenset[str]) -> list[str]:
    """The C shield's names: the open identifiers the handshake headers reach, sorted.

    Classified with the whole surface's first-party macros and configuration
    names, so that they are a subset of shield_names and the same names the
    hostile-macro units of the C ABI and the binding define.
    """
    include: Path = root / 'include'
    roots: list[Path] = [include.resolve()] + extra_roots
    defined, conditions = consumer_macros.surface_exclusions(surface_closure(include, roots))
    seeds: list[consumer_macros.Include] = []
    for relative in HANDSHAKE_HEADERS:
        seeds.append(consumer_macros.Include(relative, False, 1))
    closure: consumer_macros.Closure = consumer_macros.compute_closure(seeds, roots, include)
    classification: consumer_macros.Classification = consumer_macros.classify(
        closure.headers, standard_names, first_party_macros=defined, configuration=conditions)
    return sorted(classification.hostile)


def render_begin(shield: Shield, names: list[str]) -> str:
    lines: list[str] = [
        f'/* Generated by tools/consumer_shield.py from {shield.source}; do not edit.',
        '   The consumer-macro shield, opening half: saves and removes every macro a',
        f'   consumer may have defined for one of the {len(names)} open names of',
        f'   {shield.source}, at the outermost level only',
        '   (tools/consumer_shield.py explains the handshake). No include guard:',
        '   it acts at every inclusion. */',
        '#pragma push_macro("GF_SHIELD_OPENER")',
        '#undef GF_SHIELD_OPENER',
        '#ifndef GF_SHIELD_ACTIVE',
        '#define GF_SHIELD_ACTIVE',
        '#define GF_SHIELD_OPENER',
    ]
    for name in names:
        lines.append(f'#pragma push_macro("{name}")')
        lines.append(f'#undef {name}')
    lines.append('#endif')
    return '\n'.join(lines) + '\n'


def render_end(shield: Shield, names: list[str]) -> str:
    begin_name: str = Path(shield.begin).name
    lines: list[str] = [
        f'/* Generated by tools/consumer_shield.py from {shield.source}; do not edit.',
        '   The consumer-macro shield, closing half: restores the consumer\'s macros',
        f'   at the level that opened the shield ({begin_name}). No include guard. */',
        '#ifdef GF_SHIELD_OPENER',
    ]
    for name in reversed(names):
        lines.append(f'#pragma pop_macro("{name}")')
    lines.append('#undef GF_SHIELD_ACTIVE')
    lines.append('#endif')
    lines.append('#pragma pop_macro("GF_SHIELD_OPENER")')
    return '\n'.join(lines) + '\n'


def blank_comments(text: str) -> str:
    """text with comments replaced by spaces; newlines and offsets kept."""
    output: list[str] = []
    index: int = 0
    length: int = len(text)
    while index < length:
        if text.startswith('//', index):
            end: int = text.find('\n', index)
            end = length if end < 0 else end
            output.append(' ' * (end - index))
            index = end
        elif text.startswith('/*', index):
            end = text.find('*/', index + 2)
            end = length if end < 0 else end + 2
            output.append(re.sub(r'[^\n]', ' ', text[index:end]))
            index = end
        elif text[index] in '"\'':
            quote: str = text[index]
            end = index + 1
            while end < length and text[end] != quote and text[end] != '\n':
                end += 2 if text[end] == '\\' else 1
            end = min(end + 1, length)
            output.append(text[index:end])
            index = end
        else:
            output.append(text[index])
            index += 1
    return ''.join(output)


def insertion_points(text: str) -> tuple[int, int]:
    """Offsets for the begin and end include lines.

    After #pragma once and at the end; else inside a leading include guard
    (#ifndef X, #define X, ..., final #endif). A header with neither is
    refused: the handshake needs the header to be read once.
    """
    code: str = blank_comments(text)
    once: re.Match[str] | None = PRAGMA_ONCE.search(code)
    if once:
        return once.end(), len(text)
    directives: list[re.Match[str]] = list(DIRECTIVE.finditer(code))
    if len(directives) >= 3:
        first: re.Match[str] = directives[0]
        second: re.Match[str] = directives[1]
        last: re.Match[str] = directives[-1]
        if (first.group(1) == 'ifndef' and second.group(1) == 'define' and second.group(2) == first.group(2)
                and last.group(1) == 'endif'):
            line_end: int = code.find('\n', second.end())
            opening: int = len(text) if line_end < 0 else line_end + 1
            return opening, last.start()
    raise ValueError('header has neither #pragma once nor a leading include guard')


def has_handshake(text: str) -> bool:
    """True when begin follows the once-only marker and end closes the header."""
    if BEGIN_LINE not in text or END_LINE not in text:
        return False
    stripped: str = remove_handshake(text)
    opening, closing = insertion_points(stripped)
    return text == insert_handshake_at(stripped, opening, closing)


def remove_handshake(text: str) -> str:
    output: list[str] = []
    for line in text.splitlines(keepends=True):
        if line.strip() not in (BEGIN_LINE, END_LINE):
            output.append(line)
    return ''.join(output)


def insert_handshake_at(text: str, opening: int, closing: int) -> str:
    before: str = text[:opening]
    middle: str = text[opening:closing]
    after: str = text[closing:]
    if before and not before.endswith('\n'):
        before += '\n'
    if middle and not middle.endswith('\n'):
        middle += '\n'
    return before + BEGIN_LINE + '\n' + middle + END_LINE + '\n' + after


def insert_handshake(text: str) -> str:
    """text with the two include lines in place (idempotent)."""
    stripped: str = remove_handshake(text)
    opening, closing = insertion_points(stripped)
    return insert_handshake_at(stripped, opening, closing)


def read_roots(arguments: argparse.Namespace) -> list[Path]:
    roots: list[Path] = [Path(root).resolve() for root in arguments.include_root]
    if arguments.include_directories_file:
        for line in arguments.include_directories_file.read_text(encoding='utf-8').splitlines():
            directory: str = line.strip()
            if directory and Path(directory).is_dir() and Path(directory).resolve() not in roots:
                roots.append(Path(directory).resolve())
    return roots


def compare(expected: dict[Path, str], write: bool, touch: bool, regenerate: str) -> int:
    """Writes each changed file, or reports it stale; 1 when one was stale.

    touch writes unchanged files too, so a build rule's outputs are never
    older than its inputs.
    """
    failed: int = 0
    for path, text in expected.items():
        current: str | None = path.read_text(encoding='utf-8') if path.is_file() else None
        if current == text and not (write and touch):
            continue
        if write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8', newline='\n')
        else:
            state: str = 'stale' if current is not None else 'missing'
            print(f'consumer_shield: {path} is {state}; {regenerate}', file=sys.stderr)
            failed = 1
    return failed


def main(argument_list: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('command', choices=('c', 'full'))
    parser.add_argument('--root', type=Path, required=True, help='the GUI.Forms checkout')
    parser.add_argument('--output-dir', type=Path, help='full: the gui_forms directory to write or check')
    parser.add_argument('--check', action='store_true', help='report stale files instead of writing them')
    parser.add_argument('--include-root', action='append', default=[])
    parser.add_argument('--include-directories-file', type=Path)
    parser.add_argument('--standard-names', type=Path)
    arguments: argparse.Namespace = parser.parse_args(argument_list)

    root: Path = arguments.root.resolve()
    names_file: Path = arguments.standard_names or root / 'manifests/standard_library_names.txt'
    standard_names: frozenset[str] = consumer_macros.load_standard_names(names_file)
    roots: list[Path] = read_roots(arguments)
    if arguments.command == 'full':
        if arguments.output_dir is None:
            parser.error('full needs --output-dir')
        names: list[str] = shield_names(root, roots, standard_names)
        directory: Path = arguments.output_dir
        expected: dict[Path, str] = {
            directory / Path(FULL_SHIELD.begin).name: render_begin(FULL_SHIELD, names),
            directory / Path(FULL_SHIELD.end).name: render_end(FULL_SHIELD, names),
        }
        return compare(expected, not arguments.check, True,
                       'rebuild the consumer shield (target gui_forms_consumer_shield) and install again')
    names = c_shield_names(root, roots, standard_names)
    expected = {
        root / 'include' / C_SHIELD.begin: render_begin(C_SHIELD, names),
        root / 'include' / C_SHIELD.end: render_end(C_SHIELD, names),
    }
    failed: int = compare(expected, not arguments.check, False,
                          'run `python3 -B tools/consumer_shield.py c --root .`')
    for relative in HANDSHAKE_HEADERS:
        header: Path = root / 'include' / relative
        if not has_handshake(header.read_text(encoding='utf-8')):
            print(f'consumer_shield: {relative} lacks the shield handshake (begin after the once-only '
                  'marker, end at the bottom)', file=sys.stderr)
            failed = 1
    return failed


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
