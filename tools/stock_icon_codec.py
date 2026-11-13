#!/usr/bin/env python3
"""Convert the Heroicons SVG files into GUI.Forms' stock icon records.

GUI.Forms ships Heroicons v2.2.0 (MIT; owner decision, 2026-11-13) as
vector records in its own format, embedded in the library and baked to
rasters at launch (src/core/resources/stock_icons/). This tool reads the
committed SVG files, `assets/icons/heroicons/` (upstream's `optimized/`
layout: 24/outline, 24/solid, 20/solid, 16/solid), and writes
`stock_icon_data.inc`. The build runs it (CMakeLists.txt, into
`<build>/generated/stock_icons/`); the output is not committed.

The SVG files are data. Only the subset Heroicons uses is admitted, and
anything else is refused with the file named:
- the root `svg` with `viewBox="0 0 N N"` (N 16, 20 or 24, matching the
  directory), `fill` (`none` or `currentColor`), `stroke`
  (`currentColor`), `stroke-width`, and the inert `xmlns`, `aria-hidden`
  and `data-slot`;
- `path` with `d`, `fill-rule` (`evenodd` or `nonzero`), `clip-rule` (inert
  outside a clip path), `stroke-linecap` (`butt`, `round`, `square`) and
  `stroke-linejoin` (`round` only: the baked stroke draws round joins);
- `rect` with `x`, `y`, `width`, `height`, `rx` and `ry`;
- path commands M L H V C S Q T A Z in both cases.

The record format (the C++ reader is stock_icon_records.hpp):
- verbs are PaintPathVerb values: 0 move_to, 1 line_to, 3 cubic_to,
  5 close (2 quad_to for Q and T; arcs and rectangle corners become cubic
  Bezier curves of at most a quarter turn each, so 4 arc_to is not used);
- coordinates are signed 16-bit integers in thousandths of a grid unit,
  x then y for each point the verbs consume (a grid of 24 units fits
  within +-32.767 with room for control points outside the view box);
- a layer is one path element: its verb run, its coordinate run and its
  paint (0 a non-zero fill, 1 an even-odd fill, 2 a stroke with its width
  in thousandths and its cap: 0 butt, 1 round, 2 square);
- a variant is one file: the icon name's index, its style (0 outline,
  1 solid), its grid (16, 20, 24) and its layer run.

Usage:
  stock_icon_codec.py generate --output FILE [--source DIR]
  stock_icon_codec.py check --output FILE [--source DIR]
DIR defaults to assets/icons/heroicons; `check` proves FILE (the build's
generated copy) matches what the SVG files give.
"""
from __future__ import annotations

import argparse
import math
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent
SOURCE: Path = ROOT / 'assets/icons/heroicons'
SOURCE_NAME: str = 'Heroicons v2.2.0 (tailwindlabs/heroicons, commit ca7b62e)'

VERB_MOVE: int = 0
VERB_LINE: int = 1
VERB_QUAD: int = 2
VERB_CUBIC: int = 3
VERB_CLOSE: int = 5
POINTS_FOR: dict[int, int] = {VERB_MOVE: 1, VERB_LINE: 1, VERB_QUAD: 2, VERB_CUBIC: 3, VERB_CLOSE: 0}

PAINT_NONZERO: int = 0
PAINT_EVEN_ODD: int = 1
PAINT_STROKE: int = 2
CAPS: dict[str, int] = {'butt': 0, 'round': 1, 'square': 2}
STYLE_OUTLINE: int = 0
STYLE_SOLID: int = 1
# The directories read, each with its style and grid.
VARIANT_DIRECTORIES: tuple[tuple[str, int, int], ...] = (
    ('24/outline', STYLE_OUTLINE, 24),
    ('24/solid', STYLE_SOLID, 24),
    ('20/solid', STYLE_SOLID, 20),
    ('16/solid', STYLE_SOLID, 16),
)
SCALE: int = 1000
COORDINATE_LIMIT: int = 32767
INERT_ROOT: frozenset[str] = frozenset({'xmlns', 'aria-hidden', 'data-slot'})
SVG_NAMESPACE: str = '{http://www.w3.org/2000/svg}'


class CodecError(ValueError):
    """A file outside the admitted subset."""


