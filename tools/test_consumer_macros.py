"""Tests of the consumer-surface macro tools.

consumer_macros (the lexer, the header scan, the closure and the
classification), generate_hostile_macros (the conformance header and its
seeds), consumer_shield (both shields' name sets, the handshake run through a
small preprocessor in nested, repeated and mixed include orders, and the
placement of its include lines, the install check) and module_exports (the declarations the
gui_forms module exports). They read and write only temporary
files; nothing is compiled.
"""
from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path, PureWindowsPath

import consumer_macros
import consumer_shield
import generate_hostile_macros
import module_exports

STANDARD: frozenset[str] = frozenset(('vector', 'size_t', 'uint8_t', 'min', 'max', 'size', 'std', 'value'))


def write(root: Path, relative: str, text: str) -> Path:
    path: Path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path


class LexerTests(unittest.TestCase):
    def identifiers(self, source: str) -> list[str]:
        names: list[str] = []
        for token in consumer_macros.lex(source):
            if token.kind == 'identifier':
                names.append(token.text)
        return names

    def test_comments_and_literals_hold_no_identifiers(self) -> None:
        source: str = ('// near\n/* far\n small */ int a = 0; const char* s = "linux";\n'
                       "char c = 'x'; auto r = R\"tag(Status)tag\";\n")
        self.assertEqual(self.identifiers(source), ['int', 'a', 'const', 'char', 's', 'char', 'c', 'auto', 'r'])

    def test_encoding_prefixes_and_suffixes_belong_to_the_literal(self) -> None:
        source: str = 'auto a = u8"x"sv; auto b = L\'y\'; auto c = LR"(z)"; auto d = U"w"_tag;'
        self.assertEqual(self.identifiers(source), ['auto', 'a', 'auto', 'b', 'auto', 'c', 'auto', 'd'])

    def test_numbers_hold_no_identifiers(self) -> None:
        source: str = "x = 10ms + 0x1Fu + 1'000'000 + 1.5e+3f + .5f;"
        self.assertEqual(self.identifiers(source), ['x'])

    def test_continued_line_stays_one_line(self) -> None:
        tokens: list[consumer_macros.Token] = consumer_macros.lex('#define A \\\n  near\nfar\n')
        newline_count: int = 0
        for token in tokens:
            if token.kind == 'newline':
                newline_count += 1
        self.assertEqual(newline_count, 2)
        self.assertEqual(tokens[-2].text, 'far')
        self.assertEqual(tokens[-2].line, 3)

    def test_include_and_pragma_lines_are_opaque(self) -> None:
        self.assertEqual(self.identifiers('#include <vector>\n#pragma once\n#error near far\nint x;\n'),
                         ['int', 'x'])


class ScanTests(unittest.TestCase):
    def test_scan_sorts_identifiers_by_role(self) -> None:
        source: str = ('#pragma once\n#include "a/b.hpp"\n#include <vector>\n'
                       '#define GF_F(a, b) ((a) + other)\n#define GF_G (spaced)\n'
                       '#if defined(TARGET_X) && FOO\n#endif\n#undef near\n'
                       'enum class E { near, far };\n')
        scan: consumer_macros.HeaderScan = consumer_macros.scan_header(source)
        self.assertEqual(scan.defined, {'GF_F', 'GF_G'})
        self.assertEqual(scan.undefined, {'near'})
        self.assertEqual(scan.conditions, {'defined', 'TARGET_X', 'FOO'})
        # The function-like macro's parameters are not uses; the object-like
        # macro's parenthesised body is.
        self.assertEqual(set(scan.uses), {'other', 'spaced', 'enum', 'class', 'E', 'near', 'far'})
        self.assertEqual(scan.uses['near'], 9)
        self.assertEqual([(include.spelling, include.angled) for include in scan.includes],
                         [('a/b.hpp', False), ('vector', True)])


