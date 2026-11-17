#!/usr/bin/env python3
# Copied from STXlib tools/check_std_rules.py at commit 60dea8f
# (60dea8f0175319987ac067080a37be278846f795, branch claude/std-rules-adoptable;
# D15, decision 6a), with STXlib branch claude/std-rules-speed's whole-text
# reading applied (one-pass strip_comments_and_literals, matching_lines; the
# same change to STXlib's own copy). Update it by copying a newer STXlib
# commit over this file and changing this line; GUI.Forms' configuration is
# tools/std_rules.toml, and tools/test_check_std_rules.py checks the reading.
r"""check_std_rules: rules for the C and C++ standard library, as a CI gate.

Audit B (STXlib's notes, audits/B-std-capabilities.md, section 3.16) sorts
the C and C++ standard libraries into what STXlib owns, wraps, leaves alone
and governs. Governing means a rule: a facility whose result the standard
leaves to the host, the locale, a hidden static state or the library's own
policy is either not used, or used only under a condition. This tool checks
the rules that can be read from source text. It is one file with no
dependency beyond Python 3.11's standard library, so a project copies it
(or runs it from a pinned STXlib checkout) and describes itself in a
configuration file; nothing in the tool names any project's paths.
tools/STD_RULES.md is the consumer's guide.

Usage:
  python3 -I check_std_rules.py [--config FILE] [options] [PATH...]

PATH is a file or a directory (searched for C and C++ sources); without
one, the configuration's `paths` are checked. Options:
  --config FILE        the project's configuration (TOML; format below)
  --profile NAME       consumer (the default) or strict: the consumer rules
                       plus the ones STXlib holds itself to (no streams, no
                       std::format, no assert, no locale at all, no
                       recursive or timed mutex, a lock-free check for every
                       atomic type). `stxlib` is accepted for strict.
  --root DIR           the directory that paths, globs and the allowance are
                       relative to (default: the configuration's `root`, or
                       the current directory without a configuration)
  --host-path GLOB     host code: rules marked "host code only" do not apply
                       there (repeatable; added to the configuration's)
  --exclude GLOB       a path not checked at all (repeatable; added)
  --allowance FILE     the shrink-only allowance (default: the
                       configuration's `allowance`; none without one)
  --write-allowance    rewrite the allowance from the current findings, exit 0
  --report             list the findings but exit 0 (a dry run; usage and
                       configuration errors still exit 2)
  --json FILE          also write the findings and stale lines as JSON
  --list-rules         print the rules in force and exit

Output (stable; CI may parse it). On standard output, sorted by path, line
and rule, one line per finding outside the allowance:
  <path>:<line>: [<rule>] <what>; instead: <replacement>
then one line per stale allowance entry:
  <allowance>:<line>: [stale] <rule> <path> allows <n>, finds <m>: lower it
Paths are POSIX paths relative to the root. A summary line starting
`check_std_rules:` goes to standard error. Exit status: 0 when the run
passes, 1 on a finding outside the allowance or a stale allowance line,
2 on a usage, configuration or allowance-file error (and when no source
is found).

Configuration (TOML, every key optional; an unknown key is an error):
  format = 1                       # the configuration format
  root = ".."                      # relative to the configuration's directory
  profile = "consumer"             # or "strict"
  paths = ["src", "include"]       # checked when no PATH is given
  host_paths = ["src/host/*"]      # globs (fnmatch: * also matches /)
  exclude = ["third_party/*"]
  allowance = "tools/std_rules_allowance.txt"
  disable = ["wide-text"]          # built-in rules turned off
  atomic_lock_free = true          # the lock-free check (default: strict)
  atomic_assert_file = "src/core/atomic_rules.cpp"  # named in its message
  [atomic_aliases]                 # the project's integer names
  "my_u32" = "uint32"
  [[rules]]                        # the project's own forbidden names
  id = "printf"                    # lower case, digits and '-'
  what = "printf's format is checked only by a compiler warning"
  instead = "the project's formatter"
  calls = ["printf", "fprintf"]    # free calls, bare or through std::
  qualified = ["cout"]             # std::<name>, a regular expression
  patterns = ['\bLEGACY_\w+']    # regular expressions over code text
  headers = ["cstdio", "stdio.h"]  # includes, by name
  host_only = false                # true: allowed under host_paths
  [[allow]]                        # a standing exception, with its reason
  paths = ["src/legacy/*"]
  rules = ["rand"]
  reason = "the legacy importer keeps its seeded rand until R9"

The allowance file: one line per rule and file, `<rule> <path> <count>`,
`#` starting a comment line; a missing file is an empty allowance. Within a
rule and file, the first <count> findings by line are admitted and the rest
are new. It may only shrink: a count above the findings it covers is stale
and fails the run, so a fixed finding must be removed from it.
--write-allowance seeds or rewrites it.

Allowing one use in the source: a comment on the line, or alone on the line
above,
  // std-rules: allow(rand) a reason, which is required
names the rules allowed there, separated by commas. A suppression without a
reason, or naming no rule in force, is itself a finding (rule `suppression`).

How the text is read: comments, string literals and character literals are
blanked before the names are matched (line numbers are kept), so prose may
name anything. Includes are matched on the raw line. A name is matched as a
call or a qualified name: `rand(` and `std::rand` are findings, a member
`engine.rand(` or `widget->system(` and a longer name `operand(` are not.
The check reads text, not the program: a macro that expands to a forbidden
call, or a call through a function pointer, is not seen. It is a lint, not
a proof; what it reports is certain, what it passes is not.

What no text check can see, and how STXlib covers it instead:
  - `sort` only with a total order: the test builds define libc++'s
    _LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY, which shuffles before
    every unstable sort, nth_element and partial_sort, so reliance on an
    unspecified order fails a test;
  - a std::hash value never observed, and no observed iteration order of an
    unordered container: review;
  - the throwing accessors of std::string_view and the containers under no
    exceptions: review.
"""

from __future__ import annotations

import argparse
import bisect
import dataclasses
import fnmatch
import json
import pathlib
import re
import sys
from typing import Any

SOURCE_SUFFIXES: frozenset[str] = frozenset(
    ('.c', '.cc', '.cpp', '.cxx', '.c++', '.h', '.hh', '.hpp', '.hxx', '.h++', '.inl', '.ipp', '.tpp', '.m', '.mm'))

# A call of a free function: not a member (`.`, `->`), not part of a longer
# name, and not qualified by anything but std:: (matched separately).
CALL_PREFIX: str = r'(?<![\w.>:])'


