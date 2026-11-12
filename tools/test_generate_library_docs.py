#!/usr/bin/env python3
"""Tests for the API reference generator (tools/generate_library_docs.py)."""
from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import api_docstrings  # noqa: E402
import generate_library_docs as generator  # noqa: E402

ROOT: Path = Path(__file__).resolve().parents[1]


class ReferenceTests(unittest.TestCase):
    pages: dict[str, str] = {}
    examples: str = ""
    scratch: tempfile.TemporaryDirectory

    @classmethod
    def setUpClass(cls) -> None:
        cls.scratch = tempfile.TemporaryDirectory(prefix="gui-forms-api-test-")
        output = Path(cls.scratch.name) / "api"
        examples = Path(cls.scratch.name) / "examples.cpp"
        generator.render(output, examples)
        cls.pages = {path.relative_to(output).as_posix(): path.read_text(encoding="utf-8")
                     for path in sorted(output.rglob("*.rst"))}
        cls.examples = examples.read_text(encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.scratch.cleanup()

    def test_every_registry_control_has_a_page(self) -> None:
        import json
        registry = json.loads((ROOT / "manifests/controls.json").read_text(encoding="utf-8"))
        for record in registry["controls"]:
            page = record["doc"][len("api/"):]
            self.assertIn(page, self.pages, record["type"])

    def test_page_count_stays_bounded(self) -> None:
        # One page per documented type and per category; never one per member.
        self.assertLess(len(self.pages), 250)

    def test_every_include_is_a_public_header(self) -> None:
        for name, text in self.pages.items():
            for header in re.findall(r"#include <([^>]+)>", text):
                self.assertTrue((ROOT / "include" / header).is_file(), f"{name}: {header}")
                self.assertNotIn("/detail/", header, name)

    def test_no_description_is_inferred_and_no_internal_labels_leak(self) -> None:
        for name, text in self.pages.items():
            self.assertNotIn("against retained state", text, name)
            self.assertNotIn("declaration-derived", text, name)
            self.assertNotIn("OBSERVED", text, name)

    def test_checkbox_page_has_the_numpydoc_sections(self) -> None:
        page = self.pages["generated/gui_forms.CheckBox.rst"]
        for section in (":Parameters:", ":Properties:", ":Events:", ":Inherited:",
                        ".. rubric:: Methods", ".. seealso::", ".. rubric:: Notes",
                        ".. rubric:: Examples"):
            self.assertIn(section, page)
        self.assertIn(".. cpp:class:: CheckBox : public ButtonBase", page)
        self.assertIn("**checked** : ``bool``, default ``false``", page)
        self.assertIn("**checked_changed** : ``Event<bool>``", page)

    def test_owned_handlers_are_documented_and_subscribe_is_deprecated(self) -> None:
        events = self.pages["library.events-values-and-commands.rst"]
        self.assertIn("void on(Event<Arguments...>& event, Owner& owner", events)

    def test_signatures_keep_default_arguments_and_drop_initializer_lists(self) -> None:
        page = self.pages["generated/gui_forms.CheckBox.rst"]
        self.assertIn(".. cpp:function:: explicit CheckBox(StableId stable_id, std::string text = {})", page)
        for text in self.pages.values():
            self.assertNotIn("state_(std::make_shared", text)

    def test_examples_are_compiled_from_the_docstrings(self) -> None:
        self.assertIn("namespace doc_example_CheckBox_1 {", self.examples)
        self.assertIn('failures += run("CheckBox example 1", &doc_example_CheckBox_1::example);',
                      self.examples)

    def test_committed_reference_is_current(self) -> None:
        self.assertEqual(generator.check(), 0)


class BuildModeBranchTests(unittest.TestCase):
    """Both branches of a build-mode #if are parsed; each name is declared once."""

    def test_a_function_differing_only_in_specifiers_is_declared_once(self) -> None:
        checked = generator.rst.Method("lend", "BorrowedText lend(std::string_view text);")
        ordinary = generator.rst.Method("lend", "inline BorrowedText lend(std::string_view text) noexcept")
        other = generator.rst.Method("lend", "inline BorrowedText lend(const char* text) noexcept")
        self.assertEqual(generator.rst.function_identity("gui_forms", checked),
                         generator.rst.function_identity("gui_forms", ordinary))
        self.assertNotEqual(generator.rst.function_identity("gui_forms", checked),
                            generator.rst.function_identity("gui_forms", other))

    def test_an_alias_of_a_documented_class_is_not_declared_again(self) -> None:
        linker = generator.rst.Linker({"BorrowedText": "gui_forms::BorrowedText"}, {})
        alias = generator.rst.Declaration("using BorrowedText = std::string_view;")
        unrelated = generator.rst.Declaration("using PlainText = std::string_view;")
        self.assertTrue(generator.rst.documented_as_type("gui_forms", alias, linker))
        self.assertFalse(generator.rst.documented_as_type("gui_forms", unrelated, linker))
        self.assertFalse(generator.rst.documented_as_type("gui_forms::other", alias, linker))

    def test_the_committed_reference_declares_each_build_mode_name_once(self) -> None:
        page = (generator.API / "library.control-and-component.rst").read_text(encoding="utf-8")
        self.assertNotIn(".. cpp:type:: BorrowedText =", page)
        appearance = (generator.API / "library.geometry-and-appearance.rst").read_text(encoding="utf-8")
        self.assertEqual(appearance.count("lend_paint_text(std::string_view text)"), 1)


class DocstringTests(unittest.TestCase):
    def write(self, text: str) -> Path:
        directory = Path(tempfile.mkdtemp(prefix="gui-forms-docstring-"))
        path = directory / "Widget.rst"
        path.write_text(text, encoding="utf-8")
        return path

    def test_sections_and_entries_parse(self) -> None:
        doc = api_docstrings.parse(self.write(
            "A widget.\n\nMore text.\n\nProperties\n----------\nvalue : default ``0``\n"
            "    The value.\n\nSee Also\n--------\nButton : A button.\n"))
        self.assertEqual(doc.summary, "A widget.")
        self.assertEqual(doc.extended, "More text.")
        entry = doc.entry("Properties", "value")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.classifier, "default ``0``")
        self.assertEqual(doc.see_also[0].name, "Button")

    def test_unknown_or_misordered_sections_fail(self) -> None:
        with self.assertRaises(api_docstrings.DocstringError):
            api_docstrings.parse(self.write("A widget.\n\nAttributes\n----------\nx\n    y\n"))
        with self.assertRaises(api_docstrings.DocstringError):
            api_docstrings.parse(self.write(
                "A widget.\n\nEvents\n------\nx\n    y\n\nProperties\n----------\nz\n    w\n"))


if __name__ == "__main__":
    unittest.main()
