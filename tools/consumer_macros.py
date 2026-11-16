"""consumer_macros reads the consumer surface the way the preprocessor does.

The consumer surface is the set of headers a program includes: seed headers
(an umbrella, the C ABI) and every first-party header they reach. A consumer
may define any macro before including them, known to us or not: windows.h's
near, far, min and max, Xlib's Status, a predefined linux, or a name of the
client's own. This module finds, for a set of headers, every identifier
whose meaning a consumer macro could change, and sorts out the ones the
consumer is not entitled to define:

- keywords, alternative tokens, identifiers with special meaning, standard
  attribute tokens and the preprocessor's own names ([macro.names]);
- reserved identifiers (any leading underscore, or a double underscore);
- names declared by the standard library ([macro.names]: a translation unit
  that includes a standard header shall not define them), read from
  tests/hostile_macros/standard_library_names.txt;
- GUI.Forms' reserved prefixes (gf_, GF_, gd_, GD_, gui_forms, GUI_FORMS,
  gui_drawing, GUI_DRAWING, stxlib, STXLIB, stx_, STX_), which the consumer
  surface claims as the C library claims str and mem;
- names only read in #if, #ifdef and #elif lines (configuration inputs, not
  uses), and macros the first-party headers define themselves.

Everything else is open to the consumer, and the consumer surface must
survive any definition of it. A few standard names are made hostile anyway
(min, max, emit and index: ALWAYS_HOSTILE), because widely used headers
define them regardless; libc++ and the Microsoft STL defend min and max the
same way.

Two tools use this module: generate_hostile_macros.py writes the hostile
conformance header, and guard_header_macros.py wraps one header in
push_macro, #undef and pop_macro for exactly the identifiers it uses.

The lexer follows translation phases 1 to 3 closely enough for these
headers: line continuations, comments, string and character literals with
encoding prefixes and user-defined suffixes, raw strings and pp-numbers. It
does not evaluate conditionals, so a name in a block compiled on another
platform counts too; the result is the same on every host.

Run directly to refresh the standard name list from a libc++ include
directory:

    python3 -B tools/consumer_macros.py standard-names --libcxx DIR \\
        --output tests/hostile_macros/standard_library_names.txt
"""
from __future__ import annotations

import argparse
import dataclasses
import os
import re
import sys
from pathlib import Path

CXX_KEYWORDS: frozenset[str] = frozenset((
    'alignas alignof asm auto bool break case catch char char8_t char16_t char32_t class concept '
    'const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype '
    'default delete do double dynamic_cast else enum explicit export extern false float for friend '
    'goto if inline int long mutable namespace new noexcept nullptr operator private protected '
    'public register reinterpret_cast requires return short signed sizeof static static_assert '
    'static_cast struct switch template this thread_local throw true try typedef typeid typename '
    'union unsigned using virtual void volatile wchar_t while').split())
C_KEYWORDS: frozenset[str] = frozenset((
    'restrict typeof typeof_unqual constexpr nullptr '
    'complex imaginary noreturn').split())
ALTERNATIVE_TOKENS: frozenset[str] = frozenset(
    'and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq'.split())
SPECIAL_IDENTIFIERS: frozenset[str] = frozenset('override final import module'.split())
ATTRIBUTE_TOKENS: frozenset[str] = frozenset((
    'nodiscard noreturn deprecated fallthrough maybe_unused likely unlikely no_unique_address '
    'carries_dependency assume indeterminate').split())
PREPROCESSOR_NAMES: frozenset[str] = frozenset((
    'defined include include_next pragma once elifdef elifndef embed has_include '
    'has_cpp_attribute has_c_attribute has_embed').split())
NOT_DEFINABLE: frozenset[str] = (CXX_KEYWORDS | C_KEYWORDS | ALTERNATIVE_TOKENS | SPECIAL_IDENTIFIERS
                                 | ATTRIBUTE_TOKENS | PREPROCESSOR_NAMES)

RESERVED_PREFIXES: tuple[str, ...] = (
    'gf_', 'GF_', 'gd_', 'GD_', 'gui_forms', 'GUI_FORMS', 'gui_drawing', 'GUI_DRAWING',
    'stxlib', 'STXLIB', 'stx_', 'STX_')

# Standard names that widely used headers define as macros regardless of the
# standard: windows.h's min and max, Qt's emit, BSD Xos.h's index. The
# consumer surface must survive them.
ALWAYS_HOSTILE: frozenset[str] = frozenset(('min', 'max', 'emit', 'index'))

