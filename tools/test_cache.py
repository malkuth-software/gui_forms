#!/usr/bin/env python3
"""Pack and publish the test cache that malkuth-software/gui_forms_tests builds against.

The tests live in their own repository (owner, 2026-10-29 and 2026-11-17)
and never compile GUI.Forms: they link the libraries a tree built with
GUI_FORMS_TEST_CACHE=ON (cmake/TestCache.cmake), through imported targets of
the same names. A test cache is one archive per tested commit and platform:

    gui_forms_test_cache-<platform>[-<kind>].tar.gz   (and .sha256 beside it)
      gui_forms_test_cache/
        manifest.json            format, commit and tree, platform, kind,
                                 compiler, settings, counts
        GUIFormsTestCache.cmake  the loader: variables, directory settings,
                                 stx packages, one imported target per target
        settings.cmake           the initial cache (cmake -C) of a consumer
                                 configure: the toolchain file, build type
                                 and flags the tree was built with
        source/                  the gui_forms tree at the commit (headers,
                                 private headers under src/, assets, manifests,
                                 cmake modules and tools the tests read),
                                 without .github/
        build/                   the part of the build tree the tests read:
                                 each target's file (a library, an object
                                 library's objects, a program or bundle), the
                                 generated headers, and the stx package
        prefix/                  the SDK installed from the tree (cmake --install)

Every build and source path the description holds is written relative to the
archive, so it unpacks anywhere. The loader is what gui_forms_tests'
CMakeLists.txt includes; gui_forms_tests' tools/fetch_test_cache.py fetches the
archive its gui_forms.lock names and checks the digest.

Commands:
  pack     --build-dir B --platform P [--kind normal|sanitize|high-integrity] --output DIR
           packs tree B (configured with GUI_FORMS_TEST_CACHE=ON and built,
           its install step included) into DIR, printing the archive's path.
  publish  --archive A --commit C [--repository R]
           uploads A and its .sha256 to the prerelease test-cache-<C>
           (created at C when absent). C is the commit CI tested, which may
           differ from the commit A was packed from when CI promoted or
           reused a validation of the same tree (the manifest names both
           trees; publish refuses a different one). A rerun never replaces
           an asset with different bytes; equal bytes pass.

A failed pack leaves the partial staging directory under DIR/staging and no
archive; a failed publish leaves the release as it was.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import typing

ROOT: Path = Path(__file__).resolve().parents[1]
FORMAT: int = 1
ARCHIVE_ROOT: str = 'gui_forms_test_cache'
DEFAULT_REPOSITORY: str = 'malkuth-software/gui_forms'
# Placeholders the loader replaces with the unpacked cache's directories.
SOURCE_TOKEN: str = '@GF_TC_SOURCE@'
BINARY_TOKEN: str = '@GF_TC_BINARY@'
# The generated files of the build tree the tests read (besides each target's
# own file): headers and the inputs generated beside them.
# The tests also read the generated manifests and lists (gf_abi/*.json, the
# export lists).
GENERATED_SUFFIXES: frozenset[str] = frozenset({'.h', '.hpp', '.hh', '.inc', '.inl', '.ipp', '.def', '.modulemap',
                                                '.json', '.txt'})
# Build-tree directories never packed: CMake's and CTest's own, and the cache's.
SKIPPED_BUILD_DIRECTORIES: frozenset[str] = frozenset({'CMakeFiles', 'Testing', 'test-cache', '.cmake'})
# The target types the loader imports, and how.
LIBRARY_TYPES: dict[str, str] = {'STATIC_LIBRARY': 'STATIC', 'SHARED_LIBRARY': 'SHARED',
                                 'MODULE_LIBRARY': 'MODULE', 'OBJECT_LIBRARY': 'OBJECT',
                                 'INTERFACE_LIBRARY': 'INTERFACE'}
INTERFACE_PROPERTIES: tuple[str, ...] = (
    'INTERFACE_INCLUDE_DIRECTORIES', 'INTERFACE_SYSTEM_INCLUDE_DIRECTORIES', 'INTERFACE_COMPILE_DEFINITIONS',
    'INTERFACE_COMPILE_OPTIONS', 'INTERFACE_COMPILE_FEATURES', 'INTERFACE_LINK_LIBRARIES',
    'INTERFACE_LINK_OPTIONS', 'INTERFACE_LINK_DIRECTORIES', 'INTERFACE_LINK_DEPENDS', 'INTERFACE_SOURCES',
    'INTERFACE_PRECOMPILE_HEADERS', 'INTERFACE_POSITION_INDEPENDENT_CODE')
# What cmake/TestCache.cmake records of a foreign imported target.
FOREIGN_PROPERTIES: tuple[str, ...] = INTERFACE_PROPERTIES + (
    'IMPORTED_LOCATION', 'IMPORTED_LOCATION_RELEASE', 'IMPORTED_LOCATION_NOCONFIG', 'IMPORTED_IMPLIB',
    'IMPORTED_IMPLIB_RELEASE', 'IMPORTED_CONFIGURATIONS', 'IMPORTED_LINK_INTERFACE_LANGUAGES', 'IMPORTED_SONAME',
    'IMPORTED_NO_SONAME')
WINDOWS_IMPORT_SUFFIX: re.Pattern[str] = re.compile(r'\.(lib|dll\.a)$', re.IGNORECASE)
SET_PATTERN: re.Pattern[str] = re.compile(r'^set\(([A-Za-z0-9_.+:-]+) (?:\[==\[(.*?)\]==\]|([A-Za-z_]+))\)$',
                                          re.MULTILINE | re.DOTALL)
ALIAS_PATTERN: re.Pattern[str] = re.compile(r'add_(library|executable)\(\s*([A-Za-z0-9_:]+)\s+ALIAS\s+([A-Za-z0-9_]+)\s*\)')


def read_cmake_sets(path: Path) -> dict[str, str]:
    """The set(NAME [==[value]==]) and set(NAME WORD) lines of a file
    cmake/TestCache.cmake wrote, as a dictionary (a later line wins)."""
    values: dict[str, str] = {}
    match: re.Match[str]
    for match in SET_PATTERN.finditer(path.read_text(encoding='utf-8')):
        bracketed: str | None = match.group(2)
        values[match.group(1)] = bracketed if bracketed is not None else match.group(3)
    return values


def cmake_list(value: str) -> list[str]:
    """A CMake list's entries (generator expressions keep their semicolons
    inside $<...>)."""
    entries: list[str] = []
    depth: int = 0
    current: str = ''
    index: int = 0
    while index < len(value):
        character: str = value[index]
        if value.startswith('$<', index):
            depth += 1
            current += '$<'
            index += 2
            continue
        if character == '>' and depth > 0:
            depth -= 1
        if character == ';' and depth == 0:
            entries.append(current)
            current = ''
        else:
            current += character
        index += 1
    if current:
        entries.append(current)
    return [entry for entry in entries if entry]


class Relocation:
    """Rewrites the build and source directories of one tree into the
    cache's placeholders, the longer prefix first (a build tree inside the
    source tree, the usual .build/ layout, keeps its own prefix)."""

    def __init__(self, source: Path, binary: Path, package: Path) -> None:
        pairs: list[tuple[str, str]] = [(source.as_posix(), SOURCE_TOKEN), (binary.as_posix(), BINARY_TOKEN)]
        if not under(package, binary):
            # A package restored outside the tree (GUI_FORMS_STX_PACKAGE_DIR) is packed as build/stx-package.
            pairs.append((package.as_posix(), BINARY_TOKEN + '/stx-package'))
        pairs.sort(key=len_of_first, reverse=True)
        self.pairs: list[tuple[str, str]] = pairs

    def text(self, value: str) -> str:
        prefix: str
        token: str
        for prefix, token in self.pairs:
            value = value.replace(prefix, token)
        return value


def len_of_first(pair: tuple[str, str]) -> int:
    """The sort key that puts the longest directory prefix first."""
    return len(pair[0])


def bracket(value: str) -> str:
    """value as a CMake bracket argument."""
    if ']==]' in value:
        raise ValueError('a value holds "]==]", which the loader cannot quote: ' + value[:80])
    return '[==[' + value + ']==]'


def git(source: Path, *arguments: str) -> str:
    """The stripped output of a git command in source."""
    completed: subprocess.CompletedProcess[str] = subprocess.run(['git', '-C', str(source), *arguments],
                                                                 check=True, capture_output=True, text=True)
    return completed.stdout.strip()


def copy_file(source: Path, destination: Path) -> int:
    """Copies one file (or symbolic link, as a link); returns its size."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        if destination.is_symlink() or destination.exists():
            destination.unlink()
        os.symlink(os.readlink(source), destination)
        return 0
    shutil.copy2(source, destination)
    return source.stat().st_size


