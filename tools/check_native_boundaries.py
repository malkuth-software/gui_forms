#!/usr/bin/env python3
"""Checks that every native callback entry goes through the fail-fast boundary.

Owner, 2026-11-11: where foreign code calls GUI.Forms, the call goes through
gui_forms::detail::call_failing_fast (src/core/failure/fail_fast.hpp), which
reports an exception and ends the process. This check finds the entries in
the native hosts and flags each one that does not.

An entry is
  - Windows: a function defined with CALLBACK (window procedures, enumeration
    and thread-pool callbacks) or STDMETHODCALLTYPE (COM methods);
  - Linux: a function stored in a GObject or ATK table inside an `*_init`
    function, named in a GInterfaceInfo, passed to a GLib (`g_...`) call, or
    passed to XSetErrorHandler;
  - macOS: an Objective-C method whose selector AppKit calls (drawing, events,
    text input, dragging, window and application delegates, accessibility,
    timer and display-link targets).

An entry is routed when its body calls call_failing_fast, or when it is
declared noexcept (C++ only: no exception can leave it, by construction), or
(Objective-C only) when its body is a single `return` of a constant.

Entries not yet routed are listed in tools/native_boundary_baseline.txt as
`path::name`; the list may only shrink: an entry listed there that is routed,
or no longer exists, is a finding too.

Usage: check_native_boundaries.py [--root DIR] [--write-baseline] [--self-test]
Prints one line per finding and a summary; exits 1 on any finding.
"""
import argparse
import os
import re
import sys

HOST_DIRECTORIES = ('src/host/windows', 'src/host/linux', 'src/host/macos')
SOURCE_SUFFIXES = ('.cpp', '.hpp', '.inc', '.mm', '.h')
BASELINE = 'tools/native_boundary_baseline.txt'
BOUNDARY_CALL = 'call_failing_fast('

APPKIT_SELECTOR = re.compile(
    r'^(drawRect|mouse\w*|rightMouse\w*|otherMouse\w*|scrollWheel|key\w*|flagsChanged|cursorUpdate|menuForEvent'
    r'|insertText|setMarkedText|unmarkText|doCommandBySelector|attributedSubstringForProposedRange'
    r'|firstRectForCharacterRange|characterIndexForPoint|selectedRange|markedRange|hasMarkedText'
    r'|validAttributesForMarkedText|dragging\w*|prepareForDragOperation|performDragOperation|window\w*'
    r'|application\w*|accessibility\w*|isAccessibility\w*|setAccessibility\w*|viewDid\w*|viewWill\w*'
    r'|setFrameSize|updateTrackingAreas|becomeFirstResponder|resignFirstResponder|acceptsFirstResponder'
    r'|canBecomeKeyWindow|canBecomeMainWindow|isFlipped|isOpaque|displayLinkTick|scheduledWake|drainPostedWork'
    r'|tick|perform|screenParametersChanged|systemColoursChanged|filterChanged|buttonActivated|rowChosen'
    r'|alertShowHelp|invalidationOverlayTick|dealloc)$')
CONSTANT_RETURN = re.compile(r'^\{\s*return\s+(YES|NO|nil|true|false|-?\d+|[A-Z]\w*)\s*;\s*\}$')


def matching(text, start, open_char, close_char):
    """The index of the bracket closing the one at `start`, skipping string
    and character literals and comments."""
    depth = 0
    index = start
    length = len(text)
    while index < length:
        char = text[index]
        if char in '"\'':
            quote = char
            index += 1
            while index < length and text[index] != quote:
                if text[index] == '\\':
                    index += 1
                index += 1
        elif text.startswith('//', index):
            newline = text.find('\n', index)
            index = length if newline == -1 else newline
            continue
        elif text.startswith('/*', index):
            close = text.find('*/', index + 2)
            index = length if close == -1 else close + 2
            continue
        elif char == open_char:
            depth += 1
        elif char == close_char:
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def line_of(text, index):
    return text.count('\n', 0, index) + 1


class Entry:
    def __init__(self, path, name, line, routed):
        self.path = path
        self.name = name
        self.line = line
        self.routed = routed

    def key(self):
        return f'{self.path}::{self.name}'


