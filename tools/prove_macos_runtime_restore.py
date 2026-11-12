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


def relocate_library(source: Path, scratch: Path, original: Path, payload: str) -> Path:
    """Copy one build-tree dylib beside the relocated executable and point its
    runtime search path at the restored payload instead of the build host's."""
    relocated: Path = scratch / source.name
    shutil.copy2(source, relocated)
    subprocess.run(['install_name_tool', '-rpath', str(original / 'lib'),
                    '@loader_path/' + payload + '/lib', str(relocated)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(relocated)], check=True)
    return relocated


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
        threading: Path = relocate_library(executable.parent / 'libgui_forms_threading.0.dylib',
                                           scratch, original, payload)
        # Test executables also link the shared test runtime (cmake/TestRuntime.cmake),
        # which the build tree supplies through the rpath this script replaces.
        test_runtime_source: Path = executable.parent / 'libgui_forms_test_runtime.dylib'
        if test_runtime_source.is_file():
            relocate_library(test_runtime_source, scratch, original, payload)
        copied: Path = scratch / executable.name
        shutil.copy2(executable, copied)
        # Match application packaging: change only this executable's runtime
        # search path. A global DYLD_LIBRARY_PATH would also replace the runtime
        # used by Apple's system frameworks and tests a different configuration.
        subprocess.run(['install_name_tool', '-rpath', str(original / 'lib'),
                        '@executable_path/' + payload + '/lib', str(copied)], check=True)
        subprocess.run(['install_name_tool', '-rpath', str(executable.parent), '@executable_path', str(copied)], check=True)
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
                if str(library) not in result.stderr:
                    raise RuntimeError('Restored library was not loaded: ' + str(library))
        if str(threading) not in result.stderr:
            raise RuntimeError('Relocated Threading library was not loaded')
        print(result.stdout, end='')
        print('Archived runtime restored; all three relocated dylibs loaded; native test passed')


if __name__ == '__main__':
    main()
