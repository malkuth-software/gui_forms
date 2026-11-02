#!/usr/bin/env python3
"""Tests that a failed build can never become the cache the next run restores.

Owner, 2026-11-01 (remote split design, step 0, cache safety): object and
dependency caches are saved only when the build and its tests passed. A
restore picks the newest entry of a key family, and `ci_cache.py prune`
keeps that newest entry and deletes the older ones, so a cache saved by a
failed run used to replace the last good one.

The rules, checked over every workflow without a YAML library (the steps are
found by their `- name:` / `- uses:` lines, as test_workflow_pipelines.py
does):

- a cache save step has no status function other than success(): no
  always(), !cancelled(), cancelled() or failure() (a condition without a
  status function means success());
- an object (.ccache) or dependency (third_party) save comes after every
  test step of its job (a step named "Test..." or "Build and test...");
- the cache-prune job runs only when none of the jobs it follows failed.
"""
from __future__ import annotations
from pathlib import Path
import re
import unittest

WORKFLOWS: Path = Path(__file__).resolve().parents[1] / ".github" / "workflows"
JOB_START: re.Pattern[str] = re.compile(r"^  [A-Za-z0-9_-]+:\s*$", re.MULTILINE)
STEP_START: re.Pattern[str] = re.compile(r"^\s*- (?:name|uses):", re.MULTILINE)
CACHE_SAVE: re.Pattern[str] = re.compile(r"uses:\s*actions/cache/save@")
STEP_IF: re.Pattern[str] = re.compile(r"^\s{8}if:\s*(.*)$", re.MULTILINE)
STATUS_FUNCTIONS: re.Pattern[str] = re.compile(r"\b(always|cancelled|failure)\(\)")
TEST_STEP: re.Pattern[str] = re.compile(r"^\s*- name:\s*(Test\b|Build and test\b)")
GUARDED_PATHS: tuple[str, ...] = (".ccache", "third_party/")


def jobs(workflow: str) -> list[str]:
    """The text of each job under `jobs:` (an empty list without one)."""
    marker: int = workflow.find("\njobs:\n")
    if marker < 0:
        return []
    body: str = workflow[marker:]
    starts: list[int] = [match.start() for match in JOB_START.finditer(body)]
    starts.append(len(body))
    result: list[str] = []
    index: int
    for index in range(len(starts) - 1):
        result.append(body[starts[index]:starts[index + 1]])
    return result


def steps(job: str) -> list[str]:
    starts: list[int] = [match.start() for match in STEP_START.finditer(job)]
    starts.append(len(job))
    result: list[str] = []
    index: int
    for index in range(len(starts) - 1):
        result.append(job[starts[index]:starts[index + 1]])
    return result


def save_condition_violation(step: str) -> str | None:
    """Why a cache save step's condition lets a failed run save, or None."""
    condition: re.Match[str] | None = STEP_IF.search(step)
    if condition is None:
        return None
    status: re.Match[str] | None = STATUS_FUNCTIONS.search(condition.group(1))
    if status is None:
        return None
    return "saves under " + status.group(0)


def guards_build_output(step: str) -> bool:
    result: bool = any(path in step for path in GUARDED_PATHS)
    return result


def saves_before_tests(job: str) -> list[str]:
    """The first lines of object or dependency saves that precede a test step of `job`."""
    found: list[str] = []
    pending: list[str] = []
    step: str
    for step in steps(job):
        if CACHE_SAVE.search(step) is not None and guards_build_output(step):
            pending.append(step.strip().splitlines()[0])
        if TEST_STEP.search(step) is not None:
            found.extend(pending)
            pending = []
    return found


def prune_condition(workflow: str) -> str | None:
    """The `if:` of the cache-prune job, or None when the workflow has none."""
    job: str
    for job in jobs(workflow):
        if job.lstrip().startswith("cache-prune:"):
            match: re.Match[str] | None = re.search(r"^    if:\s*(.*)$", job, re.MULTILINE)
            return match.group(1) if match is not None else ""
    return None


def prune_waits_for_success(condition: str) -> bool:
    result: bool = ("!contains(needs.*.result, 'failure')" in condition
                    and STATUS_FUNCTIONS.search(condition.replace("!cancelled()", "")) is None)
    return result


class WorkflowCacheSaveTests(unittest.TestCase):
    def test_no_cache_save_runs_after_a_failure(self) -> None:
        checked: int = 0
        path: Path
        for path in sorted(WORKFLOWS.glob("*.yml")):
            job: str
            for job in jobs(path.read_text(encoding="utf-8")):
                step: str
                for step in steps(job):
                    if CACHE_SAVE.search(step) is None:
                        continue
                    checked += 1
                    violation: str | None = save_condition_violation(step)
                    self.assertIsNone(violation, f"{path.name}: {step.strip().splitlines()[0]}: {violation}")
        # native-platform, native and release save several caches each.
        self.assertGreater(checked, 5)

    def test_object_and_dependency_saves_follow_the_tests(self) -> None:
        guarded: int = 0
        path: Path
        for path in sorted(WORKFLOWS.glob("*.yml")):
            job: str
            for job in jobs(path.read_text(encoding="utf-8")):
                guarded += sum(1 for step in steps(job)
                               if CACHE_SAVE.search(step) is not None and guards_build_output(step))
                self.assertEqual(saves_before_tests(job), [], path.name)
        self.assertGreater(guarded, 3)

    def test_the_prune_waits_for_a_passing_push(self) -> None:
        condition: str | None = prune_condition((WORKFLOWS / "native.yml").read_text(encoding="utf-8"))
        self.assertIsNotNone(condition, "native.yml has no cache-prune job")
        self.assertTrue(prune_waits_for_success(condition), condition)

    def test_the_rules_reject_the_old_conditions(self) -> None:
        # The conditions this change replaced (native-platform.yml, release.yml).
        old_objects: str = ("      - uses: actions/cache/save@v4\n"
                            "        if: github.event_name != 'pull_request' && (always() && (steps.reuse.outputs.reused == 'true'))\n"
                            "        with:\n          path: .ccache\n")
        self.assertEqual(save_condition_violation(old_objects), "saves under always()")
        harness: str = ("      - uses: actions/cache/save@v4\n"
                        "        if: ${{ !cancelled() && steps.compiler.outcome == 'success' }}\n")
        self.assertEqual(save_condition_violation(harness), "saves under cancelled()")
        implicit: str = "      - uses: actions/cache/save@v4\n        if: github.event_name == 'push'\n"
        self.assertIsNone(save_condition_violation(implicit))
        early: str = ("jobs:\n  programs:\n    steps:\n"
                      "      - name: Build\n        run: make\n"
                      "      - uses: actions/cache/save@v4\n        if: success()\n        with:\n          path: .ccache\n"
                      "      - name: Test\n        run: ctest\n")
        self.assertEqual(saves_before_tests(jobs("x\n" + early)[0]), ["- uses: actions/cache/save@v4"])
        self.assertFalse(prune_waits_for_success("always() && github.event_name == 'push'"))
        self.assertTrue(prune_waits_for_success(
            "${{ !cancelled() && !contains(needs.*.result, 'failure') && github.event_name == 'push' }}"))


if __name__ == "__main__":
    unittest.main()