def copy_tree(source: Path, destination: Path) -> int:
    """Copies a directory whole, links as links; returns the bytes copied."""
    total: int = 0
    path: Path
    for path in sorted(source.rglob('*')):
        if path.is_dir() and not path.is_symlink():
            continue
        total += copy_file(path, destination / path.relative_to(source))
    return total


def stage_source(source: Path, staging: Path) -> int:
    """Copies the tracked and untracked (not ignored) files of the source
    tree, without .github/, into staging/source; returns the file count."""
    listing: str = subprocess.run(['git', '-C', str(source), 'ls-files', '-z', '--cached', '--others',
                                   '--exclude-standard'], check=True, capture_output=True, text=True).stdout
    count: int = 0
    name: str
    for name in sorted(set(listing.split('\0'))):
        if not name or name.startswith('.github/'):
            continue
        path: Path = source / name
        if not path.is_file():
            continue  # deleted in the working tree
        copy_file(path, staging / 'source' / name)
        count += 1
    return count


def under(path: Path, directory: Path) -> bool:
    """Whether path lies inside directory."""
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def stage_build(binary: Path, source: Path, description: dict[str, str], locations: dict[str, str],
                staging: Path) -> tuple[int, int]:
    """Copies into staging/build each target's file (or bundle, or objects),
    the generated headers and the stx package; returns (files, bytes)."""
    files: int = 0
    total: int = 0
    build: Path = staging / 'build'
    wanted: list[Path] = []
    key: str
    value: str
    for key, value in locations.items():
        if key.startswith('GF_TC_BUNDLE_DIR_'):
            bundle: Path = Path(value)
            total += copy_tree(bundle, build / bundle.relative_to(binary))
            files += 1
        elif key.startswith(('GF_TC_FILE_', 'GF_TC_LINKER_FILE_', 'GF_TC_SONAME_FILE_')):
            wanted.append(Path(value))
        elif key.startswith('GF_TC_OBJECTS_'):
            wanted.extend(Path(entry) for entry in cmake_list(value))
    path: Path
    for path in wanted:
        if not under(path, binary):
            raise ValueError('a target file lies outside the build tree: ' + str(path))
        if not path.exists():
            raise ValueError('a target file was not built (build the tree first): ' + str(path))
        total += copy_file(path, build / path.relative_to(binary))
        files += 1
    # The generated headers, wherever the tree writes them. The source tree
    # is skipped when the build tree lies inside it (.build/ layouts) only
    # where it is not the build tree.
    for path in sorted(binary.rglob('*')):
        relative: Path = path.relative_to(binary)
        if any(part in SKIPPED_BUILD_DIRECTORIES for part in relative.parts):
            continue
        if path.is_file() and path.suffix in GENERATED_SUFFIXES:
            destination: Path = build / relative
            if not destination.exists():
                total += copy_file(path, destination)
                files += 1
    # The build-tree files the configure names in a variable (the
    # localizable-literal plugin, generated packs): a consumer reads them by
    # that variable.
    for key, value in description.items():
        if key.startswith('GF_TC_') or '\n' in value or ';' in value or len(value) > 1024 or not Path(value).is_absolute():
            continue
        named: Path = Path(value)
        if under(named, binary) and named.is_file():
            destination = build / named.relative_to(binary)
            if not destination.exists():
                total += copy_file(named, destination)
                files += 1
    # The stx package (the libraries GUI.Forms links, and their configs).
    package: Path = Path(description['GUI_FORMS_STX_PACKAGE_ROOT'])
    for part in ('stx', 'build'):
        if (package / part).is_dir():
            target: Path = build / 'stx-package' / part
            if under(package, binary):
                # The header sweep above may have made the directory already.
                target = build / package.relative_to(binary) / part
            total += copy_tree(package / part, target)
    return files, total


