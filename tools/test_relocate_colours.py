"""Tests for tools/relocate_colours.py (stage T4, theme variants stage B)."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import relocate_colours  # noqa: E402

FORM = '''/* A page; a comment with { braces } and fore-color: #000000; inside. */
scaled-panel page.document {
  label page.heading {
    text: "HEADING { not a block }; fore-color: #111111;";
    font: content 22 700;
    fore-color: #182D42;
    bounds: 24 18 500 34;
  }
  label page.lead { text: "Lead"; back-color: #fafafa; fore-color: #5b6b79; }
  label page.role {
    fore-color: role(ink-on-paper);
  }
}
'''


def fake_dark(requests):
    """A dark colour per request, recognisable by its class."""
    darks = []
    for kind, colour in requests:
        darks.append('#000000' if kind == 'ink' else '#ffffff')
    return darks


class RelocateTests(unittest.TestCase):
    def relocate(self, sheet='', dark=fake_dark):
        new_sheet, new_forms, report = relocate_colours.relocate([('page.form', FORM)], sheet, dark)
        return new_sheet, new_forms[0][1], report

    def test_moves_literal_colours_into_rules(self):
        sheet, form, report = self.relocate()
        self.assertIn('#page\\.heading { color: #182d42 }', sheet)
        self.assertIn('#page\\.lead { background-color: #fafafa }', sheet)
        self.assertIn('#page\\.lead { color: #5b6b79 }', sheet)
        self.assertIn('  #page\\.heading { color: #000000 }', sheet)
        self.assertIn('  #page\\.lead { background-color: #ffffff }', sheet)
        self.assertTrue(sheet.startswith(':root { color-scheme: light dark }\n'))
        self.assertIn('3 colour(s) moved', report)

    def test_removes_the_statements_and_keeps_the_rest(self):
        _, form, _ = self.relocate()
        self.assertNotIn('#182D42', form)
        self.assertNotIn('#fafafa', form)
        self.assertIn('label page.lead { text: "Lead"; }', form)
        # Strings and comments are untouched, though they look like colours.
        self.assertIn('fore-color: #111111;";', form)
        self.assertIn('fore-color: #000000; inside.', form)
        self.assertIn('    font: content 22 700;\n    bounds: 24 18 500 34;\n', form)

    def test_leaves_non_literal_colours(self):
        _, form, report = self.relocate()
        self.assertIn('fore-color: role(ink-on-paper);', form)
        self.assertTrue(any('not a literal colour' in line for line in report))

    def test_keeps_the_sheets_scheme_and_rules(self):
        sheet_text = ':root { color-scheme: light }\n#page\\.lead { color: red }\n'
        sheet, form, report = self.relocate(sheet_text)
        self.assertEqual(sheet.count(':root { color-scheme'), 1)
        self.assertIn('#page\\.heading { color: #182d42 }', sheet)
        self.assertNotIn('#page\\.lead { background-color', sheet)
        self.assertIn('back-color: #fafafa', form)
        self.assertTrue(any('already selects' in line for line in report))

    def test_no_dark_rules(self):
        sheet, _, _ = self.relocate(dark=relocate_colours.no_dark_colours)
        self.assertNotIn('prefers-color-scheme', sheet)

    def test_refuses_an_unbalanced_document(self):
        with self.assertRaises(relocate_colours.Refusal):
            relocate_colours.relocate([('bad.form', 'label a.b { fore-color: #000000;\n')], '', fake_dark)

    def test_escapes_ids(self):
        self.assertEqual(relocate_colours.escaped_id('gallery.ranges.lead-2'), '#gallery\\.ranges\\.lead-2')


if __name__ == '__main__':
    unittest.main()
