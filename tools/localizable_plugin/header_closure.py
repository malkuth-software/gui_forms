"""The LLVM and Clang files the localizable-literal plugin is built from.

    python -B tools/localizable_plugin/header_closure.py --clang CLANG++ --root PREFIX [--copy DEST]

Compiles the plugin's sources with CLANG++ against the headers under
PREFIX/include (syntax only, with a dependency file) and prints every LLVM
and Clang header they include, relative to PREFIX/include, then one line of
counts and bytes per top-level directory. On Windows the plugin also links
the import libraries PREFIX/lib/libclang-cpp.dll.a and libLLVM-22.dll.a,
which are listed too. With --copy, those files are copied to DEST at the
same relative paths, making a minimal LLVM prefix the plugin builds against
(tools/localizable_plugin/CMakeLists.txt, GUI_FORMS_LLVM_ROOT).

The list is the host's own: a header that only one platform includes is
listed only on that platform. The compile uses the plugin's definitions and
its C++ standard; it fails, and so does this tool, when a header is missing.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE: Path = Path(__file__).resolve().parent
SOURCES: tuple[str, ...] = ('localizable_plugin.cpp', 'effect_pass.cpp')
TOPS: tuple[str, ...] = ('llvm', 'llvm-c', 'clang', 'clang-c')
WINDOWS_LIBRARIES: tuple[str, ...] = ('lib/libclang-cpp.dll.a', 'lib/libLLVM-22.dll.a')


def dependency_paths(depfile: Path) -> list[str]:
    """The prerequisites a compiler dependency file names, with forward
    slashes. The file is `target: prerequisite...` with backslash-newline
    continuations; the target (whose Windows drive letter holds a colon of
    its own) is every token up to the first that ends in a colon."""
    tokens: list[str] = depfile.read_text(encoding='utf-8').replace('\\\n', ' ').split()
    first: int = 0
    while first < len(tokens) and not tokens[first].endswith(':'):
        first += 1
    paths: list[str] = []
    token: str
    for token in tokens[first + 1:]:
        paths.append(token.replace('\\', '/'))
    return paths


def closure(clang: str, include: Path) -> list[str]:
    """The headers under include/{llvm,llvm-c,clang,clang-c} the sources include."""
    found: set[str] = set()
    prefix: str = include.resolve().as_posix().rstrip('/') + '/'
    # The compiler searches its own prefix's include directory already, after
    # its C++ library; naming it again would put it first and break libc++'s
    # wrapper headers.
    compiler: str | None = shutil.which(clang)
    compiler_include: Path | None = Path(compiler).resolve().parent.parent / 'include' if compiler else None
    search: list[str] = [] if compiler_include == include.resolve() else ['-isystem', str(include)]
    with tempfile.TemporaryDirectory() as scratch:
        source: str
        for source in SOURCES:
            depfile: Path = Path(scratch) / (source + '.d')
            command: list[str] = [clang, '-std=c++17', '-fno-rtti', '-fsyntax-only',
                                  '-D__STDC_CONSTANT_MACROS', '-D__STDC_FORMAT_MACROS', '-D__STDC_LIMIT_MACROS',
                                  *search, '-MD', '-MF', str(depfile), str(HERE / source)]
            subprocess.run(command, check=True)
            path: str
            for path in dependency_paths(depfile):
                resolved: str = Path(path).resolve().as_posix()
                if not resolved.startswith(prefix):
                    continue
                relative: str = resolved[len(prefix):]
                if relative.split('/')[0] in TOPS:
                    found.add(relative)
    return sorted(found)


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--clang', required=True, help='the Clang 22 C++ compiler')
    parser.add_argument('--root', type=Path, required=True, help='the LLVM 22 prefix')
    parser.add_argument('--copy', type=Path, help='copy the files to this new prefix')
    args: argparse.Namespace = parser.parse_args()
    include: Path = args.root / 'include'
    headers: list[str] = closure(args.clang, include)
    files: list[str] = ['include/' + header for header in headers]
    if sys.platform == 'win32':
        files.extend(WINDOWS_LIBRARIES)
    totals: dict[str, list[int]] = {}
    name: str
    for name in files:
        print(name)
        group: str = '/'.join(name.split('/')[:2]) if name.startswith('include/') else 'lib'
        size: int = (args.root / name).stat().st_size
        total: list[int] = totals.setdefault(group, [0, 0])
        total[0] += 1
        total[1] += size
    for name in sorted(totals):
        print(f'header_closure: {name}: {totals[name][0]} files, {totals[name][1]} bytes', file=sys.stderr)
    if args.copy is not None:
        for name in files:
            destination: Path = args.copy / name
            os.makedirs(destination.parent, exist_ok=True)
            shutil.copy2(args.root / name, destination)
    return 0


if __name__ == '__main__':
    sys.exit(main())
