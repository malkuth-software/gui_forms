#!/usr/bin/env python3
"""Build the Windows own runtime, and prove a build used nothing else.

GUI.Forms ships Windows on x86_64-pc-windows-msvc with STXlib's runtime
(cmake/llvm22-windows-msvc-own.cmake; owner, 2026-11-24: no MinGW, no
Visual C++ tools, no Windows SDK). Only the compiler binaries come from the
pinned LLVM 22.1.8 toolchain (CI: the MSYS2 CLANG64 package's clang and
lld); every header and library comes from what this tool builds:

  build     fetches STXlib at the commit stx pins (stx's stxlib.lock at the
            commit gui_forms' stx.lock names) and the LLVM 22.1.8 source
            (sha256-pinned), builds static libc++ with STXlib's
            tools/windows_libcxx/build_libcxx.sh, and builds STXlib's
            src/hosted/windows/own_runtime project with cmake/
            llvm22-windows-msvc.cmake into <out>/own-runtime. A stamp of
            the inputs skips a build whose inputs match (CI restores <out>
            from a cache). Prints the three settings the toolchain reads;
            --github-env appends them to $GITHUB_ENV.
  evidence  for a configured and built tree: the compiler's #include
            search list (clang -v), which must hold libc++'s, clang's
            resource, STXlib's C library and Windows API directories and
            nothing else; every link map (GUI_FORMS_LINK_MAP), none naming
            a MinGW, Visual C++ or Windows SDK file; and, with --dlls, the
            imports of every DLL under that folder, the C ABI DLL among them
            (ci_build.check_windows_sdk_imports' rule: Windows system DLLs,
            ucrtbase.dll and GUI.Forms' own DLLs only).

On a host other than Windows the build is a cross build (CMAKE_SYSTEM_NAME
Windows through wrapper toolchain files), for checking the target on a Mac.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tomllib
import urllib.request

ROOT: Path = Path(__file__).resolve().parents[1]
LLVM_VERSION: str = '22.1.8'
LLVM_SOURCE_URL: str = (f'https://github.com/llvm/llvm-project/releases/download/llvmorg-{LLVM_VERSION}/'
                        f'llvm-project-{LLVM_VERSION}.src.tar.xz')
LLVM_SOURCE_SHA256: str = '922f1817a0df7b1489272d18134ee0087a8b068828f87ac63b9861b1a9965888'
# The parts of the LLVM source a runtimes build of libc++ reads.
LLVM_SOURCE_PARTS: tuple[str, ...] = ('runtimes/', 'libcxx/', 'libcxxabi/', 'libc/', 'cmake/', 'llvm/cmake/',
                                      'llvm/utils/', 'third-party/')
# Files whose content decides the result, beside the two pins.
STAMP_INPUTS: tuple[str, ...] = ('cmake/llvm22-windows-msvc.cmake', 'cmake/llvm22-windows-msvc-own.cmake',
                                 'tools/own_runtime.py')
# Folder names a header or library of a build may never come from.
FORBIDDEN_FOLDERS: re.Pattern[str] = re.compile(
    r'mingw|msys64|clang64[/\\](?!lib[/\\]clang[/\\])|windows kits|microsoft visual studio|vc[/\\]tools[/\\]msvc',
    re.IGNORECASE)


def run(*command: str | Path, cwd: Path | None = None, environment: dict[str, str] | None = None) -> None:
    print('+ ' + ' '.join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], check=True, cwd=cwd, env=environment)


def stxlib_pin() -> tuple[str, str]:
    """The STXlib repository and commit stx pins at gui_forms' stx.lock commit."""
    stx: dict[str, str] = tomllib.loads((ROOT / 'stx.lock').read_text(encoding='utf-8'))['stx']
    url: str = f'https://raw.githubusercontent.com/{stx["repository"]}/{stx["commit"]}/stxlib.lock'
    with urllib.request.urlopen(url, timeout=60) as response:
        lock: dict[str, str] = tomllib.loads(response.read().decode('utf-8'))['stxlib']
    return lock['repository'], lock['commit']


