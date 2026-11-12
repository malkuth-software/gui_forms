"""Member manifest tests: the committed manifest agrees, the validator catches disagreement, catalogue rows parse."""

import copy
import tempfile
import unittest
from pathlib import Path

import control_registry as cr
import member_manifest as mm


class CommittedManifestTests(unittest.TestCase):
    model: mm.Model
    manifest: dict

    @classmethod
    def setUpClass(cls) -> None:
        cls.model = mm.build_model()
        cls.manifest = mm.load_manifest()

    def test_committed_manifest_agrees_with_registry(self) -> None:
        problems: list[str] = mm.validate(self.manifest, self.model)
        self.assertEqual(problems, [], "\n".join(problems[:40]))

    def test_generate_without_catalogue_reproduces_committed_manifest(self) -> None:
        regenerated: dict = mm.build_manifest(self.model, mm.previous_winforms(self.manifest), self.manifest,
                                              self.manifest["sources"])
        self.assertEqual(mm.dump_manifest(regenerated), mm.MANIFEST_PATH.read_text(encoding="utf-8"),
                         "manifests/members.json is stale; run `python3 tools/control_registry.py members`")

    def test_every_registry_type_has_an_entry(self) -> None:
        registry: cr.Registry = cr.load_registry()
        for record in registry.records:
            self.assertIsNotNone(mm.entry_for(self.manifest, record.type), record.type)

    def test_deferred_and_excluded_rows_carry_reasons(self) -> None:
        for entry in self.manifest["controls"]:
            for row in entry["members"]:
                if row["status"] in ("deferred", "excluded"):
                    self.assertTrue(row.get("reason"), row)

    def test_w_brief_rows_are_seeded(self) -> None:
        tab_control: dict | None = mm.entry_for(self.manifest, "TabControl")
        self.assertIsNotNone(tab_control)
        multiline: list[dict] = [row for row in tab_control["members"] if row.get("winforms") == "Multiline"]
        self.assertEqual([row["status"] for row in multiline], ["deferred"])
        self.assertEqual(multiline[0]["basis"], "w-brief")


class ValidatorTests(unittest.TestCase):
    model: mm.Model
    manifest: dict

    @classmethod
    def setUpClass(cls) -> None:
        cls.model = mm.build_model()
        cls.manifest = mm.load_manifest()

    def _button(self, manifest: dict) -> dict:
        entry: dict | None = mm.entry_for(manifest, "Button")
        self.assertIsNotNone(entry)
        return entry

    def test_a_dropped_member_is_reported(self) -> None:
        manifest: dict = copy.deepcopy(self.manifest)
        entry: dict = self._button(manifest)
        dropped: dict = next(row for row in entry["members"] if row.get("gui_forms_only"))
        entry["members"].remove(dropped)
        problems: list[str] = mm.validate(manifest, self.model)
        self.assertTrue(any(dropped["gui_forms"] in problem and "not listed" in problem for problem in problems),
                        problems)

    def test_a_member_that_no_longer_exists_is_reported(self) -> None:
        manifest: dict = copy.deepcopy(self.manifest)
        self._button(manifest)["members"].append(
            {"kind": "method", "winforms": None, "gui_forms": "no_such_member", "gui_forms_only": True,
             "status": "unclassified", "basis": "none", "question": "untested: test"})
        problems: list[str] = mm.validate(manifest, self.model)
        self.assertTrue(any("no member no_such_member" in problem for problem in problems), problems)

    def test_a_missing_type_is_reported(self) -> None:
        manifest: dict = copy.deepcopy(self.manifest)
        manifest["controls"] = [entry for entry in manifest["controls"] if entry.get("type") != "Button"]
        problems: list[str] = mm.validate(manifest, self.model)
        self.assertIn("Button: in the registry or schema but not in the manifest", problems)

    def test_an_excluded_row_without_reason_is_reported(self) -> None:
        manifest: dict = copy.deepcopy(self.manifest)
        row: dict = self._button(manifest)["members"][0]
        row.update(status="excluded", basis="owner", evidence="test")
        row.pop("reason", None)
        problems: list[str] = mm.validate(manifest, self.model)
        self.assertTrue(any("needs a reason" in problem for problem in problems), problems)

    def test_an_unknown_status_is_reported(self) -> None:
        manifest: dict = copy.deepcopy(self.manifest)
        self._button(manifest)["members"][0]["status"] = "partial"
        problems: list[str] = mm.validate(manifest, self.model)
        self.assertTrue(any("unknown status 'partial'" in problem for problem in problems), problems)


