#!/usr/bin/env python3
"""Tests for engine_gate.py: reading manifests, comparing hosts and filling the record."""
from __future__ import annotations
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import engine_gate


def manifest_text(rows: list[str], counters: list[str] | None = None, registry: int = 2) -> str:
    lines: list[str] = [engine_gate.HEADER, '# primitive\tname\tstate\tscale\trasters\tpainter_calls\trefusals\thash',
                        'registry\t' + str(registry)]
    lines.extend(rows)
    lines.extend(counters or ['counter\tribbon.idle_frames_run\t0'])
    return '\n'.join(lines) + '\n'


BULGE: str = 'primitive\tbulge\trest\t1.00\t1\t51\t0\t58212a64ba6d4616'
GLASS: str = 'primitive\tglass\trest\t1.00\t1\t51\t0\t8002a0c7d2724c39'
SAMPLES: list[str] = ['samples\t1', 'sample\tfill_conic\tmaterial\trest\t1.00\t1\t51\t0\t35e5bfc0b8f3e2a1',
                      'feature\tfill.conic_gradient\tfill_conic']


def write_evidence(root: Path, manifests: dict[str, str]) -> None:
    """Lays manifests out as the downloaded evidence artifacts are: one directory per artifact."""
    platform: str
    text: str
    for platform, text in manifests.items():
        directory: Path = root / ('gui-forms-evidence-' + platform) / '.ci'
        directory.mkdir(parents=True)
        (directory / ('engine-gate-' + platform + '.tsv')).write_text(text, encoding='utf-8')
        (directory / 'inputs.json').write_text('{}', encoding='utf-8')


class ParseTests(unittest.TestCase):
    def test_rows_and_counters_are_read(self) -> None:
        manifest: engine_gate.Manifest = engine_gate.parse_manifest(manifest_text([BULGE, GLASS]))
        self.assertEqual(manifest.registry, 2)
        self.assertEqual(manifest.images[('bulge', 'rest', '1.00')], ('1', '51', '0', '58212a64ba6d4616'))
        self.assertEqual(manifest.counters, {'ribbon.idle_frames_run': 0})

    def test_samples_and_features_are_read(self) -> None:
        manifest: engine_gate.Manifest = engine_gate.parse_manifest(manifest_text([BULGE] + SAMPLES))
        self.assertEqual(manifest.sample_count, 1)
        self.assertEqual(manifest.samples[('fill_conic', 'rest', '1.00')],
                         ('material', '1', '51', '0', '35e5bfc0b8f3e2a1'))
        self.assertEqual(manifest.features, {'fill.conic_gradient': 'fill_conic'})
        with self.assertRaises(ValueError):
            engine_gate.parse_manifest(manifest_text([BULGE] + SAMPLES + [SAMPLES[1]]))
        with self.assertRaises(ValueError):
            engine_gate.parse_manifest(manifest_text([BULGE] + SAMPLES + [SAMPLES[2]]))

    def test_a_foreign_file_or_a_malformed_row_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            engine_gate.parse_manifest('not a manifest\n')
        with self.assertRaises(ValueError):
            engine_gate.parse_manifest(manifest_text(['primitive\tbulge\trest']))
        with self.assertRaises(ValueError):
            engine_gate.parse_manifest(manifest_text([BULGE, BULGE]))


