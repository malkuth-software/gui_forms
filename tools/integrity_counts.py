#!/usr/bin/env python3
"""Counts the audit findings found, fixed and declined, from the records only.

The high-integrity report carries counts of defects found and fixed, by
source and severity, each traceable to its finding IDs and commits (owner,
2026-11-16, "Integrity report records bugs fixed"). This tool reads three
kinds of record and estimates nothing:

- Audit folders (--audit DIR, repeatable): every Markdown file under the
  folder. A finding is a heading `### <ID>: <title>` followed, before the next
  heading, by a `Severity:` line (`- **Severity:** High`, `- **Severity**: Low` or
  `- Severity: Low (latent)`). The severity is the first of Critical, High, Medium or Low on
  that line; the rest is kept as a note. An ID seen twice (a folder's summary
  and its parts/ report) is one finding, the first file read in path order
  giving its record. A section of any file in the folder whose heading names
  declines (FIX_PLAN.md's "Declined") is a decline record.
- Queue entries (--queue PATH, repeatable): a directory of hand-in entries
  (every *.md but README.md) or one file.
- Commit messages (--repo NAME=PATH[@REF], repeatable): `git log --no-merges`
  of REF (default HEAD), subject and body. Merges are left out so that each
  finding points at the commit that changed the code.

Finding IDs are `<A|B>-<area>-<NN>-F<n>`, for example A-gf-controls-09-F5.
Records also use the short forms the audits' coders wrote, resolved against
the last full reference in the same record (one commit message, one queue
entry, one decline section):

- `A-stxlib-allocator-state-01` or `A-stxlib-glyphs-01 to 04` names a
  section (the first, for a range); `A-stxlib-algorithms` names an area;
- `02-F7` is finding F7 of section 02 of the current area, and sets that
  section; `F8` is finding F8 of the current section;
- `F3 to F6` and `01-F3 to F6` are every finding between, in one section.

A short form counts only when it resolves to a finding the audit folders
record; a full ID that no folder records is reported apart ("named, not in
the audit records"). A commit whose message names F numbers that resolve to
nothing is listed for review instead of guessed at.

Each sentence that names findings is classified by its own words, else by
the heading of the Markdown list it sits under (a line ending in ":" with
indented items below it), else by the record's kind: the words "decline"
or "declined" (but not "none declined" or "no declines") make a decline,
"defer" or "deferred" a deferral, "not fixed", "left open", "still open" or
"out of scope" a note, and anything else in a commit a fix. In a queue entry an unclassified mention is "queued".

A finding's status, first match wins: fixed (a commit's fix or follow-up
mention; flagged when a record also declines its suggested fix), declined,
deferred, queued (named in a queue entry, with no commit in the histories
read), open (no record of a fix).

Output: a text table on standard output, and --json FILE and --markdown FILE
("-" for standard output).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import subprocess
import sys

SEVERITIES: tuple[str, ...] = ('Critical', 'High', 'Medium', 'Low')
STATUSES: tuple[str, ...] = ('fixed', 'declined', 'deferred', 'queued', 'open')
COMPONENT_NAMES: dict[str, str] = {'gf': 'GUI.Forms', 'stx': 'stx', 'stxlib': 'STXlib'}

AREA: str = r'[a-z][a-z0-9]*(?:-[a-z][a-z0-9]*)*'
RANGE_TAIL: str = r'(?:\s+to\s+(?:(?P<{0}s>\d{{2}})-)?F(?P<{0}f>\d+)\b)?'
TOKEN_PATTERN: re.Pattern[str] = re.compile(
    r'\b(?P<fl>[AB])-(?P<fa>' + AREA + r')-(?P<fs>\d{2})-F(?P<ff>\d+)\b' + RANGE_TAIL.format('fx')
    + r'|\b(?P<sl>[AB])-(?P<sa>' + AREA + r')-(?P<ss>\d{2})(?:\s+to\s+\d{2})?\b(?!-F)'
    + r'|\b(?P<al>[AB])-(?P<aa>' + AREA + r')\b(?!-\d)'
    + r'|(?<![\w-])(?P<ns>\d{2})-F(?P<nf>\d+)\b' + RANGE_TAIL.format('nx')
    + r'|(?<![\w-])F(?P<bf>\d+)\b' + RANGE_TAIL.format('bx'))
FULL_ID_PATTERN: re.Pattern[str] = re.compile(r'^[AB]-' + AREA + r'-\d{2}-F\d+$')
HEADING_PATTERN: re.Pattern[str] = re.compile(r'^(#{1,6})\s+(.*?)\s*$')
FINDING_HEADING_PATTERN: re.Pattern[str] = re.compile(r'^#{2,6}\s+([AB]-' + AREA + r'-\d{2}-F\d+):\s*(.*?)\s*$')
SEVERITY_PATTERN: re.Pattern[str] = re.compile(r'Severity\**:\**\s*(.*?)\s*$')
SENTENCE_BREAK: re.Pattern[str] = re.compile(r'(?<=[.;])\s+(?=[A-Z(])')
BULLET_PATTERN: re.Pattern[str] = re.compile(r'^(\s*)(?:[-*+]\s+|\d+\.\s+)?(.*)$')
NO_DECLINE_PATTERN: re.Pattern[str] = re.compile(r'none declined|no declines|declined:\s*none|declined,\s*none')
NOTE_PATTERN: re.Pattern[str] = re.compile(r'not fixed|unfixed|left open|still open|out of scope|\brevert')
# Whole words, so that "deferred-input" or "declination" classify nothing.
DEFER_PATTERN: re.Pattern[str] = re.compile(r'\bdefer(?:s|red|ral)?\b(?!-)')
DECLINE_PATTERN: re.Pattern[str] = re.compile(r'\bdeclin(?:e|es|ed|ing)\b(?!-)')
FOLLOW_UP_PATTERN: re.Pattern[str] = re.compile(r'follow-up|follow up')


@dataclass
class Finding:
    """One finding as an audit folder records it."""
    identity: str
    severity: str
    severity_note: str
    title: str
    source: str
    record: str


@dataclass
class Mention:
    """One record naming a finding: a commit, a queue entry or a decline section."""
    identity: str
    kind: str          # fix, follow-up, decline, defer, note, queued
    origin: str        # commit, queue, audit
    where: str         # repo:hash, a queue file, an audit file
    line: str          # the sentence that names it, trimmed


@dataclass
class Commit:
    repository: str
    identity: str
    message: str

    def where(self) -> str:
        return self.repository + ':' + self.identity[:12]

    def subject(self) -> str:
        first: str = self.message.split('\n', 1)[0]
        return first


@dataclass
class ResolveContext:
    """The last full reference in a record, against which short forms resolve."""
    letter: str = ''
    area: str = ''
    section: str = ''


@dataclass
class Resolution:
    """The finding IDs one sentence names, and whether it held unresolved F numbers."""
    identities: list[str] = field(default_factory=list)
    unresolved: bool = False


@dataclass
class Report:
    findings: dict[str, Finding]
    mentions: dict[str, list[Mention]]
    unknown: dict[str, list[Mention]]
    unresolved_commits: list[Commit]
    sources: list[str]
    repositories: list[str]

    def status(self, identity: str) -> str:
        kinds: set[str] = set()
        for mention in self.mentions.get(identity, []):
            if mention.origin == 'commit' and mention.kind in ('fix', 'follow-up'):
                kinds.add('fixed')
            elif mention.kind == 'decline':
                kinds.add('declined')
            elif mention.kind == 'defer':
                kinds.add('deferred')
            elif mention.origin == 'queue' and mention.kind in ('fix', 'follow-up', 'queued'):
                kinds.add('queued')
        for status in STATUSES:
            if status in kinds:
                return status
        return 'open'

    def suggestion_declined(self, identity: str) -> bool:
        declined: bool = any(mention.kind == 'decline' for mention in self.mentions.get(identity, []))
        result: bool = declined and self.status(identity) == 'fixed'
        return result


def component_of(identity: str) -> str:
    """GUI.Forms, stx or STXlib, from the area's first word."""
    first: str = identity.split('-')[1]
    name: str = COMPONENT_NAMES.get(first, first)
    return name


