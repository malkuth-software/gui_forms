#!/usr/bin/env python3
"""Build GUI.Forms with frame pointers and pack it as a profile test cache (Phase E, E5).

The recorded workloads and the hot-path profiler live in
malkuth-software/gui_forms_tests (benchmarks/, tools/profile_hot_paths.py)
and build against a test cache (tools/test_cache.py). This tool makes the
profiled half: it configures its own tree,
``.build/native-<host>-<arch>/gui-forms-profile``, with the toolkit's
ordinary options for the test cache (tools/toolkit_configure.py) at Release
plus frame pointers and line tables, so that perf walks GUI.Forms' own stacks
(and stx_skia's, built in the same tree) and names their functions. It
builds the tree whole and packs it as the ``profile`` kind into
``gui-forms-profile-cache`` beside the tree, then hands the archive to
gui_forms_tests' tools/profile_hot_paths.py:

    python3 tools/profile_cache.py [--tests-checkout DIR] [--pack-only] [--output DIR] [--jobs N]

The gui_forms_tests checkout is --tests-checkout, by default
``gui_forms_tests`` beside this checkout. --pack-only stops after packing
and prints the command that profiles; --output is the profiler's output
directory (hot-paths.json, hot-paths.md and each workload's listings).

The profile measures the toolkit as it ships, so the tree is not the
reference-arithmetic build the ordinary test cache is
(GUI_FORMS_REFERENCE_ARITHMETIC off). The test cache records
CMAKE_<LANG>_FLAGS_RELEASE (cmake/TestCache.cmake), so the workloads compile
with the same flags.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import platform
import sys

import harness_link  # noqa: F401  (puts the harness's tools on sys.path)
from ci_build_steps import configure_tree, run
from toolkit_configure import host_options, prepare_host, toolkit_options

ROOT: Path = Path(__file__).resolve().parents[2]
# Frame pointers let perf walk the stacks (inclusive shares); line tables
# name the functions.
PROFILE_FLAGS: str = '-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -gline-tables-only'
# CMake's own Release flags for clang, which a CMAKE_<LANG>_FLAGS_RELEASE
# option replaces, so they are repeated.
RELEASE_FLAGS: str = '-O3 -DNDEBUG'
CACHE_KIND: str = 'profile'


def release_flag_options(host: str) -> list[str]:
    """The options that add PROFILE_FLAGS to the Release build.

    They go in the build-type flags, not CMAKE_<LANG>_FLAGS: an option for
    the latter replaces the toolchain's CMAKE_<LANG>_FLAGS_INIT, which
    cmake/llvm22.cmake uses on macOS to select the pinned libc++ runtime.
    """
    languages: list[str] = ['C', 'CXX']
    if host == 'macos':
        languages.append('OBJCXX')
    options: list[str] = []
    language: str
    for language in languages:
        options.append(f'-DCMAKE_{language}_FLAGS_RELEASE={RELEASE_FLAGS} {PROFILE_FLAGS}')
    return options


def archive_path(output: Path, platform_label: str) -> Path:
    """Where tools/test_cache.py pack writes the profile cache for platform_label."""
    return output / f'gui_forms_test_cache-{platform_label}-{CACHE_KIND}.tar.gz'


def tests_command(tests_checkout: Path, archive: Path, output: Path, jobs: int) -> list[str]:
    """The gui_forms_tests command that builds the workloads against archive and profiles them."""
    return [sys.executable, '-B', str(tests_checkout / 'tools/profile_hot_paths.py'), 'run',
            '--archive', str(archive), '--output', str(output), '--jobs', str(jobs)]


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--tests-checkout', type=Path, default=ROOT / 'gui_forms_tests',
                        help='the gui_forms_tests checkout (default: beside this checkout)')
    parser.add_argument('--pack-only', action='store_true',
                        help='build and pack only; print the command that profiles')
    parser.add_argument('--output', type=Path, default=ROOT / 'hot-paths',
                        help="the profiler's output directory")
    parser.add_argument('--jobs', type=int, default=4)
    args: argparse.Namespace = parser.parse_args()
    hosts: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux'}
    system: str = platform.system()
    if system not in hosts:
        print(f'profile_cache: profiles are taken on Linux (and packed on macOS), not {system}', file=sys.stderr)
        return 2
    tests_checkout: Path = args.tests_checkout.resolve()
    if not args.pack_only and not (tests_checkout / 'tools/profile_hot_paths.py').is_file():
        print(f'profile_cache: no gui_forms_tests checkout at {tests_checkout} (pass --tests-checkout, '
              'or --pack-only)', file=sys.stderr)
        return 2
    host: str = hosts[system]
    architecture: str = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    platform_label: str = host + '-' + architecture
    # By its real path, as ci_build.py builds (ccache base_dir and symlinked trees).
    build: Path = (ROOT / '.build').resolve() / ('native-' + platform_label)
    tree: Path = build / 'gui-forms-profile'
    cache_output: Path = build / 'gui-forms-profile-cache'
    build.mkdir(parents=True, exist_ok=True)

    prepare_host(ROOT / 'gui_forms', True)
    # The ordinary test cache options, then the shipped arithmetic and the
    # profiling flags; no link-time optimisation, as for every test cache
    # tree (ci_build.py).
    options: list[str] = [*host_options(host), '-DGUI_FORMS_LTO=OFF', '-DGUI_FORMS_REFERENCE_ARITHMETIC=OFF',
                          *release_flag_options(host)]
    configure: list[str] = ['cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(tree), '-G', 'Ninja',
                            *toolkit_options(ROOT / 'gui_forms', build / 'gui-forms-profile-sdk', True),
                            *options]
    configure_tree(tree, configure)
    # The workloads link the toolkit's libraries and the Gallery model, so
    # the tree is built whole, as for any test cache.
    run('cmake', '--build', tree, '--parallel', args.jobs)
    run(sys.executable, '-B', ROOT / 'gui_forms/tools/test_cache.py', 'pack', '--build-dir', tree,
        '--platform', platform_label, '--kind', CACHE_KIND, '--output', cache_output)
    command: list[str] = tests_command(tests_checkout, archive_path(cache_output, platform_label),
                                       args.output.resolve(), args.jobs)
    if args.pack_only:
        print('profile_cache: packed; profile with:\n    ' + ' '.join(command), flush=True)
        return 0
    run(*command)
    return 0


if __name__ == '__main__':
    sys.exit(main())
