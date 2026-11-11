"""The program manifest validates, plans honestly, and names artifacts per platform."""
from __future__ import annotations

import copy
import io
import json
import contextlib
import tempfile
import unittest
from unittest import mock
from pathlib import Path
from typing import Any

import package_programs as pp


def manifest_data() -> dict[str, Any]:
    return json.loads(pp.MANIFEST_PATH.read_text(encoding="utf-8"))


class ManifestTests(unittest.TestCase):
    def test_checked_in_manifest_validates(self) -> None:
        manifest = pp.load_manifest()
        ids = [program.id for program in manifest.programs]
        self.assertEqual(ids, ["gallery", "explore", "correspondence"])
        self.assertTrue(manifest.program("gallery").enabled)
        self.assertTrue(manifest.program("explore").enabled)
        self.assertTrue(manifest.program("correspondence").enabled)
        self.assertEqual(manifest.program("gallery").required_platforms, ("macos-arm64", "windows-x64"))
        self.assertEqual(manifest.program("gallery").automation_flag, "--automation")
        self.assertIsNone(manifest.program("explore").automation_flag)
        self.assertIn("--product-only", manifest.program("explore").smoke["extra_arguments"])

    def test_manifest_names_the_built_outputs(self) -> None:
        gallery = pp.load_manifest().program("gallery")
        self.assertEqual(gallery.platforms["macos-arm64"].bundle, "GUI.Forms Gallery.app")
        self.assertEqual(gallery.platforms["macos-arm64"].target, "gui_forms_gallery")
        self.assertEqual(gallery.platforms["windows-x64"].executable, "GUI.Forms Gallery.exe")
        self.assertEqual(gallery.platforms["windows-x64"].target, "gui_forms_gallery_windows")
        self.assertFalse(gallery.platforms["linux-x64"].planned)
        self.assertEqual(gallery.platforms["linux-x64"].executable, "gui-forms-gallery")
        explore = pp.load_manifest().program("explore")
        self.assertEqual(explore.platforms["macos-arm64"].bundle, "File Manager Demoboard.app")
        self.assertEqual(explore.display_name, "GUI.Forms Explore (preview)")

    def test_font_lists_match_the_asset_directory(self) -> None:
        manifest = pp.load_manifest()
        source = pp.ROOT / manifest.fonts["source"][0]
        for name in manifest.fonts["files"] + manifest.fonts["notices"]:
            self.assertTrue((source / name).is_file(), name)

    def test_notice_list_is_well_formed(self) -> None:
        data = json.loads(pp.LICENSES_PATH.read_text(encoding="utf-8"))
        # Names are package paths, so they are unique among the notices one platform ships.
        for platform in pp.PLATFORMS:
            names = [notice["name"] for notice in data["notices"]
                     if not notice.get("platforms") or platform in notice["platforms"]]
            self.assertEqual(len(names), len(set(names)), platform)
        # The stx checkout the $STX notices resolve from, when this host names one
        # (a build's fetched checkout is found by the packaging run itself).
        stx = pp.stx_source_directory(None, None)
        for notice in data["notices"]:
            source: str = notice["source"]
            if source.startswith(pp.STX_PREFIX + "/"):
                relative: str = source[len(pp.STX_PREFIX) + 1:]
                # Never a tree path: GUI.Forms holds no stx sources.
                self.assertFalse((pp.ROOT / relative).exists(), source)
                if stx is not None and not notice.get("optional"):
                    self.assertTrue((stx / relative).is_file(), source)
            # Any other $PREFIX source comes from a toolchain prefix given at packaging time.
            elif not notice.get("optional") and not source.startswith("$"):
                self.assertTrue((pp.ROOT / source).is_file(), source)

    def test_stx_notices_resolve_from_the_build_checkout(self) -> None:
        # The variable would name a checkout ahead of the build's; this test reads the build's.
        with mock.patch.dict(pp.os.environ), tempfile.TemporaryDirectory() as temporary:
            pp.os.environ.pop("GUI_FORMS_STX_SOURCE_DIR", None)
            build = Path(temporary) / "build"
            (build / "_deps").mkdir(parents=True)
            self.assertIsNone(pp.stx_source_directory(build, None))
            local = Path(temporary) / "stx"
            (local / "stx_zlib").mkdir(parents=True)
            (local / "stx_zlib" / "CMakeLists.txt").write_text("", encoding="utf-8")
            (build / "CMakeCache.txt").write_text(
                pp.STX_OVERRIDE_VARIABLE + ":PATH=" + str(local) + "\n", encoding="utf-8")
            self.assertEqual(pp.stx_source_directory(build, None), local)
            self.assertEqual(pp.stx_source_directory(None, local), local)
            with self.assertRaisesRegex(RuntimeError, "not an stx checkout"):
                pp.stx_source_directory(None, build)

    def test_stx_notices_resolve_from_a_reused_package(self) -> None:
        with mock.patch.dict(pp.os.environ), tempfile.TemporaryDirectory() as temporary:
            pp.os.environ.pop("GUI_FORMS_STX_SOURCE_DIR", None)
            build = Path(temporary) / "build"
            build.mkdir()
            package = Path(temporary) / "stx-package"
            (build / "CMakeCache.txt").write_text(
                "GUI_FORMS_STX_PREFIX:PATH=\nGUI_FORMS_STX_PACKAGE_DIR:PATH=" + str(package) + "\n",
                encoding="utf-8")
            self.assertIsNone(pp.stx_source_directory(build, None))
            notices = package / "build" / "share" / "stx_text" / "notices"
            notices.mkdir(parents=True)
            self.assertEqual(pp.stx_source_directory(build, None), notices)

    def test_windows_runtime_imports_are_recognised(self) -> None:
        for name in ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "LIBSTDC++-6.DLL", "libc++.dll", "libunwind.dll",
                     "libwinpthread-1.dll", "libgomp-1.dll"):
            self.assertIsNotNone(pp.WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch(name), name)
        for name in ("libgui_forms_threading.dll", "gui_forms_abi1.dll", "kernel32.dll"):
            self.assertIsNone(pp.WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch(name), name)

    def assert_rejected(self, mutate, pattern: str) -> None:
        data = manifest_data()
        mutate(data)
        with self.assertRaisesRegex(pp.ManifestError, pattern):
            pp.validate_manifest(data)

    def test_validation_rejects_structural_mistakes(self) -> None:
        self.assert_rejected(lambda d: d.__setitem__("schema", 2), "schema")
        self.assert_rejected(lambda d: d["programs"].append(copy.deepcopy(d["programs"][0])), "duplicate id")
        self.assert_rejected(lambda d: d["programs"][0].__setitem__("id", "Gallery App"), "must match")
        self.assert_rejected(lambda d: d["programs"][0]["platforms"].__setitem__("freebsd-x64", {}), "unknown platform")
        self.assert_rejected(lambda d: d["programs"][0]["platforms"]["macos-arm64"].pop("bundle"), "bundle")
        self.assert_rejected(lambda d: d["programs"][0]["platforms"]["windows-x64"].__setitem__("executable", "Gallery"), r"\.exe")
        self.assert_rejected(lambda d: d["programs"][0]["platforms"]["windows-x64"].__setitem__("fonts", "host"), "fonts must be")
        self.assert_rejected(lambda d: (d["programs"][0]["platforms"]["linux-x64"].__setitem__("planned", True), d["programs"][0].__setitem__("required_platforms", ["linux-x64"])), "planned")
        self.assert_rejected(lambda d: d["programs"][0].pop("automation_flag"), "automation_flag")
        self.assert_rejected(lambda d: d["programs"][0]["smoke"].__setitem__("mode", "pray"), "extra_arguments")
        self.assert_rejected(lambda d: d["programs"][0].__setitem__("enabled", "yes"), "enabled")


