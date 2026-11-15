"""The System.Drawing facade's bridge is generated from its template and loads the C ABI library."""
from __future__ import annotations

import unittest

import drawing_bridge as db


class DrawingBridgeTests(unittest.TestCase):
    def test_checked_in_bridge_is_current(self) -> None:
        self.assertEqual(db.main(["--check"]), 0)

    def test_the_import_loads_the_c_abi_library(self) -> None:
        text = db.expected_bridge()
        self.assertIn('[DllImport("gui_forms_abi1", EntryPoint = "gd_get_api_v0"', text)
        self.assertNotIn('EntryPoint = "gdr_initialize"', text)
        self.assertNotIn("gui_drawing_abi0", text)
        self.assertNotIn("gui_drawing_raster0", text)

    def test_the_major_comes_from_the_header(self) -> None:
        self.assertEqual(db.abi_major("#define GF_ABI_MAJOR 1u\n"), 1)
        self.assertEqual(db.native_library(db.abi_major("x\n#define GF_ABI_MAJOR 2u\n")), "gui_forms_abi2")
        with self.assertRaises(db.BridgeError):
            db.abi_major("#define GF_ABI_MINOR 1u\n")
        with self.assertRaises(db.BridgeError):
            db.abi_major("#define GF_ABI_MAJOR 1u\n#define GF_ABI_MAJOR 2u\n")

    def test_template_defects_are_refused(self) -> None:
        with self.assertRaises(db.BridgeError):
            db.render("@NATIVE_LIBRARY@ twice @NATIVE_LIBRARY@", "gui_forms_abi1")
        with self.assertRaises(db.BridgeError):
            db.render("@NATIVE_LIBRARY@ @OTHER@", "gui_forms_abi1")
        self.assertEqual(db.render("a @NATIVE_LIBRARY@ b", "x"), "a x b")


if __name__ == "__main__":
    unittest.main()
