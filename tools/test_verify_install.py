"""An installed SDK is one module: no first-party shared library but the C ABI library."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import verify_install as vi


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")


class OneModuleSdkTests(unittest.TestCase):
    def test_shared_library_names(self) -> None:
        for name in ("libgui_forms_abi.1.dylib", "libgui_forms_abi.so.1", "gui_forms_abi1.dll", "x.so"):
            self.assertTrue(vi.shared_library_name(name), name)
        for name in ("libgui_forms_application.a", "libstxlib_state.dll.a", "gui_forms_abi.lib", "notes.txt"):
            self.assertFalse(vi.shared_library_name(name), name)

    def test_static_sdk_with_the_c_abi_library_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            for relative in ("lib/libgui_forms_application.a", "lib/libgui_forms_application_deps.a",
                             "lib/libgui_forms_threading.a", "lib/libgui_forms_abi.so.1",
                             "lib/gui_forms/gui_forms_localizable.so", "bin/gui_forms_abi1.dll",
                             "lib/libgui_forms_abi.dll.a", "lib/libfoo.so.2"):
                touch(prefix / relative)
            vi.check_one_module_sdk(prefix)

    def test_each_first_party_shared_library_is_refused(self) -> None:
        for relative in ("lib/libgui_forms_threading.0.dylib", "lib/libgui_forms_application.so.0",
                         "bin/libgui_forms_application.dll", "bin/stxlib_state.dll",
                         "lib64/libstxlib_state.so", "bin/gui_drawing_raster0.dll"):
            with tempfile.TemporaryDirectory() as temporary:
                prefix = Path(temporary)
                touch(prefix / "lib/libgui_forms_abi.so.1")
                touch(prefix / relative)
                with self.assertRaisesRegex(RuntimeError, "one module"):
                    vi.check_one_module_sdk(prefix)


if __name__ == "__main__":
    unittest.main()