class ArtifactNameTests(unittest.TestCase):
    def test_names_follow_prefix_id_version_platform(self) -> None:
        manifest = pp.load_manifest()
        gallery = manifest.program("gallery")
        self.assertEqual(pp.artifact_names(manifest, gallery, "0.1.7", "macos-arm64"),
                         {"archive": "gui-forms-gallery-0.1.7-macos-arm64.zip", "installer": "gui-forms-gallery-0.1.7-macos-arm64.pkg"})
        self.assertEqual(pp.artifact_names(manifest, gallery, "0.1.7", "windows-x64")["installer"], "gui-forms-gallery-0.1.7-windows-x64-setup.exe")
        linux = pp.artifact_names(manifest, manifest.program("explore"), "0.1.7", "linux-arm64")
        self.assertEqual(linux, {"archive": "gui-forms-explore-0.1.7-linux-arm64.tar.gz", "installer": "gui-forms-explore-0.1.7-linux-arm64.deb"})


class PlanTests(unittest.TestCase):
    def test_plan_packages_built_programs_and_skips_absent_ones(self) -> None:
        manifest = pp.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "GUI.Forms Gallery.app/Contents/MacOS").mkdir(parents=True)
            plans = {plan.program.id: plan for plan in pp.make_plan(manifest, "macos-arm64", build)}
            self.assertTrue(plans["gallery"].packages)
            self.assertEqual(plans["gallery"].built, build / "GUI.Forms Gallery.app")
            self.assertFalse(plans["explore"].packages)
            self.assertIn("not built", plans["explore"].reason)
            self.assertIn("gui_forms_file_manager_demoboard", plans["explore"].reason)
            self.assertFalse(plans["correspondence"].packages)
            self.assertIn("gui_forms_correspondence", plans["correspondence"].reason)

    def test_linux_planned_targets_are_skipped_with_a_note_not_an_error(self) -> None:
        data = manifest_data()
        data["programs"][0]["platforms"]["linux-x64"]["planned"] = True
        manifest = pp.validate_manifest(data)
        with tempfile.TemporaryDirectory() as directory:
            plans = pp.make_plan(manifest, "linux-x64", Path(directory))
            gallery = next(plan for plan in plans if plan.program.id == "gallery")
            self.assertFalse(gallery.packages)
            self.assertIn("planned target", gallery.reason)

    def test_executables_are_found_below_the_build_root(self) -> None:
        manifest = pp.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "demo").mkdir()
            (build / "demo/GUI.Forms Gallery.exe").write_bytes(b"MZ")
            (build / "CMakeFiles/GUI.Forms Gallery.exe").parent.mkdir(parents=True)
            plans = pp.make_plan(manifest, "windows-x64", build)
            gallery = next(plan for plan in plans if plan.program.id == "gallery")
            self.assertEqual(gallery.built, build / "demo/GUI.Forms Gallery.exe")


