#!/usr/bin/env python3
"""Tests for property_codemod.py's shared enumerations: an enumeration that two
families name gets its PropertyValueTraits once, from shared.json, and a
family that would declare one another family declares too is refused."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

import property_codemod


def family(name: str, value: str, enumeration: str) -> dict[str, object]:
    """A family config with one enumeration property."""
    return {
        "family": name,
        "declaring": name,
        "header": f"include/{name}.hpp",
        "source": f"src/{name}.cpp",
        "capabilities_header": f"include/{name}_capabilities.hpp",
        "properties": [
            {
                "name": "alignment",
                "capability": name + "Alignment",
                "value": value,
                "comment": "The alignment.",
                "hooks": "generated",
                "table": {
                    "kind": "enumeration",
                    "category": "Appearance",
                    "description": "The alignment.",
                    "enumeration": enumeration,
                },
            }
        ],
    }


SHARED_ENTRY: dict[str, str] = {
    "value": "HorizontalAlignment",
    "value_header": "gui_forms/controls/label_values.hpp",
    "enumeration": "label_detail::horizontal_alignment_enum()",
    "enumeration_header": "src/controls/label/horizontal_alignment_property.hpp",
}


class SharedEnumerationTests(unittest.TestCase):
    def write(self, directory: Path, name: str, config: dict[str, object]) -> Path:
        path = directory / (name + ".json")
        path.write_text(json.dumps(config), encoding="utf-8")
        return path

    def test_duplicate_traits_refused(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            label_path = self.write(directory, "label", family("Label", "HorizontalAlignment", "a()"))
            self.write(directory, "form_layout", family("FormLayout", "HorizontalAlignment", "b()"))
            self.write(directory, "shared", {"family": "Shared", "properties": []})
            label = property_codemod.load(label_path)
            errors = property_codemod.duplicate_enum_traits(label)
            self.assertEqual(len(errors), 1)
            self.assertIn("HorizontalAlignment", errors[0])
            self.assertIn("FormLayout", errors[0])

    def test_shared_enumeration_declared_once(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            label_path = self.write(directory, "label", family("Label", "HorizontalAlignment", "a()"))
            layout_path = self.write(directory, "form_layout", family("FormLayout", "HorizontalAlignment", "b()"))
            shared_path = self.write(directory, "shared",
                                     {"family": "Shared", "properties": [], "enumerations": [SHARED_ENTRY]})
            for path in (label_path, layout_path):
                config = property_codemod.load(path)
                self.assertEqual(property_codemod.duplicate_enum_traits(config), [])
                self.assertEqual(property_codemod.enum_traits_needed(config), [])
                self.assertTrue(property_codemod.uses_shared_enumeration(config))
                header = property_codemod.capabilities_header(config)
                self.assertNotIn("struct PropertyValueTraits<HorizontalAlignment>", header)
                self.assertIn('#include "gui_forms/controls/shared_property_enums.hpp"', header)
                self.assertNotIn("PropertyValueTraits<HorizontalAlignment>::type()",
                                 property_codemod.enum_traits_definitions(config))
            shared = property_codemod.load(shared_path)
            errors, _ = property_codemod.validate(shared)
            self.assertEqual(errors, [])
            header = property_codemod.shared_enum_header(shared)
            source = property_codemod.shared_enum_source(shared)
            self.assertEqual(header.count("struct PropertyValueTraits<HorizontalAlignment>"), 1)
            self.assertEqual(source.count("PropertyValueTraits<HorizontalAlignment>::type()"), 1)
            self.assertIn("label_detail::horizontal_alignment_enum()", source)
            self.assertIn('#include "../label/horizontal_alignment_property.hpp"', source)

    def test_family_only_enumeration_stays_local(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            label_path = self.write(directory, "label", family("Label", "TextWrapping", "c()"))
            self.write(directory, "shared", {"family": "Shared", "properties": [], "enumerations": [SHARED_ENTRY]})
            label = property_codemod.load(label_path)
            self.assertEqual(property_codemod.enum_traits_needed(label), [("TextWrapping", "c()")])
            self.assertFalse(property_codemod.uses_shared_enumeration(label))

    def test_enumeration_entry_validated(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            incomplete = {"value": "HorizontalAlignment", "colour": "red"}
            shared_path = self.write(directory, "shared",
                                     {"family": "Shared", "properties": [], "enumerations": [incomplete]})
            errors, _ = property_codemod.validate(property_codemod.load(shared_path))
            self.assertTrue(any("unknown key 'colour'" in error for error in errors))
            self.assertTrue(any("missing 'enumeration'" in error for error in errors))

    def test_repository_configs_have_no_duplicate_traits(self) -> None:
        directory = Path(__file__).resolve().parent / "property_families"
        for path in sorted(directory.glob("*.json")):
            config = property_codemod.load(path)
            if config.get("family") == "Shared":
                continue
            self.assertEqual(property_codemod.duplicate_enum_traits(config), [], path.name)


if __name__ == "__main__":
    unittest.main()