def stamp_of(stxlib_commit: str) -> dict[str, str]:
    stamp: dict[str, str] = {'stxlib': stxlib_commit, 'llvm': LLVM_SOURCE_SHA256}
    name: str
    for name in STAMP_INPUTS:
        stamp[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    return stamp


def fetch_stxlib(repository: str, commit: str, destination: Path) -> None:
    if not (destination / '.git').is_dir():
        destination.mkdir(parents=True, exist_ok=True)
        run('git', 'init', '-q', destination)
    run('git', '-C', destination, 'fetch', '-q', '--depth', '1', f'https://github.com/{repository}.git', commit)
    run('git', '-C', destination, 'checkout', '-q', '--force', commit)


def fetch_llvm_source(work: Path) -> Path:
    """The LLVM source root, downloaded and checked against its sha256 once."""
    source: Path = work / f'llvm-project-{LLVM_VERSION}.src'
    if (source / 'runtimes/CMakeLists.txt').is_file():
        return source
    archive: Path = work / f'llvm-project-{LLVM_VERSION}.src.tar.xz'
    if not archive.is_file():
        print(f'downloading {LLVM_SOURCE_URL}', flush=True)
        partial: Path = archive.with_suffix('.partial')
        with urllib.request.urlopen(LLVM_SOURCE_URL, timeout=600) as response, partial.open('wb') as output:
            shutil.copyfileobj(response, output, 1 << 20)
        partial.replace(archive)
    digest: hashlib._Hash = hashlib.sha256()
    with archive.open('rb') as data:
        for block in iter(lambda: data.read(1 << 20), b''):
            digest.update(block)
    if digest.hexdigest() != LLVM_SOURCE_SHA256:
        archive.unlink()
        raise RuntimeError(f'{archive.name}: sha256 {digest.hexdigest()}, expected {LLVM_SOURCE_SHA256}')
    top: str = source.name + '/'
    with tarfile.open(archive, 'r:xz') as tar:
        members: list[tarfile.TarInfo] = [member for member in tar.getmembers()
                                          if member.name.startswith(top)
                                          and member.name[len(top):].startswith(LLVM_SOURCE_PARTS)]
        tar.extractall(work, members=members, filter='data')
    archive.unlink()
    return source


def cross_toolchain(path: Path, real: Path) -> Path:
    """A toolchain file that names the Windows target system, then reads real."""
    path.write_text('set(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR AMD64)\n'
                    f'include("{real.as_posix()}")\n', encoding='utf-8')
    return path


def build_libcxx(stxlib: Path, llvm_source: Path, out: Path, jobs: int) -> Path:
    prefix: Path = out / 'libcxx'
    tools: Path = stxlib / 'tools/windows_libcxx'
    script: Path = tools / 'build_libcxx.sh'
    if platform.system() != 'Windows':
        # The script names its own toolchain file; the cross copy names a
        # wrapper of it and keeps the STXlib root it would compute.
        cross: Path = cross_toolchain(out / 'libcxx-cross.cmake', tools / 'toolchain.cmake')
        text: str = script.read_text(encoding='utf-8')
        text = text.replace('-DCMAKE_TOOLCHAIN_FILE="$here/toolchain.cmake"',
                            f'-DCMAKE_TOOLCHAIN_FILE="{cross.as_posix()}"')
        text = re.sub(r'^root=.*$', f'root="{stxlib.as_posix()}"', text, count=1, flags=re.MULTILINE)
        script = out / 'build_libcxx_cross.sh'
        script.write_text(text, encoding='utf-8')
    shutil.rmtree(out / 'libcxx-tree', ignore_errors=True)
    environment: dict[str, str] = dict(os.environ, LLVM_SRC=llvm_source.as_posix(),
                                       BUILD_DIR=(out / 'libcxx-tree').as_posix(), PREFIX=prefix.as_posix(),
                                       JOBS=str(jobs))
    run('sh', script, environment=environment)
    # The licence every image that embeds libc++ ships (CMakeLists.txt
    # installs it as libc++-LICENSE.txt).
    shutil.copyfile(llvm_source / 'libcxx/LICENSE.TXT', prefix / 'LICENSE.TXT')
    return prefix


def build_own_runtime(stxlib: Path, libcxx: Path, out: Path) -> Path:
    tree: Path = out / 'own-runtime'
    toolchain: Path = ROOT / 'cmake/llvm22-windows-msvc.cmake'
    if platform.system() != 'Windows':
        toolchain = cross_toolchain(out / 'gui-forms-cross.cmake', toolchain)
    shutil.rmtree(tree, ignore_errors=True)
    run('cmake', '-S', stxlib / 'src/hosted/windows/own_runtime', '-B', tree, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}',
        '-DGUI_FORMS_MSVC_RUNTIME=OWN', f'-DGUI_FORMS_OWN_RUNTIME_SOURCE={stxlib.as_posix()}',
        f'-DGUI_FORMS_LIBCXX_PREFIX={libcxx.as_posix()}')
    run('cmake', '--build', tree)
    return tree


