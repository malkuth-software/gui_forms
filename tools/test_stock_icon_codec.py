"""stock_icon_codec converts the Heroicons SVG subset to stock icon records.

The samples are four Heroicons v2.2.0 files (MIT, assets/icons/heroicons/),
copied here byte for byte so the parser tests stand alone: a stroked
outline, a solid with arcs, a solid rect with rounded corners and an
even-odd solid. The last tests check that the samples are the committed
files, and read the build's generated records: the C++ generator's
output (tools/stock_icon_generator/, whose oracle this codec is), named by
GUI_FORMS_STOCK_ICON_DATA, which the CMake test sets; without it, the
records the committed SVG files give. They check that those records hold
the same records for the samples and match the SVG tree whole.
"""
from __future__ import annotations

import math
import os
import unittest
from pathlib import Path

import stock_icon_codec as codec

MINUS_24_OUTLINE: str = (
    '<svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" '
    'stroke="currentColor" aria-hidden="true" data-slot="icon">\n'
    '  <path stroke-linecap="round" stroke-linejoin="round" d="M5 12h14"/>\n'
    '</svg>\n')
X_MARK_20_SOLID: str = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" '
    'data-slot="icon">\n'
    '  <path d="M6.28 5.22a.75.75 0 0 0-1.06 1.06L8.94 10l-3.72 3.72a.75.75 0 1 0 1.06 1.06L10 11.06l3.72 '
    '3.72a.75.75 0 1 0 1.06-1.06L11.06 10l3.72-3.72a.75.75 0 0 0-1.06-1.06L10 8.94 6.28 5.22Z"/>\n'
    '</svg>\n')
STOP_16_SOLID: str = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" '
    'data-slot="icon">\n'
    '  <rect width="10" height="10" x="3" y="3" rx="1.5"/>\n'
    '</svg>\n')
WARNING_16_SOLID: str = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" '
    'data-slot="icon">\n'
    '  <path fill-rule="evenodd" d="M6.701 2.25c.577-1 2.02-1 2.598 0l5.196 9a1.5 1.5 0 0 1-1.299 2.25H2.804a1.5 '
    '1.5 0 0 1-1.3-2.25l5.197-9ZM8 4a.75.75 0 0 1 .75.75v3a.75.75 0 1 1-1.5 0v-3A.75.75 0 0 1 8 4Zm0 8a1 1 0 1 0 '
    '0-2 1 1 0 0 0 0 2Z" clip-rule="evenodd"/>\n'
    '</svg>\n')

SAMPLES: tuple[tuple[str, int, int, str], ...] = (
    ('minus', codec.STYLE_OUTLINE, 24, MINUS_24_OUTLINE),
    ('x-mark', codec.STYLE_SOLID, 20, X_MARK_20_SOLID),
    ('stop', codec.STYLE_SOLID, 16, STOP_16_SOLID),
    ('exclamation-triangle', codec.STYLE_SOLID, 16, WARNING_16_SOLID),
)


def sample_variants() -> list[codec.Variant]:
    variants: list[codec.Variant] = [codec.Variant(name, style, grid, codec.parse_svg(text, grid, name))
                                     for name, style, grid, text in SAMPLES]
    variants.sort(key=codec.variant_key)
    return variants


def generated_text() -> str:
    """The build's generated records (GUI_FORMS_STOCK_ICON_DATA), or the
    records the committed SVG files give when the test runs alone."""
    location: str | None = os.environ.get('GUI_FORMS_STOCK_ICON_DATA')
    if location:
        return Path(location).read_text(encoding='utf-8')
    return codec.render(codec.encode(codec.read_variants(codec.SOURCE)))