class NoticeCheckTests(unittest.TestCase):
    """check_package_notices on a fixture package."""

    def make_package(self, root: Path) -> dict[str, Any]:
        (root / "licenses").mkdir()
        (root / "fonts").mkdir()
        lines = ["| Example | 1.0 | MIT | all | `licenses/Example.txt` | x |", pp.FREETYPE_CREDIT, "`fonts/OFL-Example.txt`"]
        (root / "licenses" / pp.NOTICES_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")
        (root / "licenses" / "Example.txt").write_text("MIT License\n", encoding="utf-8")
        (root / "fonts" / "OFL-Example.txt").write_text("SIL Open Font License\n", encoding="utf-8")
        licenses = {"notices": [{"name": "Example.txt", "source": "x", "component": "Example"}]}
        return licenses

    def test_a_covered_package_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            licenses = self.make_package(root)
            pp.check_package_notices(root, "windows-x64", licenses, ["OFL-Example.txt"])

    def test_the_checked_in_notices_carry_the_freetype_credit(self) -> None:
        text = (pp.ROOT / pp.NOTICES_NAME).read_text(encoding="utf-8")
        self.assertIn(pp.FREETYPE_CREDIT, text)
        self.assertIsNone(pp.GPL_TITLE.search(text))
        data = json.loads(pp.LICENSES_PATH.read_text(encoding="utf-8"))
        for notice in data["notices"]:
            if notice.get("component"):
                self.assertIn(f"| {notice['component']} |", text, notice["component"])

    def test_each_defect_is_refused(self) -> None:
        cases = (
            ("credit", "lacks the FreeType credit"),
            ("unnamed licence", "not named"),
            ("missing row", "has no row"),
            ("gpl text", "General Public License text"),
            ("gpl name", "whose name says GPL"),
        )
        for case, message in cases:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                licenses = self.make_package(root)
                notices = root / "licenses" / pp.NOTICES_NAME
                text = notices.read_text(encoding="utf-8")
                if case == "credit":
                    notices.write_text(text.replace(pp.FREETYPE_CREDIT, ""), encoding="utf-8")
                elif case == "unnamed licence":
                    (root / "licenses" / "Other.txt").write_text("BSD\n", encoding="utf-8")
                elif case == "missing row":
                    licenses["notices"].append({"name": "Absent.txt", "source": "y", "component": "Absent"})
                elif case == "gpl text":
                    (root / "README.txt").write_text("GNU GENERAL PUBLIC LICENSE\nVersion 2, June 1991\n",
                                                     encoding="utf-8")
                else:
                    (root / "licenses" / "GPLv2.TXT").write_text("text\n", encoding="utf-8")
                    notices.write_text(text + "`licenses/GPLv2.TXT`\n", encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, message, msg=case):
                    pp.check_package_notices(root, "windows-x64", licenses, ["OFL-Example.txt"])


class StagingTests(unittest.TestCase):
    def test_placeholder_icon_is_a_png(self) -> None:
        data = pp.placeholder_png(8)
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertIn(b"IHDR", data)
        self.assertTrue(data.endswith(b"IEND\xaeB`\x82"))

    def test_sums_file_lists_each_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "b.zip").write_bytes(b"b")
            (out / "a.pkg").write_bytes(b"a")
            text = pp.write_sums(out, [out / "b.zip", out / "a.pkg"]).read_text()
            self.assertEqual([line.split("  ")[1] for line in text.splitlines()], ["a.pkg", "b.zip"])
            self.assertEqual(len(text.splitlines()[0].split("  ")[0]), 64)

    def test_nsis_script_fills_every_placeholder(self) -> None:
        manifest = pp.load_manifest()
        gallery = manifest.program("gallery")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "GUI.Forms Gallery"
            (bundle / "fonts").mkdir(parents=True)
            (bundle / "GUI.Forms Gallery.exe").write_bytes(b"MZ")
            (bundle / "fonts/Carlito-Regular.ttf").write_bytes(b"font")
            packager = pp.Packager(manifest, "windows-x64", root, "0.1.7", root, "abc", False, None, None, True, log=lambda *_: None)
            script = root / "installer.nsi"
            packager.write_nsis_script(bundle, gallery, gallery.platforms["windows-x64"], root / "setup.exe", script)
            text = script.read_text()
            self.assertNotRegex(text.split("\n", 1)[1], r"@[A-Z0-9_]+@")
            self.assertIn('InstallDir "$LOCALAPPDATA\\Rainstar\\GUI.Forms\\gallery"', text)
            self.assertIn('WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RainstarGUIForms-gallery" "DisplayName" "GUI.Forms Gallery"', text)
            self.assertIn('Delete "$INSTDIR\\fonts\\Carlito-Regular.ttf"', text)
            self.assertIn('RMDir "$INSTDIR\\fonts"', text)
            self.assertIn('VIProductVersion "0.1.7.0"', text)
            self.assertIn('RequestExecutionLevel user', text)


class DryRunTests(unittest.TestCase):
    def test_dry_run_prints_a_plan_without_tools_or_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / "build"
            (build / "GUI.Forms Gallery.app/Contents/MacOS").mkdir(parents=True)
            out = Path(directory) / "dist"
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                status = pp.main(["--platform", "macos-arm64", "--build-dir", str(build), "--version", "0.1.7",
                                  "--out", str(out), "--dry-run"])
            text = buffer.getvalue()
            self.assertEqual(status, 0)
            self.assertFalse(out.exists())
            self.assertIn("[dry run]", text)
            self.assertIn("gui-forms-gallery-0.1.7-macos-arm64.zip", text)
            self.assertIn("org.rainstar.guiforms.gallery", text)
            self.assertIn("skipped: not built", text)
            self.assertIn("GUI.Forms Correspondence [correspondence]: skipped: not built", text)

    def test_version_must_be_canonical(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError), contextlib.redirect_stdout(io.StringIO()):
                pp.main(["--platform", "linux-x64", "--build-dir", directory, "--version", "1.0", "--out", directory, "--dry-run"])


if __name__ == "__main__":
    unittest.main()