def calls(*names: str) -> str:
    """A pattern for a call of any of `names`, bare or through std::."""
    alternatives: str = '|'.join(names)
    return rf'(?:{CALL_PREFIX}|\bstd\s*::\s*)(?:{alternatives})\s*\('


def qualified(*names: str) -> str:
    """A pattern for std::<name> (a type or object)."""
    alternatives: str = '|'.join(names)
    return rf'\bstd\s*::\s*(?:{alternatives})\b'


def includes(*headers: str) -> str:
    """A pattern for an include of any of `headers` (angle brackets or quotes)."""
    alternatives: str = '|'.join(re.escape(header) for header in headers)
    return rf'^\s*#\s*include\s*[<"](?:{alternatives})[>"]'


@dataclasses.dataclass(frozen=True)
class Rule:
    identifier: str
    profile: str  # 'consumer' (every profile) or 'strict'
    what: str
    instead: str
    names: tuple[str, ...] = ()  # patterns over the stripped text
    headers: tuple[str, ...] = ()  # patterns over the raw lines
    host_only: bool = False  # allowed in host code


RULES: tuple[Rule, ...] = (
    Rule('regex', 'consumer',
         'std::regex: matching time and stack depth are the library\'s and can be exponential; the classes follow '
         'the global locale; it allocates and throws',
         'TextView search (R3), or a glob for file-name patterns',
         names=(qualified('regex', 'wregex', 'basic_regex', r'regex_\w+'),),
         headers=(includes('regex'),)),
    Rule('rand', 'consumer',
         'rand and srand: a hidden global state, and RAND_MAX and the generator differ by C library',
         'stxlib/random.hpp (and stxlib/entropy.hpp for a seed)',
         names=(calls('rand', 'srand', 'rand_r'), r'\bRAND_MAX\b')),
    Rule('strtok', 'consumer',
         'strtok keeps its position in a hidden static',
         'TextScanner (stxlib/numeric_text.hpp) or TextView search',
         names=(calls('strtok', 'wcstok'),)),
    Rule('strerror', 'consumer',
         'strerror returns a static buffer, and strerror_r has two incompatible signatures (GNU and XSI)',
         'ErrorRecord and ForeignCode (stxlib/error.hpp)',
         names=(calls('strerror'),)),
    Rule('broken-down-time', 'consumer',
         'localtime and gmtime return a static buffer and read TZ',
         'stxlib/local_time.hpp and stxlib/calendar.hpp',
         names=(calls('localtime', 'gmtime'),)),
    Rule('clock', 'consumer',
         'clock() is CPU time on POSIX and wall time on Windows',
         'stxlib/clocks.hpp (interval_now, or a process CPU clock where one is offered)',
         names=(rf'(?:{CALL_PREFIX}|\bstd\s*::\s*)clock\s*\(\s*\)', r'\bCLOCKS_PER_SEC\b')),
    Rule('setjmp', 'consumer',
         'setjmp and longjmp skip destructors, and a jump would cross the gate',
         'a Result returned up the stack',
         names=(r'\b(?:setjmp|longjmp|sigsetjmp|siglongjmp|_setjmp|_longjmp|jmp_buf)\b',),
         headers=(includes('csetjmp', 'setjmp.h'),)),
    Rule('qsort', 'consumer',
         'qsort and bsearch: no type safety, and the order of equal elements is unspecified',
         'std::stable_sort, or std::sort with a total order; std::lower_bound',
         names=(calls('qsort', 'qsort_r', 'qsort_s', 'bsearch', 'bsearch_s'),)),
    Rule('ato', 'consumer',
         'atoi, atol, atoll and atof: undefined behaviour on overflow and no failure report',
         'parse_* in stxlib/numeric_text.hpp',
         names=(calls('atoi', 'atol', 'atoll', 'atof', '_wtoi', '_wtol', '_wtof'),)),
    Rule('tmpnam', 'consumer',
         'tmpnam, tempnam and mktemp name a file that another process can create first',
         'a file created with exclusive creation in a directory the program owns (R10)',
         names=(calls('tmpnam', 'tmpnam_r', 'tmpnam_s', 'tempnam', '_tempnam', 'mktemp', '_mktemp', '_mktemp_s'),)),
    Rule('system', 'consumer',
         'system and popen run a shell, whose parsing differs by host',
         'stxlib/children.hpp (an argument list, no shell)',
         names=(calls('system', '_wsystem', 'popen', '_popen', '_wpopen'),)),
    Rule('locale-change', 'consumer',
         'changing the locale changes number text, character classes and collation for the whole process',
         'the classic "C" locale for machine text; STXlib is locale-free (D9 a)',
         # Imbuing a stream with the classic locale is the remedy, not a change.
         names=(calls('setlocale', '_wsetlocale', 'uselocale', '_configthreadlocale'),
                r'\blocale\s*::\s*global\s*\(',
                r'(?:\.|->)\s*imbue\s*\((?!\s*(?:std\s*::\s*)?locale\s*::\s*classic\s*\(\s*\)\s*\))')),
    Rule('locale-text', 'consumer',
         'strcoll, strxfrm and the multibyte conversions follow the locale',
         'byte comparison of UTF-8, and STXlib\'s transcoding (R15)',
         names=(calls('strcoll', 'strxfrm', 'wcscoll', 'wcsxfrm', 'mblen', 'mbtowc', 'wctomb', 'mbstowcs',
                      'wcstombs', 'mbrlen', 'mbrtowc', 'wcrtomb', 'mbsrtowcs', 'wcsrtombs', 'btowc', 'wctob'),)),
    Rule('fenv', 'consumer',
         'the floating-point environment is per thread, ignored by compilers without FENV_ACCESS, and traps '
         'are host-specific; STXlib\'s numerics assume round to nearest and no traps',
         'the default environment, never changed',
         names=(r'\b(?:fesetround|fegetround|fesetenv|fegetenv|feholdexcept|feupdateenv|feclearexcept|'
                r'feraiseexcept|fetestexcept|fesetexceptflag|fegetexceptflag|feenableexcept|fedisableexcept|'
                r'_controlfp|_controlfp_s|_control87|__control87_2)\b', r'\bFENV_ACCESS\b'),
         headers=(includes('cfenv', 'fenv.h'),)),
    Rule('execution-policy', 'consumer',
         'parallel algorithms: the backend and thread count are the library\'s, and reductions regroup '
         'floating-point sums',
         'the sequential algorithm, or AtomicPool (R8)',
         names=(r'\bstd\s*::\s*execution\s*::',),
         headers=(includes('execution'),)),
    Rule('special-functions', 'consumer',
         'the special math functions: quality varies widely by library, and libc++ implements few',
         'a function STXlib certifies, on need',
         names=(qualified(r'(?:assoc_laguerre|assoc_legendre|beta|comp_ellint_[123]|cyl_bessel_[ijk]|cyl_neumann|'
                          r'ellint_[123]|expint|hermite|laguerre|legendre|riemann_zeta|sph_bessel|sph_legendre|'
                          r'sph_neumann)[fl]?'),)),
    Rule('valarray', 'consumer',
         'valarray: aliasing and expression-template freedoms are the library\'s',
         'a plain array and a loop',
         names=(qualified('valarray', 'slice_array', 'gslice_array', 'mask_array', 'indirect_array', 'gslice',
                          'slice'),),
         headers=(includes('valarray'),)),
    Rule('unbounded-copy', 'consumer',
         'strcpy, strcat and gets write without a bound, and strncpy can leave no terminator',
         'a View copy with its count, or TextWriter',
         names=(calls('strcpy', 'strcat', 'strncpy', 'wcscpy', 'wcscat', 'wcsncpy', 'gets'),)),
    Rule('c-threads', 'consumer',
         'C11 threads.h: absent on Apple',
         'stxlib/threads.hpp',
         names=(r'\b(?:thrd_\w+|mtx_(?:init|lock|timedlock|trylock|unlock|destroy)|cnd_\w+|tss_\w+)\s*\(',),
         headers=(includes('threads.h'),)),
    Rule('pthread', 'consumer',
         'pthread outside the host layer: absent on Windows',
         'stxlib/threads.hpp',
         names=(rf'{CALL_PREFIX}pthread_\w+\s*\(',),
         headers=(includes('pthread.h'),),
         host_only=True),
    Rule('wide-text', 'consumer',
         'wchar_t is 16 bits on Windows and 32 elsewhere, and the wide functions follow the locale',
         'UTF-8 TextView; host code converts at the edge',
         names=(r'\bwchar_t\b', qualified('wstring', 'wstring_view', r'w(?:i|o|io)?stream', 'wcout', 'wcerr'),),
         headers=(includes('cwchar', 'wchar.h', 'cwctype', 'wctype.h', 'cuchar', 'uchar.h'),),
         host_only=True),
    # The strict profile's rules (STXlib holds itself to them).
    Rule('streams', 'strict',
         'no streams inside STXlib: the global locale, imbue and stream state',
         'stxlib/console.hpp, stxlib/files.hpp, TextWriter and TextScanner',
         names=(qualified('cout', 'cerr', 'clog', 'cin', r'(?:i|o)?fstream', r'(?:i|o)?stringstream',
                          r'(?:i|o)?spanstream', 'osyncstream', 'istream', 'ostream', 'iostream'),),
         headers=(includes('iostream', 'fstream', 'sstream', 'iomanip', 'syncstream', 'spanstream', 'istream',
                           'ostream'),)),
    Rule('format', 'strict',
         'std::format and std::print are not used in STXlib (a runtime format string throws, format allocates, '
         'and the L flag reads the locale)',
         'format_* and Field in stxlib/numeric_text.hpp',
         names=(qualified('format', 'vformat', 'format_to', 'format_to_n', 'vformat_to', 'print', 'println',
                          'vprint_unicode', 'vprint_nonunicode'),),
         headers=(includes('format', 'print'),)),
    Rule('assert', 'strict',
         'assert depends on NDEBUG, prints a host format and calls abort',
         'STXLIB_REQUIRE (stxlib/require.hpp)',
         names=(rf'{CALL_PREFIX}assert\s*\(',),
         headers=(includes('cassert', 'assert.h'),)),
    Rule('locale-use', 'strict',
         'STXlib is locale-free (D9 a): no locale is read either',
         'the classic "C" behaviour, written out',
         names=(qualified('locale', 'use_facet', 'has_facet', 'numpunct', 'ctype'), calls('localeconv', 'newlocale')),
         headers=(includes('locale', 'clocale', 'locale.h', 'xlocale.h'),)),
    Rule('recursive-mutex', 'strict',
         'recursive and timed mutexes are not used in STXlib (recursion is a design smell the house rules avoid)',
         'stxlib/threads.hpp\'s Mutex',
         names=(qualified('recursive_mutex', 'recursive_timed_mutex', 'timed_mutex', 'shared_timed_mutex'),)),
)