def settings(out: Path) -> dict[str, str]:
    return {'GUI_FORMS_OWN_RUNTIME_SOURCE': (out / 'stxlib').as_posix(),
            'GUI_FORMS_OWN_RUNTIME_PREFIX': (out / 'own-runtime').as_posix(),
            'GUI_FORMS_LIBCXX_PREFIX': (out / 'libcxx').as_posix()}


def command_build(arguments: argparse.Namespace) -> None:
    out: Path = arguments.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    repository: str
    commit: str
    repository, commit = stxlib_pin()
    stamp: dict[str, str] = stamp_of(commit)
    stamp_path: Path = out / 'stamp.json'
    current: bool = (stamp_path.is_file()
                     and json.loads(stamp_path.read_text(encoding='utf-8')) == stamp
                     and (out / 'own-runtime/lib/ucrtbase.lib').is_file()
                     and (out / 'libcxx/lib/libc++.lib').is_file()
                     and (out / 'libcxx/LICENSE.TXT').is_file())
    if current:
        print(f'own runtime: reused (STXlib {commit[:7]}, LLVM {LLVM_VERSION})', flush=True)
    else:
        stamp_path.unlink(missing_ok=True)
        fetch_stxlib(repository, commit, out / 'stxlib')
        llvm_source: Path = fetch_llvm_source(out)
        libcxx: Path = build_libcxx(out / 'stxlib', llvm_source, out, arguments.jobs)
        build_own_runtime(out / 'stxlib', libcxx, out)
        # The source and the libc++ build tree are not needed again.
        shutil.rmtree(llvm_source, ignore_errors=True)
        shutil.rmtree(out / 'libcxx-tree', ignore_errors=True)
        stamp_path.write_text(json.dumps(stamp, indent=2) + '\n', encoding='utf-8')
        print(f'own runtime: built (STXlib {commit[:7]}, LLVM {LLVM_VERSION})', flush=True)
    values: dict[str, str] = settings(out)
    name: str
    value: str
    for name, value in values.items():
        print(f'{name}={value}')
    github_env: str = os.environ.get('GITHUB_ENV', '')
    if arguments.github_env and github_env:
        with open(github_env, 'a', encoding='utf-8') as output:
            for name, value in values.items():
                output.write(f'{name}={value}\n')


