"""Tests for tools/flatten_control_headers.py (audit F2)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flatten_control_headers as flatten  # noqa: E402


class PlanTests(unittest.TestCase):
    def test_nested_headers_move_to_the_controls_directory(self) -> None:
        moves = flatten.plan_moves([
            "include/gui_forms/controls/panel/card/card.hpp",
            "include/gui_forms/controls/panel/panel.hpp",
            "include/gui_forms/control.hpp",
            "src/controls/panel/panel.cpp",
        ])
        self.assertEqual(
            [(str(move.source), str(move.destination)) for move in moves],
            [("include/gui_forms/controls/panel/card/card.hpp", "include/gui_forms/controls/card.hpp"),
             ("include/gui_forms/controls/panel/panel.hpp", "include/gui_forms/controls/panel.hpp")])

    def test_a_flat_header_stays(self) -> None:
        self.assertEqual(flatten.plan_moves(["include/gui_forms/controls/flat.hpp"]), [])

    def test_a_shared_file_name_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            flatten.plan_moves(["include/gui_forms/controls/a/item.hpp", "include/gui_forms/controls/b/item.hpp"])

    def test_an_existing_destination_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            flatten.plan_moves(["include/gui_forms/controls/item.hpp", "include/gui_forms/controls/a/item.hpp"])


class RewriteTests(unittest.TestCase):
    def setUp(self) -> None:
        moves = [flatten.Move(PurePosixPath("include/gui_forms/controls/panel/card/card.hpp"),
                              PurePosixPath("include/gui_forms/controls/card.hpp"))]
        self.spellings = flatten.spelling_map(moves)

    def test_include_lines_paths_and_unknown_spellings(self) -> None:
        counts = flatten.RewriteCounts()
        unmatched: set[str] = set()
        text = ('#include "gui_forms/controls/panel/card/card.hpp"\n'
                '    include/gui_forms/controls/panel/card/card.hpp\n'
                '#include "../../include/gui_forms/controls/panel/card/card.hpp"\n'
                '#include "gui_forms/controls/other/other.hpp"\n')
        rewritten = flatten.rewrite_text(text, self.spellings, counts, unmatched)
        self.assertEqual(rewritten,
                         '#include "gui_forms/controls/card.hpp"\n'
                         '    include/gui_forms/controls/card.hpp\n'
                         '#include "../../include/gui_forms/controls/card.hpp"\n'
                         '#include "gui_forms/controls/other/other.hpp"\n')
        self.assertEqual(counts.include_lines, 2)
        self.assertEqual(counts.other_references, 1)
        self.assertEqual(unmatched, {"gui_forms/controls/other/other.hpp"})

    def test_a_rewritten_spelling_is_not_reported(self) -> None:
        counts = flatten.RewriteCounts()
        unmatched: set[str] = set()
        flatten.rewrite_text('#include "gui_forms/controls/card.hpp"\n', self.spellings, counts, unmatched)
        self.assertEqual(unmatched, set())
        self.assertEqual(counts.include_lines, 0)


if __name__ == "__main__":
    unittest.main()
