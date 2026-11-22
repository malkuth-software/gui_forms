#!/usr/bin/env python3
"""Packs the lamps' light-plane bakes into the shipped store (GFLP).

The baker (tools/industrial_surfaces/lamp_keys.cpp, target gui_forms_lamp_keys)
writes each bake as float32 planes (`<stem>.f32`: int32 width, height and lamp
count; the room's light, each lamp's light at unit white power, and the
coverage, the light premultiplied by coverage). Those are 0.35 to 1 MB a bake
and are never committed. This tool stores each one as GUI.Forms ships it
(engine_light::read_light_plane_store; include/gui_forms/engine/light_planes/
light_planes.hpp):

    bytes 0..3   "GFLP"
    uint32       version, 1
    uint32       width, height
    uint32       lamp planes (1: the bake's lamps summed)
    float32      aperture: the share of the bake's half-width a round hole in
                 its panel shows (the indicator jewel's crown), 0 for a key
    binary16     the room plane, the lamp plane and the coverage plane,
                 width x height values each, grey, premultiplied as baked

all little-endian. Three reductions, each measured below:
- grey: the materials are untinted and the lamps baked white, so the three
  channels are equal (checked: the tool refuses a bake whose channels differ);
- the lamps summed: every lamp of a bake takes one drive in GUI.Forms
  (LightPlaneRaster), so the sum is exact (the study's CANCEL has two);
- binary16 (IEEE half precision, round to nearest even).

Usage:
    pack_light_planes.py pack <bake directory> <store directory> [stem ...]
        packs <stem>_up.f32 (and _down.f32 for keys) for each stem, by default
        the shipped set (SHIPPED below), and prints each store's size;
    pack_light_planes.py check <bake directory> <store directory> [stem ...]
        composites each bake and its store as the study's bake study does
        (cpp/bake_study/compare.cpp: tone 1 - e^(-1.3 v) over GOST 397,
        sRGB bytes) in its states, and prints the CIEDE2000 between them:
        worst, 99.9th percentile, mean and the share above 1.

The CMake target gui_forms_light_planes runs the baker and then `pack` into
assets/light_planes, so the shipped stores can always be made again.
"""

import math
import os
import struct
import sys
from array import array

MAGIC = b"GFLP"
VERSION = 1
HEADER = struct.Struct("<4sIIIIf")

# The indicator jewel's hole: the baker's indicator scene (lamp_keys.cpp,
# make_indicator) frames a 0.19-unit hole in a view of +/-2.6 tan(9.5 deg)
# units, so the hole's radius is this share of the bake's half-width
# (the study's demo.html and dialog_demo.cpp, place_indicator).
INDICATOR_APERTURE = 0.19 / (2.6 * math.tan(9.5 * math.pi / 180.0))

# The shipped bakes: stem, states, aperture. The indicators do not press
# (their down bake equals the up one), so only the up state ships.
SHIPPED = [
    ("dialog_ok", ("up", "down"), 0.0),
    ("dialog_cancel", ("up", "down"), 0.0),
    ("indicator_backed", ("up",), INDICATOR_APERTURE),
    ("indicator_clear", ("up",), INDICATOR_APERTURE),
]


def read_f32(path):
    data = open(path, "rb").read()
    width, height, lamps = struct.unpack("<iii", data[:12])
    if width <= 0 or height <= 0 or lamps < 0:
        raise SystemExit(f"{path}: bad header {width} x {height}, {lamps} lamps")
    floats = array("f")
    floats.frombytes(data[12:])
    if sys.byteorder != "little":
        floats.byteswap()
    pixels = width * height
    if len(floats) != pixels * (3 * (1 + lamps) + 1):
        raise SystemExit(f"{path}: length does not match its header")
    room = floats[: 3 * pixels]
    lamp_planes = [floats[3 * pixels * (i + 1): 3 * pixels * (i + 2)] for i in range(lamps)]
    coverage = floats[3 * pixels * (lamps + 1):]
    return width, height, room, lamp_planes, coverage


def grey_of(plane, path, name):
    out = array("f", plane[0::3])
    for channel in (1, 2):
        if plane[channel::3] != out:
            raise SystemExit(f"{path}: the {name} plane is not grey; the store holds grey planes only")
    return out


