#!/usr/bin/env python3
"""Tests of tools/ci_docs_only.py: which pull requests skip the native builds."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import unittest

import change_selection
import ci_docs_only


class ClassifyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.rules: list[change_selection.PathRule] = change_selection.load_rules()

    def docs_only(self, changed: list[str], event: str = 'pull_request') -> bool:
        result: tuple[bool, str] = ci_docs_only.classify(event, changed, self.rules)
        return result[0]

    def test_documentation_alone_skips_the_builds(self) -> None:
        self.assertTrue(self.docs_only(['docs/user/index.rst']))
        self.assertTrue(self.docs_only(['docs/wiki/Home.md', 'README.md', 'AGENTS.md']))
        self.assertTrue(self.docs_only(['docs/building/index.md', 'docs/conf.py']))

    def test_code_builds_in_full(self) -> None:
        path: str
        for path in ('src/core/window/window.cpp', 'include/gui_forms/gui_forms.hpp', 'CMakeLists.txt',
                     'cmake/Stx.cmake', 'tests/tool_strip_tests.cpp', 'tools/ci_build.py',
                     '.github/workflows/native.yml', 'stx.lock', 'manifests/controls.json'):
            self.assertFalse(self.docs_only(['docs/user/index.rst', path]), path)

    def test_markdown_in_code_directories_builds_in_full(self) -> None:
        self.assertFalse(self.docs_only(['tests/README.md']))
        self.assertFalse(self.docs_only(['src/render/skia/README.md']))
        self.assertFalse(self.docs_only(['.github/PULL_REQUEST_TEMPLATE.md']))

    def test_documentation_a_test_reads_builds_in_full(self) -> None:
        # The API docstrings are compiled by gui_forms_doc_examples_tests.
        self.assertFalse(self.docs_only(['docs/library/docstrings/DomainUpDown.rst']))
        # The SDK installs the notices and the programs ship them.
        self.assertFalse(self.docs_only(['THIRD_PARTY_NOTICES.md']))

    def test_other_events_and_empty_changes_build_in_full(self) -> None:
        event: str
        for event in ('merge_group', 'push', 'schedule', 'workflow_dispatch'):
            self.assertFalse(self.docs_only(['docs/user/index.rst'], event), event)
        self.assertFalse(self.docs_only([]))

    def test_unclassified_paths_build_in_full(self) -> None:
        self.assertFalse(self.docs_only(['newdirectory/notes.md']))
        self.assertFalse(self.docs_only(['docs.txt']))


class CommandTest(unittest.TestCase):
    def run_tool(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        tool: Path = Path(ci_docs_only.__file__)
        return subprocess.run([sys.executable, '-B', str(tool), *arguments], capture_output=True, text=True,
                              encoding='utf-8', check=False)

    def test_a_merge_group_prints_false(self) -> None:
        completed: subprocess.CompletedProcess[str] = self.run_tool('--event', 'merge_group')
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stdout.strip(), 'docs_only=false')

    def test_an_unreadable_base_prints_false(self) -> None:
        completed: subprocess.CompletedProcess[str] = self.run_tool(
            '--event', 'pull_request', '--base', '0000000000000000000000000000000000000000')
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stdout.strip(), 'docs_only=false')
        self.assertIn('built in full', completed.stderr)


if __name__ == '__main__':
    unittest.main()
