#!/usr/bin/env python3
"""Compare T2's engine gate manifests across CI hosts and fill the gate record.

Each native CI job runs gui_forms_engine_gate_tests with
GUI_FORMS_ENGINE_GATE_MANIFEST set, which writes engine-gate-<platform>.tsv.
That file lists every registered primitive's reference-image hashes (four
scales, three states), every feature sample's image hashes (four scales,
and the frames of its drive), the census of features with the sample that
draws each, and the window counters of the idle checks.

`compare` finds those manifests under --evidence, at any depth (the native
jobs' evidence artifacts). It checks that every host lists the same
primitives, samples and features with the same hashes, and that every idle
counter is zero. It
then writes the comparison as JSON and as the record filled from
tools/engine_gate_record.template.md.

The exit status is 1 on any difference between hosts, any non-zero idle
counter, or any platform of PLATFORMS without a manifest (none found at
all included): a gate that compares fewer hosts than it covers proves
nothing about the missing ones. Otherwise it is 0.

The outcomes are reported apart, so that runner capacity never reads as an
engine regression: a cross-host difference, a busy idle counter, and host
evidence missing. --job-results (the workflow's toJSON(needs)) names why a
host is missing: a native job that did not succeed was cancelled, failed,
or never acquired a runner, and its own check reports that; a job that
succeeded without a manifest is an evidence defect.

`carry` keeps a host in the comparison when its native job ran no test:
a job that promoted (a push to main) or reused an exact-tree validation
finds the validation run in .ci/reused.json and copies that run's manifest
for its platform from the run's gui-forms-evidence-<platform> artifact
into .ci/, with a comment naming the run. The tree is the same one (the
reuse trust gate checked it), so the manifest is that tree's evidence. A
job that ran the test has no reused.json and carries nothing.
"""
from __future__ import annotations
import argparse
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

PLATFORMS: tuple[str, ...] = ('windows-x64', 'macos-arm64', 'linux-x64', 'linux-arm64')
MANIFEST_NAME: re.Pattern[str] = re.compile(r'engine-gate-([a-z0-9-]+)\.tsv')
HEADER: str = '# gui_forms engine gate manifest v1'
ROOT: Path = Path(__file__).resolve().parents[1]
TEMPLATE: Path = ROOT / 'tools/engine_gate_record.template.md'


class Manifest:
    """One host's manifest: primitive rows keyed by (name, state, scale), sample rows keyed by
    (name, frame, scale), the census of features, and counters."""

    def __init__(self) -> None:
        self.registry: int = 0
        # (name, state, scale) -> (rasters, painter_calls, refusals, hash)
        self.images: dict[tuple[str, str, str], tuple[str, str, str, str]] = {}
        self.sample_count: int = 0
        # (name, frame, scale) -> (primitive, rasters, painter_calls, refusals, hash)
        self.samples: dict[tuple[str, str, str], tuple[str, str, str, str, str]] = {}
        # feature -> the first sample drawing it
        self.features: dict[str, str] = {}
        self.counters: dict[str, int] = {}


def parse_manifest(text: str) -> Manifest:
    """Read one manifest; a malformed line raises ValueError naming it."""
    lines: list[str] = text.splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError('not an engine gate manifest (first line: ' + (lines[0] if lines else '') + ')')
    manifest: Manifest = Manifest()
    number: int
    line: str
    for number, line in enumerate(lines, start=1):
        if not line or line.startswith('#'):
            continue
        fields: list[str] = line.split('\t')
        if fields[0] == 'registry' and len(fields) == 2:
            manifest.registry = int(fields[1])
        elif fields[0] == 'primitive' and len(fields) == 8:
            key: tuple[str, str, str] = (fields[1], fields[2], fields[3])
            if key in manifest.images:
                raise ValueError(f'line {number}: {key} is listed twice')
            manifest.images[key] = (fields[4], fields[5], fields[6], fields[7])
        elif fields[0] == 'samples' and len(fields) == 2:
            manifest.sample_count = int(fields[1])
        elif fields[0] == 'sample' and len(fields) == 9:
            sample_key: tuple[str, str, str] = (fields[1], fields[3], fields[4])
            if sample_key in manifest.samples:
                raise ValueError(f'line {number}: sample {sample_key} is listed twice')
            manifest.samples[sample_key] = (fields[2], fields[5], fields[6], fields[7], fields[8])
        elif fields[0] == 'feature' and len(fields) == 3:
            if fields[1] in manifest.features:
                raise ValueError(f'line {number}: feature {fields[1]} is listed twice')
            manifest.features[fields[1]] = fields[2]
        elif fields[0] == 'counter' and len(fields) == 3:
            manifest.counters[fields[1]] = int(fields[2])
        else:
            raise ValueError(f'line {number}: unexpected {line!r}')
    return manifest


