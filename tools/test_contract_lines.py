"""Contract line tests: the `// ic:` contract lines tools/ic_declarations.py writes survive a regeneration."""

import unittest

import contract_lines as act


class KeepContractLinesTests(unittest.TestCase):
    def test_contract_lines_stay_above_their_line(self) -> None:
        current: str = "a\n// ic: first\n// ic: second\nb\nc\n"
        generated: str = "a\nb\nc\n"
        self.assertEqual(act.keep_contract_lines(current, generated), current)

    def test_no_current_file_keeps_the_generated_text(self) -> None:
        self.assertEqual(act.keep_contract_lines(None, "a\n"), "a\n")

    def test_a_group_whose_line_is_gone_is_dropped(self) -> None:
        current: str = "a\n// ic: gone\nremoved\n"
        self.assertEqual(act.keep_contract_lines(current, "a\nnew\n"), "a\nnew\n")


if __name__ == "__main__":
    unittest.main()
