"""Control registry tests: real-registry check, stale-render detection, scan rule, validator, README splice."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import control_registry as cr


def _write(root: Path, relative: str, text: str) -> None:
    target: Path = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")


def _record(**overrides: object) -> dict[str, object]:
    base: dict[str, object] = {
        "id": "widget",
        "type": "Widget",
        "header": "gui_forms/controls/widget/widget.hpp",
        "category": "Commands",
        "purpose": "Does one thing.",
        "winforms": "GUI.Forms-only",
        "status": "stable",
        "gallery_page": None,
        "doc": "api/generated/gui_forms.Widget.rst",
    }
    base.update(overrides)
    return base


class RealRegistryTests(unittest.TestCase):
    def test_check_passes_on_committed_registry(self) -> None:
        registry: cr.Registry = cr.load_registry()
        problems: list[str] = cr.validate(registry)
        self.assertEqual(problems, [], "\n".join(problems))

    def test_rendered_files_match_committed_output(self) -> None:
        registry: cr.Registry = cr.load_registry()
        for name, expected in cr.render_all(registry).items():
            committed: str = (cr.GENERATED_DIR / name).read_text(encoding="utf-8")
            self.assertEqual(
                committed,
                expected,
                f"{name} is stale; run `python3 tools/control_registry.py render`",
            )

    def test_gallery_catalog_inc_matches_committed_output(self) -> None:
        registry: cr.Registry = cr.load_registry()
        committed: str = cr.GALLERY_CATALOG_PATH.read_text(encoding="utf-8")
        self.assertEqual(
            committed,
            cr.render_gallery_catalog(registry),
            "demo/gallery_catalog.inc is stale; run `python3 tools/control_registry.py emit-cpp`",
        )

    def test_abi_catalog_matches_committed_output(self) -> None:
        registry: cr.Registry = cr.load_registry()
        committed: str = cr.ABI_CATALOG_PATH.read_text(encoding="utf-8")
        self.assertEqual(
            committed,
            cr.render_abi_catalog(registry),
            "src/abi/stable/schema/abi_schema_catalog.hpp is stale; run `python3 tools/control_registry.py emit-abi`",
        )

    def test_gallery_pages_are_assigned_to_real_records(self) -> None:
        registry: cr.Registry = cr.load_registry()
        assigned: list[cr.ControlRecord] = [record for record in registry.records if record.gallery_page is not None]
        self.assertGreater(len(assigned), 30)
        for record in assigned:
            self.assertRegex(record.gallery_page or "", cr.KEBAB_CASE.pattern, record.id)

    def test_every_fixed_category_is_used(self) -> None:
        registry: cr.Registry = cr.load_registry()
        used: set[str] = {record.category for record in registry.records}
        self.assertEqual(used, set(cr.CATEGORIES))


class ScanRuleTests(unittest.TestCase):
    """The coverage rule: namespace-level, non-template, base chain reaches Control or Component."""

    def test_scan_rule(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            include: Path = Path(temporary) / "include"
            _write(include, "gui_forms/component/component.hpp", "namespace gui_forms {\nclass Component {\n};\n}\n")
            _write(
                include,
                "gui_forms/control/control.hpp",
                "namespace gui_forms {\nclass Control : public Component, public std::enable_shared_from_this<Control> {\n};\n}\n",
            )
            _write(
                include,
                "gui_forms/controls/panel/panel.hpp",
                "namespace gui_forms {\nclass Panel : public Control {\n    struct Nested final : public Control {\n    };\n};\n}\n",
            )
            _write(
                include,
                "gui_forms/controls/panel/card/card.hpp",
                "// class Commented : public Panel {\nnamespace gui_forms {\nstruct CardLayout final {\n};\nclass Card final : public Panel {\n};\n}\n",
            )
            _write(
                include,
                "gui_forms/binding/source.hpp",
                "namespace gui_forms {\nclass Source final : public Component,\n                     public Other {\n};\n}\n",
            )
            _write(
                include,
                "gui_forms/value/value.hpp",
                "namespace gui_forms {\ntemplate <typename T>\nclass Value final : public Component {\n};\n}\n",
            )
            _write(
                include,
                "gui_forms/live_surface/surface/live_surface.hpp",
                "namespace gui_forms {\nclass LiveSurface final : public std::enable_shared_from_this<LiveSurface> {\n};\n}\n",
            )
            _write(include, "gui_forms/plain.hpp", "namespace gui_forms {\nclass Plain {\n};\nstruct Options final {\n};\n}\n")
            found: dict[str, str] = cr.scan_public_control_types(include)
        self.assertEqual(
            found,
            {
                "Panel": "gui_forms/controls/panel/panel.hpp",
                "Card": "gui_forms/controls/panel/card/card.hpp",
                "Source": "gui_forms/binding/source.hpp",
                "LiveSurface": "gui_forms/live_surface/surface/live_surface.hpp",
            },
        )

    def test_admitted_service_types_have_reasons(self) -> None:
        for name, reason in cr.ADMITTED_SERVICE_TYPES.items():
            self.assertTrue(reason.strip(), name)


class ValidatorTests(unittest.TestCase):
    def _registry(self, *records: dict[str, object]) -> cr.Registry:
        return cr.Registry(
            records=tuple(cr.ControlRecord.from_mapping(record, index) for index, record in enumerate(records)),
            notes=(),
        )

    def _include(self, temporary: str) -> Path:
        include: Path = Path(temporary) / "include"
        _write(include, "gui_forms/component/component.hpp", "namespace gui_forms {\nclass Component {\n};\n}\n")
        _write(include, "gui_forms/control/control.hpp", "namespace gui_forms {\nclass Control : public Component {\n};\n}\n")
        _write(include, "gui_forms/controls/widget/widget.hpp", "namespace gui_forms {\nclass Widget final : public Control {\n};\n}\n")
        return include

    def test_valid_record_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(cr.validate(self._registry(_record()), self._include(temporary)), [])

    def test_missing_field_is_rejected_on_load(self) -> None:
        raw: dict[str, object] = _record()
        del raw["purpose"]
        with self.assertRaises(cr.RegistryError):
            cr.ControlRecord.from_mapping(raw, 0)

    def test_duplicates_and_bad_values(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            include: Path = self._include(temporary)
            problems: list[str] = cr.validate(
                self._registry(
                    _record(),
                    _record(id="widget", type="Widget", category="Nope", status="beta", purpose="Two. Sentences.", doc="controls/x.md"),
                ),
                include,
            )
        joined: str = "\n".join(problems)
        self.assertIn("duplicate id 'widget'", joined)
        self.assertIn("duplicate type 'Widget'", joined)
        self.assertIn("category 'Nope'", joined)
        self.assertIn("status 'beta'", joined)
        self.assertIn("single sentence", joined)
        self.assertIn("doc must be 'api/generated/gui_forms.Widget.rst'", joined)

    def test_windows_forms_name_is_loaded_and_checked(self) -> None:
        loaded: cr.ControlRecord = cr.ControlRecord.from_mapping(
            _record(winforms="Widgetry", windows_forms_name="Widgetry"), 0)
        self.assertEqual(loaded.windows_forms_name, "Widgetry")
        with self.assertRaises(cr.RegistryError):
            cr.ControlRecord.from_mapping(_record(windows_forms_name=3), 0)
        with tempfile.TemporaryDirectory() as temporary:
            include: Path = self._include(temporary)
            self.assertEqual(cr.validate(self._registry(_record(winforms="Widgetry", windows_forms_name="Widgetry")),
                                         include), [])
            _write(include, "gui_forms/controls/other/other.hpp", "namespace gui_forms {\nclass Other final : public Control {\n};\n}\n")
            problems: list[str] = cr.validate(
                self._registry(
                    _record(winforms="Other", windows_forms_name="Other"),
                    _record(id="other", type="Other", header="gui_forms/controls/other/other.hpp",
                            doc="api/generated/gui_forms.Other.rst", winforms="Box", windows_forms_name="Widget-1"),
                ),
                include,
            )
        joined: str = "\n".join(problems)
        self.assertIn("is also a registry type", joined)
        self.assertIn("not a C++ identifier", joined)
        self.assertIn("must be the record's winforms counterpart ('Box')", joined)

    def test_header_must_exist_and_declare_type(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            include: Path = self._include(temporary)
            _write(include, "gui_forms/controls/other/other.hpp", "namespace gui_forms {\nclass Other final : public Control {\n};\n}\n")
            problems: list[str] = cr.validate(
                self._registry(
                    _record(id="widget", type="Widget", header="gui_forms/controls/missing.hpp"),
                    _record(id="other", type="Other", header="gui_forms/controls/widget/widget.hpp", doc="controls/other.md"),
                ),
                include,
            )
        joined: str = "\n".join(problems)
        self.assertIn("header does not exist", joined)
        self.assertIn("'class Other' or 'struct Other' not declared", joined)

    def test_coverage_both_directions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            include: Path = self._include(temporary)
            _write(include, "gui_forms/controls/other/other.hpp", "namespace gui_forms {\nclass Other final : public Control {\n};\n}\n")
            _write(include, "gui_forms/helper.hpp", "namespace gui_forms {\nstruct Helper final {\n};\n}\n")
            problems: list[str] = cr.validate(
                self._registry(
                    _record(),
                    _record(id="helper", type="Helper", header="gui_forms/helper.hpp", doc="controls/helper.md"),
                ),
                include,
            )
        joined: str = "\n".join(problems)
        self.assertIn("public type Other (gui_forms/controls/other/other.hpp) is missing", joined)
        self.assertIn("registry type Helper is not a public control/component type", joined)


class RenderTests(unittest.TestCase):
    def _registry(self) -> cr.Registry:
        return cr.Registry(
            records=(
                cr.ControlRecord.from_mapping(_record(id="zed", type="Zed", doc="controls/zed.md", purpose="Z|ed."), 0),
                cr.ControlRecord.from_mapping(_record(id="alpha", type="Alpha", doc="controls/alpha.md"), 1),
                cr.ControlRecord.from_mapping(
                    _record(id="box", type="Box", category="Layout", doc="controls/box.md", winforms="Panel"), 2
                ),
            ),
            notes=(),
        )

    def test_by_function_groups_and_fills_first_row_only(self) -> None:
        lines: list[str] = cr.render_by_function(self._registry()).splitlines()
        self.assertEqual(lines[0], "# Controls by function")
        rows: list[str] = [line for line in lines if line.startswith("| ") and "`" in line]
        self.assertEqual(rows[0].split("|")[1].strip(), "Commands")
        self.assertIn("`Alpha`", rows[0])
        self.assertEqual(rows[1].split("|")[1].strip(), "")
        self.assertIn("Z\\|ed.", rows[1])
        self.assertEqual(rows[2].split("|")[1].strip(), "Layout")
        self.assertEqual(sum(1 for line in lines if line.startswith("# ")), 1)

    def test_alphabetical_order_and_columns(self) -> None:
        text: str = cr.render_alphabetical(self._registry())
        self.assertIn("| Control | Function | Header | Status | Description |", text)
        self.assertLess(text.index("`Alpha`"), text.index("`Box`"))
        self.assertLess(text.index("`Box`"), text.index("`Zed`"))
        self.assertEqual(sum(1 for line in text.splitlines() if line.startswith("# ")), 1)

    def test_index_names_link_to_their_reference_pages(self) -> None:
        text: str = cr.render_alphabetical(self._registry())
        self.assertIn("| [`Box`](../../controls/box.md) | Layout |", text)
        self.assertIn("[`Alpha`](../../controls/alpha.md)", cr.render_by_function(self._registry()))

    def test_readme_table_shape(self) -> None:
        table: str = cr.render_readme_table(self._registry())
        self.assertTrue(table.startswith("| Control | Function | Description |\n|---|---|---|\n"))
        self.assertTrue(table.endswith("\n"))
        self.assertIn(f"[`Box`]({cr.SITE_URL}controls/box.html)", table)

    def test_gallery_catalog_shape_and_escaping(self) -> None:
        registry: cr.Registry = cr.Registry(
            records=(
                cr.ControlRecord.from_mapping(
                    _record(id="quote", type="Quote", doc="controls/quote.md", purpose='Says "hi".', gallery_page="basics"), 0
                ),
                cr.ControlRecord.from_mapping(_record(id="alpha", type="Alpha", doc="controls/alpha.md"), 1),
            ),
            notes=(),
        )
        text: str = cr.render_gallery_catalog(registry)
        self.assertIn(f"std::array<std::string_view, {len(cr.CATEGORIES)}> catalog_categories", text)
        self.assertIn("std::array<CatalogEntry, 2> control_catalog", text)
        self.assertIn('"Says \\"hi\\"."', text)
        self.assertIn('"stable", "basics"},', text)
        self.assertIn('"stable", ""},', text)
        self.assertLess(text.index('"quote"'), text.index('"alpha"'), "registry order is preserved")
        self.assertTrue(text.endswith("}};\n"))

    def test_emit_cpp_check_detects_stale_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target: Path = Path(temporary) / "gallery_catalog.inc"
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cr.main(["emit-cpp", "--out", str(target)]), 0)
                self.assertEqual(cr.main(["emit-cpp", "--out", str(target), "--check"]), 0)
            target.write_text("stale\n", encoding="utf-8")
            captured: io.StringIO = io.StringIO()
            with contextlib.redirect_stderr(captured):
                self.assertEqual(cr.main(["emit-cpp", "--out", str(target), "--check"]), 1)
            self.assertIn("is stale", captured.getvalue())

    def test_splice_requires_both_markers(self) -> None:
        with self.assertRaises(cr.RegistryError):
            cr.splice_readme("# README\n\n<!-- controls:start -->\n", "| t |\n")
        with self.assertRaises(cr.RegistryError):
            cr.splice_readme("# README\n", "| t |\n")

    def test_splice_replaces_block(self) -> None:
        readme: str = "# README\n\n<!-- controls:start -->\nold\n<!-- controls:end -->\n\ntail\n"
        updated: str = cr.splice_readme(readme, "| t |\n")
        self.assertEqual(updated, "# README\n\n<!-- controls:start -->\n| t |\n<!-- controls:end -->\n\ntail\n")
        self.assertEqual(cr.splice_readme(updated, "| t |\n"), updated)


class CliTests(unittest.TestCase):
    def test_readme_write_without_markers_exits_nonzero(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            readme: Path = Path(temporary) / "README.md"
            readme.write_text("# No markers\n", encoding="utf-8")
            captured: io.StringIO = io.StringIO()
            with contextlib.redirect_stderr(captured):
                self.assertEqual(cr.main(["readme-table", "--write", str(readme)]), 1)
            self.assertIn("must both be present", captured.getvalue())
            self.assertEqual(readme.read_text(encoding="utf-8"), "# No markers\n")

    def test_check_command_passes(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(cr.main(["check"]), 0)


if __name__ == "__main__":
    unittest.main()
