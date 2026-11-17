"""Unit tests for check_stable_id_literals.py (a stable ID written in a test uses no '_')."""
from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_stable_id_literals  # noqa: E402

TEST: Path = Path('tests/abi/example_tests.cpp')


def lines(text: str, path: Path = TEST) -> list[int]:
    found: list[check_stable_id_literals.Finding] = check_stable_id_literals.check_stable_id_literals(path, text)
    numbers: list[int] = [finding.line for finding in found]
    return numbers


class StableIdLiteralTests(unittest.TestCase):
    def test_the_audit_round_slips_are_findings(self) -> None:
        # The three run-time IDs the ABI refused in the 2026-11-15 audit round.
        text: str = '\n'.join([
            '    require(objects.create(text_view("check_box"), text_view("cpp.self_owned"), &created) == GF_STATUS_OK,',
            '    const gf_object made = create(tables, "button", "fields.element_button");',
            '        change.row_id = "inspection.rebuild.grid.property.tab_index";',
        ])
        self.assertEqual(lines(text), [1, 2, 3])

    def test_kebab_case_ids_and_type_names_pass(self) -> None:
        text: str = '\n'.join([
            '    require(objects.create(text_view("check_box"), text_view("cpp.self-owned"), &created) == GF_STATUS_OK,',
            '    const gf_object label = gf_kit_create(kit, "label", "kit.proxies.app-font");',
            '    gf_kit_text("kit.composite.card");',
            '        change.row_id = "inspection.rebuild.grid.property.tab-index";',
        ])
        self.assertEqual(lines(text), [])

    def test_strings_nested_in_other_calls_belong_to_those_calls(self) -> None:
        text: str = '    window.set_theme(Theme::create(*effect_theme("progress_woven_stripes", "progress_pulse")));'
        self.assertEqual(lines(text), [])

    def test_stable_id_literals_and_kit_text(self) -> None:
        text: str = '\n'.join([
            '    make_control<Panel>(StableId("upstream_consumer"));',
            '    gf_kit_text("kit.composite_card");',
            '    gf_kit_text("check_box");',
        ])
        self.assertEqual(lines(text), [1, 2])

    def test_window_paths_comments_and_allowances(self) -> None:
        text: str = '\n'.join([
            '    entry.row_id = "main/splash.tool_tip";',
            '    // create(tables, "button", "in.a_comment");',
            '    // stable-id: allow a refusal test',
            '    const gf_object refused = create(tables, "button", "bad.under_score");',
        ])
        self.assertEqual(lines(text), [])

    def test_only_tests_are_read(self) -> None:
        text: str = '    const gf_object made = create(tables, "button", "fields.element_button");'
        self.assertEqual(lines(text, Path('src/abi/example.cpp')), [])


if __name__ == '__main__':
    unittest.main()
