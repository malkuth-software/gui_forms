#!/usr/bin/env python3
"""Record stage T5's gate for one host and compositor state.

Stage T5's gate (notes:planning/stages/T5-compositor/PLAN.md §6) is recorded
per host and per compositor state. This tool runs the compositor lab
(demo/compositor_lab.hpp) once for each backdrop request, each run with
``--capture`` so that the host saves the presented framebuffer with its
alpha, and writes one JSON line per run:

- ``record``: the lab's own line for the grant in force once the host was
  ready, and for each later change: the host's capabilities
  (HostCapabilities::to_json), the compositor state, the request, the grant,
  its reason and its generation;
- ``capture``: the saved frame's path, size and alpha (minimum, maximum and
  the count of non-opaque pixels), decoded here;
- the command and its exit status.

The first line describes the machine (platform, OS release and build, Python's
view of the processor) and the lab's path. The host's read-back of what it
applied comes from the host's own compositor test
(gui_forms_windows_compositor_tests, gui_forms_macos_compositor_tests,
gui_forms_x11_compositor_probe); pass ``--host-test`` to run it after the lab
and append its JSON lines.

Usage:
    python3 -B tools/t5_gate_record.py --lab <lab executable> --out <file.jsonl>
        [--state <name>] [--host-test <executable>] [--launcher '<program>;<argument>;...']

``--state`` names the compositor state the machine was put in (for example
``granted``, ``transparency-off``, ``high-contrast``); it is written on every
line. ``--launcher`` prefixes each command (for example a script that runs it
on a private desktop). Exit status 0 when every run rendered and saved its
frame, 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import platform
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

# The requests the lab is run with, as --backdrop values; the same ones
# tests/compositor_lab_tests.cpp checks headlessly.
REQUESTS = (
    'window_material,transparent',
    'transient_material,transparent',
    'transparent',
    'opaque',
)

RUN_SECONDS = 60
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


def png_alpha(path: Path) -> dict:
    """The alpha of an 8-bit RGBA or RGB PNG: minimum, maximum, non-opaque count."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f'{path} is not a PNG')
    offset = len(PNG_SIGNATURE)
    width = height = depth = colour_type = interlace = 0
    compressed = bytearray()
    while offset + 8 <= len(data):
        length, kind = struct.unpack('>I4s', data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        offset = offset + 12 + length
        if kind == b'IHDR':
            width, height, depth, colour_type, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            compressed.extend(body)
        elif kind == b'IEND':
            break
    if depth != 8 or interlace != 0 or colour_type not in (2, 6):
        raise ValueError(f'{path}: only 8-bit, non-interlaced RGB or RGBA is read '
                         f'(depth {depth}, colour type {colour_type}, interlace {interlace})')
    channels = 4 if colour_type == 6 else 3
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    if len(raw) != height * (stride + 1):
        raise ValueError(f'{path}: {len(raw)} bytes of image data for {width} x {height}')
    if channels == 3:
        return {'width': width, 'height': height, 'alpha_minimum': 255, 'alpha_maximum': 255,
                'non_opaque': 0, 'colour_type': colour_type}
    previous = bytearray(stride)
    minimum, maximum, non_opaque = 255, 0, 0
    for row in range(height):
        start = row * (stride + 1)
        filter_type = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        unfilter(line, previous, filter_type, channels)
        alphas = line[3::4]
        minimum = min(minimum, min(alphas))
        maximum = max(maximum, max(alphas))
        non_opaque += len(alphas) - alphas.count(255)
        previous = line
    return {'width': width, 'height': height, 'alpha_minimum': minimum, 'alpha_maximum': maximum,
            'non_opaque': non_opaque, 'colour_type': colour_type}


def unfilter(line: bytearray, previous: bytearray, filter_type: int, channels: int) -> None:
    """Undoes one PNG scanline filter in place (PNG specification §9)."""
    if filter_type == 0:
        return
    for index in range(len(line)):
        left = line[index - channels] if index >= channels else 0
        up = previous[index]
        upper_left = previous[index - channels] if index >= channels else 0
        if filter_type == 1:
            predictor = left
        elif filter_type == 2:
            predictor = up
        elif filter_type == 3:
            predictor = (left + up) // 2
        elif filter_type == 4:
            estimate = left + up - upper_left
            distance_left = abs(estimate - left)
            distance_up = abs(estimate - up)
            distance_upper_left = abs(estimate - upper_left)
            if distance_left <= distance_up and distance_left <= distance_upper_left:
                predictor = left
            elif distance_up <= distance_upper_left:
                predictor = up
            else:
                predictor = upper_left
        else:
            raise ValueError(f'unknown PNG filter {filter_type}')
        line[index] = (line[index] + predictor) & 0xFF


def json_lines(text: str) -> list:
    """The lines of `text` that parse as JSON objects."""
    found = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith('{'):
            continue
        try:
            found.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return found


def run(command: list) -> subprocess.CompletedProcess:
    completed = subprocess.run(command, capture_output=True, text=True, timeout=RUN_SECONDS, check=False)
    return completed


def main() -> int:
    parser = argparse.ArgumentParser(description='Record stage T5\'s gate for one host and state.')
    parser.add_argument('--lab', required=True, type=Path, help='the compositor lab executable')
    parser.add_argument('--out', required=True, type=Path, help='the JSON lines file to write')
    parser.add_argument('--state', default='as-found', help='the compositor state the machine is in')
    parser.add_argument('--host-test', type=Path, help='the host\'s compositor test, run after the lab')
    parser.add_argument('--launcher', default='',
                        help='a command prefix for every run, its words separated by semicolons '
                             '(as GUI_FORMS_FOREGROUND_LAUNCHER spells it)')
    arguments = parser.parse_args()
    launcher = [word for word in arguments.launcher.split(';') if word]

    lines = [{
        'machine': {
            'platform': platform.platform(),
            'system': platform.system(),
            'release': platform.release(),
            'version': platform.version(),
            'processor': platform.processor() or platform.machine(),
        },
        'state': arguments.state,
        'lab': str(arguments.lab),
    }]
    passed = True
    with tempfile.TemporaryDirectory(prefix='t5_gate_') as scratch:
        for request in REQUESTS:
            capture = Path(scratch) / (request.replace(',', '+') + '.png')
            command = launcher + [str(arguments.lab), '--backdrop', request, '--capture', str(capture)]
            entry = {'state': arguments.state, 'request': request, 'command': command}
            try:
                completed = run(command)
            except subprocess.TimeoutExpired:
                entry['failure'] = f'no exit within {RUN_SECONDS} s'
                lines.append(entry)
                passed = False
                continue
            entry['exit'] = completed.returncode
            entry['records'] = [line for line in json_lines(completed.stdout) if line.get('lab') == 'compositor']
            saved = [line for line in json_lines(completed.stdout) if line.get('automation') == 'capture']
            entry['saved'] = bool(saved and saved[-1].get('saved'))
            if entry['saved'] and capture.exists():
                entry['capture'] = png_alpha(capture)
            else:
                entry['stderr'] = completed.stderr[-2000:]
                passed = False
            if completed.returncode != 0 or not entry['records']:
                passed = False
            lines.append(entry)
        if arguments.host_test is not None:
            command = launcher + [str(arguments.host_test)]
            entry = {'state': arguments.state, 'host_test': command}
            try:
                completed = run(command)
                entry['exit'] = completed.returncode
                entry['lines'] = json_lines(completed.stdout)
            except subprocess.TimeoutExpired:
                entry['failure'] = f'no exit within {RUN_SECONDS} s'
            lines.append(entry)

    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    with arguments.out.open('w', encoding='utf-8', newline='\n') as handle:
        for line in lines:
            handle.write(json.dumps(line, sort_keys=True) + '\n')
    for entry in lines[1:]:
        if 'request' in entry:
            records = entry.get('records') or [{}]
            last = records[-1]
            alpha = entry.get('capture', {})
            print(f"{entry['request']:32} -> {last.get('granted', '?'):18} {last.get('reason', '?'):22} "
                  f"alpha {alpha.get('alpha_minimum', '?')}..{alpha.get('alpha_maximum', '?')} "
                  f"non-opaque {alpha.get('non_opaque', '?')} exit {entry.get('exit', '?')}")
        else:
            print(f"host test exit {entry.get('exit', entry.get('failure'))}, {len(entry.get('lines', []))} lines")
    print(f'{"PASS" if passed else "FAIL"}: {arguments.out}')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
