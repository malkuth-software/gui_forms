#!/usr/bin/env python3
"""Seeded breaks for tools/abi_audit.py.

Each case edits tests/abi/audit_fixtures/base.h and audits the result against
the base's own manifest. A break case passes only when the audit reports a
finding of the expected rule; a legal case passes only when it reports none.
Each case runs in the audit mode it names; a recorded case first records its
breaks in the policy's ledger, as a change between 0.5 and 1.0 would.
The real header's audit is the gui_forms_abi_audit test.
"""
from __future__ import annotations

import argparse
import concurrent.futures
from dataclasses import dataclass
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE: Path = Path(__file__).resolve().parent
ROOT: Path = HERE.parent.parent
AUDIT: Path = ROOT / "tools" / "abi_audit.py"
BASE: Path = HERE / "audit_fixtures" / "base.h"


@dataclass(frozen=True)
class Case:
    name: str
    # Text replacements applied to the fixture in order, each exactly once.
    edits: tuple[tuple[str, str], ...]
    # The rule the audit must report, or "" for a legal change.
    rule: str
    mode: str = "report"
    # Whether the case's breaks are in the policy's ledger.
    recorded: bool = False
    # The experimental 0.x header beside the case's c/ directory, or none.
    legacy: str = ""


SLOT_FIRST: str = "    gf_status (GF_CALL* first)(gf_object object, gf_point point);\n"
SLOT_SECOND: str = "    gf_status (GF_CALL* second)(gf_object object, gf_sample_record* record);\n"
SLOT_THIRD: str = "    gf_status (GF_CALL* third)(gf_object object);\n"
COUNT_FIELD: str = "    uint64_t count;\n"
FLAGS_FIELD: str = "    uint32_t flags;\n"
MINOR: str = "#define GF_ABI_MINOR 1u"
MAJOR: str = "#define GF_ABI_MAJOR 1u"
MINOR_BUMPED: str = "#define GF_ABI_MINOR 2u"
EXPORT: str = "GF_ABI_EXPORT gf_status GF_CALL gf_abi_get_table("
TABLE: str = "typedef struct gf_sample_table {"
WIDE_RECORD: str = "typedef struct gf_wide {\n    double a;\n    double b;\n    double c;\n} gf_wide;\n\n"

