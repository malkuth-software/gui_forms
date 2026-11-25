"""Unit tests of tools/package_notices.py: the notice copy and the shared rules."""
from __future__ import annotations
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import package_notices


def quiet(_: str) -> None:
    """A log that prints nothing."""


class CopyNoticesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root: Path = Path(self.directory.name)
        (self.root / "LICENSE").write_text("MIT\n", encoding="utf-8")
        runtime: Path = self.root / "runtime"
        (runtime / "share").mkdir(parents=True)
        (runtime / "share" / "LICENSE.TXT").write_text("LLVM\n", encoding="utf-8")
        self.runtime: Path = runtime
        record: dict = {"notices": [
            {"name": "LICENSE.txt", "source": "LICENSE"},
            {"name": "llvm/LICENSE.txt", "source": "$GUI_FORMS_LLVM_RUNTIME/share/LICENSE.TXT",
             "platforms": ["macos-arm64"]},
            {"name": "absent.txt", "source": "absent.txt", "optional": True},
        ]}
        self.licenses: Path = self.root / "licenses.json"
        self.licenses.write_text(json.dumps(record), encoding="utf-8")
        self.patches = [mock.patch.object(package_notices, "ROOT", self.root),
                        mock.patch.object(package_notices, "LICENSES_PATH", self.licenses),
                        mock.patch.object(package_notices, "stx_source_directory", lambda *_: None),
                        mock.patch.object(package_notices, "stxlib_license_directory", lambda *_: None)]
        for patch in self.patches:
            patch.start()

    def tearDown(self) -> None:
        for patch in self.patches:
            patch.stop()
        self.directory.cleanup()

    def test_each_platform_gets_its_own_notices(self) -> None:
        destination: Path = self.root / "out-mac"
        package_notices.copy_notices("macos-arm64", None, destination, runtime=self.runtime, log=quiet)
        self.assertEqual((destination / "LICENSE.txt").read_text(encoding="utf-8"), "MIT\n")
        self.assertEqual((destination / "llvm" / "LICENSE.txt").read_text(encoding="utf-8"), "LLVM\n")
        self.assertFalse((destination / "absent.txt").exists())
        windows: Path = self.root / "out-windows"
        package_notices.copy_notices("windows-x64", None, windows, log=quiet)
        self.assertTrue((windows / "LICENSE.txt").is_file())
        self.assertFalse((windows / "llvm").exists())

    def test_a_missing_required_notice_raises(self) -> None:
        with self.assertRaisesRegex(FileNotFoundError, "llvm/LICENSE.txt"):
            package_notices.copy_notices("macos-arm64", None, self.root / "out", log=quiet)


class RuleTests(unittest.TestCase):
    def test_compiler_runtimes_are_forbidden_and_system_dlls_admitted(self) -> None:
        self.assertIsNotNone(package_notices.WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch("vcruntime140.dll"))
        self.assertIsNotNone(package_notices.WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch("libc++.dll"))
        self.assertIsNone(package_notices.WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch("ucrtbase.dll"))
        self.assertIn("ucrtbase.dll", package_notices.WINDOWS_SYSTEM_DLLS)

    def test_gpl_titles_are_recognised(self) -> None:
        self.assertIsNotNone(package_notices.GPL_TITLE.search("GNU LESSER GENERAL PUBLIC LICENSE"))
        self.assertIsNone(package_notices.GPL_TITLE.search("MIT License"))


if __name__ == "__main__":
    unittest.main()
