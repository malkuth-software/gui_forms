#!/usr/bin/env python3
"""Build and test the standalone toolkit in the consumer-compatible CI layout.

GUI.Forms' own build driver: its fetch scripts, configure options, SDK
checks and gallery option. The product-independent steps (run, configure,
test selection, sanitizer options, build records) are template_harness's
harness/ci_build_steps.py, reached through tools/harness_link.py (the
harness beside this checkout, at the commit harness.lock pins). File Manager
imports configure_tree, sanitizer_options and SANITIZER_LINK_FLAGS from
this module, so they stay importable here. The configure options and the
host preparation are tools/toolkit_configure.py's, which the source SDK's
tools/build_sdk.py also uses without the harness.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

import harness_link  # noqa: F401  (puts the harness's tools on sys.path)
import change_selection
from ci_build_steps import (BUILT_EVERYTHING, BUILT_NOTHING, BUILT_SELECTED, DEBUG_INFO_FLAGS,  # noqa: F401
                            DEBUG_INFO_VARIABLE, SANITIZER_FLAGS, SANITIZER_LINK_FLAGS, build_record_path,
                            captured, changed_tests_listing, configure_tree, ctest_selection, debug_info_mode,
                            run, sanitizer_options, selected_test_targets, test_listing, tree_relative,
                            write_build_record)
import native_tests
from toolkit_configure import (HOST_OPTION_NAMES, host_options, prepare_host, stx_options,  # noqa: F401
                               toolkit_options)

ROOT: Path = Path(__file__).resolve().parents[2]


def check_windows_sdk_imports(sdk: Path) -> None:
    """Fail when an installed SDK DLL imports a compiler runtime DLL.

    Every Windows binary links the LLVM runtimes and winpthreads statically
    (CMakeLists.txt), so an installed DLL imports Windows system DLLs and
    GUI.Forms' own DLLs only; the forbidden names are package_programs.py's.
    Prints each DLL's non-system imports.
    """
    from package_programs import WINDOWS_FORBIDDEN_RUNTIME_DLL, WINDOWS_SYSTEM_DLLS
    objdump: str | None = shutil.which('objdump') or shutil.which('llvm-objdump')
    if objdump is None:
        raise RuntimeError('objdump (or llvm-objdump) is needed to check the installed DLLs')
    dlls: list[Path] = sorted(sdk.rglob('*.dll'))
    if not dlls:
        raise RuntimeError('the installed SDK has no DLL under ' + str(sdk))
    dll: Path
    for dll in dlls:
        listing: str = subprocess.run([objdump, '-p', str(dll)], check=True, capture_output=True,
                                      text=True).stdout
        names: list[str] = re.findall(r'DLL Name:\s*(\S+)', listing)
        own: list[str] = []
        name: str
        for name in names:
            key: str = name.lower()
            if key in WINDOWS_SYSTEM_DLLS or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            if WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch(name):
                raise RuntimeError(f'installed {dll.name} imports the compiler runtime {name}')
            own.append(name)
        print(f'installed {dll.relative_to(sdk)}: imports {", ".join(own) if own else "system DLLs only"}', flush=True)


def check_installed_notices(sdk: Path) -> None:
    """Fail unless the installed SDK carries THIRD_PARTY_NOTICES.md with the
    FreeType credit and no GPL text (the rules of package_programs.py)."""
    from package_programs import FREETYPE_CREDIT, GPL_TITLE, NOTICES_NAME
    licences: Path = sdk / 'share' / 'licenses' / 'GUIForms'
    notices: Path = licences / NOTICES_NAME
    if not notices.is_file() or FREETYPE_CREDIT not in notices.read_text(encoding='utf-8'):
        raise RuntimeError(f'the installed SDK lacks {notices} or its FreeType credit')
    path: Path
    for path in sorted(licences.rglob('*')):
        if 'gpl' in path.name.lower():
            raise RuntimeError(f'the installed SDK ships {path.name}')
        if path.is_file() and GPL_TITLE.search(path.read_text(encoding='utf-8', errors='replace')):
            raise RuntimeError(f'the installed {path.name} carries a GNU General Public License text')
    print(f'installed notices checked: {notices.relative_to(sdk)}', flush=True)


def gallery_enabled(flag: bool, environ: dict[str, str]) -> bool:
    """Resolve whether the demo programs are configured.

    The default stays OFF: CI builds the toolkit, the Gallery model and
    their tests (GUI_FORMS_BUILD_GALLERY_MODEL follows the tests). The
    release pipeline passes --gallery or sets GUI_FORMS_CI_BUILD_GALLERY to
    1/true/on/yes to add the Gallery and lab executables.
    """
    if flag:
        return True
    value: str = environ.get('GUI_FORMS_CI_BUILD_GALLERY', '').strip().lower()
    return value in ('1', 'true', 'on', 'yes')


# The ninja rules whose outputs a test may load without naming them (a C ABI
# library a consumer test finds through its directory, a plugin): every shared
# and module library is built with the test targets.
LOADED_LIBRARY_RULES: tuple[str, ...] = ('C_SHARED_LIBRARY_LINKER', 'CXX_SHARED_LIBRARY_LINKER',
                                         'C_MODULE_LIBRARY_LINKER', 'CXX_MODULE_LIBRARY_LINKER')


def ninja_outputs(toolkit: Path) -> dict[str, str]:
    """Every output ninja can build in toolkit, with the name of the rule that builds it."""
    outputs: dict[str, str] = {}
    line: str
    for line in captured('ninja', '-C', toolkit, '-t', 'targets', 'all').splitlines():
        output: str
        rule: str
        output, _, rule = line.rpartition(': ')
        if output:
            outputs[output] = rule.strip()
    return outputs


def test_build_targets(listing: dict, outputs: dict[str, str], toolkit: Path) -> tuple[list[str], list[str]]:
    """The ninja outputs the tests of listing run, and the build-tree paths they name that ninja builds none of.

    A test runs the build outputs its command names (its executable, the
    libraries and programs a script inspects), plus every shared and module
    library (LOADED_LIBRARY_RULES), which a test may load through a directory
    it names. A named directory whose files ninja builds (a macOS .app bundle
    a probe launches, Contents/MacOS/<program> and the rest) stands for all of
    them. A named path ninja builds nothing under is a source the configure
    fetched or a file the test writes (gf_abi/...), so it needs no build; the
    second list names them. Unlike ci_build_steps.targets_for_tests, which
    builds everything for such a path, this list never falls back to `all`:
    it is the pull request build, which a merge group's full build checks.
    """
    targets: set[str] = set()
    named_directories: set[str] = set()
    test: dict
    for test in listing.get('tests', []):
        argument: str
        for argument in test.get('command', []):
            relative: str | None = tree_relative(argument, toolkit)
            if relative is None:
                continue
            if relative in outputs:
                targets.add(relative)
            else:
                named_directories.add(relative)
    # Each output is checked once against the named directories (a few), not each directory against
    # every output.
    unbuilt: set[str] = set(named_directories)
    output: str
    rule: str
    for output in outputs:
        directory: str = output
        while '/' in directory:
            directory = directory.rpartition('/')[0]
            if directory in named_directories:
                targets.add(output)
                unbuilt.discard(directory)
    for output, rule in outputs.items():
        if rule.startswith(LOADED_LIBRARY_RULES):
            targets.add(output)
    return sorted(targets), sorted(unbuilt)


# The tests a pull request runs whatever it changed: the engine gate test,
# whose manifest the engine-gate job needs from every host, and the threading
# tests, whose executable the macOS runtime relocation proof runs.
SMOKE_TESTS: tuple[str, ...] = ('gui_forms_engine_gate_tests', 'gui_forms_threading_tests')


def pull_request_listing(listing: dict, changed: list[str] | None) -> dict:
    """The tests of listing a pull request runs: those its changed paths can affect, plus SMOKE_TESTS.

    changed None (no change selection) keeps every test. The areas are
    tools/test_areas.json's (change_selection); a path every test reads, or
    one no rule classifies, keeps every test.
    """
    if changed is None:
        return listing
    affected: dict | None = changed_tests_listing(changed, listing)
    if affected is None:
        return listing
    names: set[str] = set()
    test: dict
    for test in affected['tests']:
        names.add(test['name'])
    kept: list[dict] = []
    for test in listing.get('tests', []):
        if test['name'] in names or test['name'] in SMOKE_TESTS:
            kept.append(test)
    return {'tests': kept}


def report_stx_package(toolkit: Path, restored: bool) -> None:
    """Say whether configuring toolkit built the stx package or linked one.

    restored: CI restored the package from a cache keyed by its inputs. A
    restored package that configuring still rebuilt means the key misses an
    input of the package's stamp (cmake/Stx.cmake), so CI is warned.
    """
    if not (toolkit / 'stx-package-built.txt').is_file():
        print('stx package: linked, nothing of stx compiled', flush=True)
        return
    print('stx package: built while configuring (logs: stx-configure.log, stx-build.log)', flush=True)
    if restored:
        print('::warning::the restored stx package was rebuilt: the cache key misses an input of its stamp '
              '(cmake/Stx.cmake)', flush=True)


def build_logged(toolkit: Path, sanitize: bool, *arguments: str) -> None:
    """Runs `cmake --build toolkit` with `arguments`, echoing its output and
    keeping it in toolkit/build-output.log, then checks the warnings it
    reports against tools/warning_baseline.json (cmake/Hardening.cmake,
    owner decision D11). The sanitized build's warnings are not counted: its
    flags differ from the baseline's. A build failure raises before the check."""
    log_path: Path = toolkit / 'build-output.log'
    command: list[str] = ['cmake', '--build', str(toolkit), *arguments]
    print(' '.join(command), flush=True)
    line: str
    with log_path.open('w', encoding='utf-8', errors='replace') as log:
        process: subprocess.Popen[str] = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                                          text=True, encoding='utf-8', errors='replace')
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            log.write(line)
        status: int = process.wait()
    sys.stdout.flush()
    if status != 0:
        raise subprocess.CalledProcessError(status, command)
    if sanitize:
        return
    run(sys.executable, '-B', ROOT / 'gui_forms/tools/warning_baseline.py', 'check', '--log', log_path,
        '--build-dir', toolkit)