class CompareTests(unittest.TestCase):
    def run_compare(self, manifests: dict[str, str], job_results: str = '') -> tuple[int, dict, str]:
        with tempfile.TemporaryDirectory() as scratch:
            root: Path = Path(scratch)
            write_evidence(root / 'evidence', manifests)
            (root / 'evidence').mkdir(exist_ok=True)
            arguments: list[str] = ['compare', '--evidence', str(root / 'evidence'), '--out', str(root / 'out'),
                                    '--commit', 'abc123', '--run', 'https://example.invalid/run/1',
                                    '--job-results', job_results]
            status: int = engine_gate.main(arguments)
            result: dict = json.loads((root / 'out' / 'comparison.json').read_text(encoding='utf-8'))
            record: str = (root / 'out' / 'record.md').read_text(encoding='utf-8')
        return status, result, record

    def test_identical_hosts_pass_and_fill_the_record(self) -> None:
        text: str = manifest_text([BULGE, GLASS] + SAMPLES)
        manifests: dict[str, str] = {}
        platform: str
        for platform in engine_gate.PLATFORMS:
            manifests[platform] = text
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare(manifests)
        self.assertEqual(status, 0)
        self.assertTrue(result['identical'])
        self.assertEqual(result['primitives'], ['bulge', 'glass'])
        self.assertEqual(result['samples'], ['fill_conic'])
        self.assertEqual(result['sample_images'], 1)
        self.assertEqual(result['missing'], [])
        self.assertIn('`abc123`', record)
        self.assertIn('| linux-arm64 | 2 | 2 | 1 | 1 | yes |', record)
        self.assertIn('| `fill.conic_gradient` | `fill_conic` |', record)
        self.assertNotIn('{{', record)

    def test_one_differing_hash_fails_and_names_the_host(self) -> None:
        changed: str = GLASS.replace('8002a0c7d2724c39', '0000000000000001')
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare({
            'windows-x64': manifest_text([BULGE, GLASS]), 'linux-x64': manifest_text([BULGE, GLASS]),
            'macos-arm64': manifest_text([BULGE, changed])})
        self.assertEqual(status, 1)
        self.assertEqual(len(result['differences']), 1)
        self.assertEqual(result['differences'][0]['primitive'], 'glass')
        self.assertIn('| macos-arm64 | 2 | 2 | 0 | 0 | no (see differences) |', record)
        self.assertIn('| windows-x64 | 2 | 2 | 0 | 0 | yes |', record)

    def test_a_differing_sample_or_feature_fails(self) -> None:
        changed: list[str] = [SAMPLES[0], SAMPLES[1].replace('35e5bfc0b8f3e2a1', '0000000000000002'), SAMPLES[2]]
        moved: list[str] = [SAMPLES[0], SAMPLES[1], 'feature\tfill.conic_gradient\tother_sample']
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare({
            'windows-x64': manifest_text([BULGE] + SAMPLES), 'linux-x64': manifest_text([BULGE] + changed),
            'macos-arm64': manifest_text([BULGE] + moved)})
        self.assertEqual(status, 1)
        kinds: list[str] = []
        difference: dict
        for difference in result['differences']:
            kinds.append(difference['kind'])
        self.assertEqual(kinds, ['sample', 'feature'])
        self.assertIn('- sample `fill_conic` rest at 1.00:', record)
        self.assertIn('- feature `fill.conic_gradient`:', record)

    def test_a_primitive_missing_on_one_host_is_a_difference(self) -> None:
        status: int
        result: dict
        status, result, _ = self.run_compare({
            'windows-x64': manifest_text([BULGE, GLASS]), 'linux-x64': manifest_text([BULGE], registry=1)})
        self.assertEqual(status, 1)
        self.assertFalse(result['registry_agrees'])
        self.assertEqual(result['differences'][0]['hosts']['linux-x64'], None)

    def test_a_busy_idle_counter_fails(self) -> None:
        busy: str = manifest_text([BULGE, GLASS], ['counter\tprimitive.glass.idle_frames_run\t3'])
        status: int
        result: dict
        status, result, _ = self.run_compare({'windows-x64': busy})
        self.assertEqual(status, 1)
        self.assertEqual(result['busy_idle_counters'][0]['counter'], 'primitive.glass.idle_frames_run')

    def test_a_missing_host_fails(self) -> None:
        manifests: dict[str, str] = {}
        platform: str
        for platform in engine_gate.PLATFORMS[:3]:
            manifests[platform] = manifest_text([BULGE, GLASS])
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare(manifests)
        self.assertEqual(status, 1)
        self.assertEqual(result['missing'], ['linux-arm64'])
        self.assertEqual(result['outcomes'], ['host evidence missing'])
        self.assertIn('no manifest', record)

    def test_a_host_never_acquired_is_missing_evidence_not_a_difference(self) -> None:
        # A main run whose macOS runner was never acquired: 0 differences
        # among the hosts that ran, and the gate says so and still fails.
        manifests: dict[str, str] = {}
        platform: str
        for platform in engine_gate.PLATFORMS:
            if platform != 'macos-arm64':
                manifests[platform] = manifest_text([BULGE, GLASS])
        needs: str = json.dumps({'changes': {'result': 'success'}, 'native-macos-arm64': {'result': 'cancelled'},
                                 'native-linux-x64': {'result': 'success'},
                                 'native-linux-arm64': {'result': 'success'},
                                 'native-windows-x64': {'result': 'success'}})
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare(manifests, needs)
        self.assertEqual(status, 1)
        self.assertEqual(result['outcomes'], ['host evidence missing'])
        self.assertEqual(result['differences'], [])
        self.assertEqual(result['missing_reasons']['macos-arm64'],
                         'host evidence missing: job cancelled or not acquired (job result: cancelled)')
        self.assertIn('macos-arm64: host evidence missing: job cancelled or not acquired', record)
        self.assertIn('0 cross-host differences', record)
        self.assertNotIn('DIFFERENT', record)

    def test_a_successful_job_without_a_manifest_is_an_evidence_defect(self) -> None:
        manifests: dict[str, str] = {}
        platform: str
        for platform in engine_gate.PLATFORMS[:3]:
            manifests[platform] = manifest_text([BULGE, GLASS])
        needs: str = json.dumps({'native-linux-arm64': {'result': 'success'}})
        status: int
        result: dict
        status, result, _ = self.run_compare(manifests, needs)
        self.assertEqual(status, 1)
        self.assertIn('evidence defect', result['missing_reasons']['linux-arm64'])

    def test_no_manifest_at_all_fails(self) -> None:
        # A push to main whose native jobs all promoted ran no gate test (run
        # 40019820396): comparing nothing must not pass.
        status: int
        result: dict
        record: str
        status, result, record = self.run_compare({})
        self.assertEqual(status, 1)
        self.assertEqual(result['missing'], list(engine_gate.PLATFORMS))
        self.assertEqual(result['images'], 0)

    def test_two_manifests_for_one_platform_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root: Path = Path(scratch)
            write_evidence(root / 'a', {'linux-x64': manifest_text([BULGE])})
            write_evidence(root / 'b', {'linux-x64': manifest_text([BULGE])})
            with self.assertRaises(ValueError):
                engine_gate.find_manifests(root)

    def test_the_template_names_every_placeholder_the_tool_fills(self) -> None:
        template: str = engine_gate.TEMPLATE.read_text(encoding='utf-8')
        name: str
        for name in ('commit', 'run', 'verdict', 'primitives', 'image_count', 'samples', 'sample_image_count',
                     'host_table', 'differences', 'feature_table', 'counter_table'):
            self.assertIn('{{' + name + '}}', template)



