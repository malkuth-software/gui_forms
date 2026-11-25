#!/usr/bin/env python3
"""Name the changed inputs that make a newer build miss an older build's cache.

    cache_blast_radius.py OLD_WORKSPACE NEW_WORKSPACE [BUILD_DIRECTORY]

BUILD_DIRECTORY is relative to NEW_WORKSPACE (default
.build/native-macos-arm64/gui-forms). The script reads the dependencies Ninja
recorded for NEW's objects; an input counts as changed when the same
workspace-relative path in OLD is missing or holds other bytes, which is what
makes a compiler cache keyed under CCACHE_BASEDIR miss. It prints how many
objects read a changed input, the origins of those inputs and the inputs read
by the most objects. Inputs outside the workspace (the compiler's own headers,
the SDK) are listed separately: the compiler check covers them.
"""
from __future__ import annotations

import collections
import hashlib
from pathlib import Path
import subprocess
import sys


def recorded_dependencies(build: Path) -> dict[str, list[str]]:
    text: str = subprocess.run(['ninja', '-C', str(build), '-t', 'deps'],
                               capture_output=True, text=True, check=True).stdout
    dependencies: dict[str, list[str]] = {}
    current: str | None = None
    line: str
    for line in text.splitlines():
        if not line.strip():
            current = None
        elif not line.startswith(' '):
            current = line.split(':')[0]
            dependencies[current] = []
        elif current is not None:
            dependencies[current].append(line.strip())
    return dependencies


def origin(relative: str) -> str:
    parts: list[str] = relative.split('/')
    if relative.startswith('.ci/stx-package/') and 'include' in parts:
        at: int = parts.index('include')
        return 'stx:' + '/'.join(parts[at + 1:at + 3])
    if relative.startswith('gui_forms/'):
        return 'gui_forms:' + '/'.join(parts[1:3])
    if relative.startswith('.build/'):
        return 'build:' + '/'.join(parts[2:4])
    return parts[0]


def main(arguments: list[str]) -> int:
    if len(arguments) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    old: Path = Path(arguments[0]).resolve()
    new: Path = Path(arguments[1]).resolve()
    build: Path = new / (arguments[2] if len(arguments) == 3
                         else '.build/native-macos-arm64/gui-forms')
    digests: dict[Path, str | None] = {}

    def digest(path: Path) -> str | None:
        if path not in digests:
            try:
                digests[path] = hashlib.sha256(path.read_bytes()).hexdigest()
            except OSError:
                digests[path] = None
        return digests[path]

    dependencies: dict[str, list[str]] = recorded_dependencies(build)
    objects: list[str] = [name for name in dependencies if name.endswith('.o')]
    missing: int = 0
    by_input: collections.Counter[str] = collections.Counter()
    by_origin: collections.Counter[str] = collections.Counter()
    outside: set[str] = set()
    name: str
    for name in objects:
        changed: list[str] = []
        dependency: str
        for dependency in dependencies[name]:
            path: Path = Path(dependency)
            if not path.is_absolute():
                path = (build / path).resolve()
            try:
                relative: str = path.relative_to(new).as_posix()
            except ValueError:
                text: str = str(path)
                outside.add(text.split('/include/')[0] if '/include/' in text else str(path.parent))
                continue
            if digest(new / relative) != digest(old / relative):
                changed.append(relative)
        if changed:
            missing += 1
            by_input.update(changed)
            by_origin.update({origin(relative) for relative in changed})
    print(f'objects {len(objects)}; reading a changed input {missing} '
          f'({missing * 100 // max(1, len(objects))}%)')
    print('origins (objects reading a changed input from each):')
    count: int
    for name, count in by_origin.most_common(15):
        print(f'  {count:5d} {name}')
    print('changed inputs read by the most objects:')
    for name, count in by_input.most_common(25):
        print(f'  {count:5d} {name}')
    print('input roots outside the workspace: ' + ', '.join(sorted(outside)[:8]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
