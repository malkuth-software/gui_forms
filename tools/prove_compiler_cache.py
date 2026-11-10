#!/usr/bin/env python3
"""Measure a relocated source build and fail if cache reuse changes its binary.

The caller supplies a clean source snapshot and a new scratch directory. Only
copies below scratch are edited. The proof uses the real retained-controls
event fixture, without renderer dependencies or platform GUI interaction.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Callable, TextIO


def execute(command: list[str], environment: dict[str, str], log: Path) -> float:
    stream: TextIO
    start: float = time.monotonic()
    with log.open('w', encoding='utf-8') as stream:
        subprocess.run(command, env=environment, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)
    elapsed: float = time.monotonic() - start
    return elapsed


def statistics(environment: dict[str, str]) -> dict[str, int]:
    output: str = subprocess.check_output(
        ['ccache', '--print-stats'], env=environment, text=True)
    result: dict[str, int] = {}
    line: str
    for line in output.splitlines():
        fields: list[str] = line.split()
        if len(fields) == 2:
            result[fields[0]] = int(fields[1])
    return result


def build(root: Path, cache: Path, name: str, jobs: int,
          configure: bool, extra_flag: str = '') -> dict[str, object]:
    source: Path = root / 'gui_forms'
    output: Path = root / 'build'
    environment: dict[str, str] = os.environ.copy()
    environment['CCACHE_DIR'] = str(cache)
    environment['CCACHE_BASEDIR'] = str(root)
    environment['CCACHE_COMPILERCHECK'] = 'content'
    environment['CCACHE_MAXSIZE'] = '500M'
    environment['CC'] = 'clang'
    environment['CXX'] = 'clang++'
    environment.pop('CCACHE_SLOPPINESS', None)
    if configure:
        flags: str = '-ffile-prefix-map=' + str(root) + '=.'
        if sys.platform == 'darwin':
            runtime: str = environment['GUI_FORMS_LLVM_RUNTIME']
            flags += ' -stdlib=libc++ -nostdinc++ -isystem \"' + runtime + '/include/c++/v1\"'
        if extra_flag:
            flags += ' ' + extra_flag
        command: list[str] = [
            'cmake', '-S', str(source), '-B', str(output), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DGUI_FORMS_ENABLE_SKIA=OFF',
            '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/llvm22.cmake'),
            '-DGUI_FORMS_ENABLE_HARFBUZZ_TEXT=OFF',
            '-DGUI_FORMS_ENABLE_MACOS_HOST=OFF',
            '-DGUI_FORMS_ENABLE_WINDOWS_HOST=OFF',
            '-DGUI_FORMS_ENABLE_LINUX_HOST=OFF',
            '-DGUI_FORMS_BUILD_GALLERY=OFF', '-DGUI_FORMS_BUILD_TESTS=ON',
            # The proof runs ccache without sloppiness, under which no compile
            # reading a precompiled header is cached.
            '-DGUI_FORMS_TEST_PRECOMPILED_HEADER=OFF',
            '-DCMAKE_C_COMPILER_LAUNCHER=ccache',
            '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
            '-DCMAKE_CXX_FLAGS=' + flags]
        if sys.platform == 'darwin':
            command.extend(['-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON',
                            '-DCMAKE_INSTALL_RPATH=@loader_path'])
        elif os.name != 'nt':
            command.append('-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON')
        if os.name == 'nt':
            # PE link timestamps otherwise differ even with identical object bytes.
            command.append('-DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-insert-timestamp')
        execute(command, environment, root / (name + '-configure.log'))
    subprocess.run(['ccache', '--zero-stats'], env=environment, check=True)
    elapsed: float = execute(
        ['cmake', '--build', str(output), '--target', 'gui_forms_owned_event_tests',
         '--parallel', str(jobs)], environment, root / (name + '-build.log'))
    execute(['ctest', '--test-dir', str(output), '-R', '^gui_forms_owned_event_tests$',
             '--output-on-failure'], environment, root / (name + '-test.log'))
    suffix: str = ''
    if os.name == 'nt':
        suffix = '.exe'
    executable: Path = output / ('gui_forms_owned_event_tests' + suffix)
    content: bytes = executable.read_bytes()
    digest: str = hashlib.sha256(content).hexdigest()
    stats: dict[str, int] = statistics(environment)
    result: dict[str, object] = {
        'name': name, 'seconds': elapsed, 'bytes': len(content),
        'sha256': digest, 'statistics': stats}
    print(json.dumps(result), flush=True)
    return result


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args: argparse.Namespace = parser.parse_args()
    scratch: Path = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=False)
    cache: Path = scratch / 'cache'
    producer: Path = scratch / 'provider'
    consumer: Path = scratch / 'consumer' / 'different-checkout'
    ignored: Callable[[str, list[str]], set[str]] = shutil.ignore_patterns('.git', '.build', 'build', '__pycache__')
    shutil.copytree(args.source, producer / 'gui_forms', ignore=ignored, symlinks=True)
    shutil.copytree(args.source, consumer / 'gui_forms', ignore=ignored, symlinks=True)
    records: list[dict[str, object]] = []
    cold: dict[str, object] = build(producer, cache, 'cold', args.jobs, True)
    records.append(cold)
    warm: dict[str, object] = build(consumer, cache, 'relocated', args.jobs, True)
    records.append(warm)
    # Preserve raw evidence before acceptance checks, including rejected trials.
    receipt: Path = scratch / 'receipt.json'
    receipt.write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    if cold['sha256'] != warm['sha256'] or cold['bytes'] != warm['bytes']:
        raise RuntimeError('Relocated cached build changed the final executable')
    stats: object = warm['statistics']
    if not isinstance(stats, dict):
        raise RuntimeError('Missing cache statistics')
    hits: int = int(stats.get('direct_cache_hit', 0)) + int(stats.get('preprocessed_cache_hit', 0))
    if hits == 0 or stats.get('cache_miss', 0) != 0:
        raise RuntimeError('Relocated build did not fully reuse cacheable compilations')
    stream: TextIO
    source: Path = consumer / 'gui_forms/src/core/component/component/component.cpp'
    with source.open('a', encoding='utf-8') as stream:
        stream.write('\nint cache_proof_revision() noexcept { int value = 1; return value; }\n')
    records.append(build(consumer, cache, 'implementation-change', args.jobs, False))
    header: Path = consumer / 'gui_forms/include/gui_forms/component/component/component.hpp'
    with header.open('a', encoding='utf-8') as stream:
        stream.write('\ninline int cache_proof_header_revision() noexcept { int value = 2; return value; }\n')
    records.append(build(consumer, cache, 'header-change', args.jobs, False))
    records.append(build(consumer, cache, 'option-change', args.jobs, True,
                         '-fno-inline-functions'))
    receipt.write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    record: dict[str, object]
    for record in records[2:]:
        stats = record['statistics']
        if not isinstance(stats, dict) or int(stats.get('cache_miss', 0)) == 0:
            raise RuntimeError('Changed compilation inputs did not invalidate the cache')


if __name__ == '__main__':
    main()