def find_manifests(evidence: Path) -> dict[str, Path]:
    """The manifest of each platform found under evidence; a platform found twice raises ValueError."""
    found: dict[str, Path] = {}
    path: Path
    for path in sorted(evidence.rglob('engine-gate-*.tsv')):
        match: re.Match[str] | None = MANIFEST_NAME.fullmatch(path.name)
        if match is None:
            continue
        platform: str = match.group(1)
        if platform in found:
            raise ValueError(f'two manifests for {platform}: {found[platform]} and {path}')
        found[platform] = path
    return found


def host_values(manifests: dict[str, Manifest], platforms: list[str], table: str, key: object) -> dict:
    """Each host's row under key in one of its tables ('images', 'samples', 'features'), as a list, or None."""
    values: dict[str, list[str] | None] = {}
    platform: str
    for platform in platforms:
        row: tuple[str, ...] | str | None = getattr(manifests[platform], table).get(key)
        if row is None:
            values[platform] = None
        elif isinstance(row, str):
            values[platform] = [row]
        else:
            values[platform] = list(row)
    return values


def differs(values: dict[str, list[str] | None]) -> bool:
    distinct: list = []
    value: list[str] | None
    for value in values.values():
        if value not in distinct:
            distinct.append(value)
    return len(distinct) > 1


def job_results(text: str) -> dict[str, str]:
    """Each platform's native job result, from the workflow's toJSON(needs) (empty: none known).

    A needs entry named native-<platform> carries {"result": ...}; other
    entries are ignored.
    """
    results: dict[str, str] = {}
    if not text.strip():
        return results
    needs: dict = json.loads(text)
    name: str
    entry: dict
    for name, entry in needs.items():
        if name.startswith('native-'):
            results[name[len('native-'):]] = str(entry.get('result', ''))
    return results


def missing_reason(platform: str, results: dict[str, str]) -> str:
    """Why platform has no manifest, given its native job's result (results from job_results)."""
    result: str = results.get(platform, '')
    if not result:
        return 'host evidence missing: no manifest (job result unknown)'
    if result == 'success':
        return 'host evidence missing: the job succeeded without a manifest (an evidence defect)'
    return f'host evidence missing: job cancelled or not acquired (job result: {result})'


def outcomes(result: dict) -> list[str]:
    """The comparison's outcomes, each reported apart: 'pass' alone, or any of 'cross-host difference',
    'busy idle counter' and 'host evidence missing'."""
    found: list[str] = []
    if not result['identical']:
        found.append('cross-host difference')
    if result['busy_idle_counters']:
        found.append('busy idle counter')
    if result['missing']:
        found.append('host evidence missing')
    if not found:
        found.append('pass')
    return found