class FakeArtifacts:
    """Stands in for GitHubArtifacts: one run's evidence artifacts, held as zips."""

    def __init__(self, run: str, zips: dict[str, bytes]) -> None:
        self.run: str = run
        self.ids: dict[str, int] = {}
        self.zips: dict[int, bytes] = {}
        name: str
        data: bytes
        for name, data in zips.items():
            self.ids[name] = len(self.ids) + 1
            self.zips[self.ids[name]] = data
        self.downloads: int = 0

    def find(self, run: str, name: str) -> int | None:
        if run != self.run:
            return None
        return self.ids.get(name)

    def download(self, artifact: int) -> bytes:
        self.downloads += 1
        return self.zips[artifact]


def evidence_zip(members: dict[str, str]) -> bytes:
    buffer: io.BytesIO = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        name: str
        text: str
        for name, text in members.items():
            archive.writestr(name, text)
    return buffer.getvalue()


class CarryTests(unittest.TestCase):
    """A native job that promoted or reused a validation carries that run's manifest."""

    def test_a_job_that_ran_the_test_carries_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            state: Path = Path(scratch)
            fake: FakeArtifacts = FakeArtifacts('7', {})
            self.assertEqual(engine_gate.carry('linux-x64', state, fake), 0)
            self.assertEqual(fake.downloads, 0)
            self.assertFalse((state / 'engine-gate-linux-x64.tsv').exists())

    def test_a_reused_validation_carries_its_manifest_with_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            state: Path = Path(scratch)
            (state / 'reused.json').write_text(json.dumps({'validation_run': '7', 'tree': 'abc'}), encoding='utf-8')
            text: str = manifest_text([BULGE, GLASS])
            fake: FakeArtifacts = FakeArtifacts('7', {'gui-forms-evidence-linux-x64': evidence_zip({
                '.ci/inputs.json': '{}', '.ci/engine-gate-linux-x64.tsv': text})})
            self.assertEqual(engine_gate.carry('linux-x64', state, fake), 0)
            carried: str = (state / 'engine-gate-linux-x64.tsv').read_text(encoding='utf-8')
            self.assertIn('# carried from validation run 7 of tree abc', carried)
            manifest: engine_gate.Manifest = engine_gate.parse_manifest(carried)
            self.assertEqual(manifest.images, engine_gate.parse_manifest(text).images)

    def test_a_validation_without_a_manifest_leaves_the_host_missing(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            state: Path = Path(scratch)
            (state / 'reused.json').write_text(json.dumps({'validation_run': '7', 'tree': 'abc'}), encoding='utf-8')
            fake: FakeArtifacts = FakeArtifacts('7', {'gui-forms-evidence-linux-x64': evidence_zip({
                '.ci/inputs.json': '{}'})})
            self.assertEqual(engine_gate.carry('linux-x64', state, fake), 0)
            self.assertFalse((state / 'engine-gate-linux-x64.tsv').exists())

if __name__ == '__main__':
    unittest.main()