RULES_BY_IDENTIFIER: dict[str, Rule] = {rule.identifier: rule for rule in RULES}

# The lock-free rule (stxlib profile): every atomic type named must have
# is_always_lock_free asserted somewhere in the checked tree.
LOCK_FREE_RULE: str = 'atomic-lock-free'
ATOMIC_OPEN: re.Pattern[str] = re.compile(r'\bstd\s*::\s*(atomic_ref|atomic)\s*<')
ATOMIC_ASSERT_TAIL: re.Pattern[str] = re.compile(r'\s*::\s*is_always_lock_free\b')
TYPE_ALIASES: dict[str, str] = {
    'u8': 'uint8', 'std::uint8_t': 'uint8', 'uint8_t': 'uint8', 'unsignedchar': 'uint8',
    'u16': 'uint16', 'std::uint16_t': 'uint16', 'uint16_t': 'uint16', 'unsignedshort': 'uint16',
    'u32': 'uint32', 'std::uint32_t': 'uint32', 'uint32_t': 'uint32', 'unsigned': 'uint32', 'unsignedint': 'uint32',
    'u64': 'uint64', 'std::uint64_t': 'uint64', 'uint64_t': 'uint64',
    'i8': 'int8', 'std::int8_t': 'int8', 'int8_t': 'int8', 'signedchar': 'int8',
    'i16': 'int16', 'std::int16_t': 'int16', 'int16_t': 'int16', 'short': 'int16',
    'i32': 'int32', 'std::int32_t': 'int32', 'int32_t': 'int32', 'int': 'int32',
    'i64': 'int64', 'std::int64_t': 'int64', 'int64_t': 'int64',
    'usize': 'size', 'std::size_t': 'size', 'size_t': 'size',
    'uptr': 'uintptr', 'std::uintptr_t': 'uintptr', 'uintptr_t': 'uintptr',
}

SUPPRESSION: re.Pattern[str] = re.compile(r'std-rules:\s*allow\(([^)]*)\)\s*(.*)$')
SUPPRESSION_MARK: re.Pattern[str] = re.compile(r'std-rules:')


