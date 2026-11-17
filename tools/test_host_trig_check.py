#!/usr/bin/env python3
"""Tests for host_trig_check.py: call matching, skipped comments and
literals, and the shrink-only allowance."""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import host_trig_check

SOURCE: str = """
double a = std::sin(x);                 // counted
double b = cos (y) + ::tanf(z);         // two counted (bare and C form)
double c = std::atan2(y, x) * std::hypot(x, y);  // two counted
double d = engine_sine_cosine(x).sine;  // not counted: a longer name
double e = shape.sin(x) + p->cos(x);    // not counted: members
double f = stxlib::math::sin(x);        // not counted: another namespace
// double g = std::sin(x);              not counted: a comment
/* double h = std::cos(x); */
const char* i = "sin(x)";               // not counted: a string
"""


class CountTests(unittest.TestCase):
    def test_counts_only_host_calls(self) -> None:
        self.assertEqual(host_trig_check.count_calls(SOURCE), 5)

    def test_comment_keeps_lines(self) -> None:
        stripped: str = host_trig_check.code_of("a /* x\ny */ b\n")
        self.assertEqual(stripped.count("\n"), 2)


class AllowanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "src" / "core").mkdir(parents=True)
        (self.root / "src" / "core" / "a.cpp").write_text("double v = std::sin(x) + std::cos(x);\n")
        self.allowance = self.root / "allowance.json"

    def tearDown(self) -> None:
        self.directory.cleanup()

    def run_command(self, command: str) -> int:
        result: int = host_trig_check.main([command, "--root", str(self.root), "--allowance", str(self.allowance)])
        return result

    def test_new_calls_fail(self) -> None:
        self.assertEqual(self.run_command("check"), 1)
        self.assertEqual(self.run_command("update"), 1)

    def test_allowance_only_shrinks(self) -> None:
        host_trig_check.write_allowance(self.allowance, {"src/core/a.cpp": 2})
        self.assertEqual(self.run_command("check"), 0)
        (self.root / "src" / "core" / "a.cpp").write_text("double v = std::sin(x);\n")
        self.assertEqual(self.run_command("check"), 1)
        self.assertEqual(self.run_command("update"), 0)
        self.assertEqual(host_trig_check.read_allowance(self.allowance), {"src/core/a.cpp": 1})
        self.assertEqual(self.run_command("check"), 0)
        (self.root / "src" / "core" / "a.cpp").write_text("double v = std::sin(x) + std::tan(x);\n")
        self.assertEqual(self.run_command("check"), 1)
        self.assertEqual(self.run_command("update"), 1)


if __name__ == "__main__":
    unittest.main()
