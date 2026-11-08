#!/usr/bin/env python3
"""Build and run the C ABI's verification set under ThreadSanitizer (stage C8).

The C ABI promises release from any thread, deferred destruction on the UI
thread, begin_invoke from any thread and the conformance kit's thread
handover (gf_abi.h, "Threads"); ThreadSanitizer checks those promises where
the toolchain has it: Linux and macOS. The MinGW runtime on Windows has no
ThreadSanitizer, so this tool refuses there.

It configures its own tree, ``.build/native-<host>-<arch>/gui-forms-tsan``,
with the toolkit's ordinary options (tools/toolkit_configure.py) and
``-fsanitize=thread``, builds only the programs the ``c_abi``-labelled tests
read, and runs them with every report fatal:

    python3 tools/abi_tsan.py [--jobs N] [--tests PATTERN] [--skip-labels PATTERN]

``--tests`` narrows the run further with a ctest -R pattern, and
``--skip-labels`` leaves out tests whose labels match (ctest -LE), as
tools/ci_build.py does: launched-app tests run plain only. Programs that
cannot host ThreadSanitizer are left out: the C# test (the .NET host is not
instrumented, and TSan's runtime must be loaded at process start) and the
MSVC test (Windows only). Suppressions live in tools/sanitizer/tsan.supp,
which admits system-library frames only.

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
import re
from pathlib import Path
import platform
import sys

import harness_link  # noqa: F401  (puts the harness's tools on sys.path)
from ci_build_steps import configure_tree, ctest_selection, run, selected_test_targets, test_listing
from toolkit_configure import host_options, prepare_host, toolkit_options

ROOT: Path = Path(__file__).resolve().parents[2]
LABEL: str = 'c_abi'
# Tests of the label that cannot run under ThreadSanitizer (see the docstring).
EXCLUDED: str = '^(gui_forms_abi_csharp|gui_forms_abi_msvc_consumer)$'
TSAN_FLAGS: str = '-fsanitize=thread -fno-omit-frame-pointer -fdebug-compilation-dir=.'
TSAN_LINK_FLAGS: str = '-fsanitize=thread'
BUILD_TYPE_FLAGS: str = '-O1 -gline-tables-only -DNDEBUG'


def tsan_environment(suppressions: Path) -> dict[str, str]:
    """The configure and test environment: the flags, and every report fatal."""
    return {
        'CFLAGS': TSAN_FLAGS,
        'CXXFLAGS': TSAN_FLAGS,
        'OBJCXXFLAGS': TSAN_FLAGS,
        'LDFLAGS': TSAN_LINK_FLAGS,
        'TSAN_OPTIONS': ('halt_on_error=1:abort_on_error=0:print_summary=1:second_deadlock_stack=1:'
                         'suppressions=' + str(suppressions)),
    }


def require_instrumented(tree: Path) -> None:
    """Refuse a tree whose first configure did not see the TSan flags."""
    cache: Path = tree / 'CMakeCache.txt'
    if not cache.is_file():
        return
    text: str = cache.read_text(encoding='utf-8', errors='replace')
    if '-fsanitize=thread' not in text:
        raise SystemExit(f'abi_tsan: {tree} was configured without -fsanitize=thread; delete it and run again')


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--tests', default='', help='narrow the c_abi set with this ctest -R pattern')
    parser.add_argument('--skip-labels', default='', help='leave out tests with labels matching this ctest -LE pattern')
    args: argparse.Namespace = parser.parse_args()
    hosts: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux'}
    system: str = platform.system()
    if system not in hosts:
        print(f'abi_tsan: ThreadSanitizer runs on Linux and macOS only, not {system}', file=sys.stderr)
        return 2
    host: str = hosts[system]
    architecture: str = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    build: Path = ROOT / '.build' / ('native-' + host + '-' + architecture)
    tree: Path = build / 'gui-forms-tsan'
    build.mkdir(parents=True, exist_ok=True)
    require_instrumented(tree)

    os.environ.update(tsan_environment(ROOT / 'gui_forms/tools/sanitizer/tsan.supp'))
    prepare_host(ROOT / 'gui_forms', True)
    options: list[str] = [*host_options(host), '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                          '-DCMAKE_C_FLAGS_RELWITHDEBINFO=' + BUILD_TYPE_FLAGS,
                          '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=' + BUILD_TYPE_FLAGS]
    if host == 'macos':
        options.append('-DCMAKE_OBJCXX_FLAGS_RELWITHDEBINFO=' + BUILD_TYPE_FLAGS)
    configure: list[str] = ['cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(tree), '-G', 'Ninja',
                            *toolkit_options(ROOT / 'gui_forms', build / 'gui-forms-sdk', False, True), *options]
    configure_tree(tree, configure)
    require_instrumented(tree)

    selection: list[str] = ['-L', '^' + LABEL + '$', '-E', EXCLUDED]
    if args.tests or args.skip_labels:
        selection.extend(ctest_selection(args.tests, args.skip_labels))
    listing: dict = test_listing(tree, args.tests, args.skip_labels)
    kept: list[dict] = []
    test: dict
    for test in listing['tests']:
        labels: list[str] = []
        prop: dict
        for prop in test.get('properties', []):
            if prop.get('name') == 'LABELS':
                labels = list(prop.get('value', []))
        if LABEL in labels and not re.search(EXCLUDED, test['name']):
            kept.append(test)
    listing['tests'] = kept
    targets: list[str] | None = selected_test_targets(tree, listing)
    if targets:
        print(f'abi_tsan: {len(kept)} tests; {len(targets)} build outputs', flush=True)
        run('cmake', '--build', tree, '--parallel', args.jobs, '--target', *targets)
    os.environ['GUI_FORMS_FONT_DIR'] = str(ROOT / 'gui_forms/assets/fonts')
    run('ctest', '--test-dir', tree, '--output-on-failure', '--timeout', '900', *selection)
    return 0


if __name__ == '__main__':
    sys.exit(main())
