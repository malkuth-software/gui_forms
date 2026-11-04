"""Control schema tests: real-manifest check, stale-page detection, event scan and spelling."""

import json
import tempfile
import unittest
from pathlib import Path

import control_registry as cr
import control_schema as cs


class RealTreeTests(unittest.TestCase):
    def test_committed_manifest_matches_registry_and_headers(self) -> None:
        schema = cs.load_schema()
        findings = cs.validate(schema, cr.load_registry(), cs.scan_classes())
        self.assertEqual(findings, [])

    def test_committed_page_is_current(self) -> None:
        page = cs.render_page(cs.load_schema())
        committed = cs.PAGE_PATH.read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(committed, page, "run `python3 tools/control_schema.py render`")


class SpellingTests(unittest.TestCase):
    def test_payload_drops_const_and_reference_marks(self) -> None:
        self.assertEqual(cs.interaction_from_accessor("text_changed", "const std::string&"),
                         cs.Interaction("text_changed", "std::string", False))
        self.assertEqual(cs.interaction_from_accessor("validating", "ControlValidationEvent&"),
                         cs.Interaction("validating", "ControlValidationEvent", True))
        self.assertEqual(cs.interaction_from_accessor("item_check_state_changed", "std::size_t, CheckState"),
                         cs.Interaction("item_check_state_changed", "std::size_t, CheckState", False))
        self.assertEqual(cs.interaction_from_accessor("tick", ""), cs.Interaction("tick", "", False))