CONDITIONAL_DIRECTIVES: frozenset[str] = frozenset(
    'if ifdef ifndef elif elifdef elifndef'.split())
SKIPPED_DIRECTIVES: frozenset[str] = frozenset(
    'include include_next import pragma error warning line embed ident sccs'.split())

ENCODING_PREFIX = re.compile(r'(?:u8|u|U|L)?R?$')
IDENTIFIER_START = re.compile(r'[A-Za-z_]')
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
PP_NUMBER = re.compile(r"\.?[0-9](?:[eEpP][+-]|[A-Za-z0-9_.]|'(?=[A-Za-z0-9_]))*")
INCLUDE_SPELLING = re.compile(r'#\s*(?:include|include_next|import)\s*([<"])([^>"]+)[>"]')


@dataclasses.dataclass
class Token:
    kind: str  # 'identifier', 'number', 'literal', 'punctuator', 'directive' or 'newline'
    text: str
    line: int
    start: int = 0  # source offsets of the token's spelling
    end: int = 0


@dataclasses.dataclass
class Include:
    spelling: str
    angled: bool
    line: int


@dataclasses.dataclass
class HeaderScan:
    """What one header does with identifiers.

    uses maps each identifier the header's code or macro bodies use to the
    first line it appears on. conditions holds identifiers read in #if,
    #ifdef and #elif lines. defined and undefined hold the names of its
    #define and #undef lines. includes keeps the header's includes in order.
    """
    uses: dict[str, int]
    conditions: set[str]
    defined: set[str]
    undefined: set[str]
    includes: list[Include]


def lex(source: str) -> list[Token]:
    """Splits source into preprocessing tokens; comments become whitespace.

    A newline token ends every physical line that is not continued by a
    backslash; newlines inside block comments and raw strings produce none,
    as in translation phase 3. Include, pragma, error and line directives
    are returned as one 'directive' literal token holding their raw text, so
    their header names and messages never count as identifiers.
    """
    tokens: list[Token] = []
    index: int = 0
    line: int = 1
    length: int = len(source)
    at_line_start: bool = True
    while index < length:
        character: str = source[index]
        if character == '\\' and source.startswith('\n', index + 1):
            index += 2
            line += 1
            continue
        if character == '\\' and source.startswith('\r\n', index + 1):
            index += 3
            line += 1
            continue
        if character == '\n':
            tokens.append(Token('newline', '\n', line, index, index + 1))
            index += 1
            line += 1
            at_line_start = True
            continue
        if character in ' \t\r\f\v':
            index += 1
            continue
        if source.startswith('//', index):
            end: int = index
            while end < length and source[end] != '\n':
                if source[end] == '\\' and source.startswith('\n', end + 1):
                    line += 1
                    end += 2
                    continue
                end += 1
            index = end
            continue
        if source.startswith('/*', index):
            end = source.find('*/', index + 2)
            end = length if end < 0 else end + 2
            line += source.count('\n', index, end)
            index = end
            continue
        if at_line_start and character == '#':
            directive_end: int = index + 1
            while directive_end < length and source[directive_end] in ' \t':
                directive_end += 1
            name_match: re.Match[str] | None = IDENTIFIER.match(source, directive_end)
            directive_name: str = name_match.group(0) if name_match else ''
            if directive_name in SKIPPED_DIRECTIVES:
                end = index
                while end < length and source[end] != '\n':
                    if source[end] == '\\' and source.startswith('\n', end + 1):
                        end += 2
                        continue
                    if source.startswith('/*', end):
                        comment_end: int = source.find('*/', end + 2)
                        end = length if comment_end < 0 else comment_end + 2
                        continue
                    if source.startswith('//', end):
                        while end < length and source[end] != '\n':
                            end += 1
                        break
                    end += 1
                raw: str = source[index:end]
                tokens.append(Token('directive', raw, line, index, end))
                line += raw.count('\n')
                index = end
                at_line_start = False
                continue
        at_line_start = False
        if IDENTIFIER_START.match(character):
            identifier: re.Match[str] = IDENTIFIER.match(source, index)  # type: ignore[assignment]
            word: str = identifier.group(0)
            after: int = identifier.end()
            if after < length and source[after] in '"\'' and ENCODING_PREFIX.match(word):
                literal_start: int = index
                index = skip_literal(source, index, after)
                tokens.append(Token('literal', source[literal_start:index], line, literal_start, index))
                line += source.count('\n', identifier.start(), index)
                continue
            tokens.append(Token('identifier', word, line, index, after))
            index = after
            continue
        if character.isdigit() or (character == '.' and index + 1 < length and source[index + 1].isdigit()):
            number: re.Match[str] = PP_NUMBER.match(source, index)  # type: ignore[assignment]
            tokens.append(Token('number', number.group(0), line, index, number.end()))
            index = number.end()
            continue
        if character in '"\'':
            start: int = index
            index = skip_literal(source, index, index)
            tokens.append(Token('literal', source[start:index], line, start, index))
            continue
        tokens.append(Token('punctuator', character, line, index, index + 1))
        index += 1
    return tokens