def compare(manifests: dict[str, Manifest], required: tuple[str, ...]) -> dict:
    """The comparison: every difference between hosts, every non-zero idle counter, missing hosts."""
    platforms: list[str] = sorted(manifests)
    keys: set[tuple[str, str, str]] = set()
    sample_keys: set[tuple[str, str, str]] = set()
    features: set[str] = set()
    platform: str
    for platform in platforms:
        keys.update(manifests[platform].images)
        sample_keys.update(manifests[platform].samples)
        features.update(manifests[platform].features)
    differences: list[dict] = []
    key: tuple[str, str, str]
    values: dict[str, list[str] | None]
    for key in sorted(keys):
        values = host_values(manifests, platforms, 'images', key)
        if differs(values):
            differences.append({'kind': 'primitive', 'primitive': key[0], 'state': key[1], 'scale': key[2],
                                'hosts': values})
    for key in sorted(sample_keys):
        values = host_values(manifests, platforms, 'samples', key)
        if differs(values):
            differences.append({'kind': 'sample', 'sample': key[0], 'frame': key[1], 'scale': key[2],
                                'hosts': values})
    feature: str
    for feature in sorted(features):
        values = host_values(manifests, platforms, 'features', feature)
        if differs(values):
            differences.append({'kind': 'feature', 'feature': feature, 'hosts': values})
    registries: dict[str, int] = {}
    sample_counts: dict[str, int] = {}
    for platform in platforms:
        registries[platform] = manifests[platform].registry
        sample_counts[platform] = manifests[platform].sample_count
    busy_counters: list[dict] = []
    for platform in platforms:
        name: str
        count: int
        for name, count in sorted(manifests[platform].counters.items()):
            if '.idle_' in name and count != 0:
                busy_counters.append({'platform': platform, 'counter': name, 'value': count})
    primitives: list[str] = []
    for key in sorted(keys):
        if key[0] not in primitives:
            primitives.append(key[0])
    samples: list[str] = []
    for key in sorted(sample_keys):
        if key[0] not in samples:
            samples.append(key[0])
    missing: list[str] = []
    for platform in required:
        if platform not in manifests:
            missing.append(platform)
    distinct_registries: set[int] = set(registries.values())
    distinct_sample_counts: set[int] = set(sample_counts.values())
    counts_agree: bool = len(distinct_registries) <= 1 and len(distinct_sample_counts) <= 1
    result: dict = {
        'platforms': platforms, 'missing': missing, 'primitives': primitives, 'images': len(keys),
        'samples': samples, 'sample_images': len(sample_keys), 'features': sorted(features),
        'registry': registries, 'registry_agrees': len(distinct_registries) <= 1,
        'sample_counts': sample_counts, 'sample_counts_agree': len(distinct_sample_counts) <= 1,
        'differences': differences, 'busy_idle_counters': busy_counters,
        'identical': not differences and counts_agree,
    }
    return result


def counter_table(manifests: dict[str, Manifest]) -> str:
    """A Markdown table of every counter, one column per host."""
    platforms: list[str] = sorted(manifests)
    names: list[str] = []
    platform: str
    for platform in platforms:
        name: str
        for name in manifests[platform].counters:
            if name not in names:
                names.append(name)
    rows: list[str] = ['| Counter | ' + ' | '.join(platforms) + ' |', '|---|' + '---:|' * len(platforms)]
    for name in names:
        cells: list[str] = []
        for platform in platforms:
            value: int | None = manifests[platform].counters.get(name)
            cells.append('-' if value is None else str(value))
        rows.append('| `' + name + '` | ' + ' | '.join(cells) + ' |')
    return '\n'.join(rows)


def minority_hosts(hosts: dict[str, list[str] | None]) -> list[str]:
    """The hosts whose value differs from the one most hosts have (ties: the first host's)."""
    counts: list[tuple[list[str] | None, int]] = []
    platform: str
    value: list[str] | None
    for value in hosts.values():
        index: int
        for index in range(len(counts)):
            if counts[index][0] == value:
                counts[index] = (value, counts[index][1] + 1)
                break
        else:
            counts.append((value, 1))
    common: list[str] | None = counts[0][0]
    most: int = counts[0][1]
    entry: tuple[list[str] | None, int]
    for entry in counts:
        if entry[1] > most:
            common = entry[0]
            most = entry[1]
    minority: list[str] = []
    for platform, value in hosts.items():
        if value != common:
            minority.append(platform)
    return minority


def host_table(result: dict, manifests: dict[str, Manifest]) -> str:
    """A Markdown table: per host, the primitives, the images, the samples, their images and whether
    they match every other host."""
    rows: list[str] = ['| Host | Registered primitives | Images | Feature samples | Sample images '
                       '| Matches every other host |', '|---|---:|---:|---:|---:|---|']
    differing: set[str] = set()
    difference: dict
    for difference in result['differences']:
        differing.update(minority_hosts(difference['hosts']))
    for platform in sorted(manifests):
        verdict: str = 'yes'
        if platform in differing:
            verdict = 'no (see differences)'
        manifest: Manifest = manifests[platform]
        rows.append(f'| {platform} | {manifest.registry} | {len(manifest.images)} | {manifest.sample_count} '
                    f'| {len(manifest.samples)} | {verdict} |')
    for platform in result['missing']:
        reason: str = result.get('missing_reasons', {}).get(platform, 'host evidence missing: no manifest')
        rows.append(f'| {platform} | - | - | - | - | {reason} |')
    return '\n'.join(rows)


