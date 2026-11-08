#!/usr/bin/env python3
"""The toolkit's configure options and host preparation, without the harness.

tools/ci_build.py (CI, through template_harness) and tools/build_sdk.py (a
consumer's build of the source SDK, which carries no harness) configure
GUI.Forms with these, so the SDK a consumer builds is the build CI tests.
This module therefore imports nothing from template_harness: run and the
pinned-checkout check are its own, and match harness/ci_build_steps.py's.
"""
from __future__ import annotations
from pathlib import Path
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

    Nothing is fetched here any more: the renderer (stx_skia) and the text
    sources (HarfBuzz, FreeType, libunibreak, MD4C, SheenBidi) are in the stx
    repository, which cmake/Stx.cmake fetches at stx.lock's commit during the
    configure (a source SDK carries it, tools/build_sdk.py). fetch_text is
    kept for the callers and has no effect.
    """
    del source, fetch_text


def host_options(host: str) -> list[str]:
    """The configure options that select host's native host and renderer.

    Every host builds the Skia adapters on stx_skia (from the stx source) and uses the
    HarfBuzz/FreeType text engine.
    """
    options: list[str] = []
    name: str
    for name in HOST_OPTION_NAMES:
        enabled: str = 'OFF'
        if HOST_OPTION_NAMES[name] == host:
            enabled = 'ON'
        options.append(f'-DGUI_FORMS_ENABLE_{name}_HOST={enabled}')
    options.extend(['-DGUI_FORMS_ENABLE_SKIA=ON', '-DGUI_FORMS_ENABLE_HARFBUZZ_TEXT=ON'])
    return options


def toolkit_options(source: Path, prefix: Path, gallery: bool, tests: bool) -> list[str]:
    """The configure options every toolkit build shares, ahead of host_options.

    One Release build with the pinned LLVM toolchain file, position-
    independent code, the audio service with the stx Vorbis decoder, and
    prefix as the install prefix; gallery and tests switch the demonstration
    programs and the test suite. tools/build_sdk.py configures with these
    too (gallery and tests off), so the SDK a consumer builds is this build.
    """
    gallery_option: str = 'ON' if gallery else 'OFF'
    tests_option: str = 'ON' if tests else 'OFF'
    return ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_LIBDIR=lib',
            '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/llvm22.cmake'),
            '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', f'-DGUI_FORMS_BUILD_GALLERY={gallery_option}',
            f'-DGUI_FORMS_BUILD_TESTS={tests_option}', '-DGUI_FORMS_BUILD_AUDIO=ON',
            '-DGUI_FORMS_AUDIO_VORBIS_BACKEND=stx', f'-DCMAKE_INSTALL_PREFIX={prefix}']
