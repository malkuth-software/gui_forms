"""Unit tests of GUI.Forms' own parts of ci_build.py (no build is run).

The product-independent steps it imports (sanitizer options, configure
records) are tested with them in template_harness
(harness/test_ci_build_steps.py)."""
from __future__ import annotations
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

import ci_build
import ci_build_steps


class GalleryOptionTests(unittest.TestCase):
    def test_default_is_off(self) -> None:
        self.assertFalse(ci_build.gallery_enabled(False, {}))

    def test_flag_turns_gallery_on(self) -> None:
        self.assertTrue(ci_build.gallery_enabled(True, {}))

    def test_environment_turns_gallery_on(self) -> None:
        value: str
        for value in ('1', 'true', 'ON', ' yes '):
            self.assertTrue(ci_build.gallery_enabled(False, {'GUI_FORMS_CI_BUILD_GALLERY': value}), value)

    def test_environment_other_values_keep_default(self) -> None:
        value: str
        for value in ('', '0', 'false', 'off', 'maybe'):
            self.assertFalse(ci_build.gallery_enabled(False, {'GUI_FORMS_CI_BUILD_GALLERY': value}), value)


CHECKOUT: Path = Path(__file__).resolve().parents[1]
FIRST_PARTY_DIRECTORIES: tuple[str, ...] = ('include', 'src', 'demo', 'examples',
                                            'file_manager_demoboard')
SOURCE_SUFFIXES: tuple[str, ...] = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inc', '.m', '.mm')


def is_module_rule(pattern: str) -> bool:
    """Whether a suppression names a library (a path or a library file) rather than a function."""
    return (pattern.startswith('/') or '.so' in pattern or pattern.endswith('.dylib')
            or '.framework' in pattern)


def function_rules(suppressions: str) -> list[str]:
    """The function-name patterns among a LeakSanitizer suppression file's leak: rules."""
    rules: list[str] = []
    line: str
    for line in suppressions.splitlines():
        stripped: str = line.strip()
        if not stripped.startswith('leak:'):
            continue
        pattern: str = stripped[len('leak:'):]
        if not is_module_rule(pattern):
            rules.append(pattern)
    return rules


def rule_expression(pattern: str) -> re.Pattern[str]:
    """LeakSanitizer matches a pattern anywhere in a frame's name, with * as a wildcard."""
    pieces: list[str] = []
    piece: str
    for piece in pattern.split('*'):
        pieces.append(re.escape(piece))
    return re.compile('.*'.join(pieces))


class CheckoutFileTests(unittest.TestCase):
    """The GUI.Forms file the shared steps read: the sanitizer job."""

    def test_the_sanitizer_job_sets_the_variable_this_tool_reads(self) -> None:
        workflow: str = (CHECKOUT / '.github/workflows/native.yml').read_text(encoding='utf-8')
        self.assertIn(ci_build.DEBUG_INFO_VARIABLE + '=$mode', workflow)
        self.assertIn('gui-sanitize-v1-${{ steps.debug-info.outputs.mode }}-', workflow)


class LeakSuppressionTests(unittest.TestCase):
    def test_no_function_rule_can_match_a_first_party_frame(self) -> None:
        # A rule suppresses a leak when it matches any frame of the allocation
        # stack, so a system function pattern that also occurs in first-party
        # source would hide our leaks there (house style section 1: never
        # suppress a GUI.Forms leak). Comments count too: the check is textual.
        suppressions: str = (CHECKOUT / 'tools/sanitizer/lsan.supp').read_text(encoding='utf-8')
        rules: list[str] = function_rules(suppressions)
        self.assertTrue(rules)
        expressions: list[tuple[str, re.Pattern[str]]] = []
        rule: str
        for rule in rules:
            expressions.append((rule, rule_expression(rule)))
        collisions: list[str] = []
        directory: str
        for directory in FIRST_PARTY_DIRECTORIES:
            source: Path
            for source in sorted((CHECKOUT / directory).rglob('*')):
                if source.suffix not in SOURCE_SUFFIXES or not source.is_file():
                    continue
                text: str = source.read_text(encoding='utf-8', errors='replace')
                expression: tuple[str, re.Pattern[str]]
                for expression in expressions:
                    if expression[1].search(text) is not None:
                        collisions.append(f'{source.relative_to(CHECKOUT)}: {expression[0]}')
        self.assertEqual(collisions, [])

    def test_module_and_function_rules_are_told_apart(self) -> None:
        lines: list[str] = ['# note', 'leak:libX11.so', 'leak:/ColorSync.framework/',
                            'leak:/libxpc.dylib', 'leak:fetch_cache', 'leak:-[NSThread init]']
        rules: list[str] = function_rules('\n'.join(lines))
        self.assertEqual(rules, ['fetch_cache', '-[NSThread init]'])
        self.assertIsNotNone(rule_expression('CA::Transaction::create').search('x CA::Transaction::create y'))
        self.assertIsNotNone(rule_expression('a*b').search('xa1by'))



