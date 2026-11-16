#!/usr/bin/env python3
"""Tests for integrity_counts.py: finding IDs and their short forms, sentence
classification, audit folders, finding status, and whole runs over a small
audit folder, queue and git checkout."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import integrity_counts

KNOWN: set[str] = {
    'A-gf-controls-09-F5', 'A-gf-controls-09-F6', 'A-gf-controls-03-F5', 'A-gf-controls-02-F11',
    'A-stxlib-glyphs-01-F1', 'A-stxlib-glyphs-01-F2', 'A-stxlib-glyphs-02-F1', 'A-stxlib-glyphs-02-F7',
    'A-stxlib-glyphs-02-F8', 'A-stxlib-markdown-01-F1', 'A-stxlib-markdown-01-F2', 'A-stxlib-markdown-01-F3',
    'A-stxlib-algorithms-02-F1', 'A-stxlib-algorithms-02-F3', 'A-stxlib-allocator-state-01-F2',
    'A-stx-codecs-01-F8', 'A-stx-codecs-01-F9', 'B-gf-tests-03-F4',
}


def names(text: str) -> list[str]:
    context: integrity_counts.ResolveContext = integrity_counts.ResolveContext()
    resolution: integrity_counts.Resolution = integrity_counts.resolve(text, context, KNOWN)
    return resolution.identities


class ResolveTests(unittest.TestCase):
    def test_full_identity(self) -> None:
        self.assertEqual(names('fix (A-gf-controls-09-F5)'), ['A-gf-controls-09-F5'])
        self.assertEqual(names('B-gf-tests-03-F4: stale version'), ['B-gf-tests-03-F4'])

    def test_short_forms_take_the_last_full_reference(self) -> None:
        self.assertEqual(names('A-gf-controls-09-F5, 09-F6, 03-F5 and 02-F11 (coder D): cost only.'),
                         ['A-gf-controls-09-F5', 'A-gf-controls-09-F6', 'A-gf-controls-03-F5',
                          'A-gf-controls-02-F11'])

    def test_bare_number_takes_the_current_section(self) -> None:
        self.assertEqual(names('A-stxlib-glyphs-01 to 04: saturated advances (02-F7, F8)'),
                         ['A-stxlib-glyphs-02-F7', 'A-stxlib-glyphs-02-F8'])

    def test_section_and_range(self) -> None:
        self.assertEqual(names('markdown (A-stxlib-markdown-01, F1 to F3)'),
                         ['A-stxlib-markdown-01-F1', 'A-stxlib-markdown-01-F2', 'A-stxlib-markdown-01-F3'])

    def test_range_after_a_full_identity(self) -> None:
        self.assertEqual(names('A-stxlib-markdown-01-F1 to F2'),
                         ['A-stxlib-markdown-01-F1', 'A-stxlib-markdown-01-F2'])

    def test_area_without_section(self) -> None:
        self.assertEqual(names("fix the A-stxlib-algorithms findings: 02-F1 and 02-F3"),
                         ['A-stxlib-algorithms-02-F1', 'A-stxlib-algorithms-02-F3'])

    def test_context_carries_across_calls(self) -> None:
        context: integrity_counts.ResolveContext = integrity_counts.ResolveContext()
        integrity_counts.resolve('A-stxlib-allocator-state-01, audited at e549318:', context, KNOWN)
        resolution: integrity_counts.Resolution = integrity_counts.resolve('- F2 (Medium): the exit rule', context,
                                                                           KNOWN)
        self.assertEqual(resolution.identities, ['A-stxlib-allocator-state-01-F2'])

    def test_short_form_with_no_context_is_unresolved(self) -> None:
        context: integrity_counts.ResolveContext = integrity_counts.ResolveContext()
        resolution: integrity_counts.Resolution = integrity_counts.resolve('the findings (F1 to F11)', context,
                                                                           KNOWN)
        self.assertEqual(resolution.identities, [])
        self.assertTrue(resolution.unresolved)

    def test_short_form_outside_the_records_does_not_count(self) -> None:
        context: integrity_counts.ResolveContext = integrity_counts.ResolveContext()
        resolution: integrity_counts.Resolution = integrity_counts.resolve('A-gf-controls-09-F5 and F40', context,
                                                                           KNOWN)
        self.assertEqual(resolution.identities, ['A-gf-controls-09-F5'])
        self.assertTrue(resolution.unresolved)

    def test_unknown_full_identity_is_kept(self) -> None:
        self.assertEqual(names('audit B-stxlib-tests-proofs-01-F1'), ['B-stxlib-tests-proofs-01-F1'])

    def test_words_that_look_like_numbers(self) -> None:
        self.assertEqual(names('press F1 or 0xF2; the hash df3859aac'), [])


class ClassifyTests(unittest.TestCase):
    def test_sentence_words(self) -> None:
        self.assertEqual(integrity_counts.classify('X declined: cost only', '', 'fix'), 'decline')
        self.assertEqual(integrity_counts.classify('X is deferred, since', '', 'fix'), 'defer')
        self.assertEqual(integrity_counts.classify('X follow-up', '', 'fix'), 'follow-up')
        self.assertEqual(integrity_counts.classify('X is not fixed here', '', 'fix'), 'note')
        self.assertEqual(integrity_counts.classify('X: bound the read', '', 'fix'), 'fix')

    def test_words_containing_the_keywords(self) -> None:
        self.assertEqual(integrity_counts.classify('X: stop the deferred-input route', '', 'fix'), 'fix')

    def test_none_declined_is_not_a_decline(self) -> None:
        self.assertEqual(integrity_counts.classify('none declined (X)', '', 'queued'), 'queued')

    def test_heading_applies_when_the_sentence_is_silent(self) -> None:
        self.assertEqual(integrity_counts.classify('X: cost only', 'declined or deferred:', 'queued'), 'decline')
        self.assertEqual(integrity_counts.classify('X deferred', 'declined or deferred:', 'queued'), 'defer')


def record(text: str, default: str, origin: str) -> dict[str, list[integrity_counts.Mention]]:
    mentions: dict[str, list[integrity_counts.Mention]] = {}
    unknown: dict[str, list[integrity_counts.Mention]] = {}
    integrity_counts.read_record(text, default, origin, 'here', KNOWN, mentions, unknown)
    return mentions


class RecordTests(unittest.TestCase):
    def test_list_heading_covers_its_items(self) -> None:
        text: str = ('- Commits:\n  - abc A-gf-controls-09-F5 (Low): bound it\n'
                     '- declined (cost only):\n  - A-stx-codecs-01-F9: quadratic\n'
                     '- Tests: x\n')
        mentions: dict[str, list[integrity_counts.Mention]] = record(text, 'queued', 'queue')
        self.assertEqual(mentions['A-gf-controls-09-F5'][0].kind, 'queued')
        self.assertEqual(mentions['A-stx-codecs-01-F9'][0].kind, 'decline')

    def test_sentences_are_classified_apart(self) -> None:
        text: str = 'A-stx-codecs-01-F9: cost only. A-stx-codecs-01-F8 is deferred, since it changes output.'
        mentions: dict[str, list[integrity_counts.Mention]] = record(text, 'decline', 'audit')
        self.assertEqual(mentions['A-stx-codecs-01-F9'][0].kind, 'decline')
        self.assertEqual(mentions['A-stx-codecs-01-F8'][0].kind, 'defer')

    def test_commit_body_decline(self) -> None:
        text: str = ("algorithms: fix the security audit's A-stxlib-algorithms findings\n\n"
                     "- 02-F1 (Medium), voting.cpp: the width is clamped.\n"
                     "Declined: 02-F3 (reserve per batch), see the queue entry.\n")
        mentions: dict[str, list[integrity_counts.Mention]] = record(text, 'fix', 'commit')
        self.assertEqual(mentions['A-stxlib-algorithms-02-F1'][0].kind, 'fix')
        self.assertEqual(mentions['A-stxlib-algorithms-02-F3'][0].kind, 'decline')


AUDIT_SUMMARY: str = """# Area