def loader_text(description: dict[str, str], locations: dict[str, str], relocation: Relocation,
                aliases: list[tuple[str, str, str]], package_relative: str) -> str:
    """The GUIFormsTestCache.cmake the tests include."""
    out: list[str] = []
    out.append('# GUI.Forms test cache loader, written by gui_forms tools/test_cache.py.')
    out.append('# Include once from the top-level CMakeLists.txt, before any test target.')
    out.append('cmake_minimum_required(VERSION 3.25)')
    out.append('get_filename_component(GUI_FORMS_TEST_CACHE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)')
    out.append('set(GUI_FORMS_SOURCE_DIR "${GUI_FORMS_TEST_CACHE_DIR}/source")')
    out.append('set(GUI_FORMS_BINARY_DIR "${GUI_FORMS_TEST_CACHE_DIR}/build")')
    out.append('set(GUI_FORMS_TEST_CACHE_PREFIX "${GUI_FORMS_TEST_CACHE_DIR}/prefix")')
    out.append(f'set(GUI_FORMS_VERSION {bracket(description["GF_TC_VERSION"])})')
    out.append('# A value with the cache\'s placeholders, made absolute.')
    out.append('function(_gf_tc_resolve output value)')
    out.append(f'    string(REPLACE "{SOURCE_TOKEN}" "${{GUI_FORMS_SOURCE_DIR}}" value "${{value}}")')
    out.append(f'    string(REPLACE "{BINARY_TOKEN}" "${{GUI_FORMS_BINARY_DIR}}" value "${{value}}")')
    out.append('    set(${output} "${value}" PARENT_SCOPE)')
    out.append('endfunction()')
    out.append('macro(_gf_tc_set name value)')
    out.append('    _gf_tc_resolve(_gf_tc_value "${value}")')
    out.append('    set(${name} "${_gf_tc_value}")')
    out.append('endmacro()')
    out.append('function(_gf_tc_property target property value)')
    out.append('    _gf_tc_resolve(resolved "${value}")')
    out.append('    set_property(TARGET ${target} PROPERTY ${property} "${resolved}")')
    out.append('endfunction()')
    out.append('')
    out.append('# The variables the gui_forms configure left at directory scope.')
    name: str
    for name in cmake_list(description.get('GF_TC_VARIABLES', '')):
        if name in description:
            out.append(f'_gf_tc_set({name} {bracket(relocation.text(description[name]))})')
    out.append('')
    out.append('# The directory scope\'s compile and link settings.')
    for name in ('COMPILE_OPTIONS', 'COMPILE_DEFINITIONS', 'INCLUDE_DIRECTORIES', 'LINK_OPTIONS',
                 'LINK_DIRECTORIES'):
        value: str = relocation.text(description.get('GF_TC_DIRECTORY_' + name, ''))
        out.append(f'_gf_tc_resolve(_gf_tc_value {bracket(value)})')
        out.append(f'set_property(DIRECTORY "${{CMAKE_SOURCE_DIR}}" APPEND PROPERTY {name} ${{_gf_tc_value}})')
    out.append('')
    out.append('# The stx packages GUI.Forms links (cmake/Stx.cmake).')
    out.append(f'set(GUI_FORMS_STX_PACKAGE_ROOT "${{GUI_FORMS_BINARY_DIR}}/{package_relative}")')
    out.append('set(GUI_FORMS_STX_PREFIX_DIR "${GUI_FORMS_STX_PACKAGE_ROOT}/stx")')
    out.append('set(GUI_FORMS_STX_BUILD_PREFIX_DIR "${GUI_FORMS_STX_PACKAGE_ROOT}/build")')
    out.append('list(PREPEND CMAKE_PREFIX_PATH "${GUI_FORMS_STX_PREFIX_DIR}" "${GUI_FORMS_STX_BUILD_PREFIX_DIR}")')
    package: str
    for package in ('stxlib', 'stx_zlib', 'stx_png', 'stx_conv', 'stx_raster3d', 'stx_rtf2md'):
        out.append(f'find_package({package} CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${{GUI_FORMS_STX_PREFIX_DIR}}")')
    out.append('if(EXISTS "${GUI_FORMS_STX_PREFIX_DIR}/lib/cmake/stx_vorbis")')
    out.append('    find_package(stx_vorbis CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_PREFIX_DIR}")')
    out.append('endif()')
    out.append('find_package(stx_skia CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GUI_FORMS_STX_BUILD_PREFIX_DIR}")')
    out.append('set(THREADS_PREFER_PTHREAD_FLAG ON)')
    out.append('find_package(Threads REQUIRED)')
    out.append('')
    out.append('# The foreign imported targets the tree found (system libraries).')
    foreign_kinds: dict[str, str] = {'STATIC_LIBRARY': 'STATIC', 'SHARED_LIBRARY': 'SHARED',
                                     'MODULE_LIBRARY': 'MODULE', 'UNKNOWN_LIBRARY': 'UNKNOWN',
                                     'INTERFACE_LIBRARY': 'INTERFACE'}
    foreign: str
    for foreign in cmake_list(description.get('GF_TC_FOREIGN', '')):
        kind: str = description['GF_TC_FOREIGN_TYPE_' + foreign]
        out.append(f'if(NOT TARGET {foreign})')
        if kind == 'EXECUTABLE':
            out.append(f'    add_executable({foreign} IMPORTED GLOBAL)')
        else:
            out.append(f'    add_library({foreign} {foreign_kinds[kind]} IMPORTED GLOBAL)')
        prop: str
        for prop in FOREIGN_PROPERTIES:
            value: str | None = description.get(f'GF_TC_FOREIGN_{prop}_{foreign}')
            if value:
                out.append(f'    _gf_tc_property({foreign} {prop} {bracket(relocation.text(value))})')
        out.append('endif()')
    out.append('')
    out.append('# One imported target per target of the tree, under its own name.')
    targets: list[str] = cmake_list(description.get('GF_TC_TARGETS', ''))
    target: str
    for target in targets:
        kind: str = description['GF_TC_TYPE_' + target]
        if kind == 'EXECUTABLE':
            out.append(f'add_executable({target} IMPORTED GLOBAL)')
        else:
            out.append(f'add_library({target} {LIBRARY_TYPES[kind]} IMPORTED GLOBAL)')
        # Not a system library to its consumers: its headers are first-party
        # in the tests, as they were in the tree (warnings, the effect pass).
        out.append(f'set_property(TARGET {target} PROPERTY SYSTEM FALSE)')
        if kind == 'OBJECT_LIBRARY':
            objects: str = relocation.text(locations['GF_TC_OBJECTS_' + target])
            out.append(f'_gf_tc_property({target} IMPORTED_OBJECTS {bracket(objects)})')
        elif kind != 'INTERFACE_LIBRARY':
            location: str = relocation.text(locations['GF_TC_FILE_' + target])
            out.append(f'_gf_tc_property({target} IMPORTED_LOCATION {bracket(location)})')
            linker: str | None = locations.get('GF_TC_LINKER_FILE_' + target)
            if linker and WINDOWS_IMPORT_SUFFIX.search(linker):
                out.append(f'_gf_tc_property({target} IMPORTED_IMPLIB {bracket(relocation.text(linker))})')
            if kind == 'STATIC_LIBRARY':
                out.append(f'set_property(TARGET {target} PROPERTY IMPORTED_LINK_INTERFACE_LANGUAGES C CXX)')
            if description.get('GF_TC_BUNDLE_' + target) == 'ON':
                out.append(f'set_property(TARGET {target} PROPERTY MACOSX_BUNDLE TRUE)')
        prop: str
        for prop in INTERFACE_PROPERTIES:
            raw: str | None = description.get(f'GF_TC_{prop}_{target}')
            if raw:
                out.append(f'_gf_tc_property({target} {prop} {bracket(relocation.text(raw))})')
    out.append('')
    out.append('# The tree\'s custom targets, empty: their outputs are in the cache.')
    for target in cmake_list(description.get('GF_TC_UTILITIES', '')):
        out.append(f'if(NOT TARGET {target})')
        out.append(f'    add_custom_target({target})')
        out.append('endif()')
    out.append('')
    out.append('# The aliases the tree defines (GUIForms::Core and the rest).')
    command: str
    alias: str
    for command, alias, target in aliases:
        if target in targets:
            out.append(f'if(NOT TARGET {alias})')
            out.append(f'    {command}({alias} ALIAS {target})')
            out.append('endif()')
    return '\n'.join(out) + '\n'


