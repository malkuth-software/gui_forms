"""The forwarding shims at the moved tools' old paths reach template_harness beside this checkout.

Each shim, run, runs the harness's tool (the release and prune jobs call
them by these paths), and, imported, is the harness's module (as File
Manager and package_sdk.py import them). Without the harness, a shim
fails with a message naming the pinned commit.
"""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import harness_link

TOOLS: Path = Path(__file__).resolve().parent
# The tools that take no options: run with --help they reach the harness's
# code and fail there on the argument, which their traceback shows.
POSITIONAL_TOOLS: tuple[str, ...] = ('cache_manifest', 'compiler_identity', 'macos_runtime_manifest')


def run_python(arguments: list[str], environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        [sys.executable, '-B', *arguments], capture_output=True, text=True, check=False, env=environment)
    return completed


def without_harness_variables() -> dict[str, str]:
    environment: dict[str, str] = dict(os.environ)
    environment.pop('HARNESS_PROJECT', None)
    environment.pop('HARNESS_WORKSPACE', None)
    return environment


class ShimTests(unittest.TestCase):
    def test_every_shim_runs_the_harness_tool(self) -> None:
        name: str
        for name in harness_link.SHIMS:
            completed: subprocess.CompletedProcess[str] = run_python(
                [str(TOOLS / (name + '.py')), '--help'], without_harness_variables())
            if name in POSITIONAL_TOOLS:
                self.assertIn(str(harness_link.TOOLS / (name + '.py')), completed.stderr, name)
            else:
                self.assertEqual(completed.returncode, 0, name + ': ' + completed.stderr)
                self.assertIn('usage:', completed.stdout, name)

    def test_every_shim_imports_as_the_harness_module(self) -> None:
        name: str
        for name in harness_link.SHIMS:
            program: str = ('import sys; sys.path.insert(0, ' + repr(str(TOOLS)) + '); import ' + name
                            + '; print(' + name + '.__file__)')
            completed: subprocess.CompletedProcess[str] = run_python(['-c', program], without_harness_variables())
            self.assertEqual(completed.returncode, 0, name + ': ' + completed.stderr)
            self.assertEqual(Path(completed.stdout.strip()), harness_link.TOOLS / (name + '.py'), name)

    def test_every_moved_tool_has_its_shim(self) -> None:
        name: str
        for name in harness_link.SHIMS:
            self.assertTrue((harness_link.TOOLS / (name + '.py')).is_file(), name)
            text: str = (TOOLS / (name + '.py')).read_text(encoding='utf-8')
            self.assertIn("harness_link.forward_import('" + name + "')", text)
            self.assertIn("harness_link.run_tool('" + name + "')", text)

    def test_the_project_is_gui_forms_beside_this_checkout(self) -> None:
        program: str = ('import sys; sys.path.insert(0, ' + repr(str(TOOLS)) + '); import harness_link, project; '
                        'current = project.current(); print(current.name, current.source_root())')
        completed: subprocess.CompletedProcess[str] = run_python(['-c', program], without_harness_variables())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        name: str
        root: str
        name, root = completed.stdout.strip().split(' ', 1)
        self.assertEqual(name, 'gui_forms')
        self.assertEqual(Path(root), harness_link.SOURCE)

    @unittest.skipUnless(os.name == 'posix' and shutil.which('python3') is not None, 'the Linux package shim runs on Linux CI')
    def test_the_package_shim_runs_the_harness_script(self) -> None:
        with tempfile.TemporaryDirectory() as archives:
            completed: subprocess.CompletedProcess[str] = subprocess.run(
                ['sh', str(TOOLS / 'install_linux_ci_packages.sh'), 'no-such-profile', archives],
                capture_output=True, text=True, check=False, env=without_harness_variables())
        # The harness's script, through project.py, refuses an unknown profile with status 2.
        self.assertEqual(completed.returncode, 2, completed.stderr)


class MissingHarnessTests(unittest.TestCase):
    """A copy of this checkout's shims with no harness beside it."""

    def setUp(self) -> None:
        directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.tools: Path = Path(directory.name) / 'gui_forms' / 'tools'
        self.tools.mkdir(parents=True)
        shutil.copy2(TOOLS / 'harness_link.py', self.tools)
        shutil.copy2(TOOLS / 'ci_reuse.py', self.tools)
        shutil.copy2(TOOLS / 'install_linux_ci_packages.sh', self.tools)
        shutil.copy2(harness_link.LOCK_FILE, self.tools.parent)

    def test_a_shim_names_the_pinned_commit(self) -> None:
        completed: subprocess.CompletedProcess[str] = run_python([str(self.tools / 'ci_reuse.py'), '--help'])
        self.assertEqual(completed.returncode, 1)
        self.assertIn('template_harness is not beside this checkout', completed.stderr)
        self.assertIn(harness_link.pinned_commit(), completed.stderr)
        self.assertRegex(harness_link.pinned_commit(), '^[0-9a-f]{40}$')

    def test_the_source_sdk_build_entry_needs_no_harness(self) -> None:
        # A source SDK archive carries tools/ but no template_harness.
        shutil.copy2(TOOLS / 'build_sdk.py', self.tools)
        shutil.copy2(TOOLS / 'toolkit_configure.py', self.tools)
        completed: subprocess.CompletedProcess[str] = run_python([str(self.tools / 'build_sdk.py'), '--help'])
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn('usage:', completed.stdout)

    @unittest.skipUnless(shutil.which('sh') is not None, 'needs sh')
    def test_the_package_shim_names_the_pinned_commit(self) -> None:
        completed: subprocess.CompletedProcess[str] = subprocess.run(
            ['sh', str(self.tools / 'install_linux_ci_packages.sh'), 'toolkit', str(self.tools)],
            capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 1)
        self.assertIn('template_harness is not beside this checkout', completed.stderr)
        self.assertIn(harness_link.pinned_commit(), completed.stderr)


if __name__ == '__main__':
    unittest.main()