# One left-to-right pass over the text (re.sub), each alternative tried at a
# position in this order, exactly the cases of the character loop it replaced:
# a line comment (removed up to its line break), a block comment (its line
# breaks kept; an unterminated one runs to the end), a raw string literal not
# preceded by a letter, digit or underscore (one space and its line breaks; an
# unterminated one runs to the end), a digit separator (a quote between two
# alphanumerics: one space), and a quoted literal (one space; a backslash
# takes the next character, line break included, and an unescaped line break
# ends the literal and is consumed with it). Anything else is kept.
STRIPPED_LEXEMES: re.Pattern[str] = re.compile(
    r'(?P<line>//[^\n]*)'
    r'|(?P<block>/\*(?s:.*?)(?:\*/|\Z))'
    r'|(?P<raw>(?<!\w)R"(?P<delimiter>[^()\\ \n]{0,16})\((?s:.*?)(?:\)(?P=delimiter)"|\Z))'
    r"|(?P<separator>(?<=[^\W_])'(?=[^\W_]))"
    r'|(?P<literal>"(?:[^"\\\n]|\\(?s:.)|\\\Z)*(?:"|\n|\Z)'
    r"|'(?:[^'\\\n]|\\(?s:.)|\\\Z)*(?:'|\n|\Z))")


def stripped_lexeme(match: re.Match[str]) -> str:
    kind: str | None = match.lastgroup
    if kind == 'line':
        return ''
    if kind == 'block':
        return '\n' * match.group(0).count('\n')
    if kind == 'raw':
        return ' ' + '\n' * match.group(0).count('\n')
    # A digit separator or a quoted literal.
    return ' '


def strip_comments_and_literals(text: str) -> str:
    """Blanks comments and literals, keeping line breaks so line numbers hold.

    The same reading as tools/check_public_headers.py, plus raw string
    literals (R"delimiter( ... )delimiter"). STRIPPED_LEXEMES lists the cases.
    """
    return STRIPPED_LEXEMES.sub(stripped_lexeme, text)


# Whole-text matching. A rule's pattern is written for one line at a time; it
# is searched once over the file's lines joined with '\n' (re.MULTILINE, so ^
# and $ still mean a line's ends) when it can be rewritten so that no match or
# lookaround can cross or consume a line break: \s becomes [^\S\n], \W and \D
# become [^\w\n] and [^\d\n], and a negated class also refuses '\n'. A '\n'
# then reads like a line's ends to everything else (\b, lookbehinds and
# lookaheads over the rewritten classes), so the lines holding a match are
# exactly the lines a search of each line finds. A pattern using anything this
# rewriting does not cover (\A, \Z, \B, escapes naming a character or group by
# number, inline flags other than i, a class holding \s, \W or \D outside a
# negation) is searched line by line as before.
LINE_SAFE_FLAGS: frozenset[str] = frozenset('iaLu')


def line_safe(pattern: str) -> str | None:
    """The pattern rewritten for whole-text matching, or None when it must stay per line."""
    result: list[str] = []
    index: int = 0
    length: int = len(pattern)
    in_class: bool = False
    negated: bool = False
    class_start: int = 0
    while index < length:
        character: str = pattern[index]
        if character == '\\':
            if index + 1 >= length:
                return None
            escaped: str = pattern[index + 1]
            if escaped.isdigit() or escaped in 'AZBxuUNnrfvtag0':
                return None
            if in_class:
                if escaped in 'sWD' and not negated:
                    return None
                result.append(character + escaped)
            elif escaped == 's':
                result.append('[^\\S\\n]')
            elif escaped == 'W':
                result.append('[^\\w\\n]')
            elif escaped == 'D':
                result.append('[^\\d\\n]')
            else:
                result.append(character + escaped)
            index += 2
            continue
        if in_class:
            if character == ']' and index > class_start:
                if negated:
                    result.append('\\n')
                result.append(']')
                in_class = False
                index += 1
                continue
            if character == '\n':
                return None
            result.append(character)
            index += 1
            continue
        if character == '[':
            in_class = True
            negated = index + 1 < length and pattern[index + 1] == '^'
            result.append('[^' if negated else '[')
            index += 2 if negated else 1
            # A ']' first in a class is a member, not its end.
            class_start = index
            continue
        if character == '(' and pattern.startswith('(?', index):
            following: str = pattern[index + 2:index + 3]
            if following.isalpha() or following == '-':
                flags: str = ''
                cursor: int = index + 2
                while cursor < length and (pattern[cursor].isalpha() or pattern[cursor] == '-'):
                    flags += pattern[cursor]
                    cursor += 1
                if not set(flags.replace('-', '')) <= LINE_SAFE_FLAGS:
                    return None
        if character == '\n':
            return None
        result.append(character)
        index += 1
    if in_class:
        return None
    return ''.join(result)


@dataclasses.dataclass(frozen=True)
class JoinedLines:
    """A file's lines (str.splitlines) joined with '\n', and where each line starts."""
    text: str
    starts: list[int]


def joined_lines(lines: list[str]) -> JoinedLines:
    starts: list[int] = []
    offset: int = 0
    line: str
    for line in lines:
        starts.append(offset)
        offset += len(line) + 1
    return JoinedLines('\n'.join(lines), starts)


@dataclasses.dataclass(frozen=True)
class LinePattern:
    """A rule's pattern, with its whole-text form when it has one."""
    per_line: re.Pattern[str]
    whole: re.Pattern[str] | None


def line_pattern(pattern: str) -> LinePattern:
    safe: str | None = line_safe(pattern)
    whole: re.Pattern[str] | None = None if safe is None else re.compile(safe, re.MULTILINE)
    return LinePattern(re.compile(pattern), whole)


def matching_lines(pattern: LinePattern, lines: list[str], joined: JoinedLines) -> list[int]:
    """The 1-based numbers of the lines a search of each line finds, in order, each once."""
    numbers: list[int] = []
    if not lines:
        return numbers
    if pattern.whole is None:
        line_number: int
        line: str
        for line_number, line in enumerate(lines, start=1):
            if pattern.per_line.search(line):
                numbers.append(line_number)
        return numbers
    match: re.Match[str]
    for match in pattern.whole.finditer(joined.text):
        number: int = bisect.bisect_right(joined.starts, match.start())
        if not numbers or numbers[-1] != number:
            numbers.append(number)
    return numbers


# ---- configuration ------------------------------------------------------------

CONFIG_FORMAT: int = 1
PROFILES: dict[str, str] = {'consumer': 'consumer', 'strict': 'strict', 'stxlib': 'strict'}
RULE_IDENTIFIER: re.Pattern[str] = re.compile(r'^[a-z0-9][a-z0-9-]*$')
CONFIG_KEYS: frozenset[str] = frozenset(
    ('format', 'root', 'profile', 'paths', 'host_paths', 'exclude', 'allowance', 'disable', 'atomic_lock_free',
     'atomic_assert_file', 'atomic_aliases', 'rules', 'allow'))