def source_key(finding: Finding) -> str:
    key: str = finding.source + ' / ' + component_of(finding.identity)
    return key


def finding_sort_key(identity: str) -> tuple[str, int]:
    head, number = identity.rsplit('-F', 1)
    return (head, int(number))


def expand_range(letter: str, area: str, section: str, first: int, last_section: str | None,
                 last: str | None) -> list[str]:
    """The IDs of `F<first>` or `F<first> to [NN-]F<last>`; a range across sections keeps both ends."""
    prefix: str = letter + '-' + area + '-' + section + '-F'
    if last is None:
        return [prefix + str(first)]
    end: int = int(last)
    if last_section is not None and last_section != section:
        other: str = letter + '-' + area + '-' + last_section + '-F' + last
        return [prefix + str(first), other]
    if end < first:
        return [prefix + str(first)]
    identities: list[str] = [prefix + str(number) for number in range(first, end + 1)]
    return identities


def resolve(text: str, context: ResolveContext, known: set[str]) -> Resolution:
    """The finding IDs a sentence names, updating the record's context as it reads."""
    result: Resolution = Resolution()
    for match in TOKEN_PATTERN.finditer(text):
        candidates: list[str] = []
        short: bool = True
        if match.group('fl') is not None:
            context.letter = match.group('fl')
            context.area = match.group('fa')
            context.section = match.group('fs')
            candidates = expand_range(context.letter, context.area, context.section, int(match.group('ff')),
                                      match.group('fxs'), match.group('fxf'))
            short = False
        elif match.group('sl') is not None:
            context.letter = match.group('sl')
            context.area = match.group('sa')
            context.section = match.group('ss')
            continue
        elif match.group('al') is not None:
            context.letter = match.group('al')
            context.area = match.group('aa')
            context.section = ''
            continue
        elif match.group('ns') is not None:
            if context.area == '':
                result.unresolved = True
                continue
            context.section = match.group('ns')
            candidates = expand_range(context.letter, context.area, context.section, int(match.group('nf')),
                                      match.group('nxs'), match.group('nxf'))
        else:
            if context.area == '' or context.section == '':
                result.unresolved = True
                continue
            candidates = expand_range(context.letter, context.area, context.section, int(match.group('bf')),
                                      match.group('bxs'), match.group('bxf'))
        for candidate in candidates:
            if short and candidate not in known:
                result.unresolved = True
                continue
            if candidate not in result.identities:
                result.identities.append(candidate)
    return result


