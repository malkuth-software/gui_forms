#!/usr/bin/env python3
"""Extract the published runtime payload and execute a relocated native test."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from audit_macos_minimum import audit
from macos_runtime_manifest import verify


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    args: argparse.Namespace = parser.parse_args()
    original: Path = Path(os.environ['GUI_FORMS_LLVM_RUNTIME']).resolve()
    archive: Path = args.archive.resolve()
    executable: Path = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix='gui-forms-runtime-restore-') as directory:
        scratch: Path = Path(directory).resolve()
        payload: str = '.build/toolchain/llvm-22.1.8-macos14'
        subprocess.run(['tar', '-xzf', str(archive), '-C', str(scratch), payload], check=True)
        restored: Path = scratch / payload
        verify(restored)
        library: Path
        for library in sorted((restored / 'lib').glob('*.dylib')):
            if not library.is_symlink():
                if not audit(library):
                    raise RuntimeError('Restored runtime is not a Mach-O image: ' + str(library))
        # Every GUI.Forms library, the test runtime included, is a static
        # archive (one module, owner decision 2026-11-15): the executable
        # loads no library of ours.
        copied: Path = scratch / executable.name
        shutil.copy2(executable, copied)
        # A program that links the runtime's static archives (cmake/llvm22.cmake,
        # GUI_FORMS_STATIC_CXX_RUNTIME) loads none of the dylibs and has no
        # runtime search path to change: then the proof is that it runs without
        # them, and none may be loaded from elsewhere.
        linked: str = subprocess.run(['otool', '-L', str(copied)], text=True, stdout=subprocess.PIPE,
                                     check=True).stdout
        dynamic_runtime: bool = 'libc++' in linked or 'libunwind' in linked
        # Match application packaging: change only this executable's runtime
        # search path. A global DYLD_LIBRARY_PATH would also replace the runtime
        # used by Apple's system frameworks and tests a different configuration.
        if dynamic_runtime:
            subprocess.run(['install_name_tool', '-rpath', str(original / 'lib'),
                            '@executable_path/' + payload + '/lib', str(copied)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', str(copied)], check=True)
        environment: dict[str, str] = os.environ.copy()
        environment.pop('DYLD_LIBRARY_PATH', None)
        environment.pop('DYLD_FALLBACK_LIBRARY_PATH', None)
        environment['DYLD_PRINT_LIBRARIES'] = '1'
        result: subprocess.CompletedProcess[str] = subprocess.run(
            [str(copied)], env=environment, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=60, check=True)
        for library in sorted((restored / 'lib').glob('*.dylib')):
            if not library.is_symlink():
                if dynamic_runtime and str(library) not in result.stderr:
                    raise RuntimeError('Restored library was not loaded: ' + str(library))
                if not dynamic_runtime and str(library) in result.stderr:
                    raise RuntimeError('A statically linked program loaded ' + str(library))
        print(result.stdout, end='')
        print('Archived runtime restored; its dylibs loaded; native test passed')


if __name__ == '__main__':
    main()