CASES: tuple[Case, ...] = (
    Case("unchanged", (), ""),
    Case("slot added with a minor bump", ((SLOT_SECOND, SLOT_SECOND + SLOT_THIRD), (MINOR, MINOR_BUMPED)), ""),
    Case("extensible record grown with a minor bump", ((COUNT_FIELD, COUNT_FIELD + "    uint64_t more;\n"),
                                                       (MINOR, MINOR_BUMPED)), ""),
    Case("slots reordered", ((SLOT_FIRST + SLOT_SECOND, SLOT_SECOND + SLOT_FIRST),), "break"),
    Case("slot removed", ((SLOT_SECOND, ""),), "break"),
    Case("slot parameter type changed",
         (("gf_object object, gf_point point", "gf_object object, gf_object point"),), "break"),
    Case("callback signature changed",
         (("(void* context, uint32_t value)", "(void* context, uint64_t value)"),), "break"),
    Case("field type changed", ((FLAGS_FIELD, "    int32_t flags;\n"),), "break"),
    Case("field inserted in the middle", ((FLAGS_FIELD, FLAGS_FIELD + "    uint64_t inserted;\n"),), "break"),
    Case("fixed record grown", (("    double y;\n", "    double y;\n    double z;\n"), (MINOR, MINOR_BUMPED)), "break"),
    Case("constant value changed",
         (("#define GF_ERROR_INVALID_ARGUMENT 1", "#define GF_ERROR_INVALID_ARGUMENT 2"),), "break"),
    Case("constant removed", (("#define GF_ERROR_INVALID_ARGUMENT 1\n", ""),), "break"),
    Case("scalar typedef changed", (("typedef int32_t gf_status;", "typedef uint32_t gf_status;"),), "break"),
    Case("minor went back", ((MINOR, "#define GF_ABI_MINOR 0u"),), "break"),
    Case("slot added without a minor bump", ((SLOT_SECOND, SLOT_SECOND + SLOT_THIRD),), "version"),
    Case("export added without a minor bump",
         ((EXPORT, "GF_ABI_EXPORT gf_status GF_CALL gf_abi_other(uint32_t value);\n" + EXPORT),), "version"),
    Case("implicit padding", ((COUNT_FIELD, COUNT_FIELD + "    uint32_t tail;\n"),), "padding"),
    Case("enum type", (("typedef uint32_t gf_table_id;", "typedef enum gf_table_id { GF_A = 1 } gf_table_id;"),),
         "forbidden-type"),
    Case("bool field", ((FLAGS_FIELD, "    bool flags;\n"),), "forbidden-type"),
    Case("size_t field", ((COUNT_FIELD, "    size_t count;\n"),), "forbidden-type"),
    Case("bit-field", ((FLAGS_FIELD, "    uint32_t flags : 4;\n"),), "bit-field"),
    Case("flexible array", ((COUNT_FIELD, COUNT_FIELD + "    uint8_t bytes[];\n"),), "flexible-array"),
    Case("record over 16 bytes passed by value",
         ((TABLE, WIDE_RECORD + TABLE), ("gf_object object, gf_point point", "gf_object object, gf_wide point")),
         "by-value"),
    Case("function pointer without GF_CALL", (("(GF_CALL* second)", "(*second)"),), "calling-convention"),
    Case("slot that does not return gf_status", (("gf_status (GF_CALL* second)", "gf_object (GF_CALL* second)"),),
         "status"),
    Case("table without a table id", (("#define GF_TABLE_SAMPLE 1u\n", ""),), "table"),
    Case("blocking mode: slot removed", ((SLOT_SECOND, ""),), "break", "blocking"),
    Case("report mode: recorded break", ((SLOT_SECOND, ""),), "", "report", True),
    Case("blocking mode: recorded break", ((SLOT_SECOND, ""),), "break", "blocking", True),
    Case("report mode: recorded break among unrecorded", ((SLOT_FIRST + SLOT_SECOND, SLOT_SECOND + SLOT_FIRST),),
         "break", "report", True),
    Case("a name the 0.x header declares", (), "legacy-name",
         legacy="typedef struct gf_point {\n    double x;\n    double y;\n} gf_point;\n"),
    Case("names the 0.x header does not declare", (), "",
         legacy="typedef struct gf_legacy_point {\n    double x;\n} gf_legacy_point;\n"),
    Case("blocking mode: major changed", ((MAJOR, "#define GF_ABI_MAJOR 2u"),), "break", "blocking"),
    Case("report mode: major changed", ((MAJOR, "#define GF_ABI_MAJOR 2u"),), "", "report"),
)
UNRECORDED: str = " (unrecorded: "


def write_policy(path: Path, recorded: list[str]) -> None:
    breaks: list[dict[str, str]] = []
    text: str = ""
    for text in recorded:
        breaks.append({"finding": text, "reason": "seeded by the fixture test", "recorded": "2026-11-01"})
    policy: dict[str, object] = {"format": 1, "mode": "report", "baseline": None, "breaks": breaks}
    path.write_text(json.dumps(policy, indent=2) + "\n", encoding="utf-8", newline="\n")


def audit(python: str, clang: str, header: Path, baseline: Path, policy: Path, mode: str,
          findings_out: list[str]) -> int:
    command: list[str] = [python, "-B", str(AUDIT), "check", "--clang", clang, "--header", str(header),
                          "--include", str(header.parent), "--manifest", str(baseline), "--no-manifest",
                          "--baseline", str(baseline), "--policy", str(policy), "--mode", mode]
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    line: str = ""
    for line in result.stdout.splitlines():
        if line.startswith("abi_audit: ["):
            findings_out.append(line)
    return result.returncode


