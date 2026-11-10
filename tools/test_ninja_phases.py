#!/usr/bin/env python3
"""Tests of tools/ninja_phases.py: a build's time by phase, from its ninja log."""
from __future__ import annotations
from pathlib import Path
import tempfile
import unittest

import ninja_phases

LOG: str = '\n'.join([
    '# ninja log v6',
    '0\t9000\t1\told.obj\tx',
    '500\t700\t1\t_deps/gui_forms_stx-src/stx_skia/b.cpp.obj\tx',
    '0\t1000\t1\tCMakeFiles/gui_forms_core.dir/src/core/a.cpp.obj\tx',
    '1000\t1200\t1\tlibgui_forms_core.a\tx',
    '0\t3000\t1\tCMakeFiles/gui_forms_text_tests.dir/tests/text_tests.cpp.obj\tx',
    '3000\t3500\t1\tgui_forms_text_tests.exe\tx',
    '3500\t3600\t1\tgf_abi/schema.json\tx',
]) + '\n'


class PhaseTest(unittest.TestCase):
    def test_the_last_build_is_split_by_phase(self) -> None:
        edges: list[ninja_phases.Edge] = ninja_phases.last_build(LOG)
        outputs: list[str] = []
        edge: ninja_phases.Edge
        for edge in edges:
            outputs.append(edge.output)
        self.assertNotIn('old.obj', outputs)
        text: str = ninja_phases.summary(edges, 'linux-x64')
        self.assertIn('6 edges; wall 0.1 min', text)
        self.assertIn('| test compiles | 1 | 0.1 |', text)
        self.assertIn('| stx or third-party compiles | 1 |', text)
        self.assertIn('| test links | 1 |', text)
        self.assertIn('| other links | 1 |', text)
        self.assertIn('| other edges | 1 |', text)

    def test_a_tree_without_a_log_says_nothing_was_built(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out: Path = Path(scratch) / 'summary.md'
            self.assertEqual(ninja_phases.main([scratch, '--title', 'empty', '--out', str(out)]), 0)
            self.assertIn('nothing was built', out.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