def difference_list(result: dict) -> str:
    if not result['differences']:
        return 'None: every image has the same hash on every host.'
    lines: list[str] = []
    difference: dict
    for difference in result['differences']:
        hosts: str = json.dumps(difference['hosts'], sort_keys=True)
        if difference['kind'] == 'sample':
            lines.append(f"- sample `{difference['sample']}` {difference['frame']} at {difference['scale']}: {hosts}")
        elif difference['kind'] == 'feature':
            lines.append(f"- feature `{difference['feature']}`: {hosts}")
        else:
            lines.append(f"- `{difference['primitive']}` {difference['state']} at {difference['scale']}: {hosts}")
    return '\n'.join(lines)


def feature_table(manifests: dict[str, Manifest]) -> str:
    """A Markdown table of the census: each feature and the first sample drawing it (the first host's; the
    hosts are compared)."""
    if not manifests:
        return 'None: no host ran.'
    first: Manifest = manifests[sorted(manifests)[0]]
    rows: list[str] = ['| Feature | First sample drawing it |', '|---|---|']
    feature: str
    for feature in sorted(first.features):
        rows.append(f'| `{feature}` | `{first.features[feature]}` |')
    return '\n'.join(rows)


def fill_record(template: str, result: dict, manifests: dict[str, Manifest], commit: str, run: str) -> str:
    """The record: the template with its {{placeholders}} filled."""
    verdict: str = f"identical on every host that ran ({len(result['differences'])} cross-host differences)"
    if not result['identical']:
        verdict = f"cross-host difference: DIFFERENT between hosts ({len(result['differences'])} differences)"
    if result['busy_idle_counters']:
        verdict = verdict + '; a window ran frames while idle'
    platform: str
    for platform in result['missing']:
        reason: str = result.get('missing_reasons', {}).get(platform, 'host evidence missing: no manifest')
        verdict = verdict + f'; {platform}: {reason}'
    quoted: list[str] = []
    primitive: str
    for primitive in result['primitives']:
        quoted.append('`' + primitive + '`')
    quoted_samples: list[str] = []
    sample: str
    for sample in result['samples']:
        quoted_samples.append('`' + sample + '`')
    values: dict[str, str] = {
        'commit': commit, 'run': run, 'verdict': verdict,
        'primitives': ', '.join(quoted),
        'image_count': str(result['images']),
        'samples': ', '.join(quoted_samples),
        'sample_image_count': str(result['sample_images']),
        'feature_table': feature_table(manifests),
        'host_table': host_table(result, manifests),
        'differences': difference_list(result),
        'counter_table': counter_table(manifests),
    }
    record: str = template
    name: str
    value: str
    for name, value in values.items():
        record = record.replace('{{' + name + '}}', value)
    return record


class GitHubArtifacts:
    """A run's artifacts, read through the gh command line (GH_TOKEN in the environment)."""

    def __init__(self, repository: str) -> None:
        self.repository: str = repository

    def find(self, run: str, name: str) -> int | None:
        """The id of the run's unexpired artifact with this name, or None."""
        endpoint: str = f'repos/{self.repository}/actions/runs/{run}/artifacts?name={name}'
        listing: dict = json.loads(subprocess.check_output(['gh', 'api', endpoint], text=True))
        artifact: dict
        for artifact in listing.get('artifacts', []):
            if artifact.get('name') == name and not artifact.get('expired'):
                return int(artifact['id'])
        return None

    def download(self, artifact: int) -> bytes:
        endpoint: str = f'repos/{self.repository}/actions/artifacts/{artifact}/zip'
        return subprocess.check_output(['gh', 'api', endpoint])


def manifest_in_zip(data: bytes, platform: str) -> str | None:
    """The text of engine-gate-<platform>.tsv at any depth in an artifact zip, or None."""
    wanted: str = 'engine-gate-' + platform + '.tsv'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        name: str
        for name in archive.namelist():
            if name.rsplit('/', 1)[-1] == wanted:
                return archive.read(name).decode('utf-8')
    return None