CUSTOM_RULE_KEYS: frozenset[str] = frozenset(
    ('id', 'what', 'instead', 'calls', 'qualified', 'patterns', 'headers', 'host_only'))
ALLOW_KEYS: frozenset[str] = frozenset(('paths', 'rules', 'reason'))


class ConfigError(Exception):
    """A configuration, allowance or usage error: exit status 2."""


@dataclasses.dataclass(frozen=True)
class PathAllowance:
    paths: tuple[str, ...]
    rules: frozenset[str]
    reason: str


@dataclasses.dataclass
class Settings:
    root: pathlib.Path
    profile: str = 'consumer'
    paths: list[str] = dataclasses.field(default_factory=list)
    host_paths: list[str] = dataclasses.field(default_factory=list)
    exclude: list[str] = dataclasses.field(default_factory=list)
    allowance: str | None = None
    disable: frozenset[str] = frozenset()
    atomic_lock_free: bool | None = None  # None: on for the strict profile
    atomic_assert_file: str | None = None
    atomic_aliases: dict[str, str] = dataclasses.field(default_factory=dict)
    custom_rules: list[Rule] = dataclasses.field(default_factory=list)
    path_allowances: list[PathAllowance] = dataclasses.field(default_factory=list)

    def lock_free_check(self) -> bool:
        if self.atomic_lock_free is None:
            return self.profile == 'strict'
        return self.atomic_lock_free


def string_list(table: dict[str, Any], key: str, where: str) -> list[str]:
    value: Any = table.get(key, [])
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ConfigError(f'{where}: {key} must be a list of strings')
    return list(value)


def text_value(table: dict[str, Any], key: str, where: str, required: bool) -> str | None:
    value: Any = table.get(key)
    if value is None:
        if required:
            raise ConfigError(f'{where}: {key} is required')
        return None
    if not isinstance(value, str) or (required and not value.strip()):
        raise ConfigError(f'{where}: {key} must be a non-empty string')
    return value


def checked_patterns(patterns: list[str], where: str) -> None:
    pattern: str
    for pattern in patterns:
        try:
            re.compile(pattern)
        except re.error as problem:
            raise ConfigError(f'{where}: the pattern {pattern!r} does not compile: {problem}') from None


def custom_rule(table: Any, index: int) -> Rule:
    where: str = f'[[rules]] {index + 1}'
    if not isinstance(table, dict):
        raise ConfigError(f'{where}: must be a table')
    unknown: set[str] = set(table) - CUSTOM_RULE_KEYS
    if unknown:
        raise ConfigError(f'{where}: unknown key(s) {", ".join(sorted(unknown))}')
    identifier: str = text_value(table, 'id', where, True) or ''
    if RULE_IDENTIFIER.match(identifier) is None:
        raise ConfigError(f'{where}: id {identifier!r} must be lower case letters, digits and "-"')
    where = f'[[rules]] {identifier}'
    what: str = text_value(table, 'what', where, True) or ''
    instead: str = text_value(table, 'instead', where, True) or ''
    call_names: list[str] = string_list(table, 'calls', where)
    qualified_names: list[str] = string_list(table, 'qualified', where)
    patterns: list[str] = string_list(table, 'patterns', where)
    header_names: list[str] = string_list(table, 'headers', where)
    host_only: Any = table.get('host_only', False)
    if not isinstance(host_only, bool):
        raise ConfigError(f'{where}: host_only must be true or false')
    names: list[str] = []
    if call_names:
        names.append(calls(*(re.escape(name) for name in call_names)))
    if qualified_names:
        names.append(qualified(*qualified_names))
    names.extend(patterns)
    headers: tuple[str, ...] = (includes(*header_names),) if header_names else ()
    if not names and not headers:
        raise ConfigError(f'{where}: names nothing (give calls, qualified, patterns or headers)')
    checked_patterns(names, where)
    return Rule(identifier, 'project', what, instead, names=tuple(names), headers=headers, host_only=host_only)


def path_allowance(table: Any, index: int) -> PathAllowance:
    where: str = f'[[allow]] {index + 1}'
    if not isinstance(table, dict):
        raise ConfigError(f'{where}: must be a table')
    unknown: set[str] = set(table) - ALLOW_KEYS
    if unknown:
        raise ConfigError(f'{where}: unknown key(s) {", ".join(sorted(unknown))}')
    paths: list[str] = string_list(table, 'paths', where)
    rules: list[str] = string_list(table, 'rules', where)
    reason: str = text_value(table, 'reason', where, True) or ''
    if not paths or not rules:
        raise ConfigError(f'{where}: paths and rules must each name at least one')
    return PathAllowance(tuple(paths), frozenset(rules), reason)


def read_config(path: pathlib.Path) -> Settings:
    try:
        import tomllib
    except ImportError:
        raise ConfigError('a configuration needs Python 3.11 or later (tomllib)') from None
    try:
        document: dict[str, Any] = tomllib.loads(path.read_text(encoding='utf-8'))
    except OSError as problem:
        raise ConfigError(f'{path}: {problem.strerror}') from None
    except tomllib.TOMLDecodeError as problem:
        raise ConfigError(f'{path}: {problem}') from None
    where: str = path.as_posix()
    unknown: set[str] = set(document) - CONFIG_KEYS
    if unknown:
        raise ConfigError(f'{where}: unknown key(s) {", ".join(sorted(unknown))}')
    form: Any = document.get('format', CONFIG_FORMAT)
    if form != CONFIG_FORMAT:
        raise ConfigError(f'{where}: format {form!r} is not {CONFIG_FORMAT}, the one this tool reads')
    root_text: str = text_value(document, 'root', where, False) or '.'
    settings: Settings = Settings(root=(path.parent / root_text).resolve())
    profile: str = text_value(document, 'profile', where, False) or 'consumer'
    if profile not in PROFILES:
        raise ConfigError(f'{where}: profile must be one of {", ".join(sorted(PROFILES))}')
    settings.profile = PROFILES[profile]
    settings.paths = string_list(document, 'paths', where)
    settings.host_paths = string_list(document, 'host_paths', where)
    settings.exclude = string_list(document, 'exclude', where)
    settings.allowance = text_value(document, 'allowance', where, False)
    settings.disable = frozenset(string_list(document, 'disable', where))
    lock_free: Any = document.get('atomic_lock_free')
    if lock_free is not None and not isinstance(lock_free, bool):
        raise ConfigError(f'{where}: atomic_lock_free must be true or false')
    settings.atomic_lock_free = lock_free
    settings.atomic_assert_file = text_value(document, 'atomic_assert_file', where, False)
    aliases: Any = document.get('atomic_aliases', {})
    if not isinstance(aliases, dict) or not all(isinstance(value, str) for value in aliases.values()):
        raise ConfigError(f'{where}: atomic_aliases must be a table of strings')
    settings.atomic_aliases = {re.sub(r'\s+', '', key): value for key, value in aliases.items()}
    rules: Any = document.get('rules', [])
    if not isinstance(rules, list):
        raise ConfigError(f'{where}: rules must be an array of tables ([[rules]])')
    settings.custom_rules = [custom_rule(table, index) for index, table in enumerate(rules)]
    allows: Any = document.get('allow', [])
    if not isinstance(allows, list):
        raise ConfigError(f'{where}: allow must be an array of tables ([[allow]])')
    settings.path_allowances = [path_allowance(table, index) for index, table in enumerate(allows)]
    return settings


