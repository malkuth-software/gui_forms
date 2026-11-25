#!/usr/bin/env python3
"""Tests for tools/sdk_manifest.py: the per-platform SDK records and the manifest the devkit resolves."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_manifest  # noqa: E402

REVISION: str = 'a' * 40


def cache_manifest(platform: str, revision: str = REVISION) -> dict[str, object]:
    result: dict[str, object] = {'schema': 2, 'platform': platform, 'revision': revision,
                                 'compiler_version': '22.1.8', 'compiler_sha256': 'c' * 64}
    return result


ENVIRONMENT: dict[str, str] = {'CCACHE_COMPILERCHECK': 'content', 'CCACHE_SLOPPINESS': 'pch_defines,time_macros',
                               'LANG': 'C.UTF-8', 'SDK_VERSION': '15.2'}


class RecordTests(unittest.TestCase):
    def test_a_record_carries_the_compiler_environment_and_components(self) -> None:
        record: dict[str, object] = sdk_manifest.platform_record('linux-x64', cache_manifest('linux-x64'), ENVIRONMENT)
        self.assertEqual(record['compiler_version'], '22.1.8')
        self.assertEqual(record['compiler_sha256'], 'c' * 64)
        self.assertEqual(record['environment'], {'CCACHE_COMPILERCHECK': 'content',
                                                 'CCACHE_SLOPPINESS': 'pch_defines,time_macros', 'LANG': 'C.UTF-8',
                                                 'LC_ALL': '', 'LC_CTYPE': '', 'LC_MESSAGES': ''})
        self.assertEqual(record['components'], ['GalleryModel', 'Lab', 'DemoControls'])
        self.assertNotIn('sdk_version', record)

    def test_macos_records_its_sdk_version(self) -> None:
        record: dict[str, object] = sdk_manifest.platform_record('macos-arm64', cache_manifest('macos-arm64'),
                                                                 ENVIRONMENT)
        self.assertEqual(record['sdk_version'], '15.2')

    def test_a_cache_manifest_of_another_platform_is_refused(self) -> None:
        with self.assertRaisesRegex(sdk_manifest.ManifestError, 'not linux-arm64'):
            sdk_manifest.platform_record('linux-arm64', cache_manifest('linux-x64'), ENVIRONMENT)

    def test_a_cache_manifest_without_the_compiler_digest_is_refused(self) -> None:
        manifest: dict[str, object] = cache_manifest('windows-x64')
        del manifest['compiler_sha256']
        with self.assertRaisesRegex(sdk_manifest.ManifestError, 'compiler_sha256'):
            sdk_manifest.platform_record('windows-x64', manifest, ENVIRONMENT)


class AssembleTests(unittest.TestCase):
    def records(self) -> dict[str, dict[str, object]]:
        result: dict[str, dict[str, object]] = {}
        platform: str
        for platform in sdk_manifest.PLATFORMS:
            result[platform] = sdk_manifest.platform_record(platform, cache_manifest(platform), ENVIRONMENT)
        return result

    def test_the_manifest_has_the_shape_the_devkit_resolver_reads(self) -> None:
        manifest: dict[str, object] = sdk_manifest.assemble('1.0.4', REVISION, self.records())
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(manifest['version'], '1.0.4')
        self.assertEqual(manifest['revision'], REVISION)
        platforms: object = manifest['platforms']
        assert isinstance(platforms, dict)
        self.assertEqual(sorted(platforms), sorted(sdk_manifest.PLATFORMS))
        self.assertEqual(platforms['linux-x64']['compiler_version'], '22.1.8')
        self.assertNotIn('revision', platforms['linux-x64'])

    def test_a_missing_platform_is_refused(self) -> None:
        records: dict[str, dict[str, object]] = self.records()
        del records['windows-x64']
        with self.assertRaisesRegex(sdk_manifest.ManifestError, 'windows-x64'):
            sdk_manifest.assemble('1.0.4', REVISION, records)

    def test_a_record_of_another_revision_is_refused(self) -> None:
        records: dict[str, dict[str, object]] = self.records()
        records['macos-arm64'] = sdk_manifest.platform_record('macos-arm64', cache_manifest('macos-arm64', 'b' * 40),
                                                              ENVIRONMENT)
        with self.assertRaisesRegex(sdk_manifest.ManifestError, 'macos-arm64'):
            sdk_manifest.assemble('1.0.4', REVISION, records)

    def test_the_command_line_reads_records_from_artifact_folders(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root: Path = Path(directory)
            platform: str
            for platform in sdk_manifest.PLATFORMS:
                folder: Path = root / 'records' / ('gui-forms-sdk-record-' + platform)
                folder.mkdir(parents=True)
                (root / 'cache.json').write_text(json.dumps(cache_manifest(platform)), encoding='utf-8')
                status: int = sdk_manifest.main(['record', '--platform', platform, '--cache-manifest',
                                                 str(root / 'cache.json'), '--output',
                                                 str(folder / sdk_manifest.record_name(platform))])
                self.assertEqual(status, 0)
            output: Path = root / sdk_manifest.MANIFEST_NAME
            self.assertEqual(sdk_manifest.main(['assemble', '--version', '1.0.4', '--revision', REVISION,
                                                '--records', str(root / 'records'), '--output', str(output)]), 0)
            self.assertEqual(sorted(json.loads(output.read_text(encoding='utf-8'))['platforms']),
                             sorted(sdk_manifest.PLATFORMS))


if __name__ == '__main__':
    unittest.main()
