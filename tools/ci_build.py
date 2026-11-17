#!/usr/bin/env python3
"""Build the standalone toolkit in the consumer-compatible CI layout, install
it, check the install, and pack the test cache.

The tests live in malkuth-software/gui_forms_tests (owner, 2026-10-29 and
2026-11-17) and build against the test cache this build packs
(tools/test_cache.py; cmake/TestCache.cmake): the installed SDK, the
headers, and every library, object library and program of the tree. This
build runs no test of its own: its smoke check is the installed SDK's
(the notice and import checks, and the installed reference examples).

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
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

import harness_link  # noqa: F401  (puts the harness's tools on sys.path)
from ci_build_steps import (BUILT_EVERYTHING, BUILT_NOTHING, BUILT_SELECTED, DEBUG_INFO_FLAGS,  # noqa: F401
                            DEBUG_INFO_VARIABLE, SANITIZER_FLAGS, SANITIZER_LINK_FLAGS, build_record_path,
                            configure_tree, debug_info_mode, run, sanitizer_options, write_build_record)
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
    """Fail unless the installed SDK carries THIRD_PARTY_NOTICES.md and no GPL
    text (the rules of package_programs.py). FreeType is no longer built
    (STXlib's glyph library since 2026-11-16), so no FreeType credit is
    required."""
    from package_programs import GPL_TITLE, NOTICES_NAME
    licences: Path = sdk / 'share' / 'licenses' / 'GUIForms'
    notices: Path = licences / NOTICES_NAME
    if not notices.is_file():
        raise RuntimeError(f'the installed SDK lacks {notices}')
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
    the test cache (GUI_FORMS_BUILD_GALLERY_MODEL follows it). The
    release pipeline passes --gallery or sets GUI_FORMS_CI_BUILD_GALLERY to
    1/true/on/yes to add the Gallery and lab executables.
    """
    if flag:
        return True
    value: str = environ.get('GUI_FORMS_CI_BUILD_GALLERY', '').strip().lower()
    return value in ('1', 'true', 'on', 'yes')


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


# The most distinct error lines a failed build prints at its end.
KEEP_GOING_SUMMARY: int = 200
def print_build_errors(log_path: Path) -> None:
    """Prints the distinct compiler error lines of a failed build's log, in
    the order they first appear, up to KEEP_GOING_SUMMARY."""
    seen: set[str] = set()
    errors: list[str] = []
    line: str
    for line in log_path.read_text(encoding='utf-8', errors='replace').splitlines():
        if ' error: ' not in line and not line.startswith('error: '):
            continue
        if line in seen:
            continue
        seen.add(line)
        errors.append(line)
    print(f'build failed: {len(errors)} distinct error lines', flush=True)
    shown: str
    for shown in errors[:KEEP_GOING_SUMMARY]:
        print(shown, flush=True)


# The wall-clock times of a tree's last logged build (milliseconds since the
# Unix epoch): tools/ninja_phases.py places the .ninja_log's relative times on
# the clock with them, to line a quiet stretch up with resource samples
# (tools/windows_resource_sampler.ps1). Ninja exits as its last edge ends, so
# finished_ms is that edge's end.
BUILD_CLOCK_NAME: str = 'build-clock.json'


def write_build_clock(toolkit: Path, started_ms: int, finished_ms: int) -> None:
    """Writes toolkit/BUILD_CLOCK_NAME with the build's start and end, replacing an earlier one."""
    clock: dict[str, int] = {'started_ms': started_ms, 'finished_ms': finished_ms}
    (toolkit / BUILD_CLOCK_NAME).write_text(json.dumps(clock) + '\n', encoding='utf-8')


def build_logged(toolkit: Path, sanitize: bool, *arguments: str) -> None:
    """Runs `cmake --build toolkit` with `arguments`, echoing its output and
    keeping it in toolkit/build-output.log, then checks the warnings it
    reports against tools/warning_baseline.json (cmake/Hardening.cmake,
    owner decision D11). The sanitized build's warnings are not counted: its
    flags differ from the baseline's. A build failure raises before the check.

    Ninja keeps going past a failed command (-k 0, every tree here is
    configured with -G Ninja), so one run reports every compile error, and a
    failed build ends with the distinct error lines (KEEP_GOING_SUMMARY)."""
    log_path: Path = toolkit / 'build-output.log'
    command: list[str] = ['cmake', '--build', str(toolkit), *arguments, '--', '-k', '0']
    print(' '.join(command), flush=True)
    line: str
    started_ms: int = time.time_ns() // 1000000
    with log_path.open('w', encoding='utf-8', errors='replace') as log:
        process: subprocess.Popen[str] = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                                          text=True, encoding='utf-8', errors='replace')
        assert process.stdout is not None
        # Each line is flushed as it arrives, so the job log's timestamps
        # are when ninja finished each edge, not when a block of output
        # filled (a silence in the log is then a silence in the build).
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
        status: int = process.wait()
    sys.stdout.flush()
    write_build_clock(toolkit, started_ms, time.time_ns() // 1000000)
    if status != 0:
        print_build_errors(log_path)
        raise subprocess.CalledProcessError(status, command)
    if sanitize:
        return
    run(sys.executable, '-B', ROOT / 'gui_forms/tools/warning_baseline.py', 'check', '--log', log_path,
        '--build-dir', toolkit)


def build_toolkit(host: str, build: Path, sdk: Path, jobs: int, gallery: bool = False,
                  sanitize: bool = False, debug_info: str = 'lines', install_only: bool = False,
                  test_cache: Path | None = None, platform_label: str = '') -> None:
    """Build the tree, install it into sdk, check the install, and pack the test cache.

    The tree is configured for the test cache (GUI_FORMS_TEST_CACHE, with
    the reference arithmetic: toolkit_options) and built whole. With
    sanitize, it is built in its own directory with the sanitizers
    (debug_info: `lines` or `full`, DEBUG_INFO_FLAGS) and checked borrowed
    views; nothing is installed into sdk, and the cache packed from it is
    the sanitize kind, which gui_forms_tests' sanitizer runs link. Otherwise
    the SDK is installed into sdk and checked (its notices; on Windows its
    DLLs' imports), and the installed reference examples are built against
    it and run: the toolkit's smoke check. install_only stops there (a
    consumer's smoke build at a pin). test_cache, a directory, receives the
    packed cache (tools/test_cache.py pack, as platform_label).
    """
    toolkit: Path = build / ('gui-forms-sanitize' if sanitize else 'gui-forms')
    build_record_path(toolkit).unlink(missing_ok=True)
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
    if sanitize:
        sanitizer_cmake: list[str]
        sanitizer_environment: dict[str, str]
        # On Linux these are -DCMAKE_<LANG>_FLAGS values, which replace the
        # toolchain's _INIT flags; cmake/llvm22.cmake appends -stdlib=libc++
        # to them, so the sanitized tree builds against libc++ 22 too, and
        # the configure refuses a tree that would not.
        sanitizer_cmake, sanitizer_environment = sanitizer_options(
            host, ROOT / 'gui_forms/tools/sanitizer/lsan.supp', debug_info)
        options.extend(sanitizer_cmake)
        # The sanitizer tree checks borrowed views against their owners
        # (gui_forms/borrow/borrowed_view/borrowed_view.hpp, owner ruling
        # 2026-11-11); the plain tree compiles the plain views.
        options.append('-DGUI_FORMS_CHECKED_BORROWS=ON')
        os.environ.update(sanitizer_environment)
    # The installed reference examples generate every Gallery page document
    # with the installed Gallery form compiler, against GUIForms::Lab and
    # GUIForms::DemoControls (audit F9), so the SDK this build installs
    # carries the lab and the Gallery model.
    options.extend(['-DGUI_FORMS_INSTALL_LAB=ON', '-DGUI_FORMS_INSTALL_GALLERY_MODEL=ON'])
    # The text masks, as build_sdk.py --features all builds them: the
    # tests' SDK-consumer link test (gui_forms_sdk_consumer_link) then links
    # GUIForms::TextMasks from the installed Application archive, which the
    # one-module SDK once shipped without the masks (b68c80ecc, 2d4bb797b)
    # while a tree without them passed.
    options.append('-DGUI_FORMS_BUILD_TEXT_MASKS=ON')
    # No link-time optimisation in the tree the test cache is packed from
    # (owner, 2026-11-17: build times). With GUI_FORMS_LTO the archives hold
    # bitcode, and each of the several hundred test programs ran ThinLTO's
    # code generation over the whole toolkit at its link: on the Windows
    # runner about 79 of the build's 112 minutes went to linking. Release
    # builds of the SDK (build_sdk.py) keep LTO; GUI_FORMS_CI_LTO=1 keeps it
    # here too.
    if os.environ.get('GUI_FORMS_CI_LTO') != '1':
        options.append('-DGUI_FORMS_LTO=OFF')
    # Warnings are failures in CI (owner, 2026-11-17).
    options.append('-DGUI_FORMS_WARNINGS_AS_ERRORS=ON')
    configure: list[str] = [
        'cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(toolkit), '-G', 'Ninja',
        *toolkit_options(ROOT / 'gui_forms', sdk, gallery, True), *options]
    configure_tree(toolkit, configure)
    report_stx_package(toolkit, os.environ.get('GUI_FORMS_STX_PACKAGE_RESTORED', '') == 'true')
    build_logged(toolkit, sanitize, '--parallel', str(jobs))
    write_build_record(toolkit, BUILT_EVERYTHING, 'the whole tree')
    os.environ['GUI_FORMS_FONT_DIR'] = str(ROOT / 'gui_forms/assets/fonts')
    if not sanitize:
        # Windows: the installed DLLs and programs drop their COFF symbol
        # tables (about a fifth of a Release DLL); a Release build carries
        # no debug information, and nothing reads the symbols at run time.
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
    if install_only or test_cache is None:
        return
    run(sys.executable, '-B', ROOT / 'gui_forms/tools/test_cache.py', 'pack', '--build-dir', toolkit,
        '--platform', platform_label, '--kind', 'sanitize' if sanitize else 'normal', '--output', test_cache)


def build_high_integrity(host: str, build: Path, jobs: int, clipboard_text: bool,
                         test_cache: Path | None, platform_label: str) -> None:
    """Configures and builds the high-integrity tree (cmake/HighIntegrity.cmake)
    in build/gui-forms-high-integrity, and packs its test cache (the
    high-integrity kind: gui_forms_tests builds its high-integrity tests
    alone against it) into test_cache when given.

    Its own configure: toolkit_options turns on the audio service, which the
    configuration refuses. The warning baseline is not checked: the tree
    compiles a different set of sources. Nothing is installed.
    """
    toolkit: Path = build / 'gui-forms-high-integrity'
    prepare_host(ROOT / 'gui_forms', True)
    stx_package_dir: str = os.environ.get('GUI_FORMS_STX_PACKAGE_DIR', '')
    if stx_package_dir:
        stx_package_dir += '-high-integrity'
    allowance: str = 'ON' if clipboard_text else 'OFF'
    configure: list[str] = [
        'cmake', '-S', str(ROOT / 'gui_forms'), '-B', str(toolkit), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'gui_forms/cmake/llvm22.cmake'),
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DGUI_FORMS_HIGH_INTEGRITY=ON', '-DGUI_FORMS_TEST_CACHE=ON',
        f'-DGUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT={allowance}',
        *host_options(host),
        *stx_options(os.environ.get('GUI_FORMS_STX_PREFIX', ''), stx_package_dir)]
    configure_tree(toolkit, configure)
    run('cmake', '--build', toolkit, '--parallel', jobs)
    if test_cache is not None:
        run(sys.executable, '-B', ROOT / 'gui_forms/tools/test_cache.py', 'pack', '--build-dir', toolkit,
            '--platform', platform_label, '--kind', 'high-integrity', '--output', test_cache)


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--debug-info', choices=tuple(DEBUG_INFO_FLAGS), default='',
                        help='with --sanitize, the debug information: lines (-gline-tables-only, the default) '
                             'or full (-g); also ' + DEBUG_INFO_VARIABLE)
    parser.add_argument('--sanitize', action='store_true',
                        help='build in gui-forms-sanitize with ASan and UBSan (and LSan on Linux and macOS) '
                             'and pack the sanitize test cache; nothing is installed')
    parser.add_argument('--install-only', action='store_true',
                        help='build, install and check the SDK (with the installed reference examples) and '
                             'pack no test cache: a consumer\'s smoke build at a tested pin')
    parser.add_argument('--test-cache', type=Path,
                        help='pack the test cache (tools/test_cache.py) into this directory')
    parser.add_argument('--gallery', action='store_true',
                        help='also configure GUI_FORMS_BUILD_GALLERY=ON '
                             '(or set GUI_FORMS_CI_BUILD_GALLERY=1); default OFF')
    parser.add_argument('--high-integrity', action='store_true',
                        help='build the high-integrity tree (cmake/HighIntegrity.cmake) in '
                             'gui-forms-high-integrity')
    parser.add_argument('--allow-clipboard-text', action='store_true',
                        help='with --high-integrity, keep the clipboard text allowance')
    args: argparse.Namespace = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.install_only and (args.sanitize or args.test_cache):
        parser.error('--install-only builds and installs the plain tree and packs nothing')
    if args.high_integrity and (args.sanitize or args.install_only or args.gallery):
        parser.error('--high-integrity builds its own tree; it takes only --jobs, --test-cache and '
                     '--allow-clipboard-text')
    if args.allow_clipboard_text and not args.high_integrity:
        parser.error('--allow-clipboard-text is an allowance of --high-integrity')
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
    platform_label: str = host + '-' + architecture
    test_cache: Path | None = args.test_cache.resolve() if args.test_cache else None
    # The build root by its real path: with ccache's base_dir set, depfiles are
    # written relative to the compiler's real working directory, so a build
    # tree reached through a symlink records generated headers under paths
    # ninja does not match to their generating rule (form_shared_source).
    build_root: Path = ROOT / '.build'
    build_root.mkdir(parents=True, exist_ok=True)
    build: Path = build_root.resolve() / ('native-' + platform_label)
    build.mkdir(parents=True, exist_ok=True)
    if args.high_integrity:
        build_high_integrity(host, build, args.jobs, args.allow_clipboard_text, test_cache, platform_label)
        return
    build_toolkit(host, build, build / 'gui-forms-sdk', args.jobs,
                  gallery_enabled(args.gallery, dict(os.environ)), args.sanitize, debug_info, args.install_only,
                  test_cache, platform_label)


if __name__ == '__main__':
    main()