def cache_values(tree: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    line: str
    for line in (tree / 'CMakeCache.txt').read_text(encoding='utf-8', errors='replace').splitlines():
        match: re.Match[str] | None = re.match(r'^([A-Za-z0-9_]+):[A-Z]+=(.*)$', line)
        if match is not None:
            values[match.group(1)] = match.group(2)
    return values


def normal(path: str) -> str:
    return os.path.normcase(os.path.normpath(path.strip().replace('\\', '/'))).replace('\\', '/')


def check_include_list(tree: Path) -> list[str]:
    """Problems with the C++ #include search list the tree's compiler uses."""
    values: dict[str, str] = cache_values(tree)
    compiler: str = values['CMAKE_CXX_COMPILER']
    flags: list[str] = shlex.split(values.get('CMAKE_CXX_FLAGS', ''))
    target: str = values.get('CMAKE_CXX_COMPILER_TARGET', 'x86_64-pc-windows-msvc')
    result: subprocess.CompletedProcess[str] = subprocess.run(
        [compiler, f'--target={target}', *flags, '-v', '-E', '-x', 'c++', '-o', os.devnull, '-'],
        input='', capture_output=True, text=True, check=True)
    listing: list[str] = []
    inside: bool = False
    line: str
    for line in result.stderr.splitlines():
        if line.startswith('#include <...> search starts here:'):
            inside = True
        elif line.startswith('End of search list.'):
            inside = False
        elif inside:
            listing.append(line.strip())
    resource: str = subprocess.run([compiler, f'--target={target}', '-print-resource-dir'], capture_output=True,
                                   text=True, check=True).stdout.strip()
    source: str = values.get('GUI_FORMS_OWN_RUNTIME_SOURCE', '')
    expected: list[str] = [normal(values.get('GUI_FORMS_LIBCXX_PREFIX', '') + '/include/c++/v1'),
                           normal(resource + '/include'),
                           normal(source + '/src/hosted/windows/crt/include'),
                           normal(source + '/src/hosted/windows/api/include')]
    print('#include <...> search list (clang -v):', flush=True)
    for line in listing:
        print('  ' + line)
    problems: list[str] = []
    if [normal(entry) for entry in listing] != expected:
        problems.append('the #include search list is not libc++, clang resource, STXlib C library, STXlib '
                        'Windows API: ' + '; '.join(listing))
    return problems


def check_link_maps(tree: Path) -> list[str]:
    """Problems in the tree's link maps: an input from a forbidden folder."""
    # lld-link /MAP writes <image>.map beside <image>.exe or .dll; other .map
    # files (export version scripts) are not link maps.
    maps: list[Path] = sorted(path for path in tree.rglob('*.map')
                              if path.with_suffix('.exe').is_file() or path.with_suffix('.dll').is_file())
    problems: list[str] = []
    if not maps:
        return ['no link map under ' + str(tree) + ' (configure with -DGUI_FORMS_LINK_MAP=ON)']
    inputs: set[str] = set()
    link_map: Path
    for link_map in maps:
        text: str = link_map.read_text(encoding='utf-8', errors='replace')
        found: list[str] = re.findall(r'([A-Za-z]:[\\/][^\s:(]+|/[^\s:(]+|[^\s:(]+)\.(lib|obj|o|a|res)\b', text)
        stem: str
        suffix: str
        for stem, suffix in found:
            inputs.add(f'{stem}.{suffix}')
        line: str
        for line in text.splitlines():
            if FORBIDDEN_FOLDERS.search(line):
                problems.append(f'{link_map.relative_to(tree)}: {line.strip()}')
                break
    libraries: list[str] = sorted({Path(name.replace('\\', '/')).name for name in inputs
                                   if name.lower().endswith('.lib')})
    print(f'link maps: {len(maps)}; libraries read: {", ".join(libraries)}', flush=True)
    return problems


def command_evidence(arguments: argparse.Namespace) -> None:
    tree: Path = arguments.build.resolve()
    problems: list[str] = check_include_list(tree) + check_link_maps(tree)
    if arguments.dlls is not None:
        sys.path.insert(0, str(ROOT / 'tools'))
        from ci_build import check_windows_sdk_imports
        try:
            check_windows_sdk_imports(arguments.dlls.resolve())
        except RuntimeError as error:
            problems.append(str(error))
    problem: str
    for problem in problems:
        print('own runtime evidence: ' + problem, file=sys.stderr)
    if problems:
        raise SystemExit(1)
    print('own runtime evidence: headers, libraries and DLL imports are the own runtime\'s only', flush=True)


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    commands: argparse._SubParsersAction = parser.add_subparsers(dest='command', required=True)
    build: argparse.ArgumentParser = commands.add_parser('build', help='build the own runtime into --out')
    build.add_argument('--out', type=Path, required=True)
    build.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    build.add_argument('--github-env', action='store_true', help='append the settings to $GITHUB_ENV')
    evidence: argparse.ArgumentParser = commands.add_parser('evidence', help='prove a tree used the own runtime')
    evidence.add_argument('--build', type=Path, required=True, help='a configured and built tree')
    evidence.add_argument('--dlls', type=Path, help='a folder whose DLLs\' imports are checked')
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.command == 'build':
        command_build(arguments)
    else:
        command_evidence(arguments)


if __name__ == '__main__':
    main()