def rules_in_force(settings: Settings) -> list[Rule]:
    """The built-in rules of the profile, less the disabled ones, then the project's."""
    built_in: set[str] = set(RULES_BY_IDENTIFIER) | {LOCK_FREE_RULE}
    unknown: set[str] = settings.disable - built_in
    if unknown:
        raise ConfigError(f'disable: no built-in rule {", ".join(sorted(unknown))}')
    chosen: list[Rule] = [rule for rule in RULES if (rule.profile == 'consumer' or settings.profile == 'strict')
                          and rule.identifier not in settings.disable]
    custom: Rule
    for custom in settings.custom_rules:
        if custom.identifier in built_in or custom.identifier in ('suppression', 'stale'):
            raise ConfigError(f'[[rules]] {custom.identifier}: the id is a built-in rule\'s')
        if any(rule.identifier == custom.identifier for rule in chosen):
            raise ConfigError(f'[[rules]] {custom.identifier}: defined twice')
        chosen.append(custom)
    return chosen


def known_identifiers(settings: Settings, rules: list[Rule]) -> frozenset[str]:
    names: set[str] = {rule.identifier for rule in rules}
    if settings.lock_free_check():
        names.add(LOCK_FREE_RULE)
    return frozenset(names)


# ---- reading one file -----------------------------------------------------------


@dataclasses.dataclass(order=True)
class Finding:
    path: str
    line: int
    rule: str
    message: str

    def text(self) -> str:
        return f'{self.path}:{self.line}: [{self.rule}] {self.message}'


@dataclasses.dataclass
class Suppressions:
    by_line: dict[int, frozenset[str]]
    findings: list[Finding]


def read_suppressions(path_text: str, lines: list[str], known: frozenset[str]) -> Suppressions:
    """The rules allowed on each line, and the malformed suppressions."""
    by_line: dict[int, frozenset[str]] = {}
    findings: list[Finding] = []
    line_number: int
    line: str
    for line_number, line in enumerate(lines, start=1):
        if SUPPRESSION_MARK.search(line) is None:
            continue
        match: re.Match[str] | None = SUPPRESSION.search(line)
        if match is None:
            findings.append(Finding(path_text, line_number, 'suppression', 'write std-rules: allow(<rule>) <reason>'))
            continue
        names: list[str] = [name.strip() for name in match.group(1).split(',') if name.strip()]
        reason: str = match.group(2).strip().rstrip('*/').strip()
        allowed_names: list[str] = [name for name in names if name in known]
        unknown: list[str] = [name for name in names if name not in known]
        if unknown or not names:
            findings.append(Finding(path_text, line_number, 'suppression',
                                    f'no such rule: {", ".join(unknown) if unknown else "(none named)"}'))
        if not reason:
            findings.append(Finding(path_text, line_number, 'suppression', 'a suppression needs a reason'))
            continue
        allowed: frozenset[str] = frozenset(allowed_names)
        by_line[line_number] = by_line.get(line_number, frozenset()) | allowed
        # A comment alone on its line covers the next line too.
        code_before: str = line[:match.start()].strip()
        if code_before in ('//', '/*', ''):
            by_line[line_number + 1] = by_line.get(line_number + 1, frozenset()) | allowed
    return Suppressions(by_line, findings)


def line_of(text: str, offset: int) -> int:
    return text.count('\n', 0, offset) + 1


def matching_angle(text: str, start: int) -> int:
    """The index of the '>' that closes the '<' just before `start`, or -1."""
    depth: int = 1
    index: int = start
    while index < len(text):
        character: str = text[index]
        if character == '<':
            depth += 1
        elif character == '>':
            depth -= 1
            if depth == 0:
                return index
        elif character in ';{}':
            return -1
        index += 1
    return -1


def normal_type(argument: str, aliases: dict[str, str]) -> str:
    compact: str = re.sub(r'\bconst\b|\bvolatile\b', '', argument)
    compact = re.sub(r'\s+', '', compact)
    if compact.endswith('*'):
        # Every object pointer is one size; one assert covers them all.
        return 'pointer'
    if compact in aliases:
        return aliases[compact]
    return TYPE_ALIASES.get(compact, compact)


@dataclasses.dataclass
class AtomicUse:
    path: str
    line: int
    kind: str  # 'atomic' or 'atomic_ref'
    type_name: str


def atomic_uses(path_text: str, stripped: str,
                aliases: dict[str, str]) -> tuple[list[AtomicUse], set[tuple[str, str]]]:
    """The atomic types named in a file, and those with is_always_lock_free asserted."""
    uses: list[AtomicUse] = []
    asserted: set[tuple[str, str]] = set()
    match: re.Match[str]
    for match in ATOMIC_OPEN.finditer(stripped):
        close: int = matching_angle(stripped, match.end())
        if close < 0:
            continue
        type_name: str = normal_type(stripped[match.end():close], aliases)
        kind: str = match.group(1)
        if ATOMIC_ASSERT_TAIL.match(stripped, close + 1):
            asserted.add((kind, type_name))
        else:
            uses.append(AtomicUse(path_text, line_of(stripped, match.start()), kind, type_name))
    return uses, asserted


@dataclasses.dataclass(frozen=True)
class CompiledRule:
    rule: Rule
    names: tuple[LinePattern, ...]
    headers: tuple[LinePattern, ...]


def compile_rules(rules: list[Rule]) -> list[CompiledRule]:
    return [CompiledRule(rule, tuple(line_pattern(pattern) for pattern in rule.names),
                         tuple(line_pattern(pattern) for pattern in rule.headers)) for rule in rules]