class ClassificationTests(unittest.TestCase):
    def test_consumer_may_define_only_open_names(self) -> None:
        source: str = ('#pragma once\n#define GF_LOCAL 1\n#ifdef CONFIGURED\n#endif\n'
                       'namespace gui_forms { enum class Alignment { near, far }; '
                       'inline std::size_t f(std::vector<int> v) { return (std::max)(v.size(), 1u); } '
                       'int __reserved; int _Also; int gf_thing; int value; int CONFIGURED; }\n')
        headers: dict[Path, consumer_macros.HeaderScan] = {Path('h.hpp'): consumer_macros.scan_header(source)}
        result: consumer_macros.Classification = consumer_macros.classify(headers, STANDARD)
        self.assertEqual(set(result.hostile), {'Alignment', 'near', 'far', 'f', 'v', 'max'})
        self.assertEqual(result.excluded['standard library'], {'std', 'size_t', 'vector', 'size', 'value'})
        self.assertEqual(result.excluded['prefix'], {'gui_forms', 'gf_thing'})
        self.assertEqual(result.excluded['reserved'], {'__reserved', '_Also'})
        self.assertEqual(result.excluded['configuration'], {'CONFIGURED'})
        self.assertIn('namespace', result.excluded['language'])
        self.assertEqual(result.unprefixed_macros, {})

    def test_unprefixed_first_party_macros_are_reported(self) -> None:
        headers: dict[Path, consumer_macros.HeaderScan] = {
            Path('h.hpp'): consumer_macros.scan_header('#define HELPER 1\n#undef Status\nint x = HELPER;\n')}
        result: consumer_macros.Classification = consumer_macros.classify(headers, STANDARD)
        self.assertEqual(set(result.unprefixed_macros), {'HELPER'})
        self.assertEqual(set(result.undefined_macros), {'Status'})
        self.assertNotIn('HELPER', result.hostile)


class ClosureTests(unittest.TestCase):
    def test_closure_follows_first_party_includes_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            write(root, 'include/gui_forms/a.hpp', '#pragma once\n#include "gui_forms/b.hpp"\n#include <vector>\n')
            write(root, 'include/gui_forms/b.hpp', '#pragma once\n#include "detail/c.hpp"\n#include <cstdint>\n')
            write(root, 'include/gui_forms/detail/c.hpp', '#pragma once\n#include "gui_forms/a.hpp"\nint c;\n')
            seeds: list[consumer_macros.Include] = [consumer_macros.Include('gui_forms/a.hpp', False, 1)]
            closure: consumer_macros.Closure = consumer_macros.compute_closure(seeds, [root / 'include'], root)
            names: list[str] = []
            for path in closure.headers:
                names.append(path.name)
            self.assertEqual(names, ['a.hpp', 'b.hpp', 'c.hpp'])
            self.assertEqual(closure.system_includes, ['cstdint', 'vector'])


class HostileGeneratorTests(unittest.TestCase):
    def test_header_defines_every_open_name_after_the_standard_headers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            write(root, 'include/gui_forms/label.hpp',
                  '#pragma once\n#include <vector>\nnamespace gui_forms { enum class HorizontalAlignment '
                  '{ near, far }; inline std::vector<int> f() { return {}; } }\n')
            names: Path = write(root, 'standard.txt', '# comment\nvector\nstd\n')
            unit: Path = write(root, 'tests/hostile_macro_label.cpp',
                               '// test\n#include "hostile_macros_label.h"\n\n#include "gui_forms/label.hpp"\n')
            output: Path = root / 'build/hostile_macros_label.h'
            report: Path = root / 'build/report.txt'
            status: int = generate_hostile_macros.main([
                '--translation-unit', str(unit), '--include-root', str(root / 'include'),
                '--standard-names', str(names), '--output', str(output), '--report', str(report)])
            self.assertEqual(status, 0)
            text: str = output.read_text(encoding='utf-8')
            self.assertLess(text.index('#include <vector>'), text.index('#define'))
            self.assertIn('#define near _Pragma("GCC error \\"hostile consumer macro expanded: near\\"")'
                          ' hostile_consumer_macro_near\n', text)
            defined: list[str] = []
            for line in text.splitlines():
                if line.startswith('#define '):
                    defined.append(line.split()[1])
            self.assertEqual(defined, ['HorizontalAlignment', 'f', 'far', 'near'])
            self.assertIn('gui_forms/label.hpp:3\tnear', report.read_text(encoding='utf-8'))

    def test_report_paths_use_forward_slashes_on_every_host(self) -> None:
        root: PureWindowsPath = PureWindowsPath('D:/a/gui_forms/include')
        header: PureWindowsPath = PureWindowsPath('D:/a/gui_forms/include/gui_forms/label.hpp')
        outside: PureWindowsPath = PureWindowsPath('D:/b/other.hpp')
        self.assertEqual(generate_hostile_macros.display_path(header, [root]), 'gui_forms/label.hpp')
        self.assertEqual(generate_hostile_macros.display_path(outside, [root]), 'D:/b/other.hpp')

    def test_translation_unit_must_name_the_generated_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            unit: Path = write(root, 'unit.cpp', '#include "gui_forms/label.hpp"\n')
            with self.assertRaises(SystemExit):
                generate_hostile_macros.seeds_of(unit, 'hostile_macros_label.h')