def cpp_definition(text, name):
    """(line, routed) of the definition of `name` (optionally Class::name),
    or None when the text defines no such function."""
    pattern = re.compile(r'^[ \t]*(?!//|\*)[^\n;{}()#/]*?\b(?:\w+::)?' + re.escape(name) + r'\s*\(', re.M)
    for match in pattern.finditer(text):
        close = matching(text, match.end() - 1, '(', ')')
        if close == -1:
            continue
        index = close + 1
        while index < len(text) and text[index] not in '{;=':
            index += 1
        if index >= len(text) or text[index] != '{':
            continue
        qualifiers = text[close + 1:index]
        end = matching(text, index, '{', '}')
        body = text[index:end + 1]
        routed = 'noexcept' in qualifiers or BOUNDARY_CALL in body
        return line_of(text, match.start()), routed
    return None


def windows_entries(path, text):
    pattern = re.compile(r'^[ \t]*(?:static\s+)?[\w\*]+\s+(?:CALLBACK|STDMETHODCALLTYPE)\s+((?:\w+::)?\w+)\s*\(', re.M)
    entries = []
    for match in pattern.finditer(text):
        close = matching(text, match.end() - 1, '(', ')')
        index = close + 1
        while index < len(text) and text[index] not in '{;':
            index += 1
        if index >= len(text) or text[index] != '{':
            continue
        qualifiers = text[close + 1:index]
        end = matching(text, index, '{', '}')
        body = text[index:end + 1]
        routed = 'noexcept' in qualifiers or BOUNDARY_CALL in body
        entries.append(Entry(path, match.group(1), line_of(text, match.start()), routed))
    return entries


def linux_entry_names(text):
    names = []
    init = re.compile(r'^[\w\*\s]+?\b(\w+_init)\s*\(', re.M)
    for match in init.finditer(text):
        close = matching(text, match.end() - 1, '(', ')')
        brace = text.find('{', close)
        semicolon = text.find(';', close)
        if brace == -1 or (semicolon != -1 and semicolon < brace and '{' not in text[close:semicolon]):
            continue
        end = matching(text, brace, '{', '}')
        for assigned in re.finditer(r'\.\s*\w+\s*=\s*(\w+)\s*;', text[brace:end]):
            names.append(assigned.group(1))
    for info in re.finditer(r'GInterfaceInfo\s+\w+\s*\{\s*(\w+)', text):
        names.append(info.group(1))
    for handler in re.finditer(r'XSetErrorHandler\s*\(\s*(\w+)\s*\)', text):
        names.append(handler.group(1))
    for call in re.finditer(r'\bg_\w+\s*\(', text):
        close = matching(text, call.end() - 1, '(', ')')
        if close == -1:
            continue
        for argument in re.split(r',', text[call.end():close]):
            word = argument.strip()
            if re.fullmatch(r'[a-z_]\w*', word):
                names.append(word)
    return names


def linux_entries(path, text):
    entries = []
    seen = set()
    for name in linux_entry_names(text):
        if name in seen:
            continue
        seen.add(name)
        found = cpp_definition(text, name)
        if found is None:
            continue
        line, routed = found
        entries.append(Entry(path, name, line, routed))
    return entries


def objc_entries(path, text):
    entries = []
    for implementation in re.finditer(r'^@implementation\s+(\w+)', text, re.M):
        end = text.find('\n@end', implementation.end())
        block_end = len(text) if end == -1 else end
        method = re.compile(r'^- \([^)]*\)\s*([^{;]*?)\s*\{', re.M)
        position = implementation.end()
        while True:
            match = method.search(text, position, block_end)
            if match is None:
                break
            signature = match.group(1)
            keywords = re.findall(r'(\w+)\s*:', signature)
            selector = ':'.join(keywords) + ':' if keywords else signature.strip()
            first = keywords[0] if keywords else selector
            brace = match.end() - 1
            close = matching(text, brace, '{', '}')
            body = text[brace:close + 1]
            position = close + 1
            if not APPKIT_SELECTOR.match(first):
                continue
            compact = ' '.join(body.split())
            routed = BOUNDARY_CALL in body or CONSTANT_RETURN.match(compact) is not None
            name = f'{implementation.group(1)}.{selector}'
            entries.append(Entry(path, name, line_of(text, match.start()), routed))
    return entries


def entries_of(path, text):
    normalized = path.replace('\\', '/')
    if '/windows/' in normalized:
        return windows_entries(path, text)
    if '/linux/' in normalized:
        return linux_entries(path, text)
    if '/macos/' in normalized and normalized.endswith('.mm'):
        return objc_entries(path, text)
    return []


def scan(root):
    entries = []
    for directory in HOST_DIRECTORIES:
        base = os.path.join(root, directory)
        for current, _, files in os.walk(base):
            for name in sorted(files):
                if not name.endswith(SOURCE_SUFFIXES):
                    continue
                full = os.path.join(current, name)
                relative = os.path.relpath(full, root).replace('\\', '/')
                with open(full, encoding='utf-8') as handle:
                    entries.extend(entries_of(relative, handle.read()))
    return entries