def pack_one(source, target, aperture):
    width, height, room, lamp_planes, coverage = read_f32(source)
    pixels = width * height
    room_grey = grey_of(room, source, "room")
    summed = array("f", [0.0]) * pixels
    for index, plane in enumerate(lamp_planes):
        grey = grey_of(plane, source, f"lamp {index}")
        for at in range(pixels):
            summed[at] += grey[at]
    out = bytearray(HEADER.pack(MAGIC, VERSION, width, height, 1, aperture))
    for plane in (room_grey, summed, coverage):
        out += struct.pack(f"<{pixels}e", *plane)
    with open(target, "wb") as file:
        file.write(out)
    return len(out)


def read_store(path):
    data = open(path, "rb").read()
    magic, version, width, height, lamps, aperture = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise SystemExit(f"{path}: not a version {VERSION} store")
    pixels = width * height
    values = struct.unpack_from(f"<{pixels * (2 + lamps)}e", data, HEADER.size)
    room = values[:pixels]
    lamp_planes = [values[pixels * (i + 1): pixels * (i + 2)] for i in range(lamps)]
    coverage = values[pixels * (1 + lamps):]
    return width, height, room, lamp_planes, coverage


# -- the measure (cpp/bake_study/compare.cpp) -----------------------------------------

def linear(byte):
    c = byte / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def encode(v):
    v = 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)
    s = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1.0 / 2.4) - 0.055
    return int(math.floor(s * 255.0 + 0.5))


BODY = (linear(0x4D), linear(0x67), linear(0x6D))


def lab(rgb):
    r, g, b = (linear(v) for v in rgb)
    x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047
    y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b
    z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883

    def f(t):
        return t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (24389.0 / 27.0 * t + 16.0) / 116.0

    return (116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z)))


def de2000(p, q):
    l1, a1, b1 = p
    l2, a2, b2 = q
    c1, c2 = math.hypot(a1, b1), math.hypot(a2, b2)
    c7 = ((c1 + c2) / 2.0) ** 7
    g = 0.5 * (1.0 - math.sqrt(c7 / (c7 + 25.0 ** 7)))
    ap1, ap2 = (1.0 + g) * a1, (1.0 + g) * a2
    cp1, cp2 = math.hypot(ap1, b1), math.hypot(ap2, b2)
    h1 = math.atan2(b1, ap1) % (2.0 * math.pi)
    h2 = math.atan2(b2, ap2) % (2.0 * math.pi)
    dl, dc = l2 - l1, cp2 - cp1
    dh = 0.0
    if cp1 * cp2 != 0.0:
        dh = h2 - h1
        if dh > math.pi:
            dh -= 2.0 * math.pi
        elif dh < -math.pi:
            dh += 2.0 * math.pi
    dhh = 2.0 * math.sqrt(cp1 * cp2) * math.sin(dh / 2.0)
    lm, cpm = (l1 + l2) / 2.0, (cp1 + cp2) / 2.0
    hm = h1 + h2
    if cp1 * cp2 != 0.0:
        if abs(h1 - h2) > math.pi:
            hm = (h1 + h2 + 2.0 * math.pi) / 2.0 if h1 + h2 < 2.0 * math.pi else (h1 + h2 - 2.0 * math.pi) / 2.0
        else:
            hm = (h1 + h2) / 2.0
    t = (1.0 - 0.17 * math.cos(hm - math.pi / 6.0) + 0.24 * math.cos(2.0 * hm)
         + 0.32 * math.cos(3.0 * hm + math.pi / 30.0) - 0.2 * math.cos(4.0 * hm - 63.0 * math.pi / 180.0))
    dt = 30.0 * math.pi / 180.0 * math.exp(-(((hm * 180.0 / math.pi) - 275.0) / 25.0) ** 2)
    cp7 = cpm ** 7
    rc = 2.0 * math.sqrt(cp7 / (cp7 + 25.0 ** 7))
    l50 = (lm - 50.0) ** 2
    sl = 1.0 + 0.015 * l50 / math.sqrt(20.0 + l50)
    sc = 1.0 + 0.045 * cpm
    sh = 1.0 + 0.015 * cpm * t
    rt = -math.sin(2.0 * dt) * rc
    return math.sqrt((dl / sl) ** 2 + (dc / sc) ** 2 + (dhh / sh) ** 2 + rt * (dc / sc) * (dhh / sh))