Findings: 0 Critical, 1 High, 1 Medium, 1 Low.

## High

### A-gf-core-01-F1: a read past the end
- **Severity:** High
- **Kind:** memory

## Medium

### A-gf-core-01-F2: quadratic
- Severity: Medium (High for a 32-bit build)

## Low

### A-gf-core-02-F1: a stale comment
- **Severity:** Low (latent)

### A-gf-core-02-F2: an unused branch

- **Severity**: Low
"""

AUDIT_PART: str = """# A-gf-core-01

### A-gf-core-01-F1: a read past the end (the part's copy)
- **Severity:** High
"""

FIX_PLAN: str = """# Fix plan

## Assignments

A-gf-core-02-F1 goes to coder A.

### Declined

- A-gf-core-01-F2 (coder D): cost only.
"""

QUEUE_ENTRY: str = """### gui_forms claude/audit-x @ 123

- Commits:
  - 123abc A-gf-core-02-F1 (Low): the comment is current.
- Declines: none declined.
"""


def git(checkout: Path, arguments: list[str]) -> None:
    subprocess.run(['git', '-C', str(checkout)] + arguments, check=True, capture_output=True)


class CollectTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        root: Path = Path(self.directory.name)
        self.audit: Path = root / 'security_audit_test'
        (self.audit / 'parts').mkdir(parents=True)
        (self.audit / '04_CORE.md').write_text(AUDIT_SUMMARY, encoding='utf-8')
        (self.audit / 'parts' / 'A-gf-core-01.md').write_text(AUDIT_PART, encoding='utf-8')
        (self.audit / 'FIX_PLAN.md').write_text(FIX_PLAN, encoding='utf-8')
        self.queue: Path = root / 'queue'
        self.queue.mkdir()
        (self.queue / 'claude__audit-x.md').write_text(QUEUE_ENTRY, encoding='utf-8')
        (self.queue / 'README.md').write_text('A-gf-core-02-F2 is an example.\n', encoding='utf-8')
        self.checkout: Path = root / 'repository'
        self.checkout.mkdir()
        git(self.checkout, ['init', '-q'])
        git(self.checkout, ['config', 'user.email', 'test@example.invalid'])
        git(self.checkout, ['config', 'user.name', 'Test'])
        git(self.checkout, ['commit', '-q', '--allow-empty', '-m', 'A-gf-core-01-F1: bound the read'])
        git(self.checkout, ['commit', '-q', '--allow-empty', '-m',
                            'Security audit: the include findings (F1 to F3)'])
        git(self.checkout, ['commit', '-q', '--allow-empty', '-m', 'Test fix (A-gf-core-09-F9)'])

    def tearDown(self) -> None:
        self.directory.cleanup()

    def collect(self) -> integrity_counts.Report:
        report: integrity_counts.Report = integrity_counts.collect(
            [self.audit], [self.queue], ['gf=' + str(self.checkout)])
        return report

    def test_findings_and_severities(self) -> None:
        report: integrity_counts.Report = self.collect()
        self.assertEqual(sorted(report.findings), ['A-gf-core-01-F1', 'A-gf-core-01-F2', 'A-gf-core-02-F1',
                                                   'A-gf-core-02-F2'])
        self.assertEqual(report.findings['A-gf-core-01-F1'].record, 'security_audit_test/04_CORE.md')
        self.assertEqual(report.findings['A-gf-core-01-F2'].severity, 'Medium')
        self.assertEqual(report.findings['A-gf-core-01-F2'].severity_note, '(High for a 32-bit build)')
        self.assertEqual(report.findings['A-gf-core-02-F1'].severity, 'Low')

    def test_status(self) -> None:
        report: integrity_counts.Report = self.collect()
        self.assertEqual(report.status('A-gf-core-01-F1'), 'fixed')
        self.assertEqual(report.status('A-gf-core-01-F2'), 'declined')
        self.assertEqual(report.status('A-gf-core-02-F1'), 'queued')
        self.assertEqual(report.status('A-gf-core-02-F2'), 'open')

    def test_unknown_and_unresolved(self) -> None:
        report: integrity_counts.Report = self.collect()
        self.assertEqual(sorted(report.unknown), ['A-gf-core-09-F9'])
        self.assertEqual([commit.subject() for commit in report.unresolved_commits],
                         ['Security audit: the include findings (F1 to F3)'])

    def test_counts(self) -> None:
        report: integrity_counts.Report = self.collect()
        table: dict[str, dict[str, dict[str, int]]] = integrity_counts.count_table(report)
        rows: dict[str, dict[str, int]] = table['security_audit_test / GUI.Forms']
        self.assertEqual(rows['High'], {'found': 1, 'fixed': 1, 'declined': 0, 'deferred': 0, 'queued': 0,
                                        'open': 0})
        self.assertEqual(rows['Low']['found'], 2)
        self.assertEqual(rows['Low']['queued'], 1)
        self.assertEqual(rows['Low']['open'], 1)
        last: list[str] = integrity_counts.summary_rows(report)[-1]
        self.assertEqual(last, ['all', 'all', '4', '1', '1', '0', '1', '1'])

    def test_fixed_after_a_declined_suggestion_is_flagged(self) -> None:
        git(self.checkout, ['commit', '-q', '--allow-empty', '-m', 'A-gf-core-01-F2: fixed another way'])
        report: integrity_counts.Report = self.collect()
        self.assertEqual(report.status('A-gf-core-01-F2'), 'fixed')
        self.assertTrue(report.suggestion_declined('A-gf-core-01-F2'))

    def test_outputs(self) -> None:
        output: io.StringIO = io.StringIO()
        destination: Path = Path(self.directory.name) / 'report.json'
        with contextlib.redirect_stdout(output):
            status: int = integrity_counts.main(['--audit', str(self.audit), '--queue', str(self.queue),
                                                 '--repo', 'gf=' + str(self.checkout) + '@HEAD',
                                                 '--json', str(destination), '--markdown', '-'])
        self.assertEqual(status, 0)
        data: dict[str, object] = json.loads(destination.read_text(encoding='utf-8'))
        findings: list[dict[str, object]] = data['findings']  # type: ignore[assignment]
        fixed: dict[str, object] = findings[0]
        self.assertEqual(fixed['id'], 'A-gf-core-01-F1')
        self.assertEqual(len(fixed['commits']), 1)  # type: ignore[arg-type]
        self.assertIn('### Fixed (1)', output.getvalue())
        self.assertIn('| all | all | 4 | 1 | 1 | 0 | 1 | 1 |', output.getvalue())

    def test_missing_folder(self) -> None:
        errors: io.StringIO = io.StringIO()
        with contextlib.redirect_stderr(errors):
            status: int = integrity_counts.main(['--audit', str(Path(self.directory.name) / 'none')])
        self.assertEqual(status, 2)


if __name__ == '__main__':
    unittest.main()
