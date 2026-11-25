#!/usr/bin/env python3
"""The SDK manifest a build-<sha> pre-release carries beside its per-platform SDK archives.

gui_forms_devkit's resolver (harness/resolve_gui_forms.py) takes a build only
when it carries gui-forms-sdk-manifest.json: the version, the revision and,
for every platform, the compiler and the compile environment that built that
platform's SDK and cache, so a consumer can match them before it builds.

    python3 -B tools/sdk_manifest.py record --platform P --cache-manifest cache-manifest.json --output FILE
    python3 -B tools/sdk_manifest.py assemble --version V --revision SHA --records DIR --output FILE

record runs in each native job beside the archived cache: it takes the
compiler fields from the job's cache-manifest.json and the compile
environment (the ccache settings and the locale) from the job's own
environment, plus the macOS SDK version on macOS (SDK_VERSION, else
`xcrun --show-sdk-version`). assemble runs once every platform has passed: it
joins the records of every platform into the manifest and refuses a missing
platform or a record of another revision.

The installed SDK it describes carries the GalleryModel, Lab and DemoControls
components (GUI_FORMS_INSTALL_GALLERY_MODEL and GUI_FORMS_INSTALL_LAB, on in
every native build through tools/toolkit_configure.py), which the devkit's
programs link.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

PLATFORMS: tuple[str, ...] = ('linux-x64', 'linux-arm64', 'macos-arm64', 'windows-x64')
SCHEMA: int = 1
MANIFEST_NAME: str = 'gui-forms-sdk-manifest.json'
# The compile environment a cache hit depends on; the devkit's build guard
# compares a consumer's against these.
ENVIRONMENT_NAMES: tuple[str, ...] = ('CCACHE_COMPILERCHECK', 'CCACHE_SLOPPINESS', 'LANG', 'LC_ALL', 'LC_CTYPE',
                                      'LC_MESSAGES')
COMPONENTS: tuple[str, ...] = ('GalleryModel', 'Lab', 'DemoControls')


class ManifestError(Exception):
    pass


def record_name(platform: str) -> str:
    result: str = 'gui-forms-sdk-record-' + platform + '.json'
    return result


def macos_sdk_version(environment: dict[str, str]) -> str:
    named: str = environment.get('SDK_VERSION', '')
    if named:
        return named
    result: str = subprocess.check_output(['xcrun', '--show-sdk-version'], text=True).strip()
    return result


def platform_record(platform: str, cache_manifest: dict[str, Any], environment: dict[str, str]) -> dict[str, Any]:
    """One platform's record: the compiler fields of its cache manifest and the compile environment."""
    if platform not in PLATFORMS:
        raise ManifestError('unknown platform ' + repr(platform))
    if cache_manifest.get('platform') != platform:
        raise ManifestError('the cache manifest is for ' + str(cache_manifest.get('platform')) + ', not ' + platform)
    field: str
    for field in ('revision', 'compiler_version', 'compiler_sha256'):
        if not cache_manifest.get(field):
            raise ManifestError('the cache manifest has no ' + field)
    name: str
    captured: dict[str, str] = {}
    for name in ENVIRONMENT_NAMES:
        captured[name] = environment.get(name, '')
    record: dict[str, Any] = {'platform': platform, 'revision': cache_manifest['revision'],
                              'compiler_version': cache_manifest['compiler_version'],
                              'compiler_sha256': cache_manifest['compiler_sha256'],
                              'environment': captured, 'components': list(COMPONENTS)}
    if platform == 'macos-arm64':
        record['sdk_version'] = macos_sdk_version(environment)
    return record


def assemble(version: str, revision: str, records: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """The manifest of every platform's record; raises ManifestError for a missing or foreign record."""
    missing: list[str] = [platform for platform in PLATFORMS if platform not in records]
    if missing:
        raise ManifestError('no SDK record for ' + ', '.join(missing))
    platforms: dict[str, Any] = {}
    platform: str
    for platform in PLATFORMS:
        record: dict[str, Any] = records[platform]
        if record.get('revision') != revision:
            raise ManifestError(platform + "'s record is for " + str(record.get('revision')) + ', not ' + revision)
        entry: dict[str, Any] = {key: value for key, value in record.items() if key not in ('platform', 'revision')}
        platforms[platform] = entry
    manifest: dict[str, Any] = {'schema': SCHEMA, 'version': version, 'revision': revision, 'platforms': platforms}
    return manifest


def read_records(directory: Path) -> dict[str, dict[str, Any]]:
    """The records under directory (download-artifact puts each in its own folder), by platform."""
    records: dict[str, dict[str, Any]] = {}
    path: Path
    for path in sorted(directory.rglob('gui-forms-sdk-record-*.json')):
        record: dict[str, Any] = json.loads(path.read_text(encoding='utf-8'))
        records[str(record.get('platform', ''))] = record
    return records


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands: argparse._SubParsersAction = parser.add_subparsers(dest='command', required=True)
    recording: argparse.ArgumentParser = commands.add_parser('record')
    recording.add_argument('--platform', required=True, choices=PLATFORMS)
    recording.add_argument('--cache-manifest', type=Path, required=True)
    recording.add_argument('--output', type=Path, required=True)
    assembling: argparse.ArgumentParser = commands.add_parser('assemble')
    assembling.add_argument('--version', required=True)
    assembling.add_argument('--revision', required=True)
    assembling.add_argument('--records', type=Path, required=True)
    assembling.add_argument('--output', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args(argv)
    try:
        if arguments.command == 'record':
            cache_manifest: dict[str, Any] = json.loads(arguments.cache_manifest.read_text(encoding='utf-8'))
            write_json(arguments.output, platform_record(arguments.platform, cache_manifest, dict(os.environ)))
        else:
            write_json(arguments.output, assemble(arguments.version, arguments.revision,
                                                  read_records(arguments.records)))
    except ManifestError as error:
        print('sdk_manifest: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