def classify(sentence: str, heading: str, default: str) -> str:
    """fix, follow-up, decline, defer, note or `default`, by the sentence, then its list heading."""
    # A sentence that says deferred is a deferral even beside "declined"; a heading
    # such as "declined or deferred:" is a decline unless its item says otherwise.
    orders: tuple[tuple[str, tuple[re.Pattern[str], re.Pattern[str]]], ...] = (
        (sentence, (DEFER_PATTERN, DECLINE_PATTERN)), (heading, (DECLINE_PATTERN, DEFER_PATTERN)))
    for text, order in orders:
        lowered: str = NO_DECLINE_PATTERN.sub('', text.lower())
        if NOTE_PATTERN.search(lowered) is not None:
            return 'note'
        for pattern in order:
            if pattern.search(lowered) is not None:
                kind: str = 'defer' if pattern is DEFER_PATTERN else 'decline'
                return kind
        if FOLLOW_UP_PATTERN.search(lowered) is not None:
            return 'follow-up'
    return default


def read_record(text: str, default: str, origin: str, where: str, known: set[str],
                mentions: dict[str, list[Mention]], unknown: dict[str, list[Mention]]) -> bool:
    """Adds the mentions of one record; true when it held F numbers that resolved to nothing."""
    context: ResolveContext = ResolveContext()
    headings: list[tuple[int, str]] = []
    unresolved: bool = False
    for raw in text.split('\n'):
        bullet: re.Match[str] | None = BULLET_PATTERN.match(raw)
        indent: int = len(bullet.group(1)) if bullet is not None else 0
        body: str = bullet.group(2).strip() if bullet is not None else raw.strip()
        if body == '':
            continue
        while len(headings) != 0 and headings[-1][0] >= indent:
            headings.pop()
        heading: str = ' '.join(entry[1] for entry in headings)
        for sentence in SENTENCE_BREAK.split(body):
            resolution: Resolution = resolve(sentence, context, known)
            unresolved = unresolved or resolution.unresolved
            if len(resolution.identities) == 0:
                continue
            kind: str = classify(sentence, heading, default)
            for identity in resolution.identities:
                mention: Mention = Mention(identity, kind, origin, where, sentence.strip()[:200])
                target: dict[str, list[Mention]] = mentions if identity in known else unknown
                target.setdefault(identity, []).append(mention)
        if body.endswith(':'):
            headings.append((indent, body))
    return unresolved