def run_case(case: Case, python: str, clang: str, base_text: str, baseline: Path, scratch: Path) -> str:
    """Return "" when the case behaves as expected, else the reason."""
    text: str = base_text
    edit: tuple[str, str]
    for edit in case.edits:
        if edit[0] not in text:
            return f"the fixture no longer contains {edit[0]!r}"
        text = text.replace(edit[0], edit[1], 1)
    header: Path = scratch / "gui_forms" / "c" / "gf_abi.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(text, encoding="utf-8", newline="\n")
    if case.legacy:
        (scratch / "gui_forms" / "c_api.h").write_text(case.legacy, encoding="utf-8", newline="\n")
    policy: Path = scratch / "policy.json"
    write_policy(policy, [])
    findings: list[str] = []
    # A recorded case first finds its breaks in report mode, which marks
    # each one unrecorded.
    first_mode: str = "report" if case.recorded else case.mode
    status: int = audit(python, clang, header, baseline, policy, first_mode, findings)
    if case.recorded:
        # Record the breaks the unrecorded audit reported. The "among
        # unrecorded" case leaves the first out, so it must still fail.
        recorded: list[str] = []
        finding_line: str = ""
        prefix: str = "abi_audit: [break] "
        for finding_line in findings:
            if finding_line.startswith(prefix) and UNRECORDED in finding_line:
                recorded.append(finding_line[len(prefix):finding_line.index(UNRECORDED)])
        if not recorded:
            return "the case produced no unrecorded break to record"
        if "among unrecorded" in case.name:
            if len(recorded) < 2:
                return "the case needs two breaks"
            recorded = recorded[1:]
        write_policy(policy, recorded)
        findings = []
        status = audit(python, clang, header, baseline, policy, case.mode, findings)
    if not case.rule:
        if status != 0 or findings:
            return "a legal change was reported: " + "; ".join(findings)
        return ""
    expected: str = f"[{case.rule}]"
    finding: str = ""
    for finding in findings:
        if expected in finding:
            return ""
    return f"no {expected} finding (status {status}): " + "; ".join(findings)


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--clang", default="clang")
    args: argparse.Namespace = parser.parse_args()
    python: str = sys.executable
    base_text: str = BASE.read_text(encoding="utf-8")
    failures: list[str] = []
    detected: int = 0
    breaks: int = 0
    with tempfile.TemporaryDirectory(prefix="gf-abi-audit-") as scratch_name:
        scratch: Path = Path(scratch_name)
        base_header: Path = scratch / "base" / "gui_forms" / "c" / "gf_abi.h"
        base_header.parent.mkdir(parents=True)
        base_header.write_text(base_text, encoding="utf-8", newline="\n")
        baseline: Path = scratch / "base.json"
        extract: list[str] = [python, "-B", str(AUDIT), "extract", "--clang", args.clang, "--header",
                              str(base_header), "--include", str(base_header.parent), "--manifest", str(baseline)]
        result: subprocess.CompletedProcess[str] = subprocess.run(
            extract, text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if result.returncode != 0:
            print("abi_audit_tests: the fixture base does not extract:\n" + result.stdout, flush=True)
            return 1
        # The cases are independent processes; run them side by side and read
        # the results back in case order.
        pool: concurrent.futures.ThreadPoolExecutor = concurrent.futures.ThreadPoolExecutor(max_workers=8)
        pending: list[concurrent.futures.Future[str]] = []
        index: int = 0
        case: Case
        for index, case in enumerate(CASES):
            pending.append(pool.submit(run_case, case, python, args.clang, base_text, baseline,
                                       scratch / f"case{index}"))
        pool.shutdown(wait=True)
        for index, case in enumerate(CASES):
            reason: str = pending[index].result()
            if case.rule:
                breaks += 1
                if not reason:
                    detected += 1
            if reason:
                failures.append(f"{case.name}: {reason}")
    failure: str = ""
    for failure in failures:
        print("abi_audit_tests: FAIL " + failure, flush=True)
    print(f"abi_audit_tests: {detected} of {breaks} seeded breaks detected; "
          f"{len(CASES) - breaks} legal cases; {len(failures)} case(s) failed", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