def settings_text(settings: dict[str, str]) -> str:
    """The settings.cmake a consumer configures with (cmake -C): every
    setting the tree was configured with, as a cache entry, its paths
    resolved against the unpacked cache."""
    out: list[str] = ['# The settings the gui_forms tree of this test cache was configured with;',
                      '# configure the tests with cmake -C <this file> (gui_forms tools/test_cache.py).']
    name: str
    value: str
    for name, value in sorted(settings.items()):
        if not value:
            continue
        resolved: str = value.replace(SOURCE_TOKEN, '${CMAKE_CURRENT_LIST_DIR}/source').replace(
            BINARY_TOKEN, '${CMAKE_CURRENT_LIST_DIR}/build')
        kind: str = 'FILEPATH' if name == 'CMAKE_TOOLCHAIN_FILE' else 'STRING'
        # Bracket arguments do not expand variables: quote, escaping what a
        # quoted argument would read.
        quoted: str = resolved.replace('\\', '\\\\').replace('"', '\\"')
        out.append(f'set({name} "{quoted}" CACHE {kind} "gui_forms test cache setting")')
    return '\n'.join(out) + '\n'


def tree_aliases(source: Path) -> list[tuple[str, str, str]]:
    """Every add_library or add_executable(<alias> ALIAS <target>) in the
    tree's CMake files, as (command, alias, target)."""
    found: list[tuple[str, str, str]] = []
    paths: list[Path] = [source / 'CMakeLists.txt', *sorted((source / 'cmake').rglob('*.cmake')),
                         *sorted(source.glob('*/CMakeLists.txt'))]
    path: Path
    for path in paths:
        match: re.Match[str]
        for match in ALIAS_PATTERN.finditer(path.read_text(encoding='utf-8', errors='replace')):
            alias: tuple[str, str, str] = ('add_' + match.group(1), match.group(2), match.group(3))
            if alias not in found:
                found.append(alias)
    return found