def read_findings(folder: Path) -> tuple[list[Finding], list[tuple[str, str]]]:
    """A folder's findings, and its decline sections as (file, text)."""
    findings: list[Finding] = []
    declines: list[tuple[str, str]] = []
    for path in sorted(folder.rglob('*.md')):
        lines: list[str] = path.read_text(encoding='utf-8').split('\n')
        relative: str = folder.name + '/' + path.relative_to(folder).as_posix()
        decline_level: int = 0
        decline_lines: list[str] = []
        pending: Finding | None = None
        for line in lines:
            heading: re.Match[str] | None = HEADING_PATTERN.match(line)
            if heading is not None:
                level: int = len(heading.group(1))
                if decline_level != 0 and level <= decline_level:
                    declines.append((relative, '\n'.join(decline_lines)))
                    decline_level = 0
                    decline_lines = []
                if decline_level == 0 and 'declin' in heading.group(2).lower():
                    decline_level = level
                    continue
                pending = None
                finding: re.Match[str] | None = FINDING_HEADING_PATTERN.match(line)
                if finding is not None:
                    pending = Finding(finding.group(1), '', '', finding.group(2), folder.name, relative)
                    findings.append(pending)
            elif decline_level != 0:
                decline_lines.append(line)
            if pending is not None and pending.severity == '':
                severity: re.Match[str] | None = SEVERITY_PATTERN.search(line)
                if severity is not None:
                    words: str = severity.group(1).replace('*', '').strip()
                    first: str = words.split(' ', 1)[0] if words != '' else ''
                    if first in SEVERITIES:
                        pending.severity = first
                        pending.severity_note = words[len(first):].strip()
        if decline_level != 0:
            declines.append((relative, '\n'.join(decline_lines)))
    return findings, declines


def run_git(checkout: Path, arguments: list[str]) -> str:
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        ['git', '-C', str(checkout)] + arguments, capture_output=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError('git ' + ' '.join(arguments) + ' in ' + str(checkout) + ': '
                           + completed.stderr.decode('utf-8', 'replace').strip())
    text: str = completed.stdout.decode('utf-8', 'replace')
    return text


def parse_repository(argument: str) -> tuple[str, Path, str]:
    """NAME=PATH[@REF] as (name, path, ref); the name defaults to the folder's, the ref to HEAD."""
    name: str = ''
    rest: str = argument
    if '=' in argument:
        name, rest = argument.split('=', 1)
    reference: str = 'HEAD'
    if '@' in rest:
        rest, reference = rest.rsplit('@', 1)
    path: Path = Path(rest).expanduser()
    if name == '':
        name = path.name
    return name, path, reference


