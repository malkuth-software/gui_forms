#!/usr/bin/env python3
"""Tests for check_platform_headers.py: which headers are platform headers,
which directories are scanned, skipped comments, and the shrink-only
allowance."""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import check_platform_headers

SOURCE: str = """#include <pthread.h>
#  include <Windows.h>
#import <AppKit/AppKit.h>
#include <sys/mman.h>
#include "windows.h"
#include <vector>
#include <cstdio>
#include <sanitizer/asan_interface.h>
#include <stxlib/threads.hpp>
#include "net/own_header.hpp"
// #include <unistd.h>
/* #include <dlfcn.h>
#include <mach/mach.h> */
#include <X11/Xlib.h>
"""


class HeaderTests(unittest.TestCase):
    def test_platform_names(self) -> None:
        for name in ("pthread.h", "windows.h", "Windows.h", "unistd.h", "dlfcn.h", "sys/stat.h", "mach-o/dyld.h",
                     "X11/Xlib.h", "Cocoa/Cocoa.h", "CoreText/CoreText.h", "emscripten/threading.h", "psapi.h",
                     "wayland-client.h", "elf.h", "link.h"):
            self.assertTrue(check_platform_headers.is_platform_header(name), name)

    def test_standard_and_compiler_names(self) -> None:
        for name in ("vector", "cstdio", "stdio.h", "signal.h", "time.h", "errno.h", "locale.h", "stdatomic.h",
                     "sanitizer/asan_interface.h", "arm_neon.h", "immintrin.h", "stxlib/threads.hpp",
                     "gui_forms/threading.hpp"):
            self.assertFalse(check_platform_headers.is_platform_header(name), name)

    def test_prefixes_need_angle_brackets(self) -> None:
        self.assertTrue(check_platform_headers.is_platform_header("sys/types.h", True))
        self.assertFalse(check_platform_headers.is_platform_header("sys/types.h", False))
        self.assertTrue(check_platform_headers.is_platform_header("windows.h", False))

    def test_includes_found_with_lines(self) -> None:
        found: list[tuple[int, str]] = check_platform_headers.platform_includes(SOURCE)
        self.assertEqual(found, [(1, "pthread.h"), (2, "Windows.h"), (3, "AppKit/AppKit.h"), (4, "sys/mman.h"),
                                 (5, "windows.h"), (14, "X11/Xlib.h")])


class DirectoryTests(unittest.TestCase):
    def test_platform_directories(self) -> None:
        for path in ("src/surface/windows/a.cpp", "src/surface/macos/view/b.mm", "src/surface/linux/c.cpp",
                     "src/surface/posix/d.cpp", "src/surface/web/e.cpp"):
            self.assertTrue(check_platform_headers.in_platform_directory(path), path)
        for path in ("src/host/windows/a.cpp", "src/host/macos/b.mm", "src/host/surface/b.cpp",
                     "src/core/a.cpp", "src/surface/windowsx/a.cpp", "src/surface/README.md",
                     "include/gui_forms/a.hpp"):
            self.assertFalse(check_platform_headers.in_platform_directory(path), path)


class AllowanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "src" / "core").mkdir(parents=True)
        (self.root / "src" / "surface" / "windows").mkdir(parents=True)
        (self.root / "src" / "host" / "windows").mkdir(parents=True)
        (self.root / "tests").mkdir()
        (self.root / "src" / "core" / "a.cpp").write_text("#include <pthread.h>\n#include <unistd.h>\n")
        (self.root / "src" / "surface" / "windows" / "b.cpp").write_text("#include <windows.h>\n")
        (self.root / "src" / "host" / "windows" / "d.cpp").write_text("#include <windows.h>\n")
        (self.root / "tests" / "c.cpp").write_text("#include <pthread.h>\n")
        self.allowance = self.root / "allowance.json"

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_empty_allowance_refuses_core_and_host(self) -> None:
        findings: list[str] = check_platform_headers.check(self.root, self.allowance)
        self.assertEqual(len(findings), 3)
        self.assertEqual(sum(1 for finding in findings if finding.startswith("src/core/a.cpp:")), 2)
        self.assertEqual(sum(1 for finding in findings if finding.startswith("src/host/windows/d.cpp:")), 1)

    def test_surface_and_tests_are_not_scanned(self) -> None:
        found: dict[str, list[tuple[int, str]]] = check_platform_headers.scan(self.root)
        self.assertEqual(sorted(found), ["src/core/a.cpp", "src/host/windows/d.cpp"])

    def test_allowance_admits_and_shrinks_only(self) -> None:
        check_platform_headers.write_allowance(self.allowance, {"src/core/a.cpp": 2, "src/host/windows/d.cpp": 1})
        self.assertEqual(check_platform_headers.check(self.root, self.allowance), [])
        (self.root / "src" / "core" / "a.cpp").write_text("#include <pthread.h>\n")
        stale: list[str] = check_platform_headers.check(self.root, self.allowance)
        self.assertEqual(len(stale), 1)
        self.assertIn("lower it", stale[0])
        self.assertEqual(check_platform_headers.update(self.root, self.allowance), [])
        self.assertEqual(check_platform_headers.load_allowance(self.allowance),
                         {"src/core/a.cpp": 1, "src/host/windows/d.cpp": 1})

    def test_update_refuses_to_raise(self) -> None:
        check_platform_headers.write_allowance(self.allowance, {"src/core/a.cpp": 1, "src/host/windows/d.cpp": 1})
        refusals: list[str] = check_platform_headers.update(self.root, self.allowance)
        self.assertEqual(len(refusals), 1)
        self.assertEqual(check_platform_headers.load_allowance(self.allowance),
                         {"src/core/a.cpp": 1, "src/host/windows/d.cpp": 1})


if __name__ == "__main__":
    unittest.main()
