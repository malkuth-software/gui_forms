"""check_test_asserts finds bare assert() in code and ignores it elsewhere.

Each case writes one small source file to a temporary directory and reads
its findings.
"""
from __future__ import annotations
from pathlib import Path
import tempfile
import unittest

import check_test_asserts


def findings(source: str) -> list[tuple[int, str]]:
    with tempfile.TemporaryDirectory() as directory:
        path: Path = Path(directory) / 'sample_tests.cpp'
        path.write_text(source, encoding='utf-8')
        found: list[tuple[int, str]] = check_test_asserts.findings_of(path)
    return found


class FindingTests(unittest.TestCase):
    def test_bare_assert_and_header_are_found(self) -> None:
        found: list[tuple[int, str]] = findings('#include <cassert>\nint main() {\n    assert(run() == 0);\n}\n')
        self.assertEqual([number for number, _ in found], [1, 3])

    def test_ndebug_switch_is_found(self) -> None:
        self.assertEqual(len(findings('#undef NDEBUG\n#  define NDEBUG 1\n')), 2)

    def test_static_assert_comments_and_literals_are_not_found(self) -> None:
        source: str = ('static_assert(sizeof(int) == 4);\n'
                       '// assert(x) in a comment\n'
                       '/* assert(y)\n   still a comment */\n'
                       'const char* text = "assert(z)";\n'
                       'const char* raw = R"(assert(w))";\n'
                       'require(value.assert_ready(), "ready");\n')
        self.assertEqual(findings(source), [])

    def test_line_numbers_survive_block_comments(self) -> None:
        found: list[tuple[int, str]] = findings('/* one\ntwo */\nassert(true);\n')
        self.assertEqual([number for number, _ in found], [3])


if __name__ == '__main__':
    unittest.main()