def read_commits(name: str, checkout: Path, reference: str) -> list[Commit]:
    text: str = run_git(checkout, ['log', '--no-merges', '--format=%H%x1f%B%x1e', reference])
    commits: list[Commit] = []
    for entry in text.split('\x1e'):
        stripped: str = entry.strip('\n')
        if '\x1f' not in stripped:
            continue
        identity, message = stripped.split('\x1f', 1)
        commits.append(Commit(name, identity.strip(), message.strip()))
    return commits


def queue_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    files: list[Path] = [item for item in sorted(path.glob('*.md')) if item.name != 'README.md']
    return files


def collect(audits: list[Path], queues: list[Path], repositories: list[str]) -> Report:
    findings: dict[str, Finding] = {}
    decline_records: list[tuple[str, str]] = []
    sources: list[str] = []
    for folder in audits:
        if not folder.is_dir():
            raise RuntimeError('no audit folder ' + str(folder))
        sources.append(str(folder))
        folder_findings, folder_declines = read_findings(folder)
        for finding in folder_findings:
            if finding.identity not in findings:
                findings[finding.identity] = finding
        decline_records.extend(folder_declines)
    known: set[str] = set(findings)
    mentions: dict[str, list[Mention]] = {}
    unknown: dict[str, list[Mention]] = {}
    for record, text in decline_records:
        read_record(text, 'decline', 'audit', record, known, mentions, unknown)
    for queue in queues:
        if not queue.exists():
            raise RuntimeError('no queue ' + str(queue))
        sources.append(str(queue))
        for path in queue_files(queue):
            read_record(path.read_text(encoding='utf-8'), 'queued', 'queue', path.name, known, mentions, unknown)
    unresolved_commits: list[Commit] = []
    names: list[str] = []
    for argument in repositories:
        name, checkout, reference = parse_repository(argument)
        names.append(name + '=' + str(checkout) + '@' + reference)
        for commit in read_commits(name, checkout, reference):
            unresolved: bool = read_record(commit.message, 'fix', 'commit', commit.where(), known,
                                           mentions, unknown)
            if unresolved and 'audit' in commit.message.lower():
                unresolved_commits.append(commit)
    report: Report = Report(findings, mentions, unknown, unresolved_commits, sources, names)
    return report


def count_table(report: Report) -> dict[str, dict[str, dict[str, int]]]:
    """{source: {severity: {found, fixed, declined, ...}}}, severities with no finding left out."""
    table: dict[str, dict[str, dict[str, int]]] = {}
    for identity in sorted(report.findings, key=finding_sort_key):
        finding: Finding = report.findings[identity]
        severity: str = finding.severity if finding.severity != '' else 'unstated'
        row: dict[str, int] = table.setdefault(source_key(finding), {}).setdefault(
            severity, {name: 0 for name in ('found',) + STATUSES})
        row['found'] += 1
        row[report.status(identity)] += 1
    return table


def severity_order(severity: str) -> int:
    order: int = SEVERITIES.index(severity) if severity in SEVERITIES else len(SEVERITIES)
    return order


def summary_rows(report: Report) -> list[list[str]]:
    table: dict[str, dict[str, dict[str, int]]] = count_table(report)
    rows: list[list[str]] = []
    totals: dict[str, int] = {name: 0 for name in ('found',) + STATUSES}
    for source in sorted(table):
        for severity in sorted(table[source], key=severity_order):
            row: dict[str, int] = table[source][severity]
            rows.append([source, severity] + [str(row[name]) for name in ('found',) + STATUSES])
            for name in totals:
                totals[name] += row[name]
    rows.append(['all', 'all'] + [str(totals[name]) for name in ('found',) + STATUSES])
    return rows


