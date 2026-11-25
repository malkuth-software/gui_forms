#!/usr/bin/env python3
"""Tests for std_surface_census.py: qualified names only, comments and
literals skipped, the C ABI excluded, and the ratchet both ways."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import std_surface_census  # noqa: E402

HEADER: str = """
#pragma once
#include <optional>
// std::optional<int> in a comment is not counted
/* std::vector<int> neither,
   std::string nor this */
namespace gui_forms {
struct Record {
    std::optional<int> width;               // one optional
    ::std::optional<double> height = std::nullopt;
    std::string_view name;                  // a string_view, not a string
    std::string label;
    std::vector<std::shared_ptr<Record>> children;
    const char* text = "std::span<int> in a literal";
    optional<int> unqualified;              // not qualified: not counted
    my_std::vector<int> other;              // another namespace
};
}
"""


class StdSurfaceCensusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.surface = Path(self.directory.name) / "gui_forms"
        (self.surface / "core").mkdir(parents=True)
        (self.surface / "c").mkdir()
        (self.surface / "core" / "record.hpp").write_text(HEADER, encoding="utf-8")
        (self.surface / "core" / "other.hpp").write_text("std::optional<long> one;\n", encoding="utf-8")
        (self.surface / "c" / "abi.h").write_text("std::optional<int> never;\n", encoding="utf-8")
        self.baseline = Path(self.directory.name) / "baseline.json"

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_counts_qualified_code_names_only(self) -> None:
        counts = std_surface_census.count_text(HEADER)
        self.assertEqual(counts["optional"], 2)
        self.assertEqual(counts["nullopt"], 1)
        self.assertEqual(counts["string_view"], 1)
        self.assertEqual(counts["string"], 1)
        self.assertEqual(counts["vector"], 1)
        self.assertEqual(counts["shared_ptr"], 1)
        self.assertEqual(counts["span"], 0)

    def test_counts_the_shield_names(self) -> None:
        text = ("std::thread worker; std::thread::id owner; std::mutex lock; std::stop_token stop;\n"
                "std::filesystem::path where; std::ifstream in; std::condition_variable_any other;\n")
        counts = std_surface_census.count_text(text)
        self.assertEqual(counts["thread"], 2)
        self.assertEqual(counts["mutex"], 1)
        self.assertEqual(counts["stop_token"], 1)
        self.assertEqual(counts["filesystem"], 1)
        self.assertEqual(counts["ifstream"], 1)
        self.assertEqual(counts["fstream"], 0)
        self.assertEqual(counts["condition_variable"], 0)

    def test_census_sums_headers_and_excludes_the_c_abi(self) -> None:
        counts = std_surface_census.census(self.surface)
        self.assertEqual(counts["optional"].occurrences, 3)
        self.assertEqual(counts["optional"].headers, 2)
        self.assertEqual(counts["vector"].headers, 1)

    def test_check_passes_on_equal_and_fails_on_rise_or_fall(self) -> None:
        self.assertEqual(std_surface_census.command_update(self.surface, self.baseline, False), 0)
        self.assertEqual(std_surface_census.command_check(self.surface, self.baseline), 0)
        (self.surface / "core" / "more.hpp").write_text("std::span<int> s;\n", encoding="utf-8")
        self.assertEqual(std_surface_census.command_check(self.surface, self.baseline), 1)
        (self.surface / "core" / "more.hpp").unlink()
        (self.surface / "core" / "other.hpp").write_text("\n", encoding="utf-8")
        self.assertEqual(std_surface_census.command_check(self.surface, self.baseline), 1)

    def test_update_lowers_and_raises_only_when_asked(self) -> None:
        self.assertEqual(std_surface_census.command_update(self.surface, self.baseline, False), 0)
        (self.surface / "core" / "other.hpp").write_text("\n", encoding="utf-8")
        self.assertEqual(std_surface_census.command_update(self.surface, self.baseline, False), 0)
        lowered = json.loads(self.baseline.read_text(encoding="utf-8"))
        self.assertEqual(lowered["names"]["optional"]["occurrences"], 2)
        (self.surface / "core" / "other.hpp").write_text("std::optional<int> a; std::optional<int> b;\n",
                                                         encoding="utf-8")
        self.assertEqual(std_surface_census.command_update(self.surface, self.baseline, False), 1)
        kept = json.loads(self.baseline.read_text(encoding="utf-8"))
        self.assertEqual(kept["names"]["optional"]["occurrences"], 2)
        self.assertEqual(std_surface_census.command_update(self.surface, self.baseline, True), 0)
        raised = json.loads(self.baseline.read_text(encoding="utf-8"))
        self.assertEqual(raised["names"]["optional"]["occurrences"], 4)


if __name__ == "__main__":
    unittest.main()