class Preprocessor:
    """Just enough of a preprocessor to run the shield's handshake.

    Files are a dict of include spelling -> text. It follows #pragma once,
    #ifndef/#ifdef/#else/#endif, #define and #undef of object-like names,
    push_macro and pop_macro (a stack per name, as Clang, GCC and MSVC keep
    one), and #include "...". '#pragma probe NAME' records whether NAME is
    defined at that point, and its value.
    """

    def __init__(self, files: dict[str, str]) -> None:
        self.files: dict[str, str] = files
        self.macros: dict[str, str] = {}
        self.stacks: dict[str, list[str | None]] = {}
        self.once: set[str] = set()
        self.probes: list[tuple[str, str | None]] = []

    def run(self, spelling: str) -> None:
        if spelling in self.once:
            return
        active: list[bool] = [True]
        for raw in self.files[spelling].splitlines():
            line: str = raw.strip()
            if not line.startswith('#'):
                continue
            words: list[str] = line[1:].split()
            directive: str = words[0]
            if directive in ('ifdef', 'ifndef'):
                defined: bool = words[1] in self.macros
                active.append(active[-1] and (defined if directive == 'ifdef' else not defined))
                continue
            if directive == 'else':
                enclosing: bool = active[-2]
                active[-1] = enclosing and not active[-1]
                continue
            if directive == 'endif':
                active.pop()
                continue
            if not active[-1]:
                continue
            if directive == 'define':
                self.macros[words[1]] = ' '.join(words[2:])
            elif directive == 'undef':
                self.macros.pop(words[1], None)
            elif directive == 'include':
                self.run(words[1].strip('"'))
            elif directive == 'pragma' and words[1] == 'once':
                self.once.add(spelling)
            elif directive == 'pragma' and words[1].startswith('push_macro('):
                name: str = words[1].split('"')[1]
                self.stacks.setdefault(name, []).append(self.macros.get(name))
            elif directive == 'pragma' and words[1].startswith('pop_macro('):
                name = words[1].split('"')[1]
                if self.stacks.get(name):
                    value: str | None = self.stacks[name].pop()
                    if value is None:
                        self.macros.pop(name, None)
                    else:
                        self.macros[name] = value
            elif directive == 'pragma' and words[1] == 'probe':
                self.probes.append((words[2], self.macros.get(words[2])))
        if len(active) != 1:
            raise AssertionError(f'{spelling}: unbalanced conditionals')


