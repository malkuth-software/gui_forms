#!/usr/bin/env python3
"""Build GUI.Forms under ThreadSanitizer and run the C ABI's verification set and the event stack against it (stage C8, gap P2).

The C ABI promises release from any thread, deferred destruction on the UI
thread, begin_invoke from any thread and the conformance kit's thread
handover (gf_abi.h, "Threads"); ThreadSanitizer checks those promises where
the toolchain has it: Linux and macOS. The MinGW runtime on Windows has no
ThreadSanitizer, so this tool refuses there.

The tests live in malkuth-software/gui_forms_tests and build against a test
cache (tools/test_cache.py). This tool makes the instrumented half: it
configures its own tree, ``.build/native-<host>-<arch>/gui-forms-tsan``,
with the toolkit's ordinary options for the test cache
(tools/toolkit_configure.py), ``-fsanitize=thread`` and Release at
``-O1 -gline-tables-only``, builds it whole,
and packs it as the ``tsan`` kind into ``gui-forms-tsan-cache`` beside the
tree. It then hands the archive to gui_forms_tests' tools/abi_tsan.py,
which builds the selected test programs against it and runs them with
every report fatal:

    python3 tools/abi_tsan.py [--tests-checkout DIR] [--pack-only] [--set all|c_abi|tsan]
                              [--jobs N] [--tests PATTERN] [--skip-labels PATTERN]

The gui_forms_tests checkout is --tests-checkout, by default
``gui_forms_tests`` beside this checkout. --pack-only stops after packing
and prints the command that runs the tests. --set, --tests and
--skip-labels pass through to the tests' tool (its docstring has the
selection: the tests labelled ``c_abi`` and ``tsan``).

As with tools/ci_build.py --sanitize on macOS, the flags reach the tree
through CFLAGS, CXXFLAGS, OBJCXXFLAGS and LDFLAGS, which CMake reads at the
first configure only (harness/cmake/llvm22.cmake puts the pinned libc++ in
the _INIT flags a -DCMAKE_<LANG>_FLAGS would replace): a tree configured
earlier without them must be deleted first, and the tool refuses one whose
cache lacks -fsanitize=thread.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import sys

import harness_link  # noqa: F401  (puts the harness's tools on sys.path)
from ci_build_steps import configure_tree, run
from toolkit_configure import host_options, prepare_host, toolkit_options

ROOT: Path = Path(__file__).resolve().parents[2]
TSAN_FLAGS: str = '-fsanitize=thread -fno-omit-frame-pointer -fdebug-compilation-dir=.'
TSAN_LINK_FLAGS: str = '-fsanitize=thread'
BUILD_TYPE_FLAGS: str = '-O1 -gline-tables-only -DNDEBUG'
CACHE_KIND: str = 'tsan'


def tsan_build_environment() -> dict[str, str]:
    """The configure and build environment: the flags only (the tests' tool
    sets TSAN_OPTIONS for the run)."""
    return {
        'CFLAGS': TSAN_FLAGS,
        'CXXFLAGS': TSAN_FLAGS,
        'OBJCXXFLAGS': TSAN_FLAGS,
        'LDFLAGS': TSAN_LINK_FLAGS,
    }


def require_instrumented(tree: Path) -> None:
    """Refuse a tree whose first configure did not see the TSan flags."""
    cache: Path = tree / 'CMakeCache.txt'
    if not cache.is_file():
        return
    text: str = cache.read_text(encoding='utf-8', errors='replace')
    if '-fsanitize=thread' not in text:
        raise SystemExit(f'abi_tsan: {tree} was configured without -fsanitize=thread; delete it and run again')


def archive_path(output: Path, platform_label: str) -> Path:
    """Where tools/test_cache.py pack writes the tsan cache for platform_label."""
    return output / f'gui_forms_test_cache-{platform_label}-{CACHE_KIND}.tar.gz'


def tests_command(tests_checkout: Path, archive: Path, arguments: argparse.Namespace) -> list[str]:
    """The gui_forms_tests command that builds and runs the tests against archive."""
    command: list[str] = [sys.executable, '-B', str(tests_checkout / 'tools/abi_tsan.py'),
                          '--archive', str(archive), '--set', arguments.set, '--jobs', str(arguments.jobs)]
    if arguments.tests:
        command.extend(['--tests', arguments.tests])
    if arguments.skip_labels:
        command.extend(['--skip-labels', arguments.skip_labels])
    return command


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--tests-checkout', type=Path, default=ROOT / 'gui_forms_tests',
                        help='the gui_forms_tests checkout (default: beside this checkout)')
    parser.add_argument('--pack-only', action='store_true',
                        help='build and pack only; print the command that runs the tests')
    parser.add_argument('--set', choices=('all', 'c_abi', 'tsan'), default='all',
                        help='the labelled tests to run: c_abi, tsan, or both (all)')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--tests', default='', help='narrow the selected set with this ctest -R pattern')
    parser.add_argument('--skip-labels', default='', help='leave out tests with labels matching this ctest -LE pattern')
    args: argparse.Namespace = parser.parse_args()
    hosts: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux'}
    system: str = platform.system()
    if system not in hosts:
        print(f'abi_tsan: ThreadSanitizer runs on Linux and macOS only, not {system}', file=sys.stderr)
        return 2
    tests_checkout: Path = args.tests_checkout.resolve()
    if not args.pack_only and not (tests_checkout / 'tools/abi_tsan.py').is_file():
        print(f'abi_tsan: no gui_forms_tests checkout at {tests_checkout} (pass --tests-checkout, '
              'or --pack-only)', file=sys.stderr)
        return 2
    host: str = hosts[system]
    architecture: str = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    platform_label: str = host + '-' + architecture
    # By its real path, as ci_build.py builds (ccache base_dir and symlinked trees).
    build: Path = (ROOT / '.build').resolve() / ('native-' + platform_label)
    tree: Path = build / 'gui-forms-tsan'
    output: Path = build / 'gui-forms-tsan-cache'
    build.mkdir(parents=True, exist_ok=True)
    require_instrumented(tree)

    os.environ.update(tsan_build_environment())
    prepare_host(ROOT / 'gui_forms', True)
    # Release, as toolkit_options sets, with the instrumented optimisation
    # level in its flags: the test cache records CMAKE_<LANG>_FLAGS_RELEASE
    # (cmake/TestCache.cmake), so the test programs compile alike. No
    # link-time optimisation, as for every test cache tree (ci_build.py).
    options: list[str] = [*host_options(host), '-DGUI_FORMS_LTO=OFF',
                          '-DCMAKE_C_FLAGS_RELEASE=' + BUILD_TYPE_FLAGS,
                          '-DCMAKE_CXX_FLAGS_RELEASE=' + BUILD_TYPE_FLAGS]
    if host == 'macos':
        options.append('-DCMAKE_OBJCXX_FLAGS_RELEASE=' + BUILD_TYPE_FLAGS)
    configure: list[str] = ['cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(tree), '-G', 'Ninja',
                            *toolkit_options(ROOT / 'gui_forms', build / 'gui-forms-sdk', True), *options]
    configure_tree(tree, configure)
    require_instrumented(tree)
    # The tests link the toolkit's libraries, its generated headers and its
    # programs, so the tree is built whole, as for any test cache.
    run('cmake', '--build', tree, '--parallel', args.jobs)
    run(sys.executable, '-B', ROOT / 'gui_forms/tools/test_cache.py', 'pack', '--build-dir', tree,
        '--platform', platform_label, '--kind', CACHE_KIND, '--output', output)
    command: list[str] = tests_command(tests_checkout, archive_path(output, platform_label), args)
    if args.pack_only:
        print('abi_tsan: packed; run the tests with:\n    ' + ' '.join(command), flush=True)
        return 0
    run(*command)
    return 0


if __name__ == '__main__':
    sys.exit(main())