def skip_literal(source: str, start: int, quote: int) -> int:
    """Returns the index after the literal whose opening quote is at quote.

    start is where its encoding prefix begins. A raw string runs to its
    delimiter; an ordinary literal stops at its closing quote or at the end
    of the line. A user-defined suffix is part of the literal's token.
    """
    length: int = len(source)
    delimiter_quote: str = source[quote]
    raw: bool = quote > start and source[quote - 1] == 'R'
    end: int
    if raw and delimiter_quote == '"':
        open_paren: int = source.find('(', quote)
        delimiter: str = source[quote + 1:open_paren]
        close: int = source.find(')' + delimiter + '"', open_paren)
        end = length if close < 0 else close + len(delimiter) + 2
    else:
        end = quote + 1
        while end < length and source[end] != delimiter_quote and source[end] != '\n':
            end += 2 if source[end] == '\\' else 1
        end = min(end + 1, length)
    suffix: re.Match[str] | None = IDENTIFIER.match(source, end)
    if suffix:
        end = suffix.end()
    return end


def scan_header(source: str) -> HeaderScan:
    """Sorts the identifiers of one header by what the header does with them."""
    tokens: list[Token] = lex(source)
    scan: HeaderScan = HeaderScan(uses={}, conditions=set(), defined=set(), undefined=set(), includes=[])
    index: int = 0
    count: int = len(tokens)
    at_line_start: bool = True
    while index < count:
        token: Token = tokens[index]
        if token.kind == 'newline':
            at_line_start = True
            index += 1
            continue
        if token.kind == 'directive':
            include: re.Match[str] | None = INCLUDE_SPELLING.match(token.text)
            if include:
                scan.includes.append(Include(include.group(2), include.group(1) == '<', token.line))
            at_line_start = False
            index += 1
            continue
        if at_line_start and token.kind == 'punctuator' and token.text == '#':
            line_end: int = index + 1
            while line_end < count and tokens[line_end].kind != 'newline':
                line_end += 1
            scan_directive(tokens[index + 1:line_end], scan)
            index = line_end
            continue
        at_line_start = False
        if token.kind == 'identifier':
            scan.uses.setdefault(token.text, token.line)
        index += 1
    return scan


def scan_directive(directive: list[Token], scan: HeaderScan) -> None:
    """Records one directive's identifiers; directive excludes the '#'."""
    if not directive or directive[0].kind != 'identifier':
        return
    name: str = directive[0].text
    rest: list[Token] = directive[1:]
    if name in CONDITIONAL_DIRECTIVES:
        for token in rest:
            if token.kind == 'identifier':
                scan.conditions.add(token.text)
        return
    if name == 'undef':
        if rest and rest[0].kind == 'identifier':
            scan.undefined.add(rest[0].text)
        return
    if name != 'define' or not rest or rest[0].kind != 'identifier':
        return
    scan.defined.add(rest[0].text)
    body: list[Token] = rest[1:]
    parameters: set[str] = set()
    # A function-like macro's '(' follows its name with no whitespace.
    if body and body[0].text == '(' and is_function_like(rest[0], body[0]):
        close: int = 1
        while close < len(body) and body[close].text != ')':
            if body[close].kind == 'identifier':
                parameters.add(body[close].text)
            close += 1
        body = body[close + 1:]
    for token in body:
        if token.kind == 'identifier' and token.text not in parameters:
            scan.uses.setdefault(token.text, token.line)


def is_function_like(name: Token, paren: Token) -> bool:
    """True when '(' follows the macro name with no whitespace between."""
    return paren.start == name.end


def resolve_include(include: Include, including: Path, roots: list[Path]) -> Path | None:
    """Finds an include under the first-party roots; None for system headers."""
    if not include.angled:
        local: Path = including.parent / include.spelling
        if local.is_file():
            return local.resolve()
    for root in roots:
        candidate: Path = root / include.spelling
        if candidate.is_file():
            return candidate.resolve()
    return None


