#!/usr/bin/env python3
"""Tests for check_test_registration.py on a small fixture tree."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_test_registration  # noqa: E402

PROGRAM: str = 'int main() {\n    return 0;\n}\n'


def write(root: Path, relative: str, text: str) -> None:
    path: Path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


class TestRegistrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        write(self.root, 'tests/registered.cpp', PROGRAM)
        write(self.root, 'tests/helper.cpp', 'int helper() { return 1; }\n')
        write(self.root, 'tests/sub/CMakeLists.txt', 'add_executable(sub_tests main.cpp)\n'
                                                     'add_test(NAME sub COMMAND sub_tests)\n')
        write(self.root, 'tests/sub/main.cpp', PROGRAM)

    def tearDown(self) -> None:
        self.directory.cleanup()

    def cmake(self, text: str) -> None:
        write(self.root, 'CMakeLists.txt', text)

    def test_registered_programs_pass(self) -> None:
        self.cmake('add_executable(registered_tests tests/registered.cpp tests/helper.cpp)\n'
                   'add_test(NAME registered COMMAND $<TARGET_FILE:registered_tests>)\n')
        self.assertEqual(check_test_registration.findings(self.root, {}), [])

    def test_unlisted_program_fails(self) -> None:
        self.cmake('# add_executable(registered_tests tests/registered.cpp)\n')
        found: list[str] = check_test_registration.findings(self.root, {})
        self.assertEqual(found, ['tests/registered.cpp: defines main but no add_executable lists it'])

    def test_program_without_a_test_fails(self) -> None:
        self.cmake('add_executable(registered_tests tests/registered.cpp)\n')
        found: list[str] = check_test_registration.findings(self.root, {})
        self.assertEqual(len(found), 1)
        self.assertIn('named by no add_test', found[0])


if __name__ == '__main__':
    unittest.main()