class BuildRecordTests(unittest.TestCase):
    """The record beside the tree says what a call built (tools/stack_frames.py reads it)."""

    def test_a_call_that_fails_leaves_no_earlier_record(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            tree: Path = build / 'gui-forms-sanitize'
            ci_build.write_build_record(tree, ci_build.BUILT_EVERYTHING, 'an earlier run')
            with mock.patch.object(ci_build, 'prepare_host'), \
                    mock.patch.object(ci_build, 'sanitizer_options', return_value=([], {})), \
                    mock.patch.object(ci_build, 'configure_tree', side_effect=RuntimeError('configure failed')):
                with self.assertRaises(RuntimeError):
                    ci_build.build_toolkit('linux', build, build / 'sdk', 1, sanitize=True)
            self.assertFalse(ci_build.build_record_path(tree).exists())


def recorder_of_builds(commands: list[list[str]]):
    """A stand-in for ci_build.build_logged that records the build it would
    run as the same `cmake --build` command, so the tests see builds and
    other commands in one list."""

    def record_build(toolkit: Path, sanitize: bool, *arguments: object) -> None:
        commands.append(['cmake', '--build', str(toolkit), *[str(argument) for argument in arguments]])

    return record_build


class InstallOnlyTests(unittest.TestCase):
    """The tree is built, installed and smoke-checked; the test cache is packed
    unless --install-only. No test of the toolkit runs here (gui_forms_tests)."""

    def commands_of(self, install_only: bool, test_cache: Path | None) -> list[list[str]]:
        commands: list[list[str]] = []

        def record(*arguments: object, **keywords: object) -> None:
            commands.append([str(argument) for argument in arguments])

        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            with mock.patch.object(ci_build, 'run', side_effect=record), \
                    mock.patch.object(ci_build, 'build_logged', side_effect=recorder_of_builds(commands)), \
                    mock.patch.object(ci_build, 'configure_tree'), \
                    mock.patch.object(ci_build, 'prepare_host'), \
                    mock.patch.object(ci_build, 'check_installed_notices'), \
                    mock.patch.object(ci_build, 'check_windows_sdk_imports'):
                ci_build.build_toolkit('linux', build, build / 'sdk', 1, install_only=install_only,
                                       test_cache=test_cache, platform_label='linux-x64')
            toolkit: str = str(build / 'gui-forms')
            self.assertIn(['cmake', '--install', toolkit], commands)
        return commands

    def packs(self, commands: list[list[str]]) -> list[list[str]]:
        found: list[list[str]] = []
        command: list[str]
        for command in commands:
            if len(command) > 3 and command[2].endswith('test_cache.py') and command[3] == 'pack':
                found.append(command)
        return found

    def test_no_toolkit_test_runs_and_the_examples_do(self) -> None:
        commands: list[list[str]] = self.commands_of(False, Path('cache'))
        ctests: list[list[str]] = []
        command: list[str]
        for command in commands:
            if command[:1] == ['ctest']:
                ctests.append(command)
        self.assertEqual(len(ctests), 1)
        self.assertTrue(ctests[0][2].endswith('installed-reference-examples'))

    def test_the_test_cache_is_packed_unless_install_only(self) -> None:
        packed: list[list[str]] = self.packs(self.commands_of(False, Path('cache')))
        self.assertEqual(len(packed), 1)
        self.assertIn('linux-x64', packed[0])
        self.assertEqual(self.packs(self.commands_of(True, Path('cache'))), [])
        self.assertEqual(self.packs(self.commands_of(False, None)), [])

    def test_install_only_packs_nothing(self) -> None:
        option: list[str]
        for option in (['--sanitize'], ['--test-cache', 'x']):
            with mock.patch.object(sys, 'argv', ['ci_build.py', '--install-only', *option]), \
                    mock.patch('sys.stderr'):
                with self.assertRaises(SystemExit):
                    ci_build.main()


class KeepGoingBuildTests(unittest.TestCase):
    """A build keeps going past a failed command (ninja -k 0) and a failed
    one ends with its distinct error lines, so one CI run reports every
    compile error (#483: one miss per run before)."""

    def test_a_failed_build_keeps_going_and_lists_each_error_once(self) -> None:
        output: str = ('[1/3] Building a.cpp\n'
                       'src/a.cpp:3:1: error: one\n'
                       'src/a.cpp:3:1: error: one\n'
                       'src/b.cpp:9:2: error: two\n'
                       'ninja: build stopped: cannot make progress due to previous errors.\n')
        commands: list[list[str]] = []

        class FailedBuild:
            def __init__(self, command: list[str], **unused: object) -> None:
                commands.append(command)
                self.stdout = io.StringIO(output)

            def wait(self) -> int:
                return 1

        printed: io.StringIO = io.StringIO()
        with tempfile.TemporaryDirectory() as scratch:
            with mock.patch.object(ci_build.subprocess, 'Popen', FailedBuild), \
                    mock.patch.object(sys, 'stdout', printed):
                with self.assertRaises(ci_build.subprocess.CalledProcessError):
                    ci_build.build_logged(Path(scratch), False, '--parallel', '4')
        self.assertEqual(commands[0][-3:], ['--', '-k', '0'])
        summary: str = printed.getvalue().split('build failed: ')[1]
        self.assertTrue(summary.startswith('2 distinct error lines'))
        self.assertEqual(summary.count('src/a.cpp:3:1: error: one'), 1)
        self.assertIn('src/b.cpp:9:2: error: two', summary)

    def test_a_logged_build_records_its_clock_even_when_it_fails(self) -> None:
        class FailedBuild:
            def __init__(self, command: list[str], **unused: object) -> None:
                self.stdout = io.StringIO('[1/1] Building CXX object a.cpp.obj\nsrc/a.cpp:1:1: error: one\n')

            def wait(self) -> int:
                return 1

        printed: io.StringIO = io.StringIO()
        with tempfile.TemporaryDirectory() as scratch:
            with mock.patch.object(ci_build.subprocess, 'Popen', FailedBuild), \
                    mock.patch.object(sys, 'stdout', printed):
                with self.assertRaises(ci_build.subprocess.CalledProcessError):
                    ci_build.build_logged(Path(scratch), False, '--parallel', '4')
            clock: dict = json.loads((Path(scratch) / ci_build.BUILD_CLOCK_NAME).read_text(encoding='utf-8'))
        self.assertGreater(clock['started_ms'], 1700000000000)
        self.assertGreaterEqual(clock['finished_ms'], clock['started_ms'])


if __name__ == '__main__':
    unittest.main()