@dataclasses.dataclass
class Closure:
    """The headers a set of seeds reaches, in first-reached order."""
    headers: dict[Path, HeaderScan]
    system_includes: list[str]


def compute_closure(seeds: list[Include], roots: list[Path], seed_directory: Path) -> Closure:
    """Scans every first-party header the seeds reach through the roots.

    seed_directory stands for the including file of the seeds (quoted seeds
    are looked up beside it first). An include that no root resolves is a
    system header: it is recorded by spelling and not scanned.
    """
    closure: Closure = Closure(headers={}, system_includes=[])
    pending: list[tuple[Include, Path]] = [(seed, seed_directory / 'seed') for seed in reversed(seeds)]
    while pending:
        include, including = pending.pop()
        resolved: Path | None = resolve_include(include, including, roots)
        if resolved is None:
            if include.spelling not in closure.system_includes:
                closure.system_includes.append(include.spelling)
            continue
        if resolved in closure.headers:
            continue
        scan: HeaderScan = scan_header(read_source(resolved))
        closure.headers[resolved] = scan
        for nested in reversed(scan.includes):
            pending.append((nested, resolved))
    return closure


def read_source(path: Path) -> str:
    return path.read_text(encoding='utf-8', errors='replace')


def is_reserved(name: str) -> bool:
    """Reserved to the implementation as a macro name ([lex.name])."""
    return name.startswith('_') or '__' in name


def has_reserved_prefix(name: str, prefixes: tuple[str, ...]) -> bool:
    for prefix in prefixes:
        if name.startswith(prefix):
            return True
    return False


def load_standard_names(path: Path) -> frozenset[str]:
    """Reads one name per line; '#' starts a comment."""
    names: set[str] = set()
    for raw_line in path.read_text(encoding='utf-8').splitlines():
        text: str = raw_line.split('#', 1)[0].strip()
        if text:
            names.add(text)
    return frozenset(names)


@dataclasses.dataclass
class Classification:
    """Identifiers of a header set, sorted by whether a consumer may define them.

    hostile: name -> (header, line) of its first use; the consumer may define
    these and the surface must survive it. excluded: reason -> names.
    first_party_macros: every #define of the set. unprefixed_macros and
    undefined_macros list #define and #undef lines that touch consumer
    names: the surface itself then breaks the consumer's space.
    """
    hostile: dict[str, tuple[Path, int]]
    excluded: dict[str, set[str]]
    first_party_macros: set[str]
    unprefixed_macros: dict[str, Path]
    undefined_macros: dict[str, Path]


def classify(headers: dict[Path, HeaderScan], standard_names: frozenset[str],
             prefixes: tuple[str, ...] = RESERVED_PREFIXES,
             always_hostile: frozenset[str] = ALWAYS_HOSTILE,
             first_party_macros: set[str] | None = None) -> Classification:
    """Sorts every identifier the headers use.

    first_party_macros adds macros defined outside the headers given (the
    rest of the closure, when one header is classified alone).
    """
    defined: set[str] = set(first_party_macros or ())
    conditions: set[str] = set()
    result: Classification = Classification(hostile={}, excluded={}, first_party_macros=defined,
                                            unprefixed_macros={}, undefined_macros={})
    for path, scan in headers.items():
        defined |= scan.defined
        conditions |= scan.conditions
        for name in scan.defined:
            if not is_reserved(name) and not has_reserved_prefix(name, prefixes):
                result.unprefixed_macros.setdefault(name, path)
        for name in scan.undefined:
            if not is_reserved(name) and not has_reserved_prefix(name, prefixes):
                result.undefined_macros.setdefault(name, path)
    for path, scan in headers.items():
        for name, line in scan.uses.items():
            reason: str = exclusion_reason(name, standard_names, prefixes, always_hostile, defined, conditions)
            if reason:
                result.excluded.setdefault(reason, set()).add(name)
            elif name not in result.hostile:
                result.hostile[name] = (path, line)
    return result


def exclusion_reason(name: str, standard_names: frozenset[str], prefixes: tuple[str, ...],
                     always_hostile: frozenset[str], defined: set[str], conditions: set[str]) -> str:
    """Returns why the consumer may not define name, or '' when it may."""
    if name in NOT_DEFINABLE:
        return 'language'
    if is_reserved(name):
        return 'reserved'
    if has_reserved_prefix(name, prefixes):
        return 'prefix'
    if name in defined:
        return 'first-party macro'
    if name in conditions:
        return 'configuration'
    if name in always_hostile:
        return ''
    if name in standard_names:
        return 'standard library'
    return ''


