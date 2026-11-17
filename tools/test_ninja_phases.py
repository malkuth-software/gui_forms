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

    def test_the_slowest_edges_are_listed_longest_first(self) -> None:
        text: str = ninja_phases.summary(ninja_phases.last_build(LOG), 'linux-x64')
        slowest_at: int = text.index('Slowest 6 edges:')
        tests_at: int = text.index('| CMakeFiles/gui_forms_text_tests.dir/tests/text_tests.cpp.obj | 3.0 | 0.0 |')
        core_at: int = text.index('| CMakeFiles/gui_forms_core.dir/src/core/a.cpp.obj | 1.0 | 0.0 |')
        self.assertLess(slowest_at, tests_at)
        self.assertLess(tests_at, core_at)
        self.assertIn('No quiet stretch', text)

    def test_a_quiet_stretch_names_the_edges_that_ran_through_it(self) -> None:
        log: str = '\n'.join([
            '# ninja log v6',
            '0\t1000\t1\ta.obj\tx',
            '500\t400000\t1\tstuck.pch\tx',
            '600\t401000\t1\talso_stuck.obj\tx',
            '401000\t402000\t1\tlast.exe\tx',
        ]) + '\n'
        edges: list[ninja_phases.Edge] = ninja_phases.last_build(log)
        stretches: list[ninja_phases.QuietStretch] = ninja_phases.quiet_stretches(edges, 120000)
        self.assertEqual(len(stretches), 1)
        self.assertEqual(stretches[0].start, 1000)
        self.assertEqual(stretches[0].end, 400000)
        names: list[str] = []
        edge: ninja_phases.Edge
        for edge in stretches[0].running:
            names.append(edge.output)
        self.assertEqual(names, ['stuck.pch', 'also_stuck.obj'])
        text: str = ninja_phases.summary(edges, 'windows-x64')
        self.assertIn('| 1.0 | 6.7 | stuck.pch, also_stuck.obj |', text)

    def test_samples_line_up_with_the_quiet_stretch(self) -> None:
        log: str = '\n'.join([
            '# ninja log v6',
            '0\t1000\t1\ta.obj\tx',
            '500\t400000\t1\tstuck.pch\tx',
            '401000\t402000\t1\tlast.exe\tx',
        ]) + '\n'
        # The last edge ends at wall clock 10_402_000 ms, so ninja's 0 is 10_000_000.
        header: str = '\t'.join(ninja_phases.SAMPLE_COLUMNS)
        samples_text: str = '\n'.join([
            '\ufeff' + header,
            't\t10000500\t9000\t8000\t20000\t100\t0\t50\t5\tclang:1:900\tclang:1:20.0',
            't\t10100000\t300\t21000\t22000\t90000\t4000\t30\t100\tclang:2:2000,ld:3:1500\tSystem:4:1.0',
            't\t10200000\terror\t\t\t\t\t\t\t\tcounter missing',
            't\t10300000\t700\t20500\t22000\t50000\t2500\t30\t100\tclang:2:1900\tMsMpEng:5:25.0',
            't\t10450000\t9000\t8000\t20000\t100\t0\t50\t5\tctest:6:50\tctest:6:1.0',
        ]) + '\n'
        samples: list[ninja_phases.ResourceSample] = ninja_phases.read_samples(samples_text)
        self.assertEqual(len(samples), 4)
        text: str = ninja_phases.summary(ninja_phases.last_build(log), 'windows-x64', samples, 10402000)
        self.assertIn('| 1.0 | 2 | 300 | 21000 / 22000 | 90000 | 4000 | clang:2:2000,ld:3:1500 | System:4:1.0 |',
                      text)

    def test_samples_without_a_clock_say_so(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            (Path(scratch) / '.ninja_log').write_text(
                '# ninja log v6\n0\t1000\t1\ta.obj\tx\n1000\t200000\t1\tb.obj\tx\n', encoding='utf-8')
            samples: Path = Path(scratch) / 'samples.tsv'
            samples.write_text('\t'.join(ninja_phases.SAMPLE_COLUMNS) + '\n', encoding='utf-8')
            out: Path = Path(scratch) / 'summary.md'
            self.assertEqual(ninja_phases.main([scratch, '--samples', str(samples), '--out', str(out)]), 0)
            self.assertIn('no build-clock.json', out.read_text(encoding='utf-8'))

    def test_a_tree_without_a_log_says_nothing_was_built(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            out: Path = Path(scratch) / 'summary.md'
            self.assertEqual(ninja_phases.main([scratch, '--title', 'empty', '--out', str(out)]), 0)
            self.assertIn('nothing was built', out.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