@dataclass
class Layer:
    """One path element: verbs, points (in grid units) and paint."""
    verbs: list[int] = field(default_factory=list)
    points: list[tuple[float, float]] = field(default_factory=list)
    paint: int = PAINT_NONZERO
    stroke_width: float = 0.0
    cap: int = 0


@dataclass
class Variant:
    name: str
    style: int
    grid: int
    layers: list[Layer]


# --- path data -------------------------------------------------------------

NUMBER: re.Pattern[str] = re.compile(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
COMMAND_ARGUMENTS: dict[str, int] = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Q': 4, 'T': 2, 'A': 7, 'Z': 0}


def tokens_of(data: str) -> list[str | float]:
    """The path data's commands (one letter) and numbers, in order. Arc
    flags may be written without separators ("a.75.75 0 01..."), so a flag
    position is read as one digit by parse_path, not here."""
    result: list[str | float] = []
    index: int = 0
    while index < len(data):
        character: str = data[index]
        if character.isspace() or character == ',':
            index += 1
            continue
        if character.upper() in COMMAND_ARGUMENTS:
            result.append(character)
            index += 1
            continue
        match: re.Match[str] | None = NUMBER.match(data, index)
        if match is None:
            raise CodecError(f'path data has an unknown character {character!r} at {index}')
        result.append(match.group(0))
        index = match.end()
    return result


class PathBuilder:
    """Appends verbs and points to a layer, keeping SVG's current point,
    subpath start and reflected control points."""

    def __init__(self, layer: Layer) -> None:
        self.layer: Layer = layer
        self.current: tuple[float, float] | None = None
        self.start: tuple[float, float] = (0.0, 0.0)
        self.closed: bool = False
        self.last_cubic_control: tuple[float, float] | None = None
        self.last_quad_control: tuple[float, float] | None = None

    def ensure_open(self) -> None:
        # A drawing command after Z starts a new subpath at the closed one's
        # start; the record states it with an explicit move.
        if self.current is None:
            raise CodecError('path data draws before its first move')
        if self.closed:
            self.move(self.current)

    def move(self, point: tuple[float, float]) -> None:
        self.layer.verbs.append(VERB_MOVE)
        self.layer.points.append(point)
        self.current = point
        self.start = point
        self.closed = False

    def line(self, point: tuple[float, float]) -> None:
        self.ensure_open()
        self.layer.verbs.append(VERB_LINE)
        self.layer.points.append(point)
        self.current = point

    def cubic(self, first: tuple[float, float], second: tuple[float, float], end: tuple[float, float]) -> None:
        self.ensure_open()
        self.layer.verbs.append(VERB_CUBIC)
        self.layer.points.extend((first, second, end))
        self.current = end

    def quad(self, control: tuple[float, float], end: tuple[float, float]) -> None:
        self.ensure_open()
        self.layer.verbs.append(VERB_QUAD)
        self.layer.points.extend((control, end))
        self.current = end

    def close(self) -> None:
        if self.current is None:
            raise CodecError('path data closes before its first move')
        if self.closed:
            return
        self.layer.verbs.append(VERB_CLOSE)
        self.current = self.start
        self.closed = True

    def arc(self, radius_x: float, radius_y: float, rotation: float, large: bool, sweep: bool,
            end: tuple[float, float]) -> None:
        """SVG's endpoint arc (SVG 1.1 appendix F.6) as cubic curves of at
        most a quarter turn each."""
        self.ensure_open()
        assert self.current is not None
        start: tuple[float, float] = self.current
        if start == end:
            return
        radius_x = abs(radius_x)
        radius_y = abs(radius_y)
        if radius_x == 0.0 or radius_y == 0.0:
            self.line(end)
            return
        phi: float = math.radians(rotation % 360.0)
        cos_phi: float = math.cos(phi)
        sin_phi: float = math.sin(phi)
        half_dx: float = (start[0] - end[0]) / 2.0
        half_dy: float = (start[1] - end[1]) / 2.0
        x1: float = cos_phi * half_dx + sin_phi * half_dy
        y1: float = -sin_phi * half_dx + cos_phi * half_dy
        # F.6.6: radii too small to reach are scaled up.
        lam: float = (x1 * x1) / (radius_x * radius_x) + (y1 * y1) / (radius_y * radius_y)
        if lam > 1.0:
            root_lam: float = math.sqrt(lam)
            radius_x *= root_lam
            radius_y *= root_lam
        numerator: float = (radius_x * radius_x * radius_y * radius_y - radius_x * radius_x * y1 * y1 -
                            radius_y * radius_y * x1 * x1)
        denominator: float = radius_x * radius_x * y1 * y1 + radius_y * radius_y * x1 * x1
        factor: float = math.sqrt(max(0.0, numerator / denominator)) if denominator != 0.0 else 0.0
        if large == sweep:
            factor = -factor
        centre_x1: float = factor * radius_x * y1 / radius_y
        centre_y1: float = -factor * radius_y * x1 / radius_x
        centre_x: float = cos_phi * centre_x1 - sin_phi * centre_y1 + (start[0] + end[0]) / 2.0
        centre_y: float = sin_phi * centre_x1 + cos_phi * centre_y1 + (start[1] + end[1]) / 2.0

        def angle(ux: float, uy: float, vx: float, vy: float) -> float:
            return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

        theta: float = angle(1.0, 0.0, (x1 - centre_x1) / radius_x, (y1 - centre_y1) / radius_y)
        delta: float = angle((x1 - centre_x1) / radius_x, (y1 - centre_y1) / radius_y,
                             (-x1 - centre_x1) / radius_x, (-y1 - centre_y1) / radius_y)
        if not sweep and delta > 0.0:
            delta -= 2.0 * math.pi
        elif sweep and delta < 0.0:
            delta += 2.0 * math.pi
        segments: int = max(1, math.ceil(abs(delta) / (math.pi / 2.0) - 1e-9))
        step: float = delta / segments
        # The tangent length of a circular arc of `step` radians, scaled by
        # each radius (the usual 4/3 tan(step/4) approximation).
        kappa: float = 4.0 / 3.0 * math.tan(step / 4.0)

        def point_at(t: float) -> tuple[float, float]:
            x: float = radius_x * math.cos(t)
            y: float = radius_y * math.sin(t)
            return (cos_phi * x - sin_phi * y + centre_x, sin_phi * x + cos_phi * y + centre_y)

        def derivative_at(t: float) -> tuple[float, float]:
            x: float = -radius_x * math.sin(t)
            y: float = radius_y * math.cos(t)
            return (cos_phi * x - sin_phi * y, sin_phi * x + cos_phi * y)

        for index in range(segments):
            t0: float = theta + step * index
            t1: float = t0 + step
            p0: tuple[float, float] = point_at(t0)
            p3: tuple[float, float] = end if index == segments - 1 else point_at(t1)
            d0: tuple[float, float] = derivative_at(t0)
            d1: tuple[float, float] = derivative_at(t1)
            first: tuple[float, float] = (p0[0] + kappa * d0[0], p0[1] + kappa * d0[1])
            second: tuple[float, float] = (p3[0] - kappa * d1[0], p3[1] - kappa * d1[1])
            self.cubic(first, second, p3)


def parse_path(data: str, layer: Layer) -> None:
    """Appends `data` (an SVG path's d attribute) to `layer`."""
    tokens: list[str | float] = tokens_of(data)
    builder: PathBuilder = PathBuilder(layer)
    index: int = 0
    command: str = ''

    def number() -> float:
        nonlocal index
        if index >= len(tokens) or not isinstance(tokens[index], str) or tokens[index][0].isalpha():
            raise CodecError(f'path data needs a number at token {index}')
        value: float = float(tokens[index])
        index += 1
        return value

    def flag() -> bool:
        # A flag is one digit, possibly run together with what follows.
        nonlocal index
        if index >= len(tokens):
            raise CodecError('path data ends inside an arc')
        text: str = str(tokens[index])
        if text[0] not in '01':
            raise CodecError(f'arc flag {text!r} is not 0 or 1')
        if len(text) == 1:
            index += 1
        else:
            tokens[index] = text[1:]
        return text[0] == '1'

    while index < len(tokens):
        token: str | float = tokens[index]
        if isinstance(token, str) and token[0].isalpha():
            command = token
            index += 1
            if command.upper() == 'Z':
                builder.close()
                builder.last_cubic_control = None
                builder.last_quad_control = None
                continue
        elif not command or command.upper() == 'Z':
            raise CodecError(f'path data has a number without a command at token {index}')
        relative: bool = command.islower()
        upper: str = command.upper()
        base: tuple[float, float] = builder.current if (relative and builder.current is not None) else (0.0, 0.0)
        if upper == 'M':
            point: tuple[float, float] = (base[0] + number(), base[1] + number())
            builder.move(point)
            # Further pairs after a move are lines.
            command = 'l' if relative else 'L'
            builder.last_cubic_control = None
            builder.last_quad_control = None
            continue
        if upper == 'L':
            builder.line((base[0] + number(), base[1] + number()))
            builder.last_cubic_control = None
            builder.last_quad_control = None
        elif upper == 'H':
            x: float = number() + (base[0] if relative else 0.0)
            if builder.current is None:
                raise CodecError('path data draws before its first move')
            builder.line((x, builder.current[1]))
            builder.last_cubic_control = None
            builder.last_quad_control = None
        elif upper == 'V':
            if builder.current is None:
                raise CodecError('path data draws before its first move')
            y: float = number() + (base[1] if relative else 0.0)
            builder.line((builder.current[0], y))
            builder.last_cubic_control = None
            builder.last_quad_control = None
        elif upper == 'C':
            first = (base[0] + number(), base[1] + number())
            second = (base[0] + number(), base[1] + number())
            end = (base[0] + number(), base[1] + number())
            builder.cubic(first, second, end)
            builder.last_cubic_control = second
            builder.last_quad_control = None
        elif upper == 'S':
            if builder.current is None:
                raise CodecError('path data draws before its first move')
            current: tuple[float, float] = builder.current
            reflected: tuple[float, float] = current
            if builder.last_cubic_control is not None:
                reflected = (2.0 * current[0] - builder.last_cubic_control[0],
                             2.0 * current[1] - builder.last_cubic_control[1])
            second = (base[0] + number(), base[1] + number())
            end = (base[0] + number(), base[1] + number())
            builder.cubic(reflected, second, end)
            builder.last_cubic_control = second
            builder.last_quad_control = None
        elif upper == 'Q':
            control = (base[0] + number(), base[1] + number())
            end = (base[0] + number(), base[1] + number())
            builder.quad(control, end)
            builder.last_quad_control = control
            builder.last_cubic_control = None
        elif upper == 'T':
            if builder.current is None:
                raise CodecError('path data draws before its first move')
            current = builder.current
            control = current
            if builder.last_quad_control is not None:
                control = (2.0 * current[0] - builder.last_quad_control[0],
                           2.0 * current[1] - builder.last_quad_control[1])
            end = (base[0] + number(), base[1] + number())
            builder.quad(control, end)
            builder.last_quad_control = control
            builder.last_cubic_control = None
        elif upper == 'A':
            radius_x: float = number()
            radius_y: float = number()
            rotation: float = number()
            large: bool = flag()
            sweep: bool = flag()
            end = (base[0] + number(), base[1] + number())
            builder.arc(radius_x, radius_y, rotation, large, sweep, end)
            builder.last_cubic_control = None
            builder.last_quad_control = None
        else:
            raise CodecError(f'path command {command!r} is not admitted')


def rect_path(x: float, y: float, width: float, height: float, radius_x: float, radius_y: float) -> str:
    """A rect element as path data (SVG 1.1 section 9.2's equivalent path)."""
    if radius_x <= 0.0 and radius_y > 0.0:
        radius_x = radius_y
    if radius_y <= 0.0 and radius_x > 0.0:
        radius_y = radius_x
    radius_x = min(radius_x, width / 2.0)
    radius_y = min(radius_y, height / 2.0)
    if radius_x <= 0.0:
        return f'M{x} {y}H{x + width}V{y + height}H{x}Z'
    return (f'M{x + radius_x} {y}H{x + width - radius_x}'
            f'A{radius_x} {radius_y} 0 0 1 {x + width} {y + radius_y}V{y + height - radius_y}'
            f'A{radius_x} {radius_y} 0 0 1 {x + width - radius_x} {y + height}H{x + radius_x}'
            f'A{radius_x} {radius_y} 0 0 1 {x} {y + height - radius_y}V{y + radius_y}'
            f'A{radius_x} {radius_y} 0 0 1 {x + radius_x} {y}Z')


# --- files -----------------------------------------------------------------

def local_name(tag: str) -> str:
    return tag[len(SVG_NAMESPACE):] if tag.startswith(SVG_NAMESPACE) else tag


def parse_svg(text: str, grid: int, where: str) -> list[Layer]:
    """The layers of one Heroicons file whose grid must be `grid`."""
    if '<!' in text:
        raise CodecError(f'{where}: declarations and entities are not admitted')
    root: ElementTree.Element = ElementTree.fromstring(text)
    if local_name(root.tag) != 'svg':
        raise CodecError(f'{where}: the root is not svg')
    root_fill: str = 'currentColor'
    root_stroke: str | None = None
    root_width: float = 1.0
    view_box: str | None = None
    for key, value in root.attrib.items():
        if key == 'viewBox':
            view_box = value
        elif key == 'fill':
            root_fill = value
        elif key == 'stroke':
            root_stroke = value
        elif key == 'stroke-width':
            root_width = float(value)
        elif key not in INERT_ROOT:
            raise CodecError(f'{where}: svg attribute {key!r} is not admitted')
    if view_box is None or [float(part) for part in view_box.split()] != [0.0, 0.0, float(grid), float(grid)]:
        raise CodecError(f'{where}: viewBox {view_box!r} is not 0 0 {grid} {grid}')
    layers: list[Layer] = []
    for element in root:
        tag: str = local_name(element.tag)
        fill: str = root_fill
        stroke: str | None = root_stroke
        width: float = root_width
        rule: str = 'nonzero'
        cap: str = 'butt'
        join: str = 'miter'
        data: str
        if tag == 'path':
            data = element.attrib.get('d', '')
            admitted: frozenset[str] = frozenset(
                {'d', 'fill', 'stroke', 'stroke-width', 'fill-rule', 'clip-rule', 'stroke-linecap', 'stroke-linejoin'})
        elif tag == 'rect':
            attributes: dict[str, float] = {key: float(element.attrib.get(key, '0'))
                                            for key in ('x', 'y', 'width', 'height', 'rx', 'ry')}
            data = rect_path(attributes['x'], attributes['y'], attributes['width'], attributes['height'],
                             attributes['rx'], attributes['ry'])
            admitted = frozenset({'x', 'y', 'width', 'height', 'rx', 'ry', 'fill', 'stroke', 'stroke-width',
                                  'stroke-linecap', 'stroke-linejoin'})
        else:
            raise CodecError(f'{where}: element {tag!r} is not admitted')
        for key, value in element.attrib.items():
            if key not in admitted:
                raise CodecError(f'{where}: {tag} attribute {key!r} is not admitted')
            if key == 'fill':
                fill = value
            elif key == 'stroke':
                stroke = value
            elif key == 'stroke-width':
                width = float(value)
            elif key == 'fill-rule':
                rule = value
            elif key == 'stroke-linecap':
                cap = value
            elif key == 'stroke-linejoin':
                join = value
        if len(element):
            raise CodecError(f'{where}: {tag} has children')
        layer: Layer = Layer()
        stroked: bool = stroke is not None and stroke != 'none'
        filled: bool = fill != 'none'
        if stroked == filled:
            raise CodecError(f'{where}: a {tag} must be filled or stroked, not both or neither')
        if filled:
            if fill != 'currentColor':
                raise CodecError(f'{where}: fill {fill!r} is not currentColor')
            if rule not in ('nonzero', 'evenodd'):
                raise CodecError(f'{where}: fill-rule {rule!r} is not admitted')
            layer.paint = PAINT_EVEN_ODD if rule == 'evenodd' else PAINT_NONZERO
        else:
            if stroke != 'currentColor':
                raise CodecError(f'{where}: stroke {stroke!r} is not currentColor')
            if cap not in CAPS:
                raise CodecError(f'{where}: stroke-linecap {cap!r} is not admitted')
            if join != 'round':
                raise CodecError(f'{where}: stroke-linejoin {join!r} is not round')
            if not (0.0 < width <= 8.0):
                raise CodecError(f'{where}: stroke-width {width} is out of range')
            layer.paint = PAINT_STROKE
            layer.stroke_width = width
            layer.cap = CAPS[cap]
        parse_path(data, layer)
        if not layer.verbs:
            raise CodecError(f'{where}: a {tag} draws nothing')
        layers.append(layer)
    if not layers:
        raise CodecError(f'{where}: the icon draws nothing')
    return layers


def read_variants(source: Path) -> list[Variant]:
    variants: list[Variant] = []
    for directory, style, grid in VARIANT_DIRECTORIES:
        folder: Path = source / directory
        if not folder.is_dir():
            raise CodecError(f'{folder} is missing')
        for path in sorted(folder.glob('*.svg')):
            name: str = path.stem
            if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', name):
                raise CodecError(f'{path}: the name is not lowercase words joined by hyphens')
            layers: list[Layer] = parse_svg(path.read_text(encoding='utf-8'), grid, str(path))
            variants.append(Variant(name, style, grid, layers))
    variants.sort(key=variant_key)
    return variants


def variant_key(variant: Variant) -> tuple[str, int, int]:
    return (variant.name, variant.style, variant.grid)


# --- records ---------------------------------------------------------------

@dataclass
class Records:
    names: list[str]
    variants: list[tuple[int, int, int, int, int]]
    layers: list[tuple[int, int, int, int, int, int]]
    verbs: list[int]
    coordinates: list[int]


def quantized(value: float, where: str) -> int:
    scaled: int = round(value * SCALE)
    if abs(scaled) > COORDINATE_LIMIT:
        raise CodecError(f'{where}: coordinate {value} is outside the 16-bit record range')
    return scaled


def encode(variants: list[Variant]) -> Records:
    names: list[str] = sorted({variant.name for variant in variants})
    index_of: dict[str, int] = {name: index for index, name in enumerate(names)}
    records: Records = Records(names, [], [], [], [])
    for variant in variants:
        first_layer: int = len(records.layers)
        where: str = f'{variant.name} {variant.grid}/{"solid" if variant.style else "outline"}'
        for layer in variant.layers:
            first_verb: int = len(records.verbs)
            first_coordinate: int = len(records.coordinates)
            records.verbs.extend(layer.verbs)
            for x, y in layer.points:
                records.coordinates.append(quantized(x, where))
                records.coordinates.append(quantized(y, where))
            width: int = round(layer.stroke_width * SCALE) if layer.paint == PAINT_STROKE else 0
            records.layers.append((first_verb, len(layer.verbs), first_coordinate, layer.paint, width, layer.cap))
        records.variants.append((index_of[variant.name], variant.style, variant.grid, first_layer,
                                 len(variant.layers)))
    return records


def decode_variant(records: Records, variant_index: int) -> list[Layer]:
    """The layers of one variant read back from records (points in grid
    units, as quantized)."""
    _, _, _, first_layer, layer_count = records.variants[variant_index]
    layers: list[Layer] = []
    for layer_index in range(first_layer, first_layer + layer_count):
        first_verb, verb_count, first_coordinate, paint, width, cap = records.layers[layer_index]
        layer: Layer = Layer(paint=paint, stroke_width=width / SCALE, cap=cap)
        coordinate: int = first_coordinate
        for verb in records.verbs[first_verb:first_verb + verb_count]:
            layer.verbs.append(verb)
            for _ in range(POINTS_FOR[verb]):
                layer.points.append((records.coordinates[coordinate] / SCALE,
                                     records.coordinates[coordinate + 1] / SCALE))
                coordinate += 2
        layers.append(layer)
    return layers


def wrapped(values: list[int], per_line: int) -> str:
    lines: list[str] = []
    for start in range(0, len(values), per_line):
        lines.append('    ' + ','.join(str(value) for value in values[start:start + per_line]) + ',')
    return '\n'.join(lines)


def render(records: Records) -> str:
    """The generated .inc text."""
    parts: list[str] = [
        '// Generated by tools/stock_icon_codec.py from ' + SOURCE_NAME + '; do not edit.',
        '// The record layout is stock_icon_records.hpp; the licence and provenance',
        '// are assets/icons/heroicons/.',
        '// clang-format off',
        '',
        'namespace gui_forms::detail::stock_icon_data {',
        '',
        f'inline constexpr std::size_t name_count = {len(records.names)}U;',
        f'inline constexpr std::size_t variant_count = {len(records.variants)}U;',
        f'inline constexpr std::size_t layer_count = {len(records.layers)}U;',
        f'inline constexpr std::size_t verb_count = {len(records.verbs)}U;',
        f'inline constexpr std::size_t coordinate_count = {len(records.coordinates)}U;',
        '',
        '// The icon names, sorted.',
        'inline constexpr std::array<std::string_view, name_count> names{',
    ]
    for name in records.names:
        parts.append(f'    "{name}",')
    parts.append('};')
    parts.append('')
    parts.append('// Name index, style, grid, first layer, layer count; sorted by name, style, grid.')
    parts.append('inline constexpr std::array<StockIconVariantRecord, variant_count> variants{{')
    for name, style, grid, first, count in records.variants:
        parts.append(f'    {{{name}U, {style}U, {grid}U, {first}U, {count}U}},')
    parts.append('}};')
    parts.append('')
    parts.append('// First verb, verb count, first coordinate, paint, stroke width (thousandths), cap.')
    parts.append('inline constexpr std::array<StockIconLayerRecord, layer_count> layers{{')
    for first_verb, verb_count, first_coordinate, paint, width, cap in records.layers:
        parts.append(f'    {{{first_verb}U, {verb_count}U, {first_coordinate}U, {paint}U, {width}U, {cap}U}},')
    parts.append('}};')
    parts.append('')
    parts.append('inline constexpr std::array<std::uint8_t, verb_count> verbs{')
    parts.append(wrapped(records.verbs, 40))
    parts.append('};')
    parts.append('')
    parts.append('// Thousandths of a grid unit, x then y.')
    parts.append('inline constexpr std::array<std::int16_t, coordinate_count> coordinates{')
    parts.append(wrapped(records.coordinates, 20))
    parts.append('};')
    parts.append('')
    parts.append('} // namespace gui_forms::detail::stock_icon_data')
    parts.append('// clang-format on')
    parts.append('')
    return '\n'.join(parts)


# --- reading the generated file back (for the tests) ------------------------

def read_generated(text: str) -> Records:
    """Records parsed back from render()'s output."""
    def block(name: str) -> str:
        match: re.Match[str] | None = re.search(name + r'\{\{?\n(.*?)\n\}\}?;', text, re.S)
        if match is None:
            raise CodecError(f'the generated file lacks {name}')
        return match.group(1)

    names: list[str] = re.findall(r'"([^"]+)"', block(r'std::array<std::string_view, name_count> names'))
    variants: list[tuple[int, int, int, int, int]] = [
        tuple(int(value) for value in re.findall(r'(\d+)U', line))  # type: ignore[misc]
        for line in block(r'std::array<StockIconVariantRecord, variant_count> variants').splitlines()]
    layers: list[tuple[int, int, int, int, int, int]] = [
        tuple(int(value) for value in re.findall(r'(\d+)U', line))  # type: ignore[misc]
        for line in block(r'std::array<StockIconLayerRecord, layer_count> layers').splitlines()]
    verbs: list[int] = [int(value) for value in re.findall(r'-?\d+', block(r'std::array<std::uint8_t, verb_count> verbs'))]
    coordinates: list[int] = [int(value) for value in
                              re.findall(r'-?\d+', block(r'std::array<std::int16_t, coordinate_count> coordinates'))]
    return Records(names, variants, layers, verbs, coordinates)


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('action', choices=('generate', 'check'))
    parser.add_argument('--source', type=Path, default=SOURCE,
                        help="the SVG tree in upstream's optimized/ layout (default assets/icons/heroicons)")
    parser.add_argument('--output', type=Path, required=True, help='the generated .inc (in the build tree)')
    options: argparse.Namespace = parser.parse_args(arguments)
    try:
        records: Records = encode(read_variants(options.source))
    except CodecError as error:
        print(f'stock_icon_codec: {error}', file=sys.stderr)
        return 1
    text: str = render(records)
    if options.action == 'check':
        current: str = options.output.read_text(encoding='utf-8') if options.output.is_file() else ''
        if current != text:
            print(f'stock_icon_codec: {options.output} is stale; run generate', file=sys.stderr)
            return 1
        print(f'stock_icon_codec: {options.output} is current')
        return 0
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(text, encoding='utf-8')
    print(f'stock_icon_codec: {len(records.names)} names, {len(records.variants)} variants, '
          f'{len(records.layers)} layers, {len(records.verbs)} verbs, {len(records.coordinates)} coordinates')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
