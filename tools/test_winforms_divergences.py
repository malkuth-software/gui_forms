#!/usr/bin/env python3
"""Tests for winforms_divergences.py: the committed manifest and page, and
each way an entry can be incomplete."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

import winforms_divergences

ENTRY: dict[str, str] = {
    "control": "Thing",
    "member": "clicked",
    "winforms": "Raises Click twice.",
    "gui_forms": "Raises clicked once.",
    "reason": "A test entry.",
    "source": "owner",
    "date": "2026-11-02",
}


def manifest_with(*entries: dict[str, object]) -> dict[str, object]:
    manifest: dict[str, object] = {"divergences": [copy.deepcopy(entry) for entry in entries]}
    return manifest


class WinFormsDivergencesTests(unittest.TestCase):
    def test_committed_manifest_is_complete(self) -> None:
        manifest = winforms_divergences.load_manifest(winforms_divergences.MANIFEST_PATH)
        self.assertEqual(winforms_divergences.validate(manifest), [])
        self.assertGreater(len(manifest["divergences"]), 0)

    def test_committed_page_is_current(self) -> None:
        manifest = winforms_divergences.load_manifest(winforms_divergences.MANIFEST_PATH)
        page: str = winforms_divergences.PAGE_PATH.read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(page, winforms_divergences.render_page(manifest),
                         "run `python3 tools/winforms_divergences.py render`")

    def test_complete_entry_passes(self) -> None:
        self.assertEqual(winforms_divergences.validate(manifest_with(ENTRY)), [])

    def test_missing_and_unknown_fields(self) -> None:
        entry: dict[str, object] = dict(ENTRY)
        del entry["reason"]
        entry["note"] = "extra"
        findings = winforms_divergences.validate(manifest_with(entry))
        self.assertTrue(any("missing reason" in finding for finding in findings))
        self.assertTrue(any("unknown note" in finding for finding in findings))

    def test_empty_or_non_text_field(self) -> None:
        blank: dict[str, object] = dict(ENTRY, winforms="  ")
        number: dict[str, object] = dict(ENTRY, member=3)
        self.assertTrue(winforms_divergences.validate(manifest_with(blank)))
        self.assertTrue(winforms_divergences.validate(manifest_with(number)))

    def test_source_and_date(self) -> None:
        self.assertTrue(winforms_divergences.validate(manifest_with(dict(ENTRY, source="worker"))))
        self.assertTrue(winforms_divergences.validate(manifest_with(dict(ENTRY, date="2026-02-30"))))
        self.assertTrue(winforms_divergences.validate(manifest_with(dict(ENTRY, date="2 Nov 2026"))))

    def test_duplicate_member(self) -> None:
        findings = winforms_divergences.validate(manifest_with(ENTRY, ENTRY))
        self.assertTrue(any("recorded twice" in finding for finding in findings))

    def test_unreadable_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path: Path = Path(directory) / "bad.json"
            path.write_text("{", encoding="utf-8")
            with self.assertRaises(winforms_divergences.ManifestError):
                winforms_divergences.load_manifest(path)
            path.write_text(json.dumps({"entries": []}), encoding="utf-8")
            with self.assertRaises(winforms_divergences.ManifestError):
                winforms_divergences.load_manifest(path)

    def test_page_groups_by_control(self) -> None:
        other: dict[str, object] = dict(ENTRY, control="Another", member="moved")
        page: str = winforms_divergences.render_page(manifest_with(ENTRY, other))
        self.assertLess(page.index("## Another"), page.index("## Thing"))
        self.assertIn("| Decision | owner, 2026-11-02 |", page)


if __name__ == "__main__":
    unittest.main()
