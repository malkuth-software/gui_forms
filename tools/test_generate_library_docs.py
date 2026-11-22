#!/usr/bin/env python3
"""Tests for the API reference generator (tools/generate_library_docs.py)."""
from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import api_docs_parse  # noqa: E402
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


class InitializerTests(unittest.TestCase):
    """A brace initializer continued on later lines stays one declaration."""

    def test_a_multi_line_initializer_is_read_whole(self) -> None:
        body = (
            "    std::string name{\"und\"};\n"
            "    std::array<std::string, 3> names{\"One\", \"Two\",\n"
            "                                     \"Three\"};\n"
            "    std::array<Pair, 2> pairs{{\n"
            "        {1, 2},\n"
            "        {3, 4},\n"
            "    }};\n"
            "    int after{7};\n")
        _, declarations = api_docs_parse.declared_members(body, "struct")
        texts = [re.sub(r"\s+", " ", item.text) for item in declarations]
        self.assertEqual(len(texts), 4, texts)
        self.assertTrue(texts[1].endswith('{"One", "Two", "Three"}'), texts[1])
        self.assertTrue(texts[2].startswith("std::array<Pair, 2> pairs{{"), texts[2])
        self.assertTrue(texts[3].startswith("int after"), texts[3])
        directive, default = generator.rst.member_directive(declarations[1])
        self.assertEqual(directive, ".. cpp:member:: std::array<std::string, 3> names")
        self.assertEqual(default, '{"One", "Two", "Three"}')

    def test_the_committed_reference_has_no_unterminated_initializer(self) -> None:
        unterminated = re.compile(r"^\s*\.\. cpp:(?:member|var):: .*[{,]$")
        for page in (ROOT / "docs" / "api").glob("*.rst"):
            for number, line in enumerate(page.read_text(encoding="utf-8").splitlines(), 1):
                self.assertIsNone(unterminated.match(line), f"{page.name}:{number}: {line}")


class SourceLinkTests(unittest.TestCase):
    """D3: declarations carry their header line for the site's [source] links
    (docs/conf.py linkcode_resolve), and pages link the header itself."""

    def test_members_record_the_line_their_declaration_starts_on(self) -> None:
        body = (
            " // the body starts on the class's line\n"
            "public:\n"
            "    // Runs it.\n"
            "    void run(int count,\n"
            "             bool twice);\n"
            "    std::array<int, 2> pairs{\n"
            "        1, 2};\n"
            "    int after{7};\n")
        methods, declarations = api_docs_parse.declared_members(body, "class", 40)
        self.assertEqual([(method.name, method.line) for method in methods], [("run", 43)])
        self.assertEqual([item.line for item in declarations], [45, 47])

    def test_source_locations_name_types_members_enums_and_functions(self) -> None:
        record = api_docs_parse.TypeRecord(
            "Thing", "class", "include/gui_forms/thing.hpp", 10, namespace="gui_forms",
            methods=[api_docs_parse.Method("run", "void run()", "public", "", 14),
                     api_docs_parse.Method("hidden", "void hidden()", "private", "", 20)],
            fields=[api_docs_parse.Declaration("int count{3}", "", 16),
                    api_docs_parse.Declaration("using Size = int", "", 17)])
        hidden = api_docs_parse.TypeRecord("Inner", "class", "include/gui_forms/detail/inner.hpp", 3,
                                           namespace="gui_forms")
        enum = api_docs_parse.EnumRecord("Mode", "include/gui_forms/thing.hpp", 30, namespace="gui_forms")
        scope = api_docs_parse.HeaderScope(
            "include/gui_forms/thing.hpp", "gui_forms",
            [api_docs_parse.Method("make_thing", "Thing make_thing()", "public", "", 40)],
            [api_docs_parse.Declaration("inline constexpr int limit = 4", "", 42)])
        found = api_docs_parse.source_locations([record, hidden], [enum], [scope])
        header = "include/gui_forms/thing.hpp"
        self.assertEqual(found, {
            "gui_forms::Thing": (header, 10), "gui_forms::Thing::run": (header, 14),
            "gui_forms::Thing::count": (header, 16), "gui_forms::Thing::Size": (header, 17),
            "gui_forms::Mode": (header, 30), "gui_forms::make_thing": (header, 40),
            "gui_forms::limit": (header, 42)})

    def test_the_real_headers_give_lines_that_hold_the_declaration(self) -> None:
        found = api_docs_parse.source_locations(*api_docs_parse.declarations())
        for name in ("gui_forms::Button", "gui_forms::OffscreenAttachment::capture_control",
                     "gui_forms::encode_window_capture_png"):
            header, line = found[name]
            text = (ROOT / header).read_text(encoding="utf-8").splitlines()[line - 1]
            self.assertIn(name.rsplit("::", 1)[1], text, name)

    def test_type_pages_link_their_header_and_name_it_as_their_source(self) -> None:
        line = generator.rst.include_line("include/gui_forms/controls/button.hpp")
        self.assertEqual(line, "``#include <gui_forms/controls/button.hpp>`` "
                               "(:src:`source <include/gui_forms/controls/button.hpp>`)")
        page = (ROOT / "docs/api/generated/gui_forms.Button.rst").read_text(encoding="utf-8")
        self.assertIn(":gf_source: include/gui_forms/controls/button.hpp", page.splitlines()[2])


class PreviewTests(unittest.TestCase):
    """D2: every registry control's page names its preview, with alt text."""

    def test_a_registry_control_names_its_specimen_preview(self) -> None:
        lines = generator.rst.preview_lines({"id": "check_box", "type": "CheckBox"})
        self.assertEqual(lines[0], ".. gf-preview:: controls/check_box")
        self.assertTrue(lines[1].startswith("   :alt: A CheckBox "), lines[1])
        self.assertEqual(generator.rst.preview_lines(None), [])

    def test_every_registry_control_page_carries_its_preview(self) -> None:
        import json
        registry = json.loads((ROOT / "manifests/controls.json").read_text(encoding="utf-8"))
        pages = "\n".join(path.read_text(encoding="utf-8")
                          for path in (ROOT / "docs/api").rglob("*.rst"))
        for entry in registry["controls"]:
            self.assertIn(f".. gf-preview:: controls/{entry['id']}\n", pages, entry["id"])


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
