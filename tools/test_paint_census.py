#!/usr/bin/env python3
"""Tests for paint_census.py: the Painter's drawing methods, call matching,
comments, classes of file, and the ratchet against a baseline."""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import paint_census

HEADER: str = """
class TextMetricsProvider { public: virtual double measure_text_utf8(int) = 0; };
class Painter : public TextMetricsProvider {
public:
    virtual ~Painter() = default;
    virtual void save() = 0;
    virtual void clip_rect(Rect rect) = 0;
    virtual void fill_rect(Rect rect, Color color) = 0;
    virtual void draw_line(Point from, Point to,
                           Color color, double width) = 0;
    [[nodiscard]] virtual bool draw_part_raster(const PartRaster& raster) { return false; }
};
"""

SOURCE: str = """
void Thing::on_paint(Painter& painter, Rect) {
    painter.fill_rect(bounds(), colour);       // counted
    // painter.fill_rect(bounds(), colour);    not counted: a comment
    /* painter.draw_line(a, b, c, 1.0);
       painter.draw_line(a, b, c, 1.0); */
    (*painter_pointer).draw_line(a, b, c, 1.0);
    paint_surface_material(painter, bounds(), material);
    detail::paint_part_from_recipe(painter, part, recipe, context);
    painter.save();
    paint_part(painter, *this, part);
}
"""


class PaintCensusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.header = self.root / "painter.hpp"
        self.header.write_text(HEADER, encoding="utf-8")
        self.controls = self.root / "controls"
        (self.controls / "thing").mkdir(parents=True)
        (self.controls / "gallery").mkdir()
        (self.controls / "canvas").mkdir()
        (self.controls / "thing" / "thing.cpp").write_text(SOURCE, encoding="utf-8")
        (self.controls / "gallery" / "page.cpp").write_text("painter.fill_rect(r, c);\n", encoding="utf-8")
        (self.controls / "canvas" / "canvas.cpp").write_text("painter.fill_rect(r, c);\n", encoding="utf-8")
        (self.controls / "thing" / "clean.cpp").write_text("paint_part(painter, *this, part);\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_drawing_methods_exclude_state_and_measurement(self) -> None:
        methods = paint_census.painter_drawing_methods(self.header)
        self.assertEqual(methods, ["draw_line", "draw_part_raster", "fill_rect"])

    def census(self) -> list[paint_census.FileCensus]:
        methods = paint_census.painter_drawing_methods(self.header)
        exempt = {"canvas/canvas.cpp": "application content"}
        return paint_census.scan(self.controls, methods, exempt)

    def test_calls_comments_and_classes(self) -> None:
        by_path = {entry.path: entry for entry in self.census()}
        self.assertEqual(sorted(by_path), ["canvas/canvas.cpp", "gallery/page.cpp", "thing/thing.cpp"])
        thing = by_path["thing/thing.cpp"]
        self.assertEqual(thing.category, "stock")
        self.assertEqual(thing.calls, {"fill_rect": 1, "draw_line": 1, "paint_surface_material": 1,
                                       "paint_part_from_recipe": 1})
        self.assertEqual(by_path["gallery/page.cpp"].category, "demo")
        self.assertEqual(by_path["canvas/canvas.cpp"].category, "exempt")
        summary = paint_census.totals(self.census())
        self.assertEqual(summary["stock"], {"files": 1, "calls": 4})

    def test_effect_canvas_calls_are_not_stock(self) -> None:
        canvas_header = self.root / "effect_canvas.hpp"
        canvas_header.write_text(
            "class EffectCanvas final {\npublic:\n"
            "    GUI_FORMS_EFFECT_VOCABULARY(4, 1)\n    void fill_rect(Rect rect, Color color) noexcept;\n"
            "    GUI_FORMS_EFFECT_VOCABULARY(6, 2)\n    void stroke_line(Point from, Point to, Color color,\n"
            "                     double width) noexcept;\n"
            "    GUI_FORMS_EFFECT_VOCABULARY(2, 0)\n    void save() noexcept;\n};\n", encoding="utf-8")
        effect_methods = paint_census.effect_canvas_methods(canvas_header)
        self.assertEqual(effect_methods, frozenset({"fill_rect", "stroke_line"}))
        (self.controls / "thing" / "thing_effects.cpp").write_text(
            "void Glow::run(const EffectView<Thing>& parent, const EffectFrame& frame, EffectCanvas& canvas) {\n"
            "    canvas.save();\n"
            "    canvas.fill_rect(r, c);\n"
            "    canvas.stroke_line(a, b, c, 1.0);\n"
            "    painter.fill_rect(r, c);\n"
            "}\n", encoding="utf-8")
        methods = paint_census.painter_drawing_methods(self.header)
        census = paint_census.scan(self.controls, methods, {}, effect_methods)
        by_path = {entry.path: entry for entry in census}
        effects = by_path["thing/thing_effects.cpp"]
        self.assertEqual(effects.calls, {"fill_rect": 1})
        self.assertEqual(effects.effect_calls, {"fill_rect": 1, "stroke_line": 1})
        summary = paint_census.totals(census)
        self.assertEqual(summary["effect"], {"files": 1, "calls": 2})
        baseline = paint_census.baseline_document(census, {}, methods)
        self.assertEqual(baseline["files"]["thing/thing_effects.cpp"]["calls"], 1)
        self.assertEqual(baseline["files"]["thing/thing_effects.cpp"]["effect_calls"], 2)

    def test_ratchet(self) -> None:
        census = self.census()
        methods = paint_census.painter_drawing_methods(self.header)
        baseline = paint_census.baseline_document(census, {"canvas/canvas.cpp": "content"}, methods)
        failures, fallen = paint_census.check(census, baseline)
        self.assertEqual((failures, fallen), ([], []))
        # A file above its baseline, or new with calls, fails.
        baseline["files"]["thing/thing.cpp"]["calls"] = 3
        del baseline["files"]["gallery/page.cpp"]
        failures, fallen = paint_census.check(census, baseline)
        self.assertEqual(len(failures), 2)
        self.assertEqual(fallen, [])
        # A file below its baseline, or gone, is reported as fallen, not failed.
        baseline = paint_census.baseline_document(census, {}, methods)
        baseline["files"]["thing/thing.cpp"]["calls"] = 9
        baseline["files"]["gone.cpp"] = {"category": "stock", "calls": 2}
        failures, fallen = paint_census.check(census, baseline)
        self.assertEqual(failures, [])
        self.assertEqual(len(fallen), 2)


if __name__ == "__main__":
    unittest.main()