class ParseTests(unittest.TestCase):
    def test_stroked_outline(self) -> None:
        layers: list[codec.Layer] = codec.parse_svg(MINUS_24_OUTLINE, 24, 'minus')
        self.assertEqual(len(layers), 1)
        self.assertEqual(layers[0].verbs, [codec.VERB_MOVE, codec.VERB_LINE])
        self.assertEqual(layers[0].points, [(5.0, 12.0), (19.0, 12.0)])
        self.assertEqual(layers[0].paint, codec.PAINT_STROKE)
        self.assertEqual(layers[0].stroke_width, 1.5)
        self.assertEqual(layers[0].cap, codec.CAPS['round'])

    def test_arcs_become_quarter_turn_cubics_on_the_circle(self) -> None:
        layer: codec.Layer = codec.parse_svg(WARNING_16_SOLID, 16, 'warning')[0]
        self.assertEqual(layer.paint, codec.PAINT_EVEN_ODD)
        # The last contour is a dot: two half-turn arcs of radius 1 about
        # (8, 11), so four cubic curves whose ends lie on the circle.
        last_move: int = max(index for index, verb in enumerate(layer.verbs) if verb == codec.VERB_MOVE)
        point_index: int = sum(codec.POINTS_FOR[verb] for verb in layer.verbs[:last_move])
        tail: list[int] = layer.verbs[last_move:]
        self.assertEqual(tail, [codec.VERB_MOVE] + [codec.VERB_CUBIC] * 4 + [codec.VERB_CLOSE])
        ends: list[tuple[float, float]] = [layer.points[point_index]] + [
            layer.points[point_index + 3 * segment + 3] for segment in range(4)]
        for x, y in ends:
            self.assertAlmostEqual(math.hypot(x - 8.0, y - 11.0), 1.0, places=9)

    def test_rect_with_rounded_corners(self) -> None:
        layer: codec.Layer = codec.parse_svg(STOP_16_SOLID, 16, 'stop')[0]
        expected: list[int] = [codec.VERB_MOVE] + [codec.VERB_LINE, codec.VERB_CUBIC] * 4 + [codec.VERB_CLOSE]
        self.assertEqual(layer.verbs, expected)
        self.assertEqual(layer.points[0], (4.5, 3.0))
        self.assertEqual(layer.points[1], (11.5, 3.0))

    def test_drawing_after_close_states_its_move(self) -> None:
        layer: codec.Layer = codec.Layer()
        codec.parse_path('M1 1h2v2zl3 0', layer)
        self.assertEqual(layer.verbs, [codec.VERB_MOVE, codec.VERB_LINE, codec.VERB_LINE, codec.VERB_CLOSE,
                                       codec.VERB_MOVE, codec.VERB_LINE])
        self.assertEqual(layer.points[-2:], [(1.0, 1.0), (4.0, 1.0)])

    def test_flags_run_together(self) -> None:
        separated: codec.Layer = codec.Layer()
        joined: codec.Layer = codec.Layer()
        codec.parse_path('M2 2a1 1 0 0 1 2 0', separated)
        codec.parse_path('M2 2a1 1 0 012 0', joined)
        self.assertEqual(separated.points, joined.points)

    def test_outside_the_subset_is_refused(self) -> None:
        circle: str = MINUS_24_OUTLINE.replace('<path stroke-linecap="round" stroke-linejoin="round" d="M5 12h14"/>',
                                               '<circle cx="12" cy="12" r="4"/>')
        with self.assertRaises(codec.CodecError):
            codec.parse_svg(circle, 24, 'circle')
        with self.assertRaises(codec.CodecError):
            codec.parse_svg(MINUS_24_OUTLINE.replace('stroke-linejoin="round"', 'stroke-linejoin="miter"'), 24, 'miter')
        with self.assertRaises(codec.CodecError):
            codec.parse_svg(MINUS_24_OUTLINE, 20, 'grid')
        with self.assertRaises(codec.CodecError):
            codec.parse_svg('<!DOCTYPE svg>' + MINUS_24_OUTLINE, 24, 'doctype')


class RoundTripTests(unittest.TestCase):
    def test_records_decode_to_the_parsed_paths(self) -> None:
        variants: list[codec.Variant] = sample_variants()
        records: codec.Records = codec.encode(variants)
        self.assertEqual(records.names, sorted(name for name, _, _, _ in SAMPLES))
        for index, variant in enumerate(variants):
            decoded: list[codec.Layer] = codec.decode_variant(records, index)
            self.assertEqual(len(decoded), len(variant.layers))
            for original, read in zip(variant.layers, decoded):
                self.assertEqual(read.verbs, original.verbs)
                self.assertEqual(read.paint, original.paint)
                self.assertEqual(read.cap, original.cap)
                self.assertAlmostEqual(read.stroke_width, original.stroke_width, places=3)
                self.assertEqual(len(read.points), len(original.points))
                for (x0, y0), (x1, y1) in zip(original.points, read.points):
                    self.assertLessEqual(abs(x0 - x1), 0.0005)
                    self.assertLessEqual(abs(y0 - y1), 0.0005)

    def test_render_reads_back(self) -> None:
        records: codec.Records = codec.encode(sample_variants())
        read: codec.Records = codec.read_generated(codec.render(records))
        self.assertEqual(read.names, records.names)
        self.assertEqual(read.variants, records.variants)
        self.assertEqual(read.layers, records.layers)
        self.assertEqual(read.verbs, records.verbs)
        self.assertEqual(read.coordinates, records.coordinates)

    def test_samples_are_the_committed_files(self) -> None:
        for name, style, grid, text in SAMPLES:
            directory: str = f'{grid}/outline' if style == codec.STYLE_OUTLINE else f'{grid}/solid'
            path: Path = codec.SOURCE / directory / f'{name}.svg'
            self.assertEqual(path.read_text(encoding='utf-8'), text, str(path))

    def test_generated_records_match_the_svg_tree(self) -> None:
        expected: str = codec.render(codec.encode(codec.read_variants(codec.SOURCE)))
        self.assertEqual(generated_text(), expected)

    def test_generated_records_hold_the_samples(self) -> None:
        committed: codec.Records = codec.read_generated(generated_text())
        sample: codec.Records = codec.encode(sample_variants())
        for sample_index, (name_index, style, grid, _, _) in enumerate(sample.variants):
            name: str = sample.names[name_index]
            matches: list[int] = [index for index, (committed_name, committed_style, committed_grid, _, _)
                                  in enumerate(committed.variants)
                                  if committed.names[committed_name] == name and committed_style == style
                                  and committed_grid == grid]
            self.assertEqual(len(matches), 1, name)
            expected: list[codec.Layer] = codec.decode_variant(sample, sample_index)
            found: list[codec.Layer] = codec.decode_variant(committed, matches[0])
            self.assertEqual([layer.verbs for layer in found], [layer.verbs for layer in expected])
            self.assertEqual([layer.points for layer in found], [layer.points for layer in expected])


if __name__ == '__main__':
    unittest.main()