def mention_places(mentions: list[Mention], kinds: tuple[str, ...], origin: str) -> list[str]:
    places: list[str] = []
    for mention in mentions:
        if mention.kind in kinds and mention.origin == origin and mention.where not in places:
            places.append(mention.where)
    return places


def finding_json(report: Report, identity: str) -> dict[str, object]:
    finding: Finding = report.findings[identity]
    mentions: list[Mention] = report.mentions.get(identity, [])
    result: dict[str, object] = {
        'id': identity,
        'severity': finding.severity,
        'severity_note': finding.severity_note,
        'title': finding.title,
        'source': source_key(finding),
        'record': finding.record,
        'status': report.status(identity),
        'suggested_fix_declined': report.suggestion_declined(identity),
        'commits': mention_places(mentions, ('fix', 'follow-up'), 'commit'),
        'queue_entries': mention_places(mentions, ('fix', 'follow-up', 'queued', 'decline', 'defer'), 'queue'),
        'decline_records': [mention.where + ': ' + mention.line for mention in mentions
                            if mention.kind == 'decline'],
        'defer_records': [mention.where + ': ' + mention.line for mention in mentions if mention.kind == 'defer'],
    }
    return result


def report_json(report: Report) -> dict[str, object]:
    identities: list[str] = sorted(report.findings, key=finding_sort_key)
    result: dict[str, object] = {
        'sources': report.sources,
        'repositories': report.repositories,
        'counts': count_table(report),
        'findings': [finding_json(report, identity) for identity in identities],
        'named_not_in_audit_records': {identity: [mention.where for mention in report.unknown[identity]]
                                       for identity in sorted(report.unknown)},
        'commits_with_unresolved_short_forms': [{'commit': commit.where(), 'subject': commit.subject()}
                                                for commit in report.unresolved_commits],
    }
    return result


def text_table(titles: list[str], rows: list[list[str]]) -> list[str]:
    widths: list[int] = [len(title) for title in titles]
    for row in rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(cell))
    lines: list[str] = []
    for row in [titles] + rows:
        cells: list[str] = []
        for index, cell in enumerate(row):
            cells.append(cell.ljust(widths[index]) if index < 2 else cell.rjust(widths[index]))
        lines.append('  '.join(cells).rstrip())
    return lines


def markdown_table(titles: list[str], rows: list[list[str]]) -> list[str]:
    lines: list[str] = ['| ' + ' | '.join(titles) + ' |',
                        '|' + '|'.join('---' if index < 2 else '---:' for index in range(len(titles))) + '|']
    for row in rows:
        lines.append('| ' + ' | '.join(cell.replace('|', '\\|') for cell in row) + ' |')
    return lines


SUMMARY_TITLES: list[str] = ['Source', 'Severity', 'Found'] + [status.capitalize() for status in STATUSES]


def status_rows(report: Report, status: str) -> list[list[str]]:
    rows: list[list[str]] = []
    for identity in sorted(report.findings, key=finding_sort_key):
        if report.status(identity) != status:
            continue
        data: dict[str, object] = finding_json(report, identity)
        if status == 'fixed':
            evidence: list[str] = list(data['commits'])  # type: ignore[arg-type]
        elif status == 'declined':
            evidence = list(data['decline_records'])  # type: ignore[arg-type]
        elif status == 'deferred':
            evidence = list(data['defer_records'])  # type: ignore[arg-type]
        else:
            evidence = list(data['queue_entries'])  # type: ignore[arg-type]
        note: str = ' (suggested fix declined)' if report.suggestion_declined(identity) else ''
        rows.append([identity, report.findings[identity].severity + note, '; '.join(evidence)])
    return rows