def matches_any(relative: str, patterns: list[str] | tuple[str, ...]) -> bool:
    return any(fnmatch.fnmatchcase(relative, pattern) for pattern in patterns)


@dataclasses.dataclass
class FileResult:
    findings: list[Finding]
    uses: list[AtomicUse]
    asserted: set[tuple[str, str]]
    by_line: dict[int, frozenset[str]]


def check_file(path: pathlib.Path, path_text: str, rules: list[CompiledRule], host_code: bool,
               standing: frozenset[str], known: frozenset[str], settings: Settings) -> FileResult:
    original: str = path.read_text(encoding='utf-8', errors='replace')
    lines: list[str] = original.splitlines()
    suppressions: Suppressions = read_suppressions(path_text, lines, known)
    findings: list[Finding] = list(suppressions.findings)
    stripped: str = strip_comments_and_literals(original)
    stripped_lines: list[str] = stripped.splitlines()
    joined: JoinedLines = joined_lines(lines)
    joined_stripped: JoinedLines = joined_lines(stripped_lines)
    seen: set[tuple[int, str]] = set()
    compiled: CompiledRule
    for compiled in rules:
        rule: Rule = compiled.rule
        if (rule.host_only and host_code) or rule.identifier in standing:
            continue
        hits: list[int] = []
        pattern: LinePattern
        for pattern in compiled.headers:
            hits.extend(matching_lines(pattern, lines, joined))
        for pattern in compiled.names:
            hits.extend(matching_lines(pattern, stripped_lines, joined_stripped))
        line_number: int
        for line_number in hits:
            if (line_number, rule.identifier) in seen:
                continue
            seen.add((line_number, rule.identifier))
            if rule.identifier in suppressions.by_line.get(line_number, frozenset()):
                continue
            where: str = ' (host code only)' if rule.host_only else ''
            findings.append(Finding(path_text, line_number, rule.identifier,
                                    f'{rule.what}{where}; instead: {rule.instead}'))
    uses: list[AtomicUse] = []
    asserted: set[tuple[str, str]] = set()
    if settings.lock_free_check():
        uses, asserted = atomic_uses(path_text, stripped, settings.atomic_aliases)
        if LOCK_FREE_RULE in standing:
            uses = []
    return FileResult(findings, uses, asserted, suppressions.by_line)


def gather(paths: list[pathlib.Path]) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    path: pathlib.Path
    for path in paths:
        if path.is_dir():
            files.extend(sorted(candidate for candidate in path.rglob('*')
                                if candidate.is_file() and candidate.suffix.lower() in SOURCE_SUFFIXES))
        elif path.is_file():
            files.append(path)
        else:
            raise ConfigError(f'no such file or directory: {path.as_posix()}')
    return files


