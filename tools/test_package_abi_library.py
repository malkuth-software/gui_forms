"""The C ABI library archive holds the library under its loader name, the manifest and a checked description."""
from __future__ import annotations

import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path

import package_abi_library as abi

MANIFEST: dict[str, object] = {"format": 1, "abi": {"major": 1, "minor": 6}, "structs": []}


class Fixture:
    """A build directory holding a fake library (behind a symbolic link where the platform has one) and a manifest."""

    def __init__(self, folder: Path, platform: str) -> None:
        self.build: Path = folder / "build"
        self.build.mkdir()
        self.manifest: Path = folder / "gf_abi.json"
        self.manifest.write_text(json.dumps(MANIFEST), encoding="utf-8")
        name: str = abi.library_name(platform, 1)
        if platform.startswith("windows-"):
            (self.build / name).write_bytes(b"MZ library")
        else:
            versioned: Path = self.build / (name + ".6.0")
            versioned.write_bytes(b"\x7fELF library")
            os.symlink(versioned.name, self.build / name)
        self.out: Path = folder / "out"
        self.licenses: Path = folder / "licenses"
        (self.licenses / "llvm").mkdir(parents=True)
        (self.licenses / "THIRD_PARTY_NOTICES.md").write_text("notices\n", encoding="utf-8")
        (self.licenses / "llvm" / "LICENSE.TXT").write_text("llvm\n", encoding="utf-8")


class PackageAbiLibraryTests(unittest.TestCase):

    def test_names_by_platform(self) -> None:
        self.assertEqual(abi.library_name("macos-arm64", 1), "libgui_forms_abi.1.dylib")
        self.assertEqual(abi.library_name("linux-x64", 1), "libgui_forms_abi.so.1")
        self.assertEqual(abi.library_name("windows-x64", 1), "gui_forms_abi1.dll")
        self.assertEqual(abi.archive_name("1.0.0", "linux-arm64"), "gui-forms-abi-1.0.0-linux-arm64.zip")

    def test_each_platform_packages_and_checks(self) -> None:
        for platform in abi.PLATFORMS:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                fixture: Fixture = Fixture(Path(folder), platform)
                target: Path = abi.package(fixture.build, platform, "1.0.0", "abc123", fixture.out, fixture.manifest,
                                           fixture.licenses)
                described: dict[str, object] = abi.check_archive(target)
                self.assertEqual(described["library"], abi.library_name(platform, 1))
                self.assertEqual(described["abi_minor"], 6)
                self.assertEqual(described["revision"], "abc123")
                with zipfile.ZipFile(target) as archive:
                    library: zipfile.ZipInfo = archive.getinfo(abi.library_name(platform, 1))
                    self.assertFalse(library.is_dir())
                    self.assertIn(b"library", archive.read(library))
                    self.assertIn("licenses/llvm/LICENSE.TXT", archive.namelist())
                    self.assertIn("licenses/THIRD_PARTY_NOTICES.md", archive.namelist())

    def test_packaging_is_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            fixture: Fixture = Fixture(Path(folder), "linux-x64")
            first: bytes = abi.package(fixture.build, "linux-x64", "1.0.0", "r", fixture.out, fixture.manifest,
                                           fixture.licenses).read_bytes()
            second: bytes = abi.package(fixture.build, "linux-x64", "1.0.0", "r", fixture.out, fixture.manifest,
                                           fixture.licenses).read_bytes()
            self.assertEqual(first, second)

    def test_a_missing_library_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            fixture: Fixture = Fixture(Path(folder), "linux-x64")
            with self.assertRaises(abi.PackagingError):
                abi.package(fixture.build, "macos-arm64", "1.0.0", "r", fixture.out, fixture.manifest,
                                           fixture.licenses)

    def test_a_changed_library_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            fixture: Fixture = Fixture(Path(folder), "windows-x64")
            target: Path = abi.package(fixture.build, "windows-x64", "1.0.0", "r", fixture.out, fixture.manifest,
                                           fixture.licenses)
            tampered: Path = fixture.out / "tampered" / target.name
            tampered.parent.mkdir()
            with zipfile.ZipFile(target) as source, zipfile.ZipFile(tampered, "w") as copy:
                for name in source.namelist():
                    data: bytes = source.read(name)
                    copy.writestr(name, data + b"!" if name.endswith(".dll") else data)
            with self.assertRaises(abi.PackagingError):
                abi.check_archive(tampered)


if __name__ == "__main__":
    unittest.main()