def build_toolkit(host: str, build: Path, sdk: Path, jobs: int, gallery: bool = False,
                  sanitize: bool = False, tests: str = '', test_jobs: int = 1,
                  debug_info: str = 'lines', skip_labels: str = '', changed_from: str = '',
                  install_only: bool = False, test_targets_only: bool = False) -> None:
    """Build, test, then install only into the explicitly supplied build SDK.

    With sanitize, the toolkit is built in its own directory with the
    sanitizers and only tested: nothing is installed and the installed-SDK
    examples are not built. tests, a ctest -R pattern, narrows the run to
    the matching tests and the build to the outputs they read (fails when it
    matches none); empty builds everything and runs the whole suite. A
    narrowed run without sanitize (the bootstrap's smoke build) is a plain
    build and those tests only: it installs nothing, builds no examples and
    writes no JUnit report, which names the whole suite's results.
    test_jobs is how many tests ctest runs at once (1, the default, runs
    them one after another). debug_info is the sanitized build's debug
    information, `lines` or `full` (see DEBUG_INFO_FLAGS). skip_labels, a
    ctest -LE pattern, leaves out the tests with matching labels (for
    example gallery_accessibility, the Gallery-wide accessibility tests)
    from the run and from a narrowed build.
    changed_from, a ref, narrows the sanitized run further to the tests the
    paths changed since it can affect (tools/change_selection.py, the areas
    of tools/test_areas.json) and the build to what they read; when no
    changed path is read by any test, nothing is configured, built or run.
    install_only builds the whole tree and installs the SDK, with its notice
    and import checks and the installed reference examples, but runs none
    of the toolkit's own tests and writes no JUnit report: a consumer's
    smoke build at a pin whose suite GUI.Forms' CI already passed (File
    Manager's bootstrap smoke).
    test_targets_only (a pull request) runs the suite, with its JUnit
    report, but builds only what the tests run (test_build_targets), never
    `all`: no program, example, bundle or package no test runs. It installs
    nothing and builds no installed reference example; the merge group's and
    main's full runs do. With changed_from it runs only the tests the
    changed paths can affect, plus SMOKE_TESTS (pull_request_listing).
    """
    toolkit: Path = build / ('gui-forms-sanitize' if sanitize else 'gui-forms')
    build_record_path(toolkit).unlink(missing_ok=True)
    changed: list[str] = []
    if changed_from:
        changed = change_selection.changed_paths(ROOT / 'gui_forms', changed_from)
        if not test_targets_only and not change_selection.any_path_read(changed, change_selection.load_rules()):
            reason: str = f'no test reads the {len(changed)} paths changed since {changed_from}'
            print(reason + '; nothing to build or run', flush=True)
            write_build_record(toolkit, BUILT_NOTHING, reason)
            return
    options: list[str] = []
    miniaudio: Path = toolkit / '_deps/gui_forms_miniaudio-src'
    if (miniaudio / 'miniaudio.h').is_file():
        # Source was restored from the pinned dependency cache. Audio.cmake
        # checks both decoder/header digests before compiling it. A missing
        # FetchContent stamp must not discard and clone these same bytes again.
        options.append(f'-DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO={miniaudio}')
    prepare_host(ROOT / 'gui_forms', True)
    options.extend(host_options(host))
    if host == 'linux' and os.environ.get('GITHUB_ACTIONS') == 'true':
        # C-10: the Linux CI image has LLVM's and Clang's development headers
        # (template_harness projects/gui_forms.toml), so effects are verified
        # there; a skipped plugin fails the configure.
        options.append('-DGUI_FORMS_EFFECT_VERIFY_REQUIRED=ON')
    # The stx package (cmake/Stx.cmake): GUI_FORMS_STX_PREFIX links one built
    # elsewhere; GUI_FORMS_STX_PACKAGE_DIR (CI: restored from a cache) is where
    # it is built and reused, with a directory of its own for the sanitized
    # build, whose flags differ.
    stx_package_dir: str = os.environ.get('GUI_FORMS_STX_PACKAGE_DIR', '')
    if stx_package_dir and sanitize:
        stx_package_dir += '-sanitize'
    options.extend(stx_options(os.environ.get('GUI_FORMS_STX_PREFIX', ''), stx_package_dir))
    # The test programs' shared precompiled headers (cmake/TestPrecompiledHeader.cmake):
    # GUI_FORMS_TEST_PRECOMPILED_HEADER=OFF (the daily CI build) compiles every
    # test without them.
    if os.environ.get('GUI_FORMS_TEST_PRECOMPILED_HEADER', '') in ('ON', 'OFF'):
        options.append('-DGUI_FORMS_TEST_PRECOMPILED_HEADER=' + os.environ['GUI_FORMS_TEST_PRECOMPILED_HEADER'])
    timeout: str = '120'
    if sanitize:
        sanitizer_cmake: list[str]
        sanitizer_environment: dict[str, str]
        sanitizer_cmake, sanitizer_environment = sanitizer_options(
            host, ROOT / 'gui_forms/tools/sanitizer/lsan.supp', debug_info)
        options.extend(sanitizer_cmake)
        # The sanitizer tree checks borrowed views against their owners
        # (gui_forms/borrow/borrowed_view/borrowed_view.hpp, owner ruling
        # 2026-11-11); the plain tree compiles the plain views.
        options.append('-DGUI_FORMS_CHECKED_BORROWS=ON')
        os.environ.update(sanitizer_environment)
        timeout = '900'
    configure: list[str] = [
        'cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(toolkit), '-G', 'Ninja',
        *toolkit_options(ROOT / 'gui_forms', sdk, gallery, True), *options]
    configure_tree(toolkit, configure)
    report_stx_package(toolkit, os.environ.get('GUI_FORMS_STX_PACKAGE_RESTORED', '') == 'true')
    # A narrowed run builds only what its tests read, not every program in the tree.
    selection: list[str] = ctest_selection(tests, skip_labels)
    targets: list[str] | None = None
    if test_targets_only:
        suite: dict = test_listing(toolkit, '', skip_labels)
        chosen: dict = pull_request_listing(suite, changed if changed_from else None)
        if len(chosen['tests']) < len(suite.get('tests', [])):
            chosen_names: list[str] = []
            chosen_test: dict
            for chosen_test in chosen['tests']:
                chosen_names.append(chosen_test['name'])
            print(f'pull request: {len(chosen_names)} of {len(suite["tests"])} tests are affected by the paths '
                  f'changed since {changed_from} or always run', flush=True)
            selection = ctest_selection(change_selection.name_pattern(chosen_names), skip_labels)
        unbuilt: list[str]
        targets, unbuilt = test_build_targets(chosen, ninja_outputs(toolkit), toolkit)
        print(f'pull request: {len(targets)} build outputs serve those tests, not `all`; '
              f'{len(unbuilt)} named paths are sources or test outputs: {", ".join(unbuilt)}', flush=True)
    elif tests or changed_from:
        listing: dict = test_listing(toolkit, tests, skip_labels)
        if changed_from:
            changed_listing: dict | None = changed_tests_listing(changed, listing)
            if changed_listing is not None:
                listing = changed_listing
                if not listing['tests']:
                    unaffected: str = f'no test is affected by the paths changed since {changed_from}'
                    print(unaffected + '; nothing to build or run', flush=True)
                    write_build_record(toolkit, BUILT_NOTHING, unaffected)
                    return
                names: list[str] = []
                test: dict
                for test in listing['tests']:
                    names.append(test['name'])
                print(f'{len(names)} tests are affected by the paths changed since {changed_from}', flush=True)
                selection = ctest_selection(change_selection.name_pattern(names), skip_labels)
        targets = selected_test_targets(toolkit, listing)
    if targets is None:
        build_logged(toolkit, sanitize, '--parallel', str(jobs))
        write_build_record(toolkit, BUILT_EVERYTHING, 'the whole tree')
    elif targets:
        print(f'{len(targets)} build outputs serve the selected tests', flush=True)
        build_logged(toolkit, sanitize, '--parallel', str(jobs), '--target', *targets)
        write_build_record(toolkit, BUILT_SELECTED, f'{len(targets)} build outputs of the selected tests')
    else:
        print('the selected tests read no build output; nothing to build', flush=True)
        write_build_record(toolkit, BUILT_NOTHING, 'the selected tests read no build output')
    os.environ['GUI_FORMS_FONT_DIR'] = str(ROOT / 'gui_forms/assets/fonts')
    if sanitize or tests:
        run('ctest', '--test-dir', toolkit, '--output-on-failure', '--timeout', timeout,
            '--parallel', test_jobs, *selection)
        return
    if install_only:
        print('install only: the toolkit tests are not run', flush=True)
    else:
        # The JUnit report names the passed tests the cache manifest records
        # (tools/native_tests.py); the programs job does not run them again.
        run('ctest', '--test-dir', toolkit, '--output-on-failure', '--timeout', timeout,
            '--parallel', test_jobs, *selection, '--output-junit', native_tests.JUNIT_NAME)
    if test_targets_only:
        print('pull request: no SDK install or installed reference examples (the merge group builds them)',
              flush=True)
        return
    # Windows: the installed DLLs and programs drop their COFF symbol tables
    # (about a fifth of a Release DLL); a Release build carries no debug
    # information, and nothing reads the symbols at run time.
    install_command: list[str] = ['cmake', '--install', str(toolkit)]
    if host == 'windows':
        install_command.append('--strip')
    run(*install_command)
    check_installed_notices(sdk)
    if host == 'windows':
        check_windows_sdk_imports(sdk)
    examples: Path = build / 'installed-reference-examples'
    run('cmake', '-S', ROOT / 'gui_forms/examples/reference', '-B', examples,
        '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_PREFIX_PATH={sdk}',
        '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'gui_forms/cmake/llvm22.cmake'))
    run('cmake', '--build', examples, '--parallel', jobs)
    run('ctest', '--test-dir', examples, '--output-on-failure')


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--test-jobs', type=int, default=1,
                        help='tests ctest runs at once (default 1: one after another)')
    parser.add_argument('--debug-info', choices=tuple(DEBUG_INFO_FLAGS), default='',
                        help='with --sanitize, the debug information: lines (-gline-tables-only, the default) '
                             'or full (-g); also ' + DEBUG_INFO_VARIABLE)
    parser.add_argument('--skip-labels', default='',
                        help='leave out tests whose labels match this ctest -LE pattern, such as '
                             'gallery_accessibility (the Gallery-wide accessibility tests); CI passes none')
    parser.add_argument('--sanitize', action='store_true',
                        help='build in gui-forms-sanitize with ASan and UBSan (and LSan on Linux '
                             'and macOS) and only test')
    parser.add_argument('--tests', default='',
                        help='build and run only what the tests matching this ctest -R pattern need; without '
                             '--sanitize nothing is installed (the bootstrap smoke build)')
    parser.add_argument('--changed-from', default='',
                        help='with --sanitize or --test-targets-only, build and run only the tests the paths '
                             'changed since this ref can affect (tools/change_selection.py); every test when a '
                             'path touches everything (--test-targets-only also runs SMOKE_TESTS)')
    parser.add_argument('--install-only', action='store_true',
                        help='build and install the SDK (with its checks and the installed reference examples) '
                             "but run none of the toolkit's tests: a consumer's smoke build at a tested pin")
    parser.add_argument('--test-targets-only', action='store_true',
                        help='a pull request: run the whole suite but build only what its tests run, and '
                             'install nothing (no programs, examples, bundles or packages)')
    parser.add_argument('--gallery', action='store_true',
                        help='also configure GUI_FORMS_BUILD_GALLERY=ON '
                             '(or set GUI_FORMS_CI_BUILD_GALLERY=1); default OFF')
    args: argparse.Namespace = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.test_jobs < 1:
        parser.error('--test-jobs must be positive')
    if args.changed_from and not (args.sanitize or args.test_targets_only):
        parser.error('--changed-from narrows the sanitized run only')
    if args.test_targets_only and (args.sanitize or args.tests or args.install_only):
        parser.error('--test-targets-only is the plain run of a pull request; '
                     'it takes no --sanitize, --tests or --install-only')
    if args.install_only and (args.sanitize or args.tests or args.changed_from or args.skip_labels):
        parser.error('--install-only builds and installs everything and runs no toolkit test; '
                     'it takes no --sanitize, --tests, --changed-from or --skip-labels')
    if args.debug_info and not args.sanitize:
        parser.error('--debug-info applies to the sanitized build only')
    debug_info: str = 'lines'
    try:
        debug_info = debug_info_mode(args.debug_info, dict(os.environ))
    except ValueError as error:
        parser.error(str(error))
    hosts: dict[str, str] = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}
    host: str = hosts[platform.system()]
    architecture: str = 'x64'
    if platform.machine().lower() in ('arm64', 'aarch64'):
        architecture = 'arm64'
    build: Path = ROOT / '.build' / ('native-' + host + '-' + architecture)
    build.mkdir(parents=True, exist_ok=True)
    build_toolkit(host, build, build / 'gui-forms-sdk', args.jobs,
                  gallery_enabled(args.gallery, dict(os.environ)), args.sanitize, args.tests,
                  args.test_jobs, debug_info, args.skip_labels, args.changed_from, args.install_only,
                  args.test_targets_only)


if __name__ == '__main__':
    main()