def render_text(report: Report) -> str:
    lines: list[str] = ['Audit findings by source and severity, from the records:', '']
    lines.extend(text_table(SUMMARY_TITLES, summary_rows(report)))
    for status in ('declined', 'deferred'):
        rows: list[list[str]] = status_rows(report, status)
        if len(rows) != 0:
            lines.extend(['', status.capitalize() + ':'])
            for row in rows:
                lines.append('  ' + row[0] + ' (' + row[1] + '): ' + row[2])
    if len(report.unknown) != 0:
        lines.extend(['', 'Named, not in the audit records: ' + ', '.join(sorted(report.unknown))])
    if len(report.unresolved_commits) != 0:
        lines.extend(['', 'Commits naming F numbers with no audit area (not counted):'])
        for commit in report.unresolved_commits:
            lines.append('  ' + commit.where() + ' ' + commit.subject())
    lines.extend(['', 'Repositories: ' + ', '.join(report.repositories)])
    return '\n'.join(lines) + '\n'


def render_markdown(report: Report) -> str:
    lines: list[str] = ['## Audit findings by source and severity', '',
                        'Counted from the audit folders, queue entries and commit messages by '
                        '`tools/integrity_counts.py`; nothing is estimated.', '']
    lines.extend(markdown_table(SUMMARY_TITLES, summary_rows(report)))
    for status in STATUSES:
        rows: list[list[str]] = status_rows(report, status)
        if len(rows) == 0:
            continue
        evidence: str = {'fixed': 'Commits', 'declined': 'Decline records', 'deferred': 'Records',
                         'queued': 'Queue entries', 'open': 'Queue entries'}[status]
        lines.extend(['', '### ' + status.capitalize() + ' (' + str(len(rows)) + ')', ''])
        lines.extend(markdown_table(['Finding', 'Severity', evidence], rows))
    if len(report.unknown) != 0:
        lines.extend(['', '### Named, not in the audit records', ''])
        for identity in sorted(report.unknown):
            places: list[str] = []
            for mention in report.unknown[identity]:
                if mention.where not in places:
                    places.append(mention.where)
            lines.append('- ' + identity + ': ' + ', '.join(places))
    if len(report.unresolved_commits) != 0:
        lines.extend(['', '### Commits naming F numbers with no audit area (not counted)', ''])
        for commit in report.unresolved_commits:
            lines.append('- `' + commit.where() + '` ' + commit.subject())
    lines.extend(['', 'Sources: ' + ', '.join('`' + source + '`' for source in report.sources) + '.',
                  'Histories: ' + ', '.join('`' + name + '`' for name in report.repositories) + '.'])
    return '\n'.join(lines) + '\n'


def write_output(destination: str, text: str) -> None:
    if destination == '-':
        sys.stdout.write(text)
    else:
        Path(destination).write_text(text, encoding='utf-8')


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description='Count audit findings found, fixed and declined, from the records.')
    parser.add_argument('--audit', action='append', type=Path, default=[], metavar='DIR',
                        help='an audit folder (findings and decline sections)')
    parser.add_argument('--queue', action='append', type=Path, default=[], metavar='PATH',
                        help='a queue directory or entry')
    parser.add_argument('--repo', action='append', default=[], metavar='NAME=PATH[@REF]',
                        help='a checkout whose history (REF, default HEAD) is read')
    parser.add_argument('--json', metavar='FILE', help='write the JSON report ("-" for standard output)')
    parser.add_argument('--markdown', metavar='FILE', help='write the Markdown report ("-" for standard output)')
    options: argparse.Namespace = parser.parse_args(arguments)
    if len(options.audit) == 0:
        parser.error('at least one --audit folder is required')
    report: Report
    try:
        report = collect(options.audit, options.queue, options.repo)
    except (RuntimeError, OSError) as error:
        sys.stderr.write('integrity_counts: ' + str(error) + '\n')
        return 2
    if options.json is not None:
        write_output(options.json, json.dumps(report_json(report), indent=2, sort_keys=True) + '\n')
    if options.markdown is not None:
        write_output(options.markdown, render_markdown(report))
    if options.json != '-' and options.markdown != '-':
        sys.stdout.write(render_text(report))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
