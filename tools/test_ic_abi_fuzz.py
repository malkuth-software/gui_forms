"""IC fuzz table tests: refusal-order blocks follow the declarations' refusal_order."""

import re
import unittest

import ic_abi_fuzz


class RefusalOrderBlockTests(unittest.TestCase):
    text: str

    @classmethod
    def setUpClass(cls) -> None:
        cls.text = ic_abi_fuzz.generate()

    def test_a_clause_ordered_first_wins_over_the_state(self) -> None:
        # windows.mdi_child_count: refusal_order = ["mdi_child_count_count", "outside"], so a null count on an
        # ended window reports INVALID_ARGUMENT, and DISPOSED would be the order inverted.
        line: str = next(line for line in self.text.split('\n')
                         if '"gf_abi.windows.mdi_child_count.order.mdi_child_count_count.ended"' in line)
        self.assertIn('Mode::ordered', line)
        self.assertTrue(line.rstrip().endswith('GF_STATUS_DISPOSED},'))
        self.assertIn(', GF_STATUS_INVALID_ARGUMENT, std::span<const Violation>(', line)

    def test_every_ordered_block_names_a_loser_unlike_its_winner(self) -> None:
        ordered: list[str] = [line for line in self.text.split('\n') if 'Mode::ordered' in line]
        self.assertTrue(ordered)
        for line in ordered:
            match: re.Match[str] | None = re.search(
                r', (GF_STATUS_\w+), std::span<const Violation>\([^)]*\), (GF_STATUS_\w+)\},$', line.rstrip())
            self.assertIsNotNone(match, line)
            self.assertNotEqual(match.group(1), match.group(2), line)


if __name__ == '__main__':
    unittest.main()