def relative_text(path: pathlib.Path, root: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


# ---- the allowance --------------------------------------------------------------

ALLOWANCE_LINE: re.Pattern[str] = re.compile(r'^([a-z0-9][a-z0-9-]*)\s+(\S+)\s+(\d+)$')


@dataclasses.dataclass(frozen=True)
class AllowanceEntry:
    count: int
    line: int


def parse_allowance(text: str, where: str) -> dict[tuple[str, str], AllowanceEntry]:
    """The allowance's counts by (rule, path); `#` starts a comment line."""
    entries: dict[tuple[str, str], AllowanceEntry] = {}
    number: int
    raw: str
    for number, raw in enumerate(text.splitlines(), start=1):
        line: str = raw.strip()
        if not line or line.startswith('#'):
            continue
        match: re.Match[str] | None = ALLOWANCE_LINE.match(line)
        if match is None:
            raise ConfigError(f'{where}:{number}: expected "<rule> <path> <count>"')
        key: tuple[str, str] = (match.group(1), match.group(2))
        if key in entries:
            raise ConfigError(f'{where}:{number}: {key[0]} {key[1]} is listed twice')
        entries[key] = AllowanceEntry(int(match.group(3)), number)
    return entries


def format_allowance(findings: list[Finding]) -> str:
    counts: dict[tuple[str, str], int] = {}
    finding: Finding
    for finding in findings:
        if re.search(r'\s', finding.path):
            raise ConfigError(f'{finding.path}: a path with a blank cannot be listed in the allowance; '
                              'fix or suppress its findings')
        key: tuple[str, str] = (finding.rule, finding.path)
        counts[key] = counts.get(key, 0) + 1
    lines: list[str] = ['# check_std_rules allowance: existing findings, one line per rule and file.',
                        '# Shrink-only: lower a count (or drop the line) as findings are fixed; never raise one.']
    ordered: list[tuple[str, str, int]] = sorted((path, rule, count) for (rule, path), count in counts.items())
    path: str
    rule: str
    count: int
    for path, rule, count in ordered:
        lines.append(f'{rule} {path} {count}')
    return '\n'.join(lines) + '\n'


@dataclasses.dataclass
class Judgement:
    new: list[Finding]
    admitted: int
    stale: list[str]


def judge(findings: list[Finding], allowance: dict[tuple[str, str], AllowanceEntry], where: str) -> Judgement:
    """Within one rule and file the first `count` findings by line are admitted; a count
    above the findings is stale."""
    by_key: dict[tuple[str, str], list[Finding]] = {}
    finding: Finding
    for finding in sorted(findings):
        by_key.setdefault((finding.rule, finding.path), []).append(finding)
    new: list[Finding] = []
    admitted: int = 0
    key: tuple[str, str]
    group: list[Finding]
    for key, group in by_key.items():
        entry: AllowanceEntry | None = allowance.get(key)
        allowed: int = entry.count if entry is not None else 0
        new.extend(group[allowed:])
        admitted += min(allowed, len(group))
    stale: list[str] = []
    by_line: list[tuple[int, tuple[str, str], AllowanceEntry]] = sorted(
        (entry.line, key, entry) for key, entry in allowance.items())
    line: int
    entry: AllowanceEntry
    for line, key, entry in by_line:
        found: int = len(by_key.get(key, []))
        if entry.count > found:
            stale.append(f'{where}:{entry.line}: [stale] {key[0]} {key[1]} allows {entry.count}, finds {found}: '
                         'lower it')
    return Judgement(sorted(new), admitted, stale)


# ---- the run ----------------------------------------------------------------------


def settings_for(arguments: argparse.Namespace) -> Settings:
    settings: Settings
    if arguments.config:
        settings = read_config(pathlib.Path(arguments.config))
    else:
        settings = Settings(root=pathlib.Path('.').resolve())
    if arguments.root:
        settings.root = pathlib.Path(arguments.root).resolve()
    if arguments.profile:
        settings.profile = PROFILES[arguments.profile]
    settings.host_paths.extend(arguments.host_path)
    settings.exclude.extend(arguments.exclude)
    if arguments.allowance:
        settings.allowance = arguments.allowance
    return settings


def run(arguments: argparse.Namespace) -> int:
    settings: Settings = settings_for(arguments)
    rules: list[Rule] = rules_in_force(settings)
    known: frozenset[str] = known_identifiers(settings, rules)
    allow: PathAllowance
    for allow in settings.path_allowances:
        unknown: frozenset[str] = allow.rules - known
        if unknown:
            raise ConfigError(f'[[allow]] {", ".join(allow.paths)}: no rule in force named {", ".join(sorted(unknown))}')
    compiled: list[CompiledRule] = compile_rules(rules)
    targets: list[pathlib.Path]
    if arguments.paths:
        targets = [pathlib.Path(path) for path in arguments.paths]
    elif settings.paths:
        targets = [settings.root / path for path in settings.paths]
    else:
        raise ConfigError('no PATH given and no paths in a configuration')
    files: list[pathlib.Path] = gather(targets)
    findings: list[Finding] = []
    all_uses: list[AtomicUse] = []
    all_asserted: set[tuple[str, str]] = set()
    allowed_lines: dict[str, dict[int, frozenset[str]]] = {}
    checked: int = 0
    path: pathlib.Path
    for path in files:
        relative: str = relative_text(path, settings.root)
        if matches_any(relative, settings.exclude):
            continue
        checked += 1
        host_code: bool = matches_any(relative, settings.host_paths)
        standing: frozenset[str] = frozenset().union(
            *(allow.rules for allow in settings.path_allowances if matches_any(relative, allow.paths)))
        result: FileResult = check_file(path, relative, compiled, host_code, standing, known, settings)
        findings.extend(result.findings)
        all_uses.extend(result.uses)
        all_asserted |= result.asserted
        allowed_lines[relative] = result.by_line
    if checked == 0:
        raise ConfigError('no C or C++ sources under the paths given')
    hint: str = (f'assert it beside the others in {settings.atomic_assert_file}' if settings.atomic_assert_file
                 else 'assert std::<kind><T>::is_always_lock_free once in the checked tree')
    reported: set[tuple[str, str]] = set()
    use: AtomicUse
    for use in all_uses:
        key: tuple[str, str] = (use.kind, use.type_name)
        if key in all_asserted or key in reported:
            continue
        if LOCK_FREE_RULE in allowed_lines.get(use.path, {}).get(use.line, frozenset()):
            continue
        reported.add(key)
        findings.append(Finding(use.path, use.line, LOCK_FREE_RULE,
                                f'std::{use.kind}<{use.type_name}> has no static_assert of is_always_lock_free in '
                                'the checked tree (a type that is not lock-free falls back to a lock in a separate '
                                f'library); instead: {hint}'))
    findings.sort()

    allowance_path: pathlib.Path | None = None
    allowance_name: str = ''
    if settings.allowance:
        allowance_path = settings.root / settings.allowance
        allowance_name = pathlib.PurePath(settings.allowance).as_posix()
    if arguments.write_allowance:
        if allowance_path is None:
            raise ConfigError('--write-allowance needs an allowance (--allowance, or allowance in the configuration)')
        allowance_path.parent.mkdir(parents=True, exist_ok=True)
        allowance_path.write_text(format_allowance(findings), encoding='utf-8')
        print(f'check_std_rules: wrote {allowance_name} ({len(findings)} finding(s) in {checked} file(s))',
              file=sys.stderr)
        return 0
    allowance: dict[tuple[str, str], AllowanceEntry] = {}
    if allowance_path is not None and allowance_path.is_file():
        allowance = parse_allowance(allowance_path.read_text(encoding='utf-8'), allowance_name)
    judgement: Judgement = judge(findings, allowance, allowance_name)

    finding: Finding
    for finding in judgement.new:
        print(finding.text())
    line: str
    for line in judgement.stale:
        print(line)
    if arguments.json_path:
        report: dict[str, Any] = {
            'format': CONFIG_FORMAT,
            'profile': settings.profile,
            'files': checked,
            'findings': [dataclasses.asdict(item) for item in judgement.new],
            'admitted': judgement.admitted,
            'stale': judgement.stale,
        }
        pathlib.Path(arguments.json_path).write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    failed: bool = bool(judgement.new) or bool(judgement.stale)
    print(f'check_std_rules: {checked} file(s), profile {settings.profile}; {len(judgement.new)} finding(s), '
          f'{judgement.admitted} admitted by the allowance, {len(judgement.stale)} stale allowance line(s)'
          f'{" (report only)" if arguments.report and failed else ""}', file=sys.stderr)
    if failed and not arguments.report:
        return 1
    return 0


def list_rules(arguments: argparse.Namespace) -> int:
    """Every built-in rule with its profile, then the project's; a rule not in force
    for this configuration is marked so."""
    settings: Settings = settings_for(arguments)
    in_force: set[str] = {rule.identifier for rule in rules_in_force(settings)}
    scopes: dict[str, str] = {'consumer': 'every profile', 'strict': 'strict profile', 'project': 'this project'}
    rule: Rule
    for rule in [*RULES, *settings.custom_rules]:
        host: str = ', allowed in host code' if rule.host_only else ''
        off: str = '' if rule.identifier in in_force else ', not in force here'
        print(f'{rule.identifier} ({scopes[rule.profile]}{host}{off}): {rule.what}; instead: {rule.instead}')
    off = '' if settings.lock_free_check() else ', not in force here'
    print(f'{LOCK_FREE_RULE} (strict profile, or atomic_lock_free{off}): every std::atomic and std::atomic_ref '
          'type named has is_always_lock_free asserted somewhere in the checked tree')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        prog='check_std_rules', description='Rules for the C and C++ standard library, as a CI gate.')
    parser.add_argument('--config')
    parser.add_argument('--profile', choices=sorted(PROFILES))
    parser.add_argument('--root')
    parser.add_argument('--host-path', action='append', default=[])
    parser.add_argument('--exclude', action='append', default=[])
    parser.add_argument('--allowance')
    parser.add_argument('--write-allowance', action='store_true')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('--json', dest='json_path')
    parser.add_argument('--list-rules', action='store_true')
    parser.add_argument('paths', nargs='*')
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        if arguments.list_rules:
            return list_rules(arguments)
        if not arguments.paths and not arguments.config:
            parser.print_usage(sys.stderr)
            return 2
        return run(arguments)
    except ConfigError as problem:
        print(f'check_std_rules: {problem}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
