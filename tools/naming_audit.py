#!/usr/bin/env python3
"""naming_audit.py: the sensible-naming audit.

Gathers every identifier GUI.Forms declares in C++ sources (types,
enumerators, functions, members, parameters, namespaces, variables and
locals) and singles out the names that say something other than what they
hold:

- platform: a platform, product or technology word used as a whole name
  for a value that is not that platform. `run_linux_application(
  std::vector<LinuxApplicationWindow> linux, ...)` is the model case: the
  parameter holds application windows, and `linux` names the platform.
- foreign: a name built from a word that is a well-known term of another
  API or technology (a native handle, a platform toolkit, a codec or a
  product) where nothing about the declaration (its type, its enclosing
  scope or its file) concerns that technology.
- handle: a name that reads as a native handle (`hwnd`, `hdc`, `nsview`,
  ...) whose type is not that handle.

Not this audit's business, by the owner's direction (2026-11-16):
- collisions with platform or library macros (the consumer partition keeps
  those headers out of the consumer's reach);
- standard-library names (`min`, `max`, `size`, `begin`, ...);
- names the owner kept: the alignment enumerators `near` and `far`, and the
  Ribbon's `small`.

A heuristic over source with comments and literals masked; every finding
is reviewed before a rename. Usage:

    python3 -B tools/naming_audit.py [--root DIR] [--format text|markdown] [PATH ...]

PATH defaults to include and src. The exit status is 0; this is a report,
not a gate.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

SOURCE_SUFFIXES: frozenset[str] = frozenset({'.h', '.hpp', '.hh', '.c', '.cc', '.cpp', '.cxx', '.mm', '.m', '.inl', '.ipp'})

# A platform, product or technology word, with the words that count as the
# same subject when the declaration's type, scope or path mentions them.
# `windows` and `window` are absent: in a GUI library they name windows.
PLATFORM_WORDS: dict[str, tuple[str, ...]] = {
    'linux': ('linux', 'x11', 'xlib', 'wayland', 'atk', 'gtk', 'glib', 'gbm', 'drm'),
    'unix': ('unix', 'posix', 'linux', 'macos', 'darwin'),
    'posix': ('posix', 'unix', 'linux', 'macos', 'darwin'),
    'win32': ('win32', 'windows', 'hwnd', 'gdi', 'wic', 'uia', 'msaa', 'dwm'),
    'win64': ('win64', 'windows'),
    'macos': ('macos', 'appkit', 'cocoa', 'darwin', 'apple', 'ns', 'cg', 'ct'),
    'osx': ('osx', 'macos', 'appkit', 'cocoa'),
    'darwin': ('darwin', 'macos', 'apple'),
    'apple': ('apple', 'macos', 'appkit', 'ios'),
    'cocoa': ('cocoa', 'appkit', 'macos'),
    'appkit': ('appkit', 'macos', 'cocoa', 'ns'),
    'uikit': ('uikit', 'ios'),
    'ios': ('ios', 'uikit', 'apple'),
    'android': ('android', 'jni', 'ndk'),
    'wayland': ('wayland', 'linux', 'wl'),
    'x11': ('x11', 'xlib', 'xcb', 'linux'),
    'xlib': ('xlib', 'x11', 'linux'),
    'xcb': ('xcb', 'x11', 'linux'),
    'gtk': ('gtk', 'linux', 'glib'),
    'qt': ('qt',),
    'gdi': ('gdi', 'windows', 'win32', 'hdc'),
    'gdiplus': ('gdiplus', 'gdi', 'windows'),
    'direct2d': ('direct2d', 'd2d', 'windows'),
    'd2d': ('d2d', 'direct2d', 'windows'),
    'd3d': ('d3d', 'direct3d', 'windows'),
    'direct3d': ('direct3d', 'd3d', 'windows'),
    'directx': ('directx', 'd3d', 'windows'),
    'dxgi': ('dxgi', 'windows'),
    'opengl': ('opengl', 'gl', 'webgl'),
    'webgl': ('webgl', 'emscripten', 'web'),
    'vulkan': ('vulkan',),
    'skia': ('skia', 'sk', 'render'),
    'cairo': ('cairo',),
    'harfbuzz': ('harfbuzz', 'hb', 'shaping', 'shaper', 'text_shaping'),
    'freetype': ('freetype', 'ft', 'font', 'glyph'),
    'emscripten': ('emscripten', 'web', 'wasm'),
    'wasm': ('wasm', 'emscripten', 'web'),
    'webkit': ('webkit',),
    'safari': ('safari',),
    'firefox': ('firefox',),
    'excel': ('excel',),
    'dotnet': ('dotnet', 'net', 'csharp'),
    'java': ('java', 'jni'),
    'uia': ('uia', 'windows', 'automation'),
    'msaa': ('msaa', 'windows', 'accessible'),
    'atk': ('atk', 'linux', 'accessib'),
}

# `chrome` (window chrome) and `metal` (a brushed-metal material) are absent:
# both are ordinary words of this library before they are products.

# A platform word followed by one of these describes that platform's
# convention, not the platform: `macos_style_sheet`, `apple_truetype_version`.
CONVENTION_WORDS: frozenset[str] = frozenset({'style', 'like', 'look', 'truetype', 'compatible', 'convention', 'metrics', 'flavour', 'flavor'})

# A raw carrier a C ABI or a portable header uses for a native handle.
RAW_HANDLE: str = r'\buintptr_t\b|\bintptr_t\b|void\s*\*'

# A native handle term and the type text that makes it true.
HANDLE_TERMS: dict[str, str] = {
    'hwnd': r'\bHWND\b',
    'hdc': r'\bHDC\b',
    'hinstance': r'\bHINSTANCE\b',
    'hmodule': r'\bHMODULE\b',
    'hbitmap': r'\bHBITMAP\b',
    'hfont': r'\bHFONT\b',
    'hmonitor': r'\bHMONITOR\b',
    'nsview': r'\bNSView\b|\bid\b|void\s*\*',
    'nswindow': r'\bNSWindow\b|\bid\b|void\s*\*',
    'cgcontext': r'\bCGContext',
    'xid': r'\bXID\b|\bWindow\b|unsigned\s+long|std::uint(?:32|64)_t',
    'xdisplay': r'\bDisplay\b',
    'jni': r'\bJNIEnv\b|\bjobject\b',
}

# Standard-library vocabulary, never this audit's business.
STD_VOCABULARY: frozenset[str] = frozenset(
    'min max size begin end empty clear erase insert find data front back swap reset get release move '
    'count length push_back emplace_back pop_back value has_value index'.split())

# Names the owner reviewed and kept (2026-11-16).
KEPT_NAMES: frozenset[str] = frozenset({'near', 'far', 'small'})

KEYWORDS: frozenset[str] = frozenset(
    'alignas alignof and asm auto bool break case catch char char8_t char16_t char32_t class co_await co_return '
    'co_yield concept const consteval constexpr constinit const_cast continue decltype default delete do double '
    'dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace '
    'new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short '
    'signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef '
    'typeid typename union unsigned using virtual void volatile wchar_t while final override import module'.split())

# Words that end a type and may come straight before a declared name.
TYPE_WORDS: frozenset[str] = frozenset('bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t auto'.split())

TOKEN: re.Pattern[str] = re.compile(r'[A-Za-z_]\w*|::|->|&&|\|\||<<=|>>=|[0-9][\w.\']*|\S')
CONTAINER: re.Pattern[str] = re.compile(r'\b(?:vector|span|array|list|deque|SmallVector|InlineVector)\s*<\s*(?:const\s+)?(?:std::)?(?:unique_ptr\s*<\s*|shared_ptr\s*<\s*)?(?:[\w:]+::)?(\w+)')
# The shape of a declared type: words, scopes, template arguments, pointers
# and references, with at least one word. Expressions (`a & b`, `x.y`,
# `[a b]`) do not match.
VALID_TYPE: re.Pattern[str] = re.compile(r'(?=.*[A-Za-z_])[\w:<>,*&\s]+')
TYPE_NAME: re.Pattern[str] = re.compile(r'(?:[\w]+::)*([A-Z]\w*)')


@dataclass
class Declaration:
    name: str
    kind: str          # type, namespace, enumerator, function, parameter, member, variable, local
    path: str
    line: int
    type_text: str     # the declared type, or the enclosing enum or record
    scope: str         # enclosing record, enum, namespace or function names, '::'-joined


@dataclass
class Finding:
    declaration: Declaration
    rule: str
    rank: int
    reading: str
    proposal: str


@dataclass
class Scope:
    kind: str          # namespace, record, enum, function, block, init
    name: str


def mask_source(text: str) -> str:
    """Replace comments, string and character literals with spaces, keeping lines."""
    pieces: list[str] = []
    index: int = 0
    length: int = len(text)
    while index < length:
        character: str = text[index]
        if text.startswith('//', index):
            end: int = text.find('\n', index)
            end = length if end < 0 else end
            pieces.append(' ' * (end - index))
            index = end
        elif text.startswith('/*', index):
            end = text.find('*/', index + 2)
            end = length if end < 0 else end + 2
            pieces.append(re.sub(r'[^\n]', ' ', text[index:end]))
            index = end
        elif character == 'R' and text.startswith('R"', index) and (index == 0 or not (text[index - 1].isalnum() or text[index - 1] == '_')):
            match: re.Match[str] | None = re.match(r'R"([^(\s]*)\(', text[index:])
            if match is None:
                pieces.append(character)
                index += 1
                continue
            delimiter: str = ')' + match.group(1) + '"'
            end = text.find(delimiter, index)
            end = length if end < 0 else end + len(delimiter)
            pieces.append(re.sub(r'[^\n]', ' ', text[index:end]))
            index = end
        elif character == '"' or (character == "'" and not (index > 0 and text[index - 1].isalnum())):
            end = index + 1
            while end < length and text[end] != character and text[end] != '\n':
                end += 2 if text[end] == '\\' else 1
            end = min(end + 1, length)
            pieces.append(re.sub(r'[^\n]', ' ', text[index:end]))
            index = end
        else:
            pieces.append(character)
            index += 1
    masked: str = ''.join(pieces)
    return masked


def mask_preprocessor(text: str) -> str:
    """Blank preprocessor lines (with continuations) of masked text."""
    lines: list[str] = text.split('\n')
    continuing: bool = False
    for number, line in enumerate(lines):
        if continuing or line.lstrip().startswith('#'):
            continuing = line.rstrip().endswith('\\')
            lines[number] = ' ' * len(line)
    joined: str = '\n'.join(lines)
    return joined


@dataclass
class Token:
    text: str
    line: int


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    for number, line in enumerate(text.split('\n'), 1):
        for match in TOKEN.finditer(line):
            tokens.append(Token(match.group(0), number))
    return tokens


def is_identifier(text: str) -> bool:
    identifier: bool = bool(re.fullmatch(r'[A-Za-z_]\w*', text)) and text not in KEYWORDS
    return identifier


def ends_type(token: str) -> bool:
    """True when a token can be the last token of a type before a declared name."""
    ending: bool = token in TYPE_WORDS or token in ('>', '*', '&', '&&') or is_identifier(token)
    return ending


def statement_start(tokens: list[Token], index: int) -> int:
    """The index after the last ; { } at this nesting before index."""
    depth: int = 0
    position: int = index - 1
    while position >= 0:
        text: str = tokens[position].text
        if text in (')', ']'):
            depth += 1
        elif text in ('(', '['):
            if depth == 0:
                return position + 1
            depth -= 1
        elif depth == 0 and text in (';', '{', '}'):
            return position + 1
        position -= 1
    return 0


def parameter_start(tokens: list[Token], index: int) -> int:
    """The index after the `(` or `,` that opens the parameter ending at index."""
    depth: int = 0
    angles: int = 0
    position: int = index - 1
    while position >= 0:
        text: str = tokens[position].text
        if text in (')', ']'):
            depth += 1
        elif text == '>' and depth == 0:
            angles += 1
        elif text == '<' and depth == 0 and angles > 0:
            angles -= 1
        elif text in ('(', '['):
            if depth == 0:
                return position + 1
            depth -= 1
        elif text == ',' and depth == 0 and angles == 0:
            return position + 1
        elif depth == 0 and text in (';', '{', '}'):
            return position + 1
        position -= 1
    return 0


def classify_brace(tokens: list[Token], index: int) -> Scope:
    """What the `{` at index opens, from the tokens of its statement."""
    start: int = statement_start(tokens, index)
    words: list[str] = [token.text for token in tokens[start:index]]
    previous: str = words[-1] if words else ''
    if 'namespace' in words and '(' not in words:
        name: str = '::'.join(word for word in words[words.index('namespace') + 1:] if is_identifier(word))
        return Scope('namespace', name)
    if 'enum' in words and '(' not in words:
        names: list[str] = [word for word in words[words.index('enum') + 1:] if is_identifier(word) and word not in ('class', 'struct')]
        return Scope('enum', names[0] if names else '')
    for keyword in ('class', 'struct', 'union'):
        if keyword in words and '(' not in words[:words.index(keyword)] and '=' not in words and previous != '=':
            following: list[str] = words[words.index(keyword) + 1:]
            if 'template' in words and words.index('template') < words.index(keyword) and '>' in following:
                following = following[following.index('>') + 1:] if words.index(keyword) < words.index('>') else following
            names = [word for word in following if is_identifier(word) and not word.isupper() and word != 'alignas']
            return Scope('record', names[0] if names else '')
    if previous in (')', 'const', 'noexcept', 'override', 'final', 'mutable', 'try', 'else', 'do') or (previous and previous[-1:] == ')'):
        head: list[str] = [word for word in words if word not in ('const', 'noexcept', 'override', 'final')]
        if head and head[0] in ('if', 'for', 'while', 'switch', 'catch', 'else', 'do', 'try'):
            return Scope('block', '')
        if '(' in words:
            opener: int = words.index('(')
            name = words[opener - 1] if opener > 0 and is_identifier(words[opener - 1]) else ''
            if words[:1] == ['[']:
                return Scope('block', '')
            return Scope('function', name)
        return Scope('block', '')
    if not words or previous in (';', ':') and 'case' in words:
        return Scope('block', '')
    if previous in ('=', ',', '(', 'return', '{') or is_identifier(previous) or previous == '>':
        return Scope('init', '')
    return Scope('block', '')


def find_type_text(tokens: list[Token], start: int, name_index: int) -> str:
    words: list[str] = [token.text for token in tokens[start:name_index]
                        if token.text not in ('static', 'inline', 'constexpr', 'mutable', 'virtual', 'explicit', 'extern', 'thread_local', 'GUI_FORMS_API', '[[', ']]', 'nodiscard', 'maybe_unused')]
    text: str = ' '.join(words)
    text = re.sub(r'\s*(::|<|>|,)\s*', r'\1', text)
    text = re.sub(r'\s*([*&])', r'\1', text)
    return text.strip()


def gather_declarations(path: str, text: str) -> list[Declaration]:
    """Every declaration the heuristic reads in one source text."""
    masked: str = mask_preprocessor(mask_source(text))
    tokens: list[Token] = tokenize(masked)
    found: list[Declaration] = []
    scopes: list[Scope] = []
    parens: list[int] = []     # indices of open parentheses
    enum_expect: bool = False  # the next identifier in an enum body is an enumerator
    for index, token in enumerate(tokens):
        word: str = token.text
        scope: Scope | None = scopes[-1] if scopes else None
        scope_name: str = '::'.join(item.name for item in scopes if item.name)
        if word == '{':
            opened: Scope = classify_brace(tokens, index)
            scopes.append(opened)
            enum_expect = opened.kind == 'enum'
            if opened.kind in ('namespace', 'record', 'enum') and opened.name:
                outer: str = '::'.join(item.name for item in scopes[:-1] if item.name)
                kind: str = 'namespace' if opened.kind == 'namespace' else 'type'
                line: int = token.line
                for back in range(index - 1, max(index - 40, -1), -1):
                    if tokens[back].text == opened.name:
                        line = tokens[back].line
                        break
                found.append(Declaration(opened.name, kind, path, line, opened.kind, outer))
            continue
        if word == '}':
            if scopes:
                scopes.pop()
            enum_expect = False
            continue
        if word == '(':
            parens.append(index)
            continue
        if word == ')':
            if parens:
                parens.pop()
            continue
        if scope is not None and scope.kind == 'enum' and not parens:
            if enum_expect and is_identifier(word):
                found.append(Declaration(word, 'enumerator', path, token.line, scope.name, scope_name))
                enum_expect = False
            elif word == ',':
                enum_expect = True
            continue
        if not is_identifier(word) or word in TYPE_WORDS:
            continue
        previous: str = tokens[index - 1].text if index > 0 else ''
        following: str = tokens[index + 1].text if index + 1 < len(tokens) else ''
        if previous in ('using', 'typedef') or (previous == 'class' or previous == 'struct' or previous == 'union'):
            if previous == 'using' and following == '=':
                found.append(Declaration(word, 'type', path, token.line, 'alias', scope_name))
            continue
        if previous == 'typename' or previous == '.' or previous == '->' or previous == '::' or previous == '~':
            continue
        if following == '=' and previous == 'using':
            continue
        after_qualifier: bool = previous == 'const' and index > 1 and tokens[index - 2].text in ('*', '&', '&&')
        if not after_qualifier and (not ends_type(previous) or previous in KEYWORDS and previous not in TYPE_WORDS):
            continue
        if following not in ('(', ';', ',', '=', '{', '[', ')', ':') or following == ':' and (scope is None or scope.kind != 'record'):
            continue
        start: int = parameter_start(tokens, index) if parens else statement_start(tokens, index)
        if start >= index:
            continue
        head: str = tokens[start].text
        if head in ('return', 'case', 'goto', 'throw', 'delete', 'new', 'co_return', 'using', 'typedef', 'friend', 'public', 'private', 'protected', 'template'):
            continue
        type_text: str = find_type_text(tokens, start, index)
        if not VALID_TYPE.fullmatch(type_text) or type_text.count('<') != type_text.count('>'):
            continue
        if following == '(' and previous in ('&', '&&', '*') and (parens or (scope is not None and scope.kind in ('function', 'block'))):
            # `ready && is_alive(...)`: an operator before a call, not a type.
            continue
        first_type_word: str = type_text.split()[0].split('::')[0].split('<')[0]
        if first_type_word in ('if', 'while', 'for', 'switch', 'sizeof', 'return', 'else', 'operator'):
            continue
        in_parameters: bool = bool(parens) and start > 0 and tokens[start - 1].text in ('(', ',')
        if parens:
            opener_index: int = parens[-1]
            before_opener: str = tokens[opener_index - 1].text if opener_index > 0 else ''
            if before_opener in ('if', 'while', 'switch', 'for', 'catch'):
                kind = 'local'
            elif in_parameters and (scope is None or scope.kind not in ('function', 'block', 'init')):
                # Parentheses inside a function body are calls, never
                # parameter lists (the house style admits no lambdas).
                kind = 'parameter'
            else:
                continue
        elif following == '(':
            if scope is not None and scope.kind in ('function', 'block'):
                kind = 'local'
            else:
                kind = 'function'
        elif scope is None or scope.kind == 'namespace':
            kind = 'variable'
        elif scope.kind == 'record':
            kind = 'member'
        elif scope.kind in ('function', 'block'):
            kind = 'local'
        else:
            continue
        found.append(Declaration(word, kind, path, token.line, type_text, scope_name))
    return found


def name_words(name: str) -> list[str]:
    spaced: str = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name)
    words: list[str] = [word.lower() for word in spaced.split('_') if word]
    return words


def subject_text(declaration: Declaration) -> str:
    text: str = ' '.join((declaration.type_text, declaration.scope, declaration.path)).lower()
    return text


def snake_case(name: str) -> str:
    snake: str = '_'.join(name_words(name))
    return snake


def plural(word: str) -> str:
    if word.endswith(('s', 'x', 'ch', 'sh')):
        return word + 'es'
    if word.endswith('y') and len(word) > 1 and word[-2] not in 'aeiou':
        return word[:-1] + 'ies'
    return word + 's'


def propose_from_type(declaration: Declaration, platform: str) -> str:
    """A name for the value, from its type with the platform word removed."""
    container: re.Match[str] | None = CONTAINER.search(declaration.type_text)
    element: str = ''
    if container is not None:
        element = container.group(1)
    else:
        named: re.Match[str] | None = TYPE_NAME.search(declaration.type_text)
        element = named.group(1) if named is not None else ''
    lowered: str = snake_case(element)
    compact: str = lowered.replace('_', '')
    if compact.startswith(platform) and compact != platform:
        # AppKitHostServices: drop the platform's letters, however split.
        remaining: int = len(platform)
        position: int = 0
        while remaining > 0 and position < len(lowered):
            if lowered[position] != '_':
                remaining -= 1
            position += 1
        element = lowered[position:].strip('_')
    words: list[str] = [word for word in name_words(element) if word not in PLATFORM_WORDS[platform] and word != platform]
    if not words:
        return '(review)'
    base: str = '_'.join(words)
    proposal: str = plural(base) if container is not None else base
    return proposal


def platform_findings(declaration: Declaration) -> list[Finding]:
    findings: list[Finding] = []
    words: list[str] = name_words(declaration.name)
    if declaration.kind in ('type', 'namespace', 'enumerator'):
        return findings
    subject: str = subject_text(declaration)
    for platform, related in PLATFORM_WORDS.items():
        if platform not in words:
            continue
        if words == [platform]:
            if declaration.kind == 'function' and any(word in subject for word in related):
                continue
            # A collection named after its platform hides what it holds; a
            # value whose type is that platform's (an AppKit services object
            # named `appkit`) says less than it could, and ranks lower.
            rank: int = 100 if declaration.kind in ('parameter', 'member', 'variable') else 80
            if CONTAINER.search(declaration.type_text) is None and any(word in declaration.type_text.lower() for word in related):
                rank = 60
            if re.fullmatch(r'(?:const\s+)?bool', declaration.type_text):
                rank = 40
            reading: str = 'names the platform `%s`, not the value it holds (%s)' % (platform, declaration.type_text or declaration.kind)
            findings.append(Finding(declaration, 'platform', rank, reading, propose_from_type(declaration, platform)))
            continue
        if any(word in subject for word in related):
            continue
        position: int = words.index(platform)
        if position + 1 < len(words) and words[position + 1] in CONVENTION_WORDS:
            continue
        reading = 'says `%s`, but nothing in its type, scope or file concerns %s' % (platform, platform)
        findings.append(Finding(declaration, 'foreign', 50, reading, '(review)'))
    return findings


def handle_findings(declaration: Declaration) -> list[Finding]:
    findings: list[Finding] = []
    if declaration.kind in ('type', 'namespace', 'enumerator', 'function'):
        return findings
    if not declaration.name[:1].islower():
        return findings
    for word in name_words(declaration.name):
        pattern: str | None = HANDLE_TERMS.get(word)
        if pattern is None or re.search(pattern, declaration.type_text) or re.search(RAW_HANDLE, declaration.type_text):
            continue
        reading: str = 'reads as the native handle `%s`, but holds %s' % (word, declaration.type_text)
        findings.append(Finding(declaration, 'handle', 70, reading, '(review)'))
    return findings


# Words that promise a truth value, first in a name.
PREDICATE_WORDS: frozenset[str] = frozenset({'is', 'has', 'can', 'should', 'was', 'were', 'did', 'does', 'needs', 'must'})
# Words that promise a whole number, last in a name.
COUNT_WORDS: frozenset[str] = frozenset({'count', 'index'})


def promise_findings(declaration: Declaration) -> list[Finding]:
    """Names whose form promises a kind of value the type does not have."""
    findings: list[Finding] = []
    if declaration.kind in ('type', 'namespace', 'enumerator'):
        return findings
    words: list[str] = name_words(declaration.name)
    if len(words) < 2:
        return findings
    type_text: str = declaration.type_text
    c_truth: bool = declaration.path.endswith(('.h', '.c')) and re.fullmatch(r'(?:const\s+)?u?int(?:8|16|32)?(?:_t)?', type_text) is not None
    callable_alias: bool = type_text.split('::')[-1] == ''.join(word.capitalize() for word in words)
    if words[0] in PREDICATE_WORDS and not re.search(r'\bbool\b', type_text) and not c_truth and not callable_alias:
        if declaration.kind == 'function' and re.search(r'\b(?:void|auto|Predicate|Condition|Truth|Answer|Check|Test|Decision)\b|Result|Status|Outcome|optional', type_text):
            return findings
        reading: str = 'reads as a truth value (`%s ...`), but holds %s' % (words[0], type_text)
        findings.append(Finding(declaration, 'promise', 45, reading, '(review)'))
    if words[-1] in COUNT_WORDS and re.fullmatch(r'(?:const\s+)?(?:double|float|long double)', type_text):
        reading = 'reads as a whole number (`... %s`), but holds %s' % (words[-1], type_text)
        findings.append(Finding(declaration, 'promise', 20, reading, '(review)'))
    return findings


def audit_declarations(declarations: list[Declaration]) -> list[Finding]:
    findings: list[Finding] = []
    for declaration in declarations:
        if declaration.name in STD_VOCABULARY or declaration.name in KEPT_NAMES:
            continue
        findings.extend(platform_findings(declaration))
        findings.extend(handle_findings(declaration))
        findings.extend(promise_findings(declaration))
    findings.sort(key=finding_order)
    return findings


def finding_order(finding: Finding) -> tuple[int, str, int, str]:
    order: tuple[int, str, int, str] = (-finding.rank, finding.declaration.path, finding.declaration.line, finding.declaration.name)
    return order


def source_files(root: Path, paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for item in paths:
        base: Path = root / item
        if base.is_file():
            files.append(base)
            continue
        for candidate in sorted(base.rglob('*')):
            if candidate.is_file() and candidate.suffix in SOURCE_SUFFIXES:
                files.append(candidate)
    return files


def audit_tree(root: Path, paths: list[str]) -> tuple[int, list[Finding]]:
    declarations: list[Declaration] = []
    for file in source_files(root, paths):
        text: str = file.read_text(encoding='utf-8', errors='replace')
        declarations.extend(gather_declarations(file.relative_to(root).as_posix(), text))
    findings: list[Finding] = audit_declarations(declarations)
    return len(declarations), findings


def render(findings: list[Finding], declaration_count: int, markdown: bool) -> str:
    lines: list[str] = []
    if markdown:
        lines.append('%d declarations read; %d findings.' % (declaration_count, len(findings)))
        lines.append('')
        lines.append('| Rank | Name | Kind | Where | Holds | Misreading | Proposed |')
        lines.append('|---|---|---|---|---|---|---|')
        for finding in findings:
            declaration: Declaration = finding.declaration
            lines.append('| %d | `%s` | %s | `%s:%d` | `%s` | %s | `%s` |' % (
                finding.rank, declaration.name, declaration.kind, declaration.path, declaration.line,
                declaration.type_text.replace('|', '\\|'), finding.reading.replace('|', '\\|'), finding.proposal))
    else:
        for finding in findings:
            declaration = finding.declaration
            lines.append('%3d %-9s %s:%d %s %s [%s] -> %s' % (
                finding.rank, finding.rule, declaration.path, declaration.line, declaration.kind,
                declaration.name, finding.reading, finding.proposal))
        lines.append('%d declarations read; %d findings.' % (declaration_count, len(findings)))
    report: str = '\n'.join(lines)
    return report


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', default='.', help='repository root (default: the current directory)')
    parser.add_argument('--format', choices=('text', 'markdown'), default='text')
    parser.add_argument('paths', nargs='*', default=['include', 'src'])
    options: argparse.Namespace = parser.parse_args(arguments)
    declaration_count, findings = audit_tree(Path(options.root), options.paths)
    print(render(findings, declaration_count, options.format == 'markdown'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
