"""Tests of the consumer-surface macro tools.

consumer_macros (the lexer, the header scan, the closure and the
classification), generate_hostile_macros (the conformance header and its
seeds) and guard_header_macros (the guard's exact name set, its placement,
its idempotence and its check mode). They read and write only temporary
files; nothing is compiled.
"""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import consumer_macros
import generate_hostile_macros
import guard_header_macros

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

    def test_translation_unit_must_name_the_generated_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            unit: Path = write(root, 'unit.cpp', '#include "gui_forms/label.hpp"\n')
            with self.assertRaises(SystemExit):
                generate_hostile_macros.seeds_of(unit, 'hostile_macros_label.h')


class GuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.root: Path = Path(self.directory.name)
        self.include: Path = self.root / 'include'
        write(self.root, 'include/gui_forms/macros.hpp', '#pragma once\n#define HELPER(x) (x)\n')

    def tearDown(self) -> None:
        self.directory.cleanup()

    def guard(self, relative: str, text: str) -> str:
        header: Path = write(self.root, relative, text)
        return guard_header_macros.guard_header(header, [self.include], STANDARD)

    def test_guard_saves_exactly_the_open_names_the_header_uses(self) -> None:
        guarded: str = self.guard(
            'include/gui_forms/a.hpp',
            '// near in a comment\n#pragma once\n#include "gui_forms/macros.hpp"\n#include <vector>\n'
            '#ifdef CONFIGURED\n#endif\nnamespace gui_forms {\nenum class A { near, far };\n'
            'inline int g(std::vector<int> v) { return HELPER((std::max)(int(v.size()), 1)); }\n'
            'inline const char* s = "Status";\n}\n')
        lines: list[str] = guarded.splitlines()
        pushed: list[str] = []
        popped: list[str] = []
        for line in lines:
            if line.startswith('#pragma push_macro'):
                pushed.append(line.split('"')[1])
            if line.startswith('#pragma pop_macro'):
                popped.append(line.split('"')[1])
        self.assertEqual(pushed, ['A', 'far', 'g', 'max', 'near', 's', 'v'])
        self.assertEqual(popped, list(reversed(pushed)))
        self.assertIn('#undef near', lines)
        # Opening after #pragma once, before the includes; closing at the end.
        self.assertEqual(lines[2], guard_header_macros.GUARD_BEGIN)
        self.assertLess(lines.index(guard_header_macros.GUARD_END), lines.index('#include "gui_forms/macros.hpp"'))
        self.assertEqual(lines[-1], guard_header_macros.RESTORE_END)

    def test_guard_goes_inside_an_include_guard(self) -> None:
        guarded: str = self.guard('include/gui_forms/c.h',
                                  '#ifndef GF_C_H\n#define GF_C_H\nstruct gf_rect { int width; };\n#endif\n')
        lines: list[str] = guarded.splitlines()
        self.assertEqual(lines[:3], ['#ifndef GF_C_H', '#define GF_C_H', guard_header_macros.GUARD_BEGIN])
        self.assertEqual(lines[-2:], [guard_header_macros.RESTORE_END, '#endif'])
        self.assertIn('#pragma push_macro("width")', lines)
        self.assertNotIn('#pragma push_macro("GF_C_H")', lines)

    def test_guard_is_idempotent_and_checkable(self) -> None:
        relative: str = 'include/gui_forms/b.hpp'
        once: str = self.guard(relative, '#pragma once\nenum class B { small };')
        twice: str = self.guard(relative, once)
        self.assertEqual(once, twice)
        header: Path = self.root / relative
        names: Path = write(self.root, 'standard.txt', 'std\n')
        arguments: list[str] = [str(header), '--include-root', str(self.include), '--standard-names', str(names),
                                '--check']
        self.assertEqual(guard_header_macros.main(arguments), 0)
        header.write_text('#pragma once\nenum class B { small, large };\n', encoding='utf-8')
        self.assertEqual(guard_header_macros.main(arguments), 1)

    def test_header_without_open_names_is_unchanged(self) -> None:
        text: str = '#pragma once\nnamespace gui_forms { int gf_value; }\n'
        self.assertEqual(self.guard('include/gui_forms/d.hpp', text), text)


if __name__ == '__main__':
    unittest.main()
