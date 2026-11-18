"""tools/sdk_consumer_link.py: the consumer's configure arguments and the CMake truth values."""
from __future__ import annotations
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_consumer_link as link  # noqa: E402


class SdkConsumerLinkTests(unittest.TestCase):
    def test_switch(self) -> None:
        for value in ("ON", "on", "1", "TRUE", "yes"):
            self.assertTrue(link.switch(value), value)
        for value in ("OFF", "0", "", "FALSE", "no"):
            self.assertFalse(link.switch(value), value)

    def test_arguments_drop_empty_parent_values(self) -> None:
        arguments: list[str] = link.consumer_arguments(
            Path('/prefix'), Path('/face.ttf'), True, False,
            ['-GNinja', '-DCMAKE_TOOLCHAIN_FILE=', '-DCMAKE_CXX_COMPILER=/usr/bin/clang++'])
        self.assertEqual(arguments[:4], ['-DCMAKE_PREFIX_PATH=/prefix', '-DGUI_FORMS_SDK_CONSUMER_FACE=/face.ttf',
                                         '-DGUI_FORMS_SDK_CONSUMER_TEXT_MASKS=ON',
                                         '-DGUI_FORMS_SDK_CONSUMER_C_ABI=OFF'])
        self.assertIn('-GNinja', arguments)
        self.assertIn('-DCMAKE_CXX_COMPILER=/usr/bin/clang++', arguments)
        self.assertNotIn('-DCMAKE_TOOLCHAIN_FILE=', arguments)

    def test_unchecked_features_are_named(self) -> None:
        self.assertEqual(link.unchecked_features(True, True), [])
        self.assertEqual(link.unchecked_features(False, True), ['TextMasks'])
        self.assertEqual(link.unchecked_features(False, False), ['TextMasks', 'CAbi'])
        self.assertEqual(link.SKIPPED_FEATURES, 77)

    def test_consumer_sources_exist(self) -> None:
        self.assertTrue(link.DEFAULT_FACE.is_file())
        # The consumer project lives in gui_forms_tests (tests/sdk_consumer);
        # gui_forms' own CI has no checkout of it.
        if link.TESTS_CHECKOUT is None:
            self.skipTest('no gui_forms_tests checkout: the consumer sources live there')
        for name in ('CMakeLists.txt', 'main.cpp', 'text_masks_consumer.cpp', 'c_abi_consumer.c'):
            self.assertTrue((link.CONSUMER / name).is_file(), name)


if __name__ == '__main__':
    unittest.main()
