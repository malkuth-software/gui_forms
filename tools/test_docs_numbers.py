#!/usr/bin/env python3
"""Tests for the documentation's one table of numbers (docs/conf.py), the
README generator (tools/readme_numbers.py), the source links and the preview
directive (docs/_ext/gf_preview.py). None needs Sphinx."""
from __future__ import annotations

import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

ROOT: Path = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import readme_numbers  # noqa: E402


def load(path: Path, name: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


conf = load(ROOT / "docs" / "conf.py", "gf_docs_conf_under_test")
preview = load(ROOT / "docs" / "_ext" / "gf_preview.py", "gf_preview_under_test")

NAMES = ("gf_version", "gf_c_abi", "gf_host_protocol", "gf_drawing_abi", "gf_media")


def fixture(root: Path, files: dict[str, str]) -> Path:
    for relative, text in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


SOURCES: dict[str, str] = {
    "CMakeLists.txt": ("    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING \"\")\n"
                       "project(GUIForms VERSION 2.3.1 LANGUAGES C CXX)\n"
                       "add_compile_definitions(_WIN32_WINNT=0x0A00)\n"),
    "include/gui_forms/c/gf_abi.h": "#define GF_ABI_MAJOR 1u\n#define GF_ABI_MINOR 7u\n",
    "include/gui_forms/host/types/host_capabilities/host_capabilities.hpp": (
        "inline constexpr std::array<HostProtocolRevision, 2> host_protocol_revisions{\n"
        "    {{8U, \"a\"},\n    {9U, \"b {nested}\"},\n}};\n"),
    "include/gui_forms/drawing_c_api.h": (
        "#define GD_ABI_VERSION_0_7 UINT32_C(0x00000007)\n"
        "/* 1.0, the seal. */\n#define GD_ABI_VERSION_1_0 UINT32_C(0x00010000)\n"
        "/* 1.1, provisional, appends something. */\n#define GD_ABI_VERSION_1_1 UINT32_C(0x00010001)\n"),
}


class TableTests(unittest.TestCase):
    def test_the_table_has_exactly_the_agreed_names(self) -> None:
        self.assertEqual(tuple(conf.GF_NUMBERS), NAMES)
        self.assertEqual(conf.myst_substitutions, conf.GF_NUMBERS)
        self.assertIn("substitution", conf.myst_enable_extensions)
        prolog = re.findall(r"^\.\. \|(\w+)\| replace:: (\S+)$", conf.rst_prolog, re.M)
        self.assertEqual(dict(prolog), conf.GF_NUMBERS)

    def test_each_number_is_read_from_its_source(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = fixture(Path(scratch), SOURCES)
            numbers = conf.read_numbers(root, {})
            self.assertEqual(numbers["gf_version"], "2.3")
            self.assertEqual(numbers["gf_c_abi"], "1.7")
            self.assertEqual(numbers["gf_host_protocol"], "9")
            self.assertEqual(numbers["gf_drawing_abi"], "1.0", "a provisional version is not shown")
            self.assertEqual(conf.read_minimum_platforms(root), {"macos": "14", "windows": "10"})

    def test_a_missing_number_stops_the_build(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            broken = dict(SOURCES)
            broken["include/gui_forms/c/gf_abi.h"] = "#define GF_ABI_MAJOR 1u\n"
            root = fixture(Path(scratch), broken)
            with self.assertRaisesRegex(ValueError, "GF_ABI_MINOR"):
                conf.read_numbers(root, {})

    def test_the_media_base_follows_the_pinned_capture(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            docs = Path(scratch)
            base = f"https://raw.githubusercontent.com/{conf.PREVIEWS_REPOSITORY}/main"
            self.assertEqual(conf.read_media_base(docs, {}), base)
            (docs / "previews.json").write_text('{"capture": "abc1234", "images": {}}', encoding="utf-8")
            self.assertEqual(conf.read_media_base(docs, {}), base + "/abc1234")
            self.assertEqual(conf.read_media_base(docs, {"GF_MEDIA": "file:///tmp/p/"}), "file:///tmp/p")
            (docs / "previews.json").write_text('{"capture": "not a commit"}', encoding="utf-8")
            with self.assertRaises(ValueError):
                conf.read_previews_pin(docs)


class SourceLinkTests(unittest.TestCase):
    def test_the_commit_comes_from_the_build_service_first(self) -> None:
        self.assertEqual(conf.resolve_commit({"READTHEDOCS_GIT_COMMIT_HASH": "ABCDEF1", "GITHUB_SHA": "1234567"},
                                             ROOT), "abcdef1")
        self.assertEqual(conf.resolve_commit({"GITHUB_SHA": "1234567"}, ROOT), "1234567")
        with tempfile.TemporaryDirectory() as scratch:
            self.assertEqual(conf.resolve_commit({"GITHUB_SHA": "not hex"}, Path(scratch)), "main")

    def test_a_cpp_name_links_its_header_line_at_the_commit(self) -> None:
        locations = {"gui_forms::Box::put": ("include/gui_forms/box.hpp", 12)}
        url = conf.linkcode_resolve("cpp", {"names": ["::gui_forms::Box<T>::put"]}, locations)
        self.assertEqual(url, f"{conf.GF_BLOB}/include/gui_forms/box.hpp#L12")
        self.assertIsNone(conf.linkcode_resolve("cpp", {"names": ["gui_forms::Other"]}, locations))
        self.assertIsNone(conf.linkcode_resolve("py", {"module": "x"}, locations))
        self.assertIsNone(conf.linkcode_resolve("cpp", {"names": ""}, locations))

    def test_the_src_role_points_at_the_commit(self) -> None:
        self.assertEqual(conf.extlinks["src"], (f"{conf.GF_BLOB}/%s", "%s"))
        self.assertIn("sphinx.ext.linkcode", conf.extensions)
        self.assertIn("page-source", conf.html_theme_options["secondary_sidebar_items"]["**"])
        self.assertTrue((ROOT / "docs/_templates/page-source.html").is_file())


class PreviewDirectiveTests(unittest.TestCase):
    previews = {"controls/button": {"width": 240, "height": 81, "bytes": 9000}}

    def test_a_pinned_preview_is_shown_at_half_size_with_its_alt_text(self) -> None:
        markup = preview.preview_html("controls/button", "A \"Button\"", "https://m/abc", self.previews, "Caption")
        self.assertIn('src="https://m/abc/controls/button.png"', markup)
        self.assertIn('alt="A &quot;Button&quot;"', markup)
        self.assertIn('width="120" height="40"', markup)
        self.assertIn("<figcaption>Caption</figcaption>", markup)
        self.assertIn('onerror="this.parentNode.remove()"', markup)

    def test_a_preview_not_captured_yet_is_left_out(self) -> None:
        self.assertEqual(preview.preview_html("controls/label", "A Label", "https://m", self.previews), "")
        self.assertEqual(preview.preview_html("controls/button", "A Button", "", self.previews), "")

    def test_alt_text_and_a_layout_name_are_required(self) -> None:
        with self.assertRaisesRegex(ValueError, "alt"):
            preview.preview_html("controls/button", " ", "https://m", self.previews)
        with self.assertRaisesRegex(ValueError, "not a preview name"):
            preview.preview_html("../Button", "A Button", "https://m", self.previews)


README = """# GUI.Forms

![C++20](x) ![Platforms](https://img.shields.io/badge/platforms-macOS-lightgrey.svg)

<!-- numbers:start -->
- **Status:** the package version is 0.5 (C ABI 1.3, host protocol 14,
  drawing ABI 0.9). 1.0 is next.
<!-- numbers:end -->

| Platform | Host | Notes |
|---|---|---|
| macOS 13+ | AppKit | old |

<!-- numbers:start -->
find_package(GUIForms 0.5 CONFIG REQUIRED)
<!-- numbers:end -->
"""
NUMBERS = {"gf_version": "0.6", "gf_c_abi": "1.5", "gf_host_protocol": "15", "gf_drawing_abi": "1.0",
           "gf_media": "https://m"}
ROWS = readme_numbers.platform_rows(conf.PLATFORMS, {"macos": "14", "windows": "10"})


class ReadmeTests(unittest.TestCase):
    def test_regions_table_and_badge_are_written_from_the_table(self) -> None:
        text = readme_numbers.render(README, NUMBERS, ROWS)
        self.assertIn("the package version is 0.6 (C ABI 1.5, host protocol 15,\n  drawing ABI 1.0). 1.0 is next.", text)
        self.assertIn("find_package(GUIForms 0.6 CONFIG", text)
        self.assertIn("| macOS 14+ | AppKit | Apple silicon (arm64) |\n| Windows 10+ |", text)
        self.assertIn("platforms-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg", text)
        self.assertEqual(readme_numbers.render(text, NUMBERS, ROWS), text, "a second run changes nothing")

    def test_a_number_typed_outside_a_region_is_refused(self) -> None:
        with self.assertRaisesRegex(readme_numbers.MarkerError, "C ABI 1.3"):
            readme_numbers.render(README + "\nNow on C ABI 1.3.\n", NUMBERS, ROWS)

    def test_markers_must_be_there_and_name_the_version_and_the_c_abi(self) -> None:
        with self.assertRaisesRegex(readme_numbers.MarkerError, "no <!-- numbers:start"):
            readme_numbers.render(README.replace("<!-- numbers:start -->", "").replace("<!-- numbers:end -->", "")
                                  .replace("C ABI 1.3", "").replace("package version is 0.5", "")
                                  .replace("host protocol 14", "").replace("drawing ABI 0.9", "")
                                  .replace("find_package(GUIForms 0.5", ""), NUMBERS, ROWS)
        with self.assertRaisesRegex(readme_numbers.MarkerError, "gf_c_abi"):
            readme_numbers.render(README.replace("C ABI 1.3, ", ""), NUMBERS, ROWS)
        with self.assertRaisesRegex(readme_numbers.MarkerError, "platform table"):
            readme_numbers.render(README.replace("| Platform | Host | Notes |\n", ""), NUMBERS, ROWS)

    def test_the_committed_readme_is_current(self) -> None:
        self.assertEqual(readme_numbers.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
