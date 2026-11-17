#!/usr/bin/env python3
"""Moved to template_harness: harness/check_lifetime_rules.py (harness.lock pins the commit).

A forwarding shim, kept so the commands and imports that name this path
(File Manager's among them) keep working: run, it runs the harness's tool
with these arguments; imported, it is the harness's module. Either way it
fails clearly when the harness is not beside this checkout
(tools/harness_link.py, docs/building/template-harness.md).

Run, it then applies GUI.Forms' own member-accessor rule
(tools/check_member_views.py) and the C ABI's termination rule
(tools/check_exit_statics.py) and the tests' stable-ID literal rule
(tools/check_stable_id_literals.py, tests/ only) to the same paths, and fails
when the harness's rules or any of those report a finding. Imported, the name resolves
to the harness's module (forward_import), which is how the run below reads
the harness's file selection and masking.
"""
import harness_link

if __name__ == '__main__':
    import sys
    from pathlib import Path
    import check_exit_statics
    import check_stable_id_literals
    import check_member_views
    harness_status: int = 0
    try:
        harness_link.run_tool('check_lifetime_rules')
    except SystemExit as stopped:
        harness_status = stopped.code if isinstance(stopped.code, int) else 1
    if harness_status == 2:
        raise SystemExit(2)
    # The harness tool has already printed its usage for -h or --help; the
    # member-view pass below would read the option as a path.
    if '-h' in sys.argv[1:] or '--help' in sys.argv[1:]:
        raise SystemExit(harness_status)
    import check_lifetime_rules as harness_rules
    selected: list[Path] = harness_rules.collect(sys.argv[1:])
    # Each file is masked once here, for the member-view and termination passes.
    masked: dict[Path, str] = {}
    path: Path
    for path in selected:
        masked[path] = harness_rules.masked_source(path)
    findings: list[check_member_views.Finding] = []
    for path in selected:
        findings.extend(check_member_views.check_member_views(path, masked[path]))
    finding: check_member_views.Finding
    for finding in findings:
        print(f'{finding.path}:{finding.line}: {finding.rule}: {finding.detail}')
    # The termination rule reads destructor triviality from the first-party
    # types of every selected file, so all are indexed before any is checked.
    import destructor_index
    types: destructor_index.TypeIndex = destructor_index.TypeIndex()
    for path in selected:
        types.add(path, masked[path])
    exit_findings: list[check_exit_statics.Finding] = []
    for path in selected:
        exit_findings.extend(check_exit_statics.check_exit_statics(path, masked[path], types))
    exit_finding: check_exit_statics.Finding
    for exit_finding in exit_findings:
        print(f'{exit_finding.path}:{exit_finding.line}: {exit_finding.rule}: {exit_finding.detail}')
    id_findings: list[check_stable_id_literals.Finding] = []
    for path in selected:
        id_findings.extend(check_stable_id_literals.check_stable_id_literals(path, path.read_text(encoding='utf-8', errors='replace')))
    id_finding: check_stable_id_literals.Finding
    for id_finding in id_findings:
        print(f'{id_finding.path}:{id_finding.line}: {id_finding.rule}: {id_finding.detail}')
    print(f'{len(selected)} files; {len(findings)} member-view findings; {len(exit_findings)} exit-static findings; '
          f'{len(id_findings)} stable-id findings.')
    if harness_status != 0 or findings or exit_findings or id_findings:
        raise SystemExit(1)
    raise SystemExit(0)
else:
    harness_link.forward_import('check_lifetime_rules')