def findings(entries, baseline):
    result = []
    keys = {}
    for entry in entries:
        keys[entry.key()] = entry
        if not entry.routed and entry.key() not in baseline:
            result.append(f'{entry.path}:{entry.line}: native entry {entry.name} is not routed through '
                          f'call_failing_fast (nor noexcept)')
    for key in sorted(baseline):
        entry = keys.get(key)
        if entry is None:
            result.append(f'{BASELINE}: {key} is listed but no longer an entry; remove it')
        elif entry.routed:
            result.append(f'{BASELINE}: {key} is routed now; remove it from the baseline')
    return result


def read_baseline(root):
    path = os.path.join(root, BASELINE)
    if not os.path.exists(path):
        return set()
    with open(path, encoding='utf-8') as handle:
        return {line.strip() for line in handle if line.strip() and not line.startswith('#')}


SELF_TEST = {
    'src/host/windows/sample.cpp': '''
LRESULT CALLBACK routed_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return gui_forms::detail::call_failing_fast("windows.sample", &deliver, window, message, wparam, lparam);
}
LRESULT CALLBACK bare_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return deliver(window, message, wparam, lparam);
}
BOOL CALLBACK quiet_enumeration(HMONITOR, HDC, LPRECT, LPARAM) noexcept { return TRUE; }
HRESULT STDMETHODCALLTYPE Sample::get_Name(BSTR* out) {
    return answer(out);
}
''',
    'src/host/linux/sample.cpp': '''
gint routed_count(AtkText* text) {
    return gui_forms::detail::call_failing_fast("linux.atk.count", &answer_count, text);
}
gint bare_offset(AtkText* text) { return 0; }
void text_init(gpointer raw, gpointer) noexcept {
    AtkTextIface& face = *static_cast<AtkTextIface*>(raw);
    face.get_character_count = routed_count; face.get_caret_offset = bare_offset;
}
void bus_changed(GDBusConnection*, gpointer) { read(); }
void subscribe() { g_dbus_connection_signal_subscribe(connection, bus_changed, nullptr); }
''',
    'src/host/macos/sample.mm': '''
@implementation SampleView
- (void)drawRect:(NSRect)dirty {
    gui_forms::detail::call_failing_fast("macos.draw", &run_draw, self, dirty);
}
- (void)mouseDown:(NSEvent*)event {
    [self dispatch:event];
}
- (BOOL)isFlipped {
    return YES;
}
- (void)helperOnly {
    [self dispatch:nil];
}
@end
''',
}
SELF_TEST_EXPECTED = {
    'src/host/windows/sample.cpp::bare_procedure',
    'src/host/windows/sample.cpp::Sample::get_Name',
    'src/host/linux/sample.cpp::bare_offset',
    'src/host/linux/sample.cpp::bus_changed',
    'src/host/macos/sample.mm::SampleView.mouseDown:',
}


def self_test():
    entries = []
    for path, text in SELF_TEST.items():
        entries.extend(entries_of(path, text))
    unrouted = {entry.key() for entry in entries if not entry.routed}
    routed = {entry.key() for entry in entries if entry.routed}
    ok = unrouted == SELF_TEST_EXPECTED and len(routed) == 5
    stale = findings(entries, {'src/host/macos/sample.mm::SampleView.drawRect:', 'src/host/gone.cpp::missing'})
    ok = ok and sum(1 for line in stale if 'routed now' in line) == 1 and \
        sum(1 for line in stale if 'no longer an entry' in line) == 1
    print(f'native boundary self-test: {"pass" if ok else "FAIL"} '
          f'(unrouted {sorted(unrouted)}, routed {sorted(routed)})')
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument('--write-baseline', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    entries = scan(arguments.root)
    if arguments.write_baseline:
        unrouted = sorted(entry.key() for entry in entries if not entry.routed)
        with open(os.path.join(arguments.root, BASELINE), 'w', encoding='utf-8', newline='\n') as handle:
            handle.write('# Native callback entries not yet routed through call_failing_fast\n'
                         '# (tools/check_native_boundaries.py). This list may only shrink.\n')
            for key in unrouted:
                handle.write(key + '\n')
        print(f'native boundaries: wrote {len(unrouted)} baseline entries')
        return 0
    problems = findings(entries, read_baseline(arguments.root))
    for problem in problems:
        print(problem)
    routed = sum(1 for entry in entries if entry.routed)
    print(f'native boundaries: {len(entries)} entries, {routed} routed, {len(problems)} findings')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