def file_digest(path: Path) -> str:
    """The SHA-256 of a file, in hexadecimal."""
    digest: hashlib._Hash = hashlib.sha256()
    with path.open('rb') as stream:
        chunk: bytes = stream.read(1 << 20)
        while chunk:
            digest.update(chunk)
            chunk = stream.read(1 << 20)
    return digest.hexdigest()


def reset_tar_info(info: tarfile.TarInfo) -> tarfile.TarInfo:
    """Fixed ownership and times, so equal trees give equal archives."""
    info.uid = 0
    info.gid = 0
    info.uname = ''
    info.gname = ''
    info.mtime = 0
    return info


def write_archive(staging_root: Path, archive: Path) -> None:
    """A deterministic .tar.gz of staging_root (its name is the top entry)."""
    entries: list[Path] = [staging_root, *sorted(staging_root.rglob('*'))]
    with archive.open('wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0, compresslevel=6) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as tar:
                path: Path
                for path in entries:
                    name: str = path.relative_to(staging_root.parent).as_posix()
                    tar.add(str(path), arcname=name, recursive=False, filter=reset_tar_info)


def pack(arguments: argparse.Namespace) -> int:
    binary: Path = arguments.build_dir.resolve()
    description_path: Path = binary / 'test-cache' / 'description.cmake'
    locations_path: Path = binary / 'test-cache' / 'locations.cmake'
    if not description_path.is_file() or not locations_path.is_file():
        print(f'test_cache: {binary} was not configured with -DGUI_FORMS_TEST_CACHE=ON', file=sys.stderr)
        return 1
    description: dict[str, str] = read_cmake_sets(description_path)
    locations: dict[str, str] = read_cmake_sets(locations_path)
    source: Path = Path(description['GF_TC_SOURCE_DIR']).resolve()
    commit: str = git(source, 'rev-parse', 'HEAD')
    dirty: bool = bool(git(source, 'status', '--porcelain', '--untracked-files=no'))
    output: Path = arguments.output.resolve()
    staging_parent: Path = output / 'staging'
    if staging_parent.exists():
        shutil.rmtree(staging_parent)
    staging: Path = staging_parent / ARCHIVE_ROOT
    staging.mkdir(parents=True)
    source_files: int = stage_source(source, staging)
    build_files: int
    build_bytes: int
    build_files, build_bytes = stage_build(binary, source, description, locations, staging)
    installed: subprocess.CompletedProcess[str] = subprocess.run(
        ['cmake', '--install', str(binary), '--prefix', str(staging / 'prefix')],
        capture_output=True, text=True)
    if installed.returncode != 0:
        print(installed.stdout[-4000:] + installed.stderr[-4000:], file=sys.stderr)
        print('test_cache: the install step failed', file=sys.stderr)
        return 1
    package: Path = Path(description['GUI_FORMS_STX_PACKAGE_ROOT'])
    package_relative: str = package.relative_to(binary).as_posix() if under(package, binary) else 'stx-package'
    relocation: Relocation = Relocation(source, binary, package)
    (staging / 'GUIFormsTestCache.cmake').write_text(
        loader_text(description, locations, relocation, tree_aliases(source), package_relative), encoding='utf-8')
    settings: dict[str, str] = {}
    key: str
    for key, value in description.items():
        if key.startswith('GF_TC_SETTING_'):
            settings[key[len('GF_TC_SETTING_'):]] = relocation.text(value)
    (staging / 'settings.cmake').write_text(settings_text(settings), encoding='utf-8')
    manifest: dict[str, object] = {
        'format': FORMAT, 'repository': DEFAULT_REPOSITORY, 'commit': commit, 'dirty': dirty,
        'tree': git(source, 'rev-parse', 'HEAD^{tree}'),
        'platform': arguments.platform, 'kind': arguments.kind, 'version': description['GF_TC_VERSION'],
        'system': description['GF_TC_SYSTEM_NAME'], 'cxx_compiler_version': description['GF_TC_CXX_COMPILER_VERSION'],
        'settings': settings, 'targets': len(cmake_list(description.get('GF_TC_TARGETS', ''))),
        'source_files': source_files, 'build_files': build_files, 'build_bytes': build_bytes,
    }
    (staging / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    suffix: str = '' if arguments.kind == 'normal' else '-' + arguments.kind
    archive: Path = output / f'{ARCHIVE_ROOT}-{arguments.platform}{suffix}.tar.gz'
    write_archive(staging, archive)
    digest: str = file_digest(archive)
    Path(str(archive) + '.sha256').write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
    if not arguments.keep_staging:
        shutil.rmtree(staging_parent)
    size: int = archive.stat().st_size
    print(f'test_cache: {archive.name}: {manifest["targets"]} targets, {source_files} source files, '
          f'{build_files} build files, {size / 1e6:.1f} MB, sha256 {digest}', flush=True)
    print(archive)
    return 0


def gh(*arguments: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    """Runs the GitHub CLI."""
    return subprocess.run(['gh', *arguments], check=check, capture_output=True, text=True)


ARCHIVE_PATTERN: re.Pattern[str] = re.compile(r'^gui_forms_test_cache-[a-z0-9-]+\.tar\.gz$')


def publish(arguments: argparse.Namespace) -> int:
    archive: Path = arguments.archive.resolve()
    match: re.Match[str] | None = ARCHIVE_PATTERN.match(archive.name)
    digest_file: Path = Path(str(archive) + '.sha256')
    if match is None or not archive.is_file() or not digest_file.is_file():
        print(f'test_cache: {archive} is not a packed test cache with its .sha256', file=sys.stderr)
        return 1
    if digest_file.read_text(encoding='utf-8').split()[0] != file_digest(archive):
        print('test_cache: the archive does not match its .sha256', file=sys.stderr)
        return 1
    commit: str = arguments.commit
    with tarfile.open(archive, 'r:gz') as tar:
        member: typing.IO[bytes] | None = tar.extractfile(ARCHIVE_ROOT + '/manifest.json')
        manifest: dict = json.loads(member.read().decode('utf-8')) if member else {}
    tree: str = git(ROOT, 'rev-parse', commit + '^{tree}')
    if manifest.get('tree') != tree or manifest.get('dirty'):
        print(f'test_cache: {archive.name} was packed from tree {manifest.get("tree")} '
              f'(dirty: {manifest.get("dirty")}), not {commit}\'s tree {tree}', file=sys.stderr)
        return 1
    tag: str = 'test-cache-' + commit
    repository: str = arguments.repository
    if gh('release', 'view', tag, '--repo', repository, check=False).returncode != 0:
        notes: str = ('The test caches malkuth-software/gui_forms_tests builds against, one per platform, '
                      f'packed by tools/test_cache.py from {commit}. Each archive has a .sha256 beside it; '
                      'gui_forms_tests\' gui_forms.lock names the commit and the digests. Build inputs, '
                      'not an application release.')
        created: subprocess.CompletedProcess[str] = gh('release', 'create', tag, '--repo', repository,
                                                       '--target', commit, '--prerelease', '--title',
                                                       'Test cache ' + commit[:9], '--notes', notes, check=False)
        if created.returncode != 0:
            gh('release', 'view', tag, '--repo', repository)  # another platform won the race
    path: Path
    for path in (archive, digest_file):
        uploaded: subprocess.CompletedProcess[str] = gh('release', 'upload', tag, str(path), '--repo', repository,
                                                        check=False)
        if uploaded.returncode == 0:
            continue
        with tempfile.TemporaryDirectory() as directory:
            gh('release', 'download', tag, '--repo', repository, '--pattern', path.name, '--dir', directory)
            if file_digest(Path(directory) / path.name) != file_digest(path):
                print(f'test_cache: refusing to replace the published {path.name} with different bytes',
                      file=sys.stderr)
                return 1
    print(f'test_cache: published {archive.name} to {repository} release {tag}')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands: argparse._SubParsersAction = parser.add_subparsers(dest='command', required=True)
    pack_parser: argparse.ArgumentParser = commands.add_parser('pack', help='pack a built tree')
    pack_parser.add_argument('--build-dir', type=Path, required=True)
    pack_parser.add_argument('--platform', required=True, help='macos-arm64, linux-x64, windows-x64, ...')
    pack_parser.add_argument('--kind', choices=('normal', 'sanitize', 'high-integrity', 'programs'), default='normal')
    pack_parser.add_argument('--output', type=Path, required=True)
    pack_parser.add_argument('--keep-staging', action='store_true', help='keep DIR/staging after packing')
    publish_parser: argparse.ArgumentParser = commands.add_parser('publish', help='upload a packed cache')
    publish_parser.add_argument('--archive', type=Path, required=True)
    publish_parser.add_argument('--commit', required=True, help='the commit CI tested (github.sha)')
    publish_parser.add_argument('--repository', default=DEFAULT_REPOSITORY)
    arguments: argparse.Namespace = parser.parse_args(argv)
    if arguments.command == 'pack':
        return pack(arguments)
    return publish(arguments)


if __name__ == '__main__':
    sys.exit(main())
