"""Unit tests of GUI.Forms' own parts of ci_build.py (no build is run).

The product-independent steps it imports (sanitizer options, test selection,
configure records) are tested with them in template_harness
(harness/test_ci_build_steps.py)."""
from __future__ import annotations
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
FIRST_PARTY_DIRECTORIES: tuple[str, ...] = ('include', 'src', 'demo', 'tests', 'examples',
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
    """The GUI.Forms files the shared steps read: the sanitizer job and the test labels."""

    def test_the_sanitizer_job_sets_the_variable_this_tool_reads(self) -> None:
        workflow: str = (CHECKOUT / '.github/workflows/native.yml').read_text(encoding='utf-8')
        self.assertIn(ci_build.DEBUG_INFO_VARIABLE + '=$mode', workflow)
        self.assertIn('gui-sanitize-v1-${{ steps.debug-info.outputs.mode }}-', workflow)


    def test_the_gallery_accessibility_tests_carry_the_label(self) -> None:
        cmake: str = (CHECKOUT / 'CMakeLists.txt').read_text(encoding='utf-8')
        name: str
        for name in ('gui_forms_macos_accessibility_client_gallery_tests', 'gui_forms_linux_accessibility_gallery_tests',
                     'gui_forms_windows_accessibility_msaa_gallery_tests',
                     'gui_forms_windows_accessibility_uia_gallery_tests'):
            self.assertIn('set_property(TEST ' + name + ' APPEND PROPERTY LABELS gallery_accessibility)', cmake)


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



class UnreadChanges:
    """Stands in for change_selection: a documentation-only change, which no test reads."""

    def changed_paths(self, root: Path, base: str) -> list[str]:
        return ['docs/index.md']

    def any_path_read(self, changed: list[str], rules: object) -> bool:
        return False

    def load_rules(self) -> list[object]:
        """No rules: the real ones live in <workspace>/gui_forms, which a
        checkout under another directory name does not have."""
        return []

    def failing_changed_paths(self, root: Path, base: str) -> list[str]:
        raise RuntimeError('git failed')


class BuildRecordTests(unittest.TestCase):
    """The record beside the tree says what a call built (tools/stack_frames.py reads it)."""

    def call_with(self, changed_paths: object, build: Path) -> None:
        fake: UnreadChanges = UnreadChanges()
        original_changed: object = ci_build.change_selection.changed_paths
        original_read: object = ci_build.change_selection.any_path_read
        original_rules: object = ci_build.change_selection.load_rules
        ci_build.change_selection.changed_paths = changed_paths
        ci_build.change_selection.any_path_read = fake.any_path_read
        ci_build.change_selection.load_rules = fake.load_rules
        try:
            ci_build.build_toolkit('linux', build, build / 'sdk', 1, sanitize=True, changed_from='origin/main')
        finally:
            ci_build.change_selection.changed_paths = original_changed
            ci_build.change_selection.any_path_read = original_read
            ci_build.change_selection.load_rules = original_rules

    def test_a_change_no_test_reads_records_that_nothing_was_built(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            self.call_with(UnreadChanges().changed_paths, build)
            tree: Path = build / 'gui-forms-sanitize'
            self.assertFalse(tree.exists())
            record: dict = json.loads(ci_build.build_record_path(tree).read_text(encoding='utf-8'))
            self.assertEqual(record['built'], ci_build.BUILT_NOTHING)
            self.assertIn('no test reads', record['reason'])

    def test_a_call_that_fails_leaves_no_earlier_record(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            tree: Path = build / 'gui-forms-sanitize'
            ci_build.write_build_record(tree, ci_build.BUILT_NOTHING, 'an earlier run')
            with self.assertRaises(RuntimeError):
                self.call_with(UnreadChanges().failing_changed_paths, build)
            self.assertFalse(ci_build.build_record_path(tree).exists())


class InstallOnlyTests(unittest.TestCase):
    """--install-only builds and installs the SDK and runs none of the toolkit's tests."""

    def commands_of(self, install_only: bool) -> list[list[str]]:
        commands: list[list[str]] = []

        def record(*arguments: object, **keywords: object) -> None:
            commands.append([str(argument) for argument in arguments])

        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            with mock.patch.object(ci_build, 'run', side_effect=record), \
                    mock.patch.object(ci_build, 'configure_tree'), \
                    mock.patch.object(ci_build, 'prepare_host'), \
                    mock.patch.object(ci_build, 'check_installed_notices'), \
                    mock.patch.object(ci_build, 'check_windows_sdk_imports'):
                ci_build.build_toolkit('linux', build, build / 'sdk', 1, install_only=install_only)
            toolkit: str = str(build / 'gui-forms')
            self.assertIn(['cmake', '--install', toolkit], commands)
        return commands

    def toolkit_tests(self, commands: list[list[str]]) -> list[list[str]]:
        found: list[list[str]] = []
        command: list[str]
        for command in commands:
            if command[:1] == ['ctest'] and command[2].endswith('gui-forms'):
                found.append(command)
        return found

    def test_install_only_runs_no_toolkit_test(self) -> None:
        self.assertEqual(self.toolkit_tests(self.commands_of(True)), [])
        self.assertEqual(len(self.toolkit_tests(self.commands_of(False))), 1)

    def test_a_pull_request_runs_the_affected_tests_and_the_smoke_set(self) -> None:
        listing: dict = {'tests': [{'name': 'gui_forms_engine_gate_tests'}, {'name': 'gui_forms_threading_tests'},
                                   {'name': 'gui_forms_text_tests'}, {'name': 'gui_forms_tab_tests'}]}
        affected: dict = {'tests': [{'name': 'gui_forms_text_tests'}]}
        chosen: dict
        with mock.patch.object(ci_build, 'changed_tests_listing', return_value=affected):
            chosen = ci_build.pull_request_listing(listing, ['src/core/text/x.cpp'])
        names: list[str] = []
        test: dict
        for test in chosen['tests']:
            names.append(test['name'])
        self.assertEqual(names, ['gui_forms_engine_gate_tests', 'gui_forms_threading_tests', 'gui_forms_text_tests'])
        # A path every test reads, and no change selection at all, keep the whole suite.
        with mock.patch.object(ci_build, 'changed_tests_listing', return_value=None):
            self.assertEqual(ci_build.pull_request_listing(listing, ['CMakeLists.txt']), listing)
        self.assertEqual(ci_build.pull_request_listing(listing, None), listing)

    def test_a_pull_request_may_narrow_by_change_but_not_by_pattern(self) -> None:
        with mock.patch.object(sys, 'argv', ['ci_build.py', '--test-targets-only', '--tests', 'x']), \
                mock.patch('sys.stderr'):
            with self.assertRaises(SystemExit):
                ci_build.main()
        with mock.patch.object(sys, 'argv', ['ci_build.py', '--test-targets-only', '--changed-from', 'HEAD^1']), \
                mock.patch.object(ci_build, 'build_toolkit') as build:
            ci_build.main()
        self.assertEqual(build.call_args.args[10], 'HEAD^1')
        self.assertTrue(build.call_args.args[12])

    def test_a_pull_request_builds_the_tests_targets_and_installs_nothing(self) -> None:
        commands: list[list[str]] = []

        def record(*arguments: object, **keywords: object) -> None:
            commands.append([str(argument) for argument in arguments])

        with tempfile.TemporaryDirectory() as scratch:
            build: Path = Path(scratch)
            toolkit: Path = build / 'gui-forms'
            listing: dict = {'tests': [
                {'name': 'a', 'command': [str(toolkit / 'a_tests'), str(toolkit / '_deps/x-src/corpus')]},
                {'name': 'b', 'command': ['cmake', '-DLIBRARY=' + str(toolkit / 'libabi.so'), '-P', 'x.cmake']}]}
            outputs: dict[str, str] = {'a_tests': 'CXX_EXECUTABLE_LINKER__a_tests_',
                                       'gallery': 'CXX_EXECUTABLE_LINKER__gallery_',
                                       'libabi.so': 'CXX_SHARED_LIBRARY_LINKER__abi_',
                                       'libplugin.so': 'CXX_MODULE_LIBRARY_LINKER__plugin_',
                                       'package': 'phony'}
            with mock.patch.object(ci_build, 'run', side_effect=record), \
                    mock.patch.object(ci_build, 'configure_tree'), \
                    mock.patch.object(ci_build, 'prepare_host'), \
                    mock.patch.object(ci_build, 'test_listing', return_value=listing), \
                    mock.patch.object(ci_build, 'ninja_outputs', return_value=outputs):
                ci_build.build_toolkit('linux', build, build / 'sdk', 1, test_targets_only=True)
        self.assertIn(['cmake', '--build', str(toolkit), '--parallel', '1', '--target',
                       'a_tests', 'libabi.so', 'libplugin.so'], commands)
        self.assertEqual(len(self.toolkit_tests(commands)), 1)
        self.assertIn('--output-junit', self.toolkit_tests(commands)[0])
        command: list[str]
        for command in commands:
            self.assertNotIn('--install', command)
            self.assertNotIn('gallery', command)

    def test_install_only_takes_no_narrowing(self) -> None:
        option: list[str]
        for option in (['--sanitize'], ['--tests', 'x'], ['--skip-labels', 'y']):
            with mock.patch.object(sys, 'argv', ['ci_build.py', '--install-only', *option]), \
                    mock.patch('sys.stderr'):
                with self.assertRaises(SystemExit):
                    ci_build.main()


if __name__ == '__main__':
    unittest.main()