class ShieldTests(unittest.TestCase):
    C_NAMES: list[str] = ['near', 'far', 'Button']
    # The full shield's names: a superset of the C shield's.
    FULL_NAMES: list[str] = ['Button', 'far', 'near', 'wide']

    def files(self) -> dict[str, str]:
        c_shield: consumer_shield.Shield = consumer_shield.C_SHIELD
        full_shield: consumer_shield.Shield = consumer_shield.FULL_SHIELD
        outer: str = consumer_shield.insert_handshake(
            '#pragma once\n#pragma probe near\n#include "inner.hpp"\n#pragma probe far\n')
        inner: str = consumer_shield.insert_handshake('#pragma once\n#pragma probe Button\n')
        guarded: str = consumer_shield.insert_handshake(
            '#ifndef GF_C_H\n#define GF_C_H\n#include "inner.hpp"\n#pragma probe near\n#endif\n')
        return {c_shield.begin: consumer_shield.render_begin(c_shield, self.C_NAMES),
                c_shield.end: consumer_shield.render_end(c_shield, self.C_NAMES),
                full_shield.begin: consumer_shield.render_begin(full_shield, self.FULL_NAMES),
                full_shield.end: consumer_shield.render_end(full_shield, self.FULL_NAMES),
                'outer.hpp': outer, 'inner.hpp': inner, 'c.h': guarded,
                'wide.hpp': '#pragma once\n#pragma probe wide\n'}

    def consumer(self, body: str) -> Preprocessor:
        files: dict[str, str] = self.files()
        files['unit.cpp'] = '#define near 1\n#define Button 3\n#define wide 4\n' + body
        preprocessor: Preprocessor = Preprocessor(files)
        preprocessor.run('unit.cpp')
        return preprocessor

    def assert_restored(self, preprocessor: Preprocessor) -> None:
        self.assertEqual(preprocessor.macros.get('near'), '1')
        self.assertEqual(preprocessor.macros.get('Button'), '3')
        self.assertEqual(preprocessor.macros.get('wide'), '4')
        self.assertNotIn('far', preprocessor.macros)
        self.assertNotIn('GF_SHIELD_ACTIVE', preprocessor.macros)
        self.assertNotIn('GF_SHIELD_OPENER', preprocessor.macros)
        for name, stack in preprocessor.stacks.items():
            self.assertEqual(stack, [], name)

    def test_nested_headers_open_and_close_once(self) -> None:
        preprocessor: Preprocessor = self.consumer('#include "outer.hpp"\n#pragma probe near\n')
        # Inside: every shielded name is absent; after: restored.
        self.assertEqual(preprocessor.probes, [('near', None), ('Button', None), ('far', None), ('near', '1')])
        self.assert_restored(preprocessor)

    def test_repeated_and_mixed_orders(self) -> None:
        preprocessor: Preprocessor = self.consumer(
            '#include "inner.hpp"\n#pragma probe Button\n#include "outer.hpp"\n#include "inner.hpp"\n'
            '#include "c.h"\n#include "c.h"\n#pragma probe near\n')
        self.assertEqual(preprocessor.probes, [
            ('Button', None), ('Button', '3'),        # inner alone, then restored
            ('near', None), ('far', None),            # outer (inner already read once)
            ('near', None),                           # c.h's guard opens its own shield
            ('near', '1')])
        self.assert_restored(preprocessor)

    def test_consumer_wrapped_block(self) -> None:
        # The full shield around C-shield headers: theirs do nothing inside it,
        # and the names only the full shield holds stay removed throughout.
        preprocessor: Preprocessor = self.consumer(
            '#include "gui_forms/gf_shield_begin.h"\n#pragma probe near\n#include "c.h"\n#include "outer.hpp"\n'
            '#include "wide.hpp"\n#pragma probe Button\n#include "gui_forms/gf_shield_end.h"\n'
            '#pragma probe Button\n#pragma probe wide\n')
        self.assertEqual(preprocessor.probes, [
            ('near', None), ('Button', None), ('near', None), ('near', None), ('far', None),
            ('wide', None), ('Button', None), ('Button', '3'), ('wide', '4')])
        self.assert_restored(preprocessor)

    def test_c_shield_leaves_other_names_alone(self) -> None:
        # A C-shield header alone touches only the C shield's names.
        preprocessor: Preprocessor = self.consumer('#include "c.h"\n#include "wide.hpp"\n')
        self.assertEqual(preprocessor.probes, [('Button', None), ('near', None), ('wide', '4')])
        self.assert_restored(preprocessor)

    def test_handshake_placement_and_idempotence(self) -> None:
        once: str = consumer_shield.insert_handshake('// x\n#pragma once\nint a;\n')
        self.assertEqual(once, '// x\n#pragma once\n' + consumer_shield.BEGIN_LINE + '\nint a;\n'
                         + consumer_shield.END_LINE + '\n')
        self.assertEqual(consumer_shield.BEGIN_LINE, '#include "gui_forms/c/gf_c_shield_begin.h"')
        self.assertEqual(consumer_shield.insert_handshake(once), once)
        self.assertTrue(consumer_shield.has_handshake(once))
        self.assertFalse(consumer_shield.has_handshake('#pragma once\nint a;\n'))
        guarded: str = consumer_shield.insert_handshake('#ifndef G\n#define G\nint a;\n#endif\n')
        self.assertEqual(guarded.splitlines(), ['#ifndef G', '#define G', consumer_shield.BEGIN_LINE, 'int a;',
                                                consumer_shield.END_LINE, '#endif'])
        with self.assertRaises(ValueError):
            consumer_shield.insert_handshake('int a;\n')

    def test_shield_names_cover_the_surface_and_skip_the_shields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            write(root, 'include/gui_forms/a.hpp', '#pragma once\n#include "gui_forms/gf_shield_begin.h"\n'
                  '#include "gui_forms/b.hpp"\nnamespace gui_forms { enum class A { near }; }\n'
                  '#include "gui_forms/gf_shield_end.h"\n')
            write(root, 'include/gui_forms/b.hpp', '#pragma once\n#define GF_B 1\n#ifdef CONFIGURED\n#endif\n'
                  'namespace gui_forms { int far = GF_B; int CONFIGURED; std::size_t s; }\n')
            write(root, 'include/gui_forms/gf_shield_begin.h', '#undef stale\n')
            write(root, 'include/gui_forms/c/gf_c_shield_begin.h', '#undef stale_c\n')
            names: list[str] = consumer_shield.shield_names(root, [], STANDARD | {'size_t'})
            self.assertEqual(names, ['A', 'far', 'near', 's'])

    def test_c_shield_names_are_the_handshake_headers_with_surface_exclusions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            # SHAPE is a first-party macro of another header: excluded here too.
            write(root, 'include/gui_forms/c/gf_abi.h', '#ifndef GF_ABI_H\n#define GF_ABI_H\n'
                  '#include "gui_forms/c/gf_c_shield_begin.h"\ntypedef int Status;\nint SHAPE;\n'
                  '#include "gui_forms/c/gf_c_shield_end.h"\n#endif\n')
            write(root, 'include/gui_forms/c/gf_c_shield_begin.h', '#undef stale\n')
            write(root, 'include/gui_forms/button.hpp', '#pragma once\n#define SHAPE 1\nint Button;\n')
            names: list[str] = consumer_shield.c_shield_names(root, [], STANDARD)
            self.assertEqual(names, ['Status'])
            self.assertEqual(consumer_shield.shield_names(root, [], STANDARD), ['Button', 'Status'])

    def test_full_check_fails_on_a_missing_or_stale_install(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            write(root, 'include/gui_forms/button.hpp', '#pragma once\nint Button;\n')
            write(root, 'names.txt', 'int\n')
            common: list[str] = ['--root', str(root), '--standard-names', str(root / 'names.txt'),
                                 '--output-dir', str(root / 'installed/gui_forms')]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(consumer_shield.main(['full', '--check'] + common), 1)
                self.assertEqual(consumer_shield.main(['full'] + common), 0)
                self.assertEqual(consumer_shield.main(['full', '--check'] + common), 0)
                write(root, 'include/gui_forms/label.hpp', '#pragma once\nint Label;\n')
                self.assertEqual(consumer_shield.main(['full', '--check'] + common), 1)


class ModuleExportsTests(unittest.TestCase):
    def names(self, source: str) -> list[str]:
        found: list[str] = []
        for declaration in module_exports.declarations_of(source):
            found.append('::'.join(declaration.namespace + (declaration.name,)))
        return found

    def test_exportable_namespace_scope_declarations(self) -> None:
        source: str = (
            '#pragma once\n#define GF_X 1\nnamespace gui_forms {\n'
            'class Forward;\nclass GUI_FORMS_API Button final : public Base { void f(); int x_{0}; };\n'
            'enum class Alignment : std::uint8_t { near, far };\n'
            'template <typename T> class Value { T v; };\n'
            'template <typename T> Value(T) -> Value<T>;\n'
            'using Interval = std::chrono::milliseconds;\n'
            'GUI_FORMS_API void run(Button& button);\n'
            'inline int twice(int a) { return a + a; }\n'
            'bool operator==(const Button& a, const Button& b) noexcept;\n'
            'inline constexpr Interval period{40};\n'
            'constexpr int internal_constant = 3;\n'
            'static void helper();\n'
            'template <> inline constexpr int kind_of<bool> = 1;\n'
            'void Value<int>::member() {}\n'
            'struct Holder { friend void hidden(Holder&); };\n'
            'namespace detail { void secret(); }\n'
            'namespace { void anonymous(); }\n'
            'namespace plots { struct Series {}; }\n'
            'template <typename T> concept Numeric = true;\n'
            '}\n')
        self.assertEqual(self.names(source), [
            'gui_forms::Forward', 'gui_forms::Button', 'gui_forms::Alignment', 'gui_forms::Value',
            'gui_forms::Interval', 'gui_forms::run', 'gui_forms::twice', 'gui_forms::operator==',
            'gui_forms::period', 'gui_forms::Holder', 'gui_forms::plots::Series', 'gui_forms::Numeric'])
        forward: list[bool] = []
        for declaration in module_exports.declarations_of('namespace gui_forms { class A; class B {}; }'):
            forward.append(declaration.forward)
        self.assertEqual(forward, [True, False])

    def test_module_unit_exports_by_namespace(self) -> None:
        declarations: list[module_exports.Declaration] = [
            module_exports.Declaration(('gui_forms',), 'Button'),
            module_exports.Declaration(('gui_forms', 'abi'), 'RuntimeTable'),
            module_exports.Declaration(('gui_forms',), 'Application')]
        text: str = module_exports.render_module(declarations)
        self.assertIn('module;\n#include "gui_forms/gui_forms.hpp"\n', text)
        self.assertIn('export module gui_forms;\n', text)
        self.assertIn('export namespace gui_forms {\nusing gui_forms::Application;\nusing gui_forms::Button;\n'
                      '} // namespace gui_forms\n', text)
        self.assertIn('export namespace gui_forms::abi {\nusing gui_forms::abi::RuntimeTable;\n', text)


if __name__ == '__main__':
    unittest.main()