class ScanTests(unittest.TestCase):
    def test_public_events_resolve_through_bases(self) -> None:
        header = """
#pragma once
namespace gui_forms {
class Widget : public Control {
public:
    [[nodiscard]] Event<std::optional<bool>>& opened() noexcept { return opened_; }
    struct Nested final {
        Event<int>& ignored() noexcept;
    };
private:
    [[nodiscard]] Event<>& hidden() noexcept { return hidden_; }
};
class FancyWidget final : public Widget {
public:
    [[nodiscard]] Event<const FancyWidget&>& changed() noexcept { return changed_; }
};
}
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "gui_forms" / "widget.hpp"
            target.parent.mkdir(parents=True)
            target.write_text(header, encoding="utf-8")
            classes = cs.scan_classes(root)
        expected = cs.expected_interactions("FancyWidget", classes)
        self.assertEqual([item.name for item in expected], ["opened", "changed"])
        self.assertEqual(expected[0].payload, "std::optional<bool>")

    def test_a_missing_interaction_is_reported(self) -> None:
        schema = cs.load_schema()
        types = [dict(record) for record in schema["types"]]
        types[0]["interactions"] = types[0]["interactions"][:-1]
        findings = cs.validate({"types": types, "vocabulary": schema["vocabulary"], "engine": schema["engine"]},
                               cr.load_registry(), cs.scan_classes())
        self.assertEqual(len(findings), 1)
        self.assertIn(types[0]["id"], findings[0])

    def test_the_vocabulary_is_required_and_lists_the_ribbon_atoms(self) -> None:
        schema = cs.load_schema()
        vocabulary = schema["vocabulary"]
        for mark in ("dialog_launcher", "more", "overflow", "customize"):
            self.assertIn(mark, vocabulary["marks"])
        self.assertIn("accent_index", vocabulary["values"])
        for family in ("card", "menu_item", "tool_tip", "link", "visited_link"):
            self.assertIn(family, vocabulary["families"])
        self.assertIn("split_button", vocabulary["semantic_roles"])
        self.assertIn("menu_button", vocabulary["semantic_roles"])
        findings = cs.validate({"types": schema["types"], "engine": schema["engine"]}, cr.load_registry(),
                               cs.scan_classes())
        self.assertEqual(len(findings), 1)
        self.assertIn("vocabulary", findings[0])

    def test_the_vocabulary_lists_the_stage_a_atoms(self) -> None:
        vocabulary = cs.load_schema()["vocabulary"]
        for role in ("tree", "tree_item", "table", "row", "cell", "column_header", "row_header", "tree_grid",
                     "dialog", "status_bar", "live_region"):
            self.assertIn(role, vocabulary["semantic_roles"])
        for state in ("checkable", "multiselectable"):
            self.assertIn(state, vocabulary["semantic_states"])
        for field in ("level", "modal", "row_index", "column_index", "row_count", "column_count", "access_key",
                      "small_step", "large_step", "text_selection", "multiline"):
            self.assertIn(field, vocabulary["semantic_fields"])

    def test_the_vocabulary_lists_the_instrument_atoms(self) -> None:
        # W-5: the instrument part roles and the meter semantic role, appended.
        vocabulary = cs.load_schema()["vocabulary"]
        self.assertEqual(vocabulary["part_roles"][-3:], ["plot", "graticule", "trace"])
        self.assertEqual(vocabulary["semantic_roles"][-1], "meter")

    def test_the_vocabulary_lists_the_docking_atoms(self) -> None:
        schema = cs.load_schema()
        vocabulary = schema["vocabulary"]
        for mark in ("pin", "dock_shape"):
            self.assertIn(mark, vocabulary["marks"])
        for state in ("modified", "pinned"):
            self.assertIn(state, vocabulary["custom_states"])
            self.assertNotIn(state, vocabulary["states"])
        for value in ("text_orientation", "dock_region"):
            self.assertIn(value, vocabulary["values"])
        for role in ("pane", "tab_panel"):
            self.assertIn(role, vocabulary["semantic_roles"])
        self.assertIn("close", vocabulary["semantic_actions"])
        for field in ("controlled_panel", "orientation"):
            self.assertIn(field, vocabulary["semantic_fields"])
        shortened = dict(vocabulary)
        del shortened["semantic_actions"]
        findings = cs.validate({"types": schema["types"], "vocabulary": shortened, "engine": schema["engine"]},
                               cr.load_registry(), cs.scan_classes())
        self.assertEqual(len(findings), 1)
        self.assertIn("semantic_actions", findings[0])

    def test_the_engine_lists_its_primitives_within_budget(self) -> None:
        schema = cs.load_schema()
        engine = schema["engine"]
        self.assertEqual(engine["cascade_levels"][0], "built_in")
        self.assertEqual(engine["cascade_levels"][-1], "important_inline_style")
        self.assertEqual(engine["budgets"]["operations_per_device_pixel"], 512)
        names = [primitive["name"] for primitive in engine["primitives"]]
        self.assertEqual(names, ["bulge", "dithered_bulge", "pill_plate", "gold_plate", "inset_well", "etched_divider",
                                 "seam_line", "grip", "material", "overlay_shadow", "sheen", "glass"])
        bulge = engine["primitives"][0]
        self.assertEqual(bulge["roles"], ["face"])
        amplitude = next(item for item in bulge["parameters"] if item["name"] == "amplitude")
        self.assertTrue(amplitude["animated"])
        registry = cr.load_registry()
        classes = cs.scan_classes()
        self.assertEqual(cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"]},
                                     registry, classes)[0][:32], "the schema has no engine section")
        broken = json.loads(json.dumps(engine))
        broken["primitives"][0]["roles"] = ["sparkle"]
        broken["primitives"][1]["cost"]["per_device_pixel"] = 600
        findings = cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"], "engine": broken},
                               registry, classes)
        self.assertEqual(len(findings), 2)
        self.assertIn("sparkle", findings[0])
        self.assertIn("budget", findings[1])
        choosing = json.loads(json.dumps(engine))
        plate = next(item for item in choosing["primitives"] if item["name"] == "pill_plate")
        shape = next(item for item in plate["parameters"] if item["name"] == "shape")
        self.assertEqual(shape["kind"], "choice")
        self.assertEqual(shape["default"], "rounded_rectangle")
        shape["choices"] = ["pill", "pill", "Oval"]
        findings = cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"], "engine": choosing},
                               registry, classes)
        self.assertEqual(len(findings), 3)
        self.assertIn("distinct", findings[0])
        self.assertIn("'Oval'", findings[1])
        self.assertIn("not a choice", findings[2])

    def test_the_engine_inputs_are_vocabulary(self) -> None:
        schema = cs.load_schema()
        self.assertEqual(schema["vocabulary"]["engine_inputs"],
                         ["pointer_x", "pointer_y", "time_in_state", "display_scale", "text_scale"])
        self.assertEqual(schema["engine"]["published_values"]["maximum"], 32)
        broken = json.loads(json.dumps(schema["engine"]))
        broken["primitives"][0]["inputs_read"] = ["density"]
        findings = cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"], "engine": broken},
                               cr.load_registry(), cs.scan_classes())
        self.assertEqual(len(findings), 1)
        self.assertIn("density", findings[0])

    def test_the_engine_lists_its_motion_caps(self) -> None:
        schema = cs.load_schema()
        engine = schema["engine"]
        self.assertEqual(engine["transitions"]["engine_properties"], ["opacity", "scale", "face", "translate_x", "translate_y"])
        self.assertEqual(engine["ambient"]["maximum_frames_per_second"], 30)
        broken = json.loads(json.dumps(engine))
        broken["ambient"]["maximum_frames_per_second"] = 60
        broken["ambient"]["maximum_window_fraction"] = 0.5
        findings = cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"], "engine": broken},
                               cr.load_registry(), cs.scan_classes())
        self.assertEqual(len(findings), 2)
        self.assertIn("30", findings[0])
        self.assertIn("10 %", findings[1])

    def test_the_engine_lists_its_expression_operations(self) -> None:
        schema = cs.load_schema()
        expressions = schema["engine"]["expressions"]
        self.assertEqual(expressions["maximum_operations"], 64)
        for operation in ("clamp", "mix", "smoothstep", "choose", "less"):
            self.assertIn(operation, expressions["operations"])
        broken = json.loads(json.dumps(schema["engine"]))
        broken["expressions"]["maximum_stack"] = 0
        broken["expressions"]["operations"].append("Loop")
        findings = cs.validate({"types": schema["types"], "vocabulary": schema["vocabulary"], "engine": broken},
                               cr.load_registry(), cs.scan_classes())
        self.assertEqual(len(findings), 2)
        self.assertIn("maximum_stack", findings[0])
        self.assertIn("Loop", findings[1])


if __name__ == "__main__":
    unittest.main()
