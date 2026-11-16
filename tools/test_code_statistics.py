#!/usr/bin/env python3
"""Tests for code_statistics.py: line classes (comments, strings, raw strings,
digit separators, Objective-C literals, continuations), the language of .h
files, components, provenance, and whole runs over small git checkouts."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import code_statistics


def classes(text: str) -> tuple[int, int, int, int]:
    """(lines, blank, comment, code) of a source text."""
    scan: code_statistics.SourceScan = code_statistics.scan_source(text)
    return (scan.lines, scan.blank, scan.comment, scan.code)


class LineClassTests(unittest.TestCase):
    def test_line_and_block_comments(self) -> None:
        text: str = 'int a; // x\n// only\n\n/* a\n\n b */ int c;\n'
        self.assertEqual(classes(text), (6, 2, 2, 2))

    def test_documentation_block(self) -> None:
        self.assertEqual(classes('/**\n * text\n *\n */\nint x;\n'), (5, 0, 4, 1))

    def test_comments_on_one_line(self) -> None:
        self.assertEqual(classes('/* a */ /* b */\n/* c */ x;\n'), (2, 0, 1, 1))

    def test_comment_markers_inside_strings(self) -> None:
        text: str = ('const char* s = "// not a comment";\n'
                     'char q = \'"\';\n'
                     '"/* still a string */";\n'
                     '// real\n')
        self.assertEqual(classes(text), (4, 0, 1, 3))

    def test_escaped_quote(self) -> None:
        self.assertEqual(classes('s = "a\\"b // x";\n// c\n'), (2, 0, 1, 1))

    def test_raw_string_over_lines(self) -> None:
        text: str = 'auto r = R"x(\n// inside\n/* inside */\n)x";\n// after\n'
        self.assertEqual(classes(text), (5, 0, 1, 4))

    def test_raw_string_with_quote_and_parenthesis(self) -> None:
        text: str = 'auto r = R"d(a)" /* not ending */ b)d"; // c\n/* x */\n'
        self.assertEqual(classes(text), (2, 0, 1, 1))

    def test_prefixed_literals(self) -> None:
        text: str = 'f(u8R"(a)" /* c */);\nchar32_t c = U\'\\\'\';\n/* c */\nL"x";\n'
        self.assertEqual(classes(text), (4, 0, 1, 3))

    def test_digit_separator_is_not_a_character_literal(self) -> None:
        # A separator taken for a quote would swallow the comment's start.
        self.assertEqual(classes("int x = 0x1'F; /* start\nend */\n"), (2, 0, 1, 1))

    def test_objective_c_literals(self) -> None:
        text: str = ('#import <Foundation/Foundation.h>\n'
                     'NSString* a = @"// not a comment";\n'
                     'NSString* b = @"x"; // c\n'
                     '// only\n'
                     '@interface Thing : NSObject\n'
                     '@end\n')
        self.assertEqual(classes(text), (6, 0, 1, 5))

    def test_continued_line_comment(self) -> None:
        self.assertEqual(classes('// a \\\ncontinued\nint x;\n'), (3, 0, 2, 1))

    def test_unterminated_quote_ends_with_its_line(self) -> None:
        self.assertEqual(classes("#error don't\n// c\n"), (2, 0, 1, 1))

    def test_line_ends(self) -> None:
        self.assertEqual(classes('a;\r\n\r\n// c'), (3, 1, 1, 1))
        self.assertEqual(classes(''), (0, 0, 0, 0))
        self.assertEqual(classes('\n'), (1, 1, 0, 0))

    def test_blank_line_inside_a_block_comment_is_blank(self) -> None:
        self.assertEqual(classes('/*\n\n*/\n'), (3, 1, 2, 0))


class LanguageTests(unittest.TestCase):
    def language(self, path: str, text: str, paths: list[str]) -> str:
        census: code_statistics.SourceCensus = code_statistics.take_census(paths)
        scan: code_statistics.SourceScan = code_statistics.scan_source(text)
        if scan.tests_cplusplus and not scan.uses_cxx:
            census.c_interfaces.add(path.rpartition('/')[0])
        return code_statistics.language_of(path, scan, census)

    def test_extensions(self) -> None:
        self.assertEqual(self.language('a.c', 'int a;', []), 'c')
        self.assertEqual(self.language('a.mm', 'int a;', []), 'cpp')
        self.assertEqual(self.language('a.inc', 'int a;', []), 'cpp')

    def test_header_follows_its_nearest_sources(self) -> None:
        self.assertEqual(self.language('lib/a.h', 'int a;', ['lib/a.c', 'app/b.cpp', 'app/c.cpp']), 'c')
        self.assertEqual(self.language('lib/a.h', 'int a;', ['lib/a.cpp', 'lib/b.c', 'lib/c.cpp']), 'cpp')
        # No sources beside it: the closest ancestor with any decides.
        self.assertEqual(self.language('lib/include/x/a.h', 'int a;', ['lib/src/a.c']), 'c')
        self.assertEqual(self.language('include/a.h', 'int a;', ['src/a.cpp', 'tests/b.c']), 'cpp')

    def test_c_interface_header_in_cxx_code(self) -> None:
        interface: str = '#ifdef __cplusplus\nextern "C" {\n#endif\nint f(void);\n'
        self.assertEqual(self.language('include/c/api.h', interface, ['src/a.cpp']), 'c')
        uses_cxx: str = interface + 'namespace n {}\n'
        self.assertEqual(self.language('include/c/api.h', uses_cxx, ['src/a.cpp']), 'cpp')
        scoped: str = interface + 'int g = ::f();\n'
        self.assertEqual(self.language('include/c/api.h', scoped, ['src/a.cpp']), 'cpp')

    def test_header_beside_a_c_interface(self) -> None:
        census: code_statistics.SourceCensus = code_statistics.take_census(['src/a.cpp'])
        census.c_interfaces.add('include/c')
        plain: code_statistics.SourceScan = code_statistics.scan_source('struct s { int a; };\n')
        self.assertEqual(code_statistics.language_of('include/c/more.h', plain, census), 'c')
        self.assertEqual(code_statistics.language_of('include/other/more.h', plain, census), 'cpp')

    def test_keywords_in_comments_and_strings_do_not_count(self) -> None:
        scan: code_statistics.SourceScan = code_statistics.scan_source(
            '/* class template */\nconst char* s = "namespace ::";\n// constexpr\n')
        self.assertFalse(scan.uses_cxx)


class ComponentTests(unittest.TestCase):
    def test_components(self) -> None:
        self.assertEqual(code_statistics.component_of('src/core/a/b.cpp'), 'src/core')
        self.assertEqual(code_statistics.component_of('src/a.cpp'), 'src')
        self.assertEqual(code_statistics.component_of('third_party/hb/src/x.cc'), 'third_party/hb')
        self.assertEqual(code_statistics.component_of('include/gui_forms/x.hpp'), 'include')
        self.assertEqual(code_statistics.component_of('stx_png/src/a.cpp'), 'stx_png')
        self.assertEqual(code_statistics.component_of('a.cpp'), '(root)')


class ProvenanceTests(unittest.TestCase):
    def test_classes(self) -> None:
        allowance: dict[str, int] = code_statistics.read_allowance(
            '# header\n 3 skia/src/a.cpp\n 0 skia/src/b.cpp\n')
        rules: code_statistics.ProvenanceRules = code_statistics.ProvenanceRules(
            code_statistics.KIND_STX, allowance)
        expected: dict[str, str] = {
            'stx_skia/skia/src/a.cpp': 'third_party',
            'stx_skia/skia/src/b.cpp': 'converted',
            'stx_skia/src/own.cpp': 'original',
            'stx_zlib/src/inflate.cpp': 'converted',
            'stx_zlib/src/deflate.cpp': 'original',
            'stx_raster3d/include/stx_raster3d/mesh.hpp': 'converted',
            'stx_raster3d/tests/raster3d.cpp': 'original',
            'stx_vorbis/third_party/bfft/a.h': 'original',
            'stx_vorbis/third_party/other/a.h': 'third_party',
            'third_party/freetype/src/a.c': 'third_party',
            'third_party.cpp': 'original',
        }
        for path, provenance in expected.items():
            self.assertEqual(code_statistics.provenance_of(path, rules), provenance, path)

    def test_rules_apply_to_their_kind_only(self) -> None:
        rules: code_statistics.ProvenanceRules = code_statistics.ProvenanceRules(
            code_statistics.KIND_GUI_FORMS, {})
        self.assertEqual(code_statistics.provenance_of('stx_zlib/src/inflate.cpp', rules), 'original')
        self.assertEqual(code_statistics.provenance_of('src/core/threading/atomic_pool/atomic_pool.c', rules),
                         'original')
        stxlib: code_statistics.ProvenanceRules = code_statistics.ProvenanceRules(
            code_statistics.KIND_STXLIB, {})
        self.assertEqual(code_statistics.provenance_of('tests/reference/atomic_pool.c', stxlib), 'original')
        self.assertEqual(code_statistics.provenance_of('src/algorithms/stx_btree/node.hpp', stxlib), 'converted')

    def test_every_rule_names_a_known_class_and_record(self) -> None:
        for rule in code_statistics.OWNER_COPIES + code_statistics.PATH_RULES:
            self.assertIn(rule.provenance, code_statistics.PROVENANCE_ORDER)
            self.assertNotEqual(rule.record, '')


def git(directory: Path, arguments: list[str]) -> str:
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        ['git', '-C', str(directory), '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
         '-c', 'commit.gpgsign=false'] + arguments,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    return completed.stdout.strip()


def make_repository(directory: Path, files: dict[str, str]) -> str:
    """A repository at directory whose one commit holds files; its commit."""
    directory.mkdir(parents=True)
    git(directory, ['init', '-q'])
    for path, text in files.items():
        target: Path = directory / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(text.encode('utf-8'))
    git(directory, ['add', '-A'])
    git(directory, ['commit', '-q', '-m', 'fixture'])
    return git(directory, ['rev-parse', 'HEAD'])


class CheckoutTests(unittest.TestCase):
    def test_counts_head_of_each_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base: Path = Path(temporary)
            stx_commit: str = make_repository(base / 'stx', {
                'stxlib.lock': '[stxlib]\ncommit = "' + '1' * 40 + '"\n',
                'stx_skia/house_style_allowance.txt': '# none\n',
                'stx_skia/skia/src/a.cpp': '// Skia\nint a;\n',
                'stx_zlib/src/inflate.cpp': 'int i;\n\n',
                'third_party/lib/a.c': '/* c */\nint c;\n',
                'third_party/lib/a.h': 'int c;\n',
                'stx_png/README.md': 'not counted\n',
            })
            make_repository(base / 'gui_forms', {
                'stx.lock': '[stx]\ncommit = "' + stx_commit + '"\n',
                'src/core/a.cpp': 'int a; // é\n',
                'include/c/api.h': '#ifdef __cplusplus\nextern "C" {\n#endif\n',
                'tests/t.c': 'int main(void) { return 0; }\n',
            })
            # Neither an uncommitted edit nor an untracked file is counted.
            (base / 'gui_forms' / 'src' / 'core' / 'a.cpp').write_text('int a;\nint b;\n', encoding='utf-8')
            (base / 'gui_forms' / 'src' / 'core' / 'b.cpp').write_text('int b;\n', encoding='utf-8')
            report: code_statistics.Report = code_statistics.collect(base / 'gui_forms', [str(base / 'stx')])
            result: dict[str, object] = json.loads(json.dumps(code_statistics.report_json(report)))

        checkouts: list[dict[str, object]] = result['checkouts']  # type: ignore[assignment]
        self.assertEqual([checkout['name'] for checkout in checkouts], ['gui_forms', 'stx'])
        self.assertEqual(checkouts[1]['pinned_commit'], stx_commit)
        self.assertIs(checkouts[1]['at_pin'], True)
        self.assertIsNone(checkouts[0]['at_pin'])
        rows: list[tuple[object, ...]] = []
        for row in result['rows']:  # type: ignore[union-attr]
            rows.append((row['checkout'], row['component'], row['language'], row['provenance'],
                         row['files'], row['lines'], row['blank'], row['comment'], row['code'],
                         row['characters'], row['bytes']))
        self.assertEqual(rows, [
            ('gui_forms', 'include', 'c', 'original', 1, 3, 0, 0, 3, 39, 39),
            ('gui_forms', 'src/core', 'cpp', 'original', 1, 1, 0, 0, 1, 12, 13),
            ('gui_forms', 'tests', 'c', 'original', 1, 1, 0, 0, 1, 29, 29),
            ('stx', 'stx_skia', 'cpp', 'converted', 1, 2, 0, 1, 1, 15, 15),
            ('stx', 'stx_zlib', 'cpp', 'converted', 1, 2, 1, 0, 1, 8, 8),
            ('stx', 'third_party/lib', 'c', 'third_party', 2, 3, 0, 1, 2, 22, 22),
        ])
        self.assertEqual(result['total'], {'files': 7, 'lines': 12, 'blank': 1, 'comment': 2,
                                           'code': 9, 'characters': 125, 'bytes': 126})
        self.assertEqual(result['by_provenance']['converted']['cpp']['files'], 2)  # type: ignore[index]
        self.assertEqual(result['headline'], {
            'label': 'first-party C++ (gui_forms and stx)',
            'first_party_cpp': {'files': 3, 'lines': 5, 'blank': 1, 'comment': 1, 'code': 3,
                                'characters': 35, 'bytes': 36}})
        # stx pins STXlib, which was not given.
        self.assertEqual(result['uncounted_pins'], [{'kind': 'stxlib', 'commit': '1' * 40}])
        self.assertEqual(sorted(result.keys()),
                         ['by_language', 'by_provenance', 'checkouts', 'headline', 'rows', 'schema',
                          'total', 'uncounted_pins'])

    def test_markdown_and_text_name_the_commits(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base: Path = Path(temporary)
            commit: str = make_repository(base / 'gui_forms', {'stx.lock': '', 'src/a/b.cpp': 'int b;\n'})
            report: code_statistics.Report = code_statistics.collect(base / 'gui_forms', [])
        markdown: str = code_statistics.render_markdown(report)
        self.assertIn('gui_forms `' + commit[:12] + '`', markdown)
        self.assertIn('| C++ | first-party original | 1 | 1 | 0 | 0 | 1 | 7 | 7 |', markdown)
        self.assertIn('| gui_forms | src/a | C++ |', markdown)
        self.assertIn('| first-party C++ (gui_forms) | 1 | 1 | 1 | 7 |', markdown)
        text: str = code_statistics.render_text(report)
        self.assertIn('gui_forms ' + commit[:12], text)

    def test_a_missing_checkout_fails_with_status_two(self) -> None:
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stderr(io.StringIO()) as errors:
            status: int = code_statistics.main(['--root', temporary, '--json', str(Path(temporary) / 'out.json')])
        self.assertIn('code_statistics: git ls-tree', errors.getvalue())
        self.assertEqual(status, 2)


if __name__ == '__main__':
    unittest.main()