def composite(up, down, colour, power, press):
    """sRGB pixels of a grey bake (room, summed lamp, coverage per state)."""
    room_u, lamp_u, cover_u = up
    room_d, lamp_d, cover_d = down
    out = []
    for at in range(len(cover_u)):
        cover = cover_u[at] + (cover_d[at] - cover_u[at]) * press
        inverse = 1.0 / cover if cover > 1e-6 else 0.0
        pixel = []
        for c in range(3):
            a = room_u[at] + power * colour[c] * lamp_u[at]
            b = room_d[at] + power * colour[c] * lamp_d[at]
            lit = 1.0 - math.exp(-1.3 * (a + (b - a) * press) * inverse)
            pixel.append(encode(lit * cover + BODY[c] * (1.0 - cover)))
        out.append(tuple(pixel))
    return out


def grey_planes_of_f32(path):
    width, height, room, lamp_planes, coverage = read_f32(path)
    pixels = width * height
    summed = [0.0] * pixels
    for plane in lamp_planes:
        for at in range(pixels):
            summed[at] += plane[3 * at]
    return [room[3 * at] for at in range(pixels)], summed, list(coverage)


def grey_planes_of_store(path):
    width, height, room, lamp_planes, coverage = read_store(path)
    return list(room), list(lamp_planes[0]), list(coverage)


GREEN = (0.16, 1.0, 0.25)
AMBER = (1.0, 0.50, 0.08)
RED = (1.0, 0.12, 0.04)


def check(bakes, stores, chosen):
    for stem, states, _ in chosen:
        reference = {s: grey_planes_of_f32(os.path.join(bakes, f"{stem}_{s}.f32")) for s in states}
        stored = {s: grey_planes_of_store(os.path.join(stores, f"{stem}_{s}.gflp")) for s in states}
        down = "down" if "down" in states else "up"
        if "down" in states:
            cases = [(RED if "cancel" in stem else GREEN, power, press)
                     for power, press in ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (1.0, 0.5))]
        else:
            cases = [(colour, power, 0.0) for colour in (GREEN, AMBER, RED) for power in (0.0, 1.0, 1.6)]
        errors = []
        for colour, power, press in cases:
            a = composite(reference["up"], reference[down], colour, power, press)
            b = composite(stored["up"], stored[down], colour, power, press)
            for p, q in zip(a, b):
                errors.append(0.0 if p == q else de2000(lab(p), lab(q)))
        errors.sort()
        count = len(errors)
        worst = errors[-1]
        p999 = errors[min(count - 1, int(0.999 * count))]
        mean = sum(errors) / count
        over = 100.0 * sum(1 for e in errors if e > 1.0) / count
        print(f"{stem:18s} {len(cases)} states  worst {worst:.2f}  99.9th {p999:.2f}  mean {mean:.4f}  >1 {over:.3f}%")


def chosen_stems(names):
    if not names:
        return SHIPPED
    known = {stem: (stem, states, aperture) for stem, states, aperture in SHIPPED}
    out = []
    for name in names:
        if name not in known:
            raise SystemExit(f"unknown stem {name}; the shipped stems are {', '.join(known)}")
        out.append(known[name])
    return out


def main(argv):
    if len(argv) < 4 or argv[1] not in ("pack", "check"):
        print(__doc__)
        return 2
    mode, bakes, stores = argv[1], argv[2], argv[3]
    chosen = chosen_stems(argv[4:])
    if mode == "pack":
        os.makedirs(stores, exist_ok=True)
        total_in = total_out = 0
        for stem, states, aperture in chosen:
            for state in states:
                source = os.path.join(bakes, f"{stem}_{state}.f32")
                target = os.path.join(stores, f"{stem}_{state}.gflp")
                size = pack_one(source, target, aperture)
                total_in += os.path.getsize(source)
                total_out += size
                print(f"{stem}_{state}: {os.path.getsize(source)} -> {size} bytes")
        print(f"total {total_in} -> {total_out} bytes ({100.0 * total_out / total_in:.1f}%)")
    else:
        check(bakes, stores, chosen)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
