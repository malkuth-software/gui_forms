#!/usr/bin/env python3
"""The toolkit's configure options and host preparation, without the harness.

tools/ci_build.py (CI, through template_harness) and tools/build_sdk.py (a
consumer's build of the source SDK, which carries no harness) configure
GUI.Forms with these, so the SDK a consumer builds is the build CI tests.
This module therefore imports nothing from template_harness: run and the
pinned-checkout check are its own, and match harness/ci_build_steps.py's.
"""
from __future__ import annotations
import os
from pathlib import Path
import platform
import subprocess


def run(*arguments: str | Path | int) -> None:
    command: list[str] = []
    argument: str | Path | int
    for argument in arguments:
        command.append(str(argument))
    print(' '.join(command), flush=True)
    subprocess.run(command, check=True)


HOST_OPTION_NAMES: dict[str, str] = {'WINDOWS': 'windows', 'MACOS': 'macos', 'LINUX': 'linux'}


def prepare_host(source: Path, fetch_text: bool) -> None:
    """Prepare what a host's configure reads, before configuring source.

    Nothing is fetched here any more: the renderer (stx_skia) is in the stx
    repository, and the text code is STXlib's, which stx pins (FreeType left
    on 2026-11-16). cmake/Stx.cmake builds stx into the stx package at
    stx.lock's commit during the configure, or links already built (a source
    SDK carries the source, tools/build_sdk.py). fetch_text is kept for the
    callers and has no effect.
    """
    del source, fetch_text


def host_options(host: str) -> list[str]:
    """The configure options that select host's native host and renderer.

    Every host builds the Skia adapters on stx_skia (from the stx package) and uses the
    text engine (STXlib).
    """
    options: list[str] = []
    name: str
    for name in HOST_OPTION_NAMES:
        enabled: str = 'OFF'
        if HOST_OPTION_NAMES[name] == host:
            enabled = 'ON'
        options.append(f'-DGUI_FORMS_ENABLE_{name}_HOST={enabled}')
    options.extend(['-DGUI_FORMS_ENABLE_SKIA=ON'])
    return options


def stx_options(stx_prefix: str, stx_package_dir: str) -> list[str]:
    """The configure options that choose the stx package a build links (cmake/Stx.cmake).

    stx_prefix names a package built elsewhere, linked as is with nothing
    fetched or compiled; otherwise stx_package_dir names where the package is
    built and reused while its stamp matches (CI restores it there from a
    cache). Both empty: the package is built in the build tree. Paths are
    passed with forward slashes, which CMake reads on every host.
    """
    options: list[str] = []
    if stx_prefix:
        options.append('-DGUI_FORMS_STX_PREFIX=' + Path(stx_prefix).resolve().as_posix())
    elif stx_package_dir:
        options.append('-DGUI_FORMS_STX_PACKAGE_DIR=' + Path(stx_package_dir).resolve().as_posix())
    return options


OWN_RUNTIME_SETTINGS: tuple[str, ...] = ('GUI_FORMS_OWN_RUNTIME_SOURCE', 'GUI_FORMS_OWN_RUNTIME_PREFIX',
                                         'GUI_FORMS_LIBCXX_PREFIX')


def toolchain_options(source: Path) -> list[str]:
    """The toolchain file and its settings for this host.

    Linux and macOS: cmake/llvm22.cmake. Windows: x86_64-pc-windows-msvc
    with STXlib's runtime (cmake/llvm22-windows-msvc.cmake with
    GUI_FORMS_MSVC_RUNTIME=OWN; owner, 2026-11-24: no MinGW, Visual C++
    tools or Windows SDK), whose three locations tools/own_runtime.py build
    prints and sets in the environment. CI (GITHUB_ACTIONS) also writes a
    link map beside every image, which own_runtime.py evidence reads.
    """
    if platform.system() != 'Windows':
        return ['-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/llvm22.cmake')]
    missing: list[str] = [name for name in OWN_RUNTIME_SETTINGS if not os.environ.get(name)]
    if missing:
        raise RuntimeError('the Windows build links STXlib\'s own runtime: run tools/own_runtime.py build and '
                           'set ' + ', '.join(missing))
    options: list[str] = ['-DCMAKE_TOOLCHAIN_FILE=' + (source / 'cmake/llvm22-windows-msvc.cmake').as_posix(),
                          '-DGUI_FORMS_MSVC_RUNTIME=OWN']
    name: str
    for name in OWN_RUNTIME_SETTINGS:
        options.append(f'-D{name}=' + Path(os.environ[name]).as_posix())
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        options.append('-DGUI_FORMS_LINK_MAP=ON')
    return options


def toolkit_options(source: Path, prefix: Path, tests: bool) -> list[str]:
    """The configure options every toolkit build shares, ahead of host_options.

    One Release build with the pinned LLVM toolchain (toolchain_options), position-
    independent code, the audio service with the stx Vorbis decoder, and
    prefix as the install prefix. Every build installs the Gallery model
    with the lab and demonstration controls (the GalleryModel, Lab and
    DemoControls components), which malkuth-software/gui_forms_devkit builds
    the Gallery and the other demonstrators from. tests switches the test
    cache (cmake/TestCache.cmake: the tree described for
    malkuth-software/gui_forms_tests, which holds the test suite and builds
    against it). tools/build_sdk.py configures with these too (tests off),
    so the SDK a consumer builds is this build.

    A build for the test cache is a reference and conformance build: it compiles with
    the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, no floating-
    point contraction; owner, 2026-11-16), which the engine gate, the
    pixel-hash tests and the cross-host comparisons pin. A build without
    tests (the SDK, profiling) is a deployment build and leaves contraction
    on.
    """
    tests_option: str = 'ON' if tests else 'OFF'
    return ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_LIBDIR=lib', *toolchain_options(source),
            '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DGUI_FORMS_BUILD_GALLERY_MODEL=ON',
            '-DGUI_FORMS_INSTALL_GALLERY_MODEL=ON', '-DGUI_FORMS_INSTALL_LAB=ON',
            f'-DGUI_FORMS_TEST_CACHE={tests_option}', f'-DGUI_FORMS_REFERENCE_ARITHMETIC={tests_option}',
            '-DGUI_FORMS_BUILD_AUDIO=ON', '-DGUI_FORMS_AUDIO_VORBIS_BACKEND=stx', f'-DCMAKE_INSTALL_PREFIX={prefix}']


if __name__ == '__main__':
    # `toolkit_configure.py toolchain` prints this host's toolchain options
    # (toolchain_options) for a workflow's own configure line.
    import sys
    if sys.argv[1:] != ['toolchain']:
        raise SystemExit('usage: toolkit_configure.py toolchain')
    print(' '.join(toolchain_options(Path(__file__).resolve().parents[1])))
