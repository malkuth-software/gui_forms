#!/usr/bin/env python3
"""Tests for normative_trace.py compare on small traces, in each host's shape.

The macOS, X11 and Windows drivers write the same version 1 format; a
Windows trace may reach the tool with CRLF line ends or a byte order mark
(copied through a Windows tool). Each case writes a reference and a host
trace into a temporary directory and runs the compare command.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import normative_trace  # noqa: E402

REFERENCE: list[str] = [
    'normative-trace version=1 scale=1.00',
    'step 0 show',
    'receipt id=normative.left',
    'receipt id=normative.root',
    'step 1 click left',
    'pointer id=normative.left phase=target action=down',
    'focus id=normative.left state=on',
    'pointer id=normative.left phase=target action=up',
    'activate id=normative.left',
    'receipt id=normative.left',
    'step 2 type a',
    'key id=normative.left phase=target action=down code=4',
    'text id=normative.left value=a',
    'key id=normative.left phase=target action=up code=4',
    'display lines=1 digest=0123456789abcdef',
    'display| fill_rect 0 0 15360 10240 #f0f0f0ff',
    'end',
]


def trace_text(lines: list[str], line_end: str = '\n') -> str:
    text: str = line_end.join(lines) + line_end
    return text


class NormativeTraceCompareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / 'normative-1.00.trace').write_bytes(trace_text(REFERENCE).encode('utf-8'))

    def tearDown(self) -> None:
        self.directory.cleanup()

    def compare(self, data: bytes) -> int:
        trace: Path = self.root / 'host.trace'
        trace.write_bytes(data)
        arguments = argparse.Namespace(reference_dir=str(self.root), trace=str(trace), strict_hover=False)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status: int = normative_trace.command_compare(arguments)
        return status

    def test_identical_trace_matches(self) -> None:
        self.assertEqual(self.compare(trace_text(REFERENCE).encode('utf-8')), 0)

    def test_windows_line_ends_and_byte_order_mark_match(self) -> None:
        data: bytes = b'\xef\xbb\xbf' + trace_text(REFERENCE, '\r\n').encode('utf-8')
        self.assertEqual(self.compare(data), 0)

    def test_added_hover_and_receipts_match(self) -> None:
        # A host may add pointer moves, enters and leaves (a posted move on
        # Windows is followed by a leave when the real cursor is elsewhere)
        # and repaint more.
        host: list[str] = list(REFERENCE)
        host.insert(5, 'pointer id=normative.left phase=target action=move')
        host.insert(6, 'pointer id=normative.root phase=target action=leave')
        host.insert(11, 'receipt id=normative.root')
        self.assertEqual(self.compare(trace_text(host).encode('utf-8')), 0)

    def test_reordered_text_and_key_up_differ(self) -> None:
        host: list[str] = list(REFERENCE)
        host[12], host[13] = host[13], host[12]
        self.assertEqual(self.compare(trace_text(host).encode('utf-8')), 1)

    def test_missing_receipt_differs(self) -> None:
        host: list[str] = [line for line in REFERENCE if line != 'receipt id=normative.root']
        self.assertEqual(self.compare(trace_text(host).encode('utf-8')), 1)

    def test_scale_without_reference_is_unusable(self) -> None:
        host: list[str] = list(REFERENCE)
        host[0] = 'normative-trace version=1 scale=1.25'
        self.assertEqual(self.compare(trace_text(host).encode('utf-8')), 2)

    def test_unfinished_trace_is_unusable(self) -> None:
        self.assertEqual(self.compare(trace_text(REFERENCE[:-1]).encode('utf-8')), 2)


if __name__ == '__main__':
    unittest.main()
