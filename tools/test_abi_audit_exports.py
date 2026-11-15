"""tools/abi_audit.py exports: the allowlist and the Win32 shim's module definition."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import abi_audit  # noqa: E402


class AbiAuditExportsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.root: Path = Path(self.directory.name)
        self.allowlist: Path = self.root / 'gf_abi_exports.txt'
        self.allowlist.write_text('# generated\ngf_abi_get_table\ngd_get_api_v0\n', encoding='utf-8')
        self.definition: Path = self.root / 'shim.def'
        self.definition.write_text('; comment\nEXPORTS\n    IsWindow=gf_compat_IsWindow\n    GetParent=gf_compat_GetParent\n',
                                   encoding='utf-8')

    def tearDown(self) -> None:
        self.directory.cleanup()

    def arguments(self, extra_def: Path | None) -> argparse.Namespace:
        return argparse.Namespace(allowlist=self.allowlist, library=self.root / 'gui_forms_abi1.dll',
                                  tool_dir=None, import_library=None, extra_def=extra_def)

    def test_definition_names(self) -> None:
        self.assertEqual(abi_audit.read_definition_exports(self.definition), ['GetParent', 'IsWindow'])

    def test_drawing_entry_point_is_allowed(self) -> None:
        with mock.patch('abi_audit.library_exports', return_value=['gd_get_api_v0', 'gf_abi_get_table']):
            self.assertEqual(abi_audit.command_exports(self.arguments(None)), 0)

    def test_shim_names_need_the_definition(self) -> None:
        exported: list[str] = ['GetParent', 'IsWindow', 'gd_get_api_v0', 'gf_abi_get_table']
        with mock.patch('abi_audit.library_exports', return_value=exported):
            self.assertNotEqual(abi_audit.command_exports(self.arguments(None)), 0)
            self.assertEqual(abi_audit.command_exports(self.arguments(self.definition)), 0)


if __name__ == '__main__':
    unittest.main()