def with_provenance(text: str, run: str, tree: str) -> str:
    """The manifest with a comment, after its header, naming the validation run it was carried from."""
    lines: list[str] = text.splitlines()
    lines.insert(1, f'# carried from validation run {run} of tree {tree}')
    return '\n'.join(lines) + '\n'


def carry(platform: str, state: Path, artifacts: GitHubArtifacts) -> int:
    """Copy the validation run's manifest into state when this job reused that validation.

    Returns 0, also when there is nothing to carry or it cannot be found
    (a warning says so): compare then reports the host as missing and fails.
    """
    reused: Path = state / 'reused.json'
    if not reused.is_file():
        print('engine gate: this job ran the gate test; nothing to carry')
        return 0
    receipt: dict = json.loads(reused.read_text(encoding='utf-8'))
    run: str = str(receipt.get('validation_run', ''))
    tree: str = str(receipt.get('tree', ''))
    if not run:
        print('::warning::engine gate: the reused receipt names no validation run; no manifest carried')
        return 0
    name: str = 'gui-forms-evidence-' + platform
    artifact: int | None = artifacts.find(run, name)
    if artifact is None:
        print(f'::warning::engine gate: validation run {run} has no {name} artifact; no manifest carried')
        return 0
    text: str | None = manifest_in_zip(artifacts.download(artifact), platform)
    if text is None:
        print(f'::warning::engine gate: validation run {run} recorded no manifest for {platform}; none carried')
        return 0
    parse_manifest(text)
    destination: Path = state / ('engine-gate-' + platform + '.tsv')
    destination.write_text(with_provenance(text, run, tree), encoding='utf-8')
    print(f'engine gate: carried the {platform} manifest of validation run {run}')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=['compare', 'carry'])
    parser.add_argument('--evidence', type=Path, help='compare: directory holding engine-gate-*.tsv files')
    parser.add_argument('--out', type=Path, help='compare: directory for comparison.json and record.md')
    parser.add_argument('--commit', default='', help='compare: the commit the manifests were made from')
    parser.add_argument('--run', default='', help='compare: the CI run that made them')
    parser.add_argument('--job-results', default='',
                        help="compare: the workflow's toJSON(needs), naming why a host's evidence is missing")
    parser.add_argument('--platform', default='', help='carry: the platform of this native job')
    parser.add_argument('--state', type=Path, default=Path('.ci'), help='carry: the .ci directory of this job')
    args: argparse.Namespace = parser.parse_args(argv)
    if args.command == 'carry':
        if not args.platform:
            parser.error('carry needs --platform')
        return carry(args.platform, args.state, GitHubArtifacts(os.environ.get('GITHUB_REPOSITORY', '')))
    if args.evidence is None or args.out is None:
        parser.error('compare needs --evidence and --out')
    paths: dict[str, Path] = find_manifests(args.evidence)
    manifests: dict[str, Manifest] = {}
    platform: str
    path: Path
    for platform, path in paths.items():
        manifests[platform] = parse_manifest(path.read_text(encoding='utf-8'))
    result: dict = compare(manifests, PLATFORMS)
    results: dict[str, str] = job_results(args.job_results)
    reasons: dict[str, str] = {}
    for platform in result['missing']:
        reasons[platform] = missing_reason(platform, results)
    result['missing_reasons'] = reasons
    result['outcomes'] = outcomes(result)
    result['commit'] = args.commit
    result['run'] = args.run
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    record: str = fill_record(TEMPLATE.read_text(encoding='utf-8'), result, manifests, args.commit, args.run)
    (args.out / 'record.md').write_text(record, encoding='utf-8')
    print(f"engine gate: {len(manifests)} hosts, {result['images']} images, "
          f"{result['sample_images']} sample images, {len(result['features'])} features, "
          f"{len(result['differences'])} differences, {len(result['busy_idle_counters'])} busy idle counters, "
          f"missing {result['missing']}")
    print('engine gate outcome: ' + ', '.join(result['outcomes']))
    for platform in result['missing']:
        # A separate annotation, so a host without evidence reads apart from an engine difference.
        print(f"::error title=engine gate: host evidence missing::{platform}: {reasons[platform]}; "
              f"{len(result['differences'])} cross-host differences among the hosts that ran")
    failed: bool = result['outcomes'] != ['pass']
    if not manifests:
        print('engine gate: no manifest found; nothing compared')
        failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