class CatalogueTests(unittest.TestCase):
    def test_names_spell_as_gui_forms_names(self) -> None:
        self.assertEqual(mm.snake_name("SelectedIndexChanged"), "selected_index_changed")
        self.assertEqual(mm.snake_name("BackColor"), "back_color")
        self.assertEqual(mm.snake_name("HScroll"), "h_scroll")

    def test_member_kinds(self) -> None:
        self.assertEqual(mm.classify_catalogue_member("Button", "Text.get", "x -> string!"), ("property", "Text"))
        self.assertEqual(mm.classify_catalogue_member("Button", "Button()", "x -> void"), ("constructor", "Button"))
        self.assertEqual(mm.classify_catalogue_member("Button", "PerformClick()", "x -> void"),
                         ("method", "PerformClick"))
        self.assertEqual(mm.classify_catalogue_member("Control", "Click", "x -> System.EventHandler?"),
                         ("event", "Click"))
        self.assertEqual(mm.classify_catalogue_member("DataGridView", "this[int c, int r].get", "x"),
                         ("property", "this[]"))

    def test_catalogue_rows_merge_and_skip_overrides(self) -> None:
        header: str = "\t".join(("surface", "source_line", "kind", "owner", "member", "api", "family", "disposition",
                                 "status", "source_marker", "reason", "versions", "obsolete"))

        def line(member: str, api: str, disposition: str = "admitted", marker: str = "", reason: str = "",
                 status: str = "missing") -> str:
            return "\t".join(("System.Windows.Forms", "8.0:x:1", "member", "System.Windows.Forms.Button", member,
                              api, "f", disposition, status, marker, reason, "8.0", ""))

        rows: list[str] = [header,
                           line("Text.get", "a -> string"), line("Text.set", "a -> void"),
                           line("OnClick(System.EventArgs! e)", "a -> void", marker="override;"),
                           line("Old.get", "a -> int", disposition="excluded", reason="printing"),
                           line("Handle.get", "a -> nint", status="platform_extension_review")]
        with tempfile.TemporaryDirectory() as scratch:
            path: Path = Path(scratch) / "catalogue.tsv"
            path.write_text("\n".join(rows) + "\n", encoding="utf-8")
            members: list[mm.WinFormsMember] = mm.read_catalogue(path, ["Button"])["Button"]
        by_name: dict[str, mm.WinFormsMember] = {member.name: member for member in members}
        self.assertEqual(sorted(by_name), ["Handle", "Old", "Text"])
        self.assertEqual(by_name["Text"].scope, "admitted")
        self.assertEqual((by_name["Old"].scope, by_name["Old"].reason), ("excluded", "printing"))
        self.assertEqual(by_name["Handle"].scope, "review")


class RuntimeReferenceTests(unittest.TestCase):
    SOURCE: str = """namespace System.ComponentModel
{
    public partial class Component : System.MarshalByRefObject, System.ComponentModel.IComponent
    {
        public Component() { }
        protected virtual bool CanRaiseEvents { get { throw null; } }
        [System.ComponentModel.BrowsableAttribute(false)]
        public virtual System.ComponentModel.ISite? Site { get { throw null; } set { } }
        public event System.EventHandler? Disposed { add { } remove { } }
        public void Dispose() { }
        protected virtual void Dispose(bool disposing) { }
        ~Component() { }
        [System.ObsoleteAttribute("old")]
        public void Old() { }
        public override string ToString() { throw null; }
    }
    public partial class ComponentCollection : System.Collections.ReadOnlyCollectionBase
    {
        public ComponentCollection() { }
    }
}
"""

    def test_reference_source_members(self) -> None:
        members: list[mm.WinFormsMember] | None = mm.read_runtime_class(self.SOURCE, "Component")
        self.assertIsNotNone(members)
        by_key: dict[tuple[str, str], mm.WinFormsMember] = {(member.kind, member.name): member for member in members}
        self.assertEqual(sorted(by_key), [("constructor", "Component"), ("event", "Disposed"), ("method", "Dispose"),
                                          ("method", "Old"), ("property", "CanRaiseEvents"), ("property", "Site")])
        self.assertEqual(by_key[("method", "Dispose")].overloads, 2)
        self.assertEqual(by_key[("method", "Old")].scope, "excluded")
        self.assertIsNone(mm.read_runtime_class(self.SOURCE, "Container"))

    def test_component_rows_come_from_the_runtime(self) -> None:
        manifest: dict = mm.load_manifest()
        component: dict | None = mm.entry_for(manifest, "Component")
        self.assertIsNotNone(component)
        self.assertEqual(component["winforms"], "Component")
        self.assertIn("Dispose", [row.get("winforms") for row in component["members"]])
        self.assertIn("runtime", manifest["sources"])

    def test_nested_types_are_listed_under_their_owner(self) -> None:
        manifest: dict = mm.load_manifest()
        nested: list[str] = [entry["winforms"] for entry in manifest["controls"] if entry.get("type") is None]
        self.assertIn("ListView.ListViewItemCollection", nested)
        self.assertIn("ListViewItem", nested)


if __name__ == "__main__":
    unittest.main()