def standard_names_from_libcxx(libcxx: Path) -> list[str]:
    """Every non-reserved identifier libc++'s headers use, sorted.

    libc++ spells everything private with a reserved name, so the rest are
    the standard library's public names (with the C library names its
    wrappers re-export, and a few extensions). A consumer may define none
    of them before a standard header ([macro.names]).
    """
    names: set[str] = set()
    for directory, _subdirectories, files in os.walk(libcxx):
        for file_name in files:
            path: Path = Path(directory) / file_name
            if path.suffix not in ('', '.h') or file_name.startswith('.'):
                continue
            for token in lex(read_source(path)):
                if token.kind == 'identifier' and not is_reserved(token.text):
                    names.add(token.text)
    return sorted(names - NOT_DEFINABLE)


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    refresh: argparse.ArgumentParser = commands.add_parser('standard-names')
    refresh.add_argument('--libcxx', type=Path, required=True)
    refresh.add_argument('--output', type=Path, required=True)
    refresh.add_argument('--version', default='', help='libc++ version recorded in the header')
    options: argparse.Namespace = parser.parse_args(arguments)
    names: list[str] = standard_names_from_libcxx(options.libcxx)
    curated: str = '\n'.join(CURATED_STANDARD_MACROS.split()) + '\n'
    header: str = (
        '# Names declared by the C and C++ standard libraries ([macro.names]).\n'
        '# A translation unit that includes a standard header may not define them,\n'
        '# so the hostile-macro conformance test does not either (except the few\n'
        '# in tools/consumer_macros.py ALWAYS_HOSTILE, such as min and max).\n'
        '# Generated by: python3 -B tools/consumer_macros.py standard-names --libcxx DIR\n'
        f'# from libc++ {options.version or "(version not recorded)"}: every non-reserved\n'
        '# identifier its headers use, then the C standard macros, listed by hand.\n')
    options.output.write_text(header + '\n'.join(names) + '\n' + curated, encoding='utf-8')
    return 0


# C standard macros (C17 and C23), listed by hand: libc++ spells few of them.
CURATED_STANDARD_MACROS: str = '''
assert errno NULL EOF BUFSIZ FILENAME_MAX FOPEN_MAX L_tmpnam TMP_MAX SEEK_SET SEEK_CUR SEEK_END
EXIT_SUCCESS EXIT_FAILURE RAND_MAX MB_CUR_MAX MB_LEN_MAX CHAR_BIT CHAR_MIN CHAR_MAX SCHAR_MIN SCHAR_MAX
UCHAR_MAX SHRT_MIN SHRT_MAX USHRT_MAX INT_MIN INT_MAX UINT_MAX LONG_MIN LONG_MAX ULONG_MAX LLONG_MIN
LLONG_MAX ULLONG_MAX INT8_MIN INT8_MAX UINT8_MAX INT16_MIN INT16_MAX UINT16_MAX INT32_MIN INT32_MAX
UINT32_MAX INT64_MIN INT64_MAX UINT64_MAX INTPTR_MIN INTPTR_MAX UINTPTR_MAX INTMAX_MIN INTMAX_MAX
UINTMAX_MAX PTRDIFF_MIN PTRDIFF_MAX SIZE_MAX SIG_ATOMIC_MIN SIG_ATOMIC_MAX WCHAR_MIN WCHAR_MAX WINT_MIN
WINT_MAX INT8_C INT16_C INT32_C INT64_C UINT8_C UINT16_C UINT32_C UINT64_C INTMAX_C UINTMAX_C
FLT_MIN FLT_MAX FLT_EPSILON FLT_DIG FLT_MANT_DIG DBL_MIN DBL_MAX DBL_EPSILON DBL_DIG DBL_MANT_DIG
LDBL_MIN LDBL_MAX LDBL_EPSILON HUGE_VAL HUGE_VALF HUGE_VALL INFINITY NAN FP_NAN FP_INFINITE FP_ZERO
FP_NORMAL FP_SUBNORMAL offsetof va_start va_end va_arg va_copy setjmp longjmp SIGABRT SIGFPE
SIGILL SIGINT SIGSEGV SIGTERM SIG_DFL SIG_IGN SIG_ERR EDOM ERANGE EILSEQ CLOCKS_PER_SEC TIME_UTC
bool true false static_assert alignas alignof noreturn thread_local complex imaginary I
'''


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
