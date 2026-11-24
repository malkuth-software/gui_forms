#!/usr/bin/env python3
"""Tests for tools/profile_cache.py: the profile test cache's configure options."""
from __future__ import annotations
from pathlib import Path
import unittest

import profile_cache


class ConfigureTests(unittest.TestCase):
    def test_profile_flags_keep_the_toolchain_language_flags(self) -> None:
        # CMAKE_<LANG>_FLAGS would replace cmake/llvm22.cmake's _INIT flags
        # (the macOS libc++ runtime); the build-type flags carry the profile flags.
        options: list[str] = profile_cache.release_flag_options('macos')
        option: str
        for option in options:
            self.assertNotRegex(option, r'^-DCMAKE_[A-Z]+_FLAGS=')
        self.assertEqual(options, [
            '-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_cache.PROFILE_FLAGS,
            '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_cache.PROFILE_FLAGS,
            '-DCMAKE_OBJCXX_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_cache.PROFILE_FLAGS,
        ])

    def test_linux_has_no_objective_cxx(self) -> None:
        options: list[str] = profile_cache.release_flag_options('linux')
        self.assertEqual(len(options), 2)
        self.assertNotIn('OBJCXX', ' '.join(options))

    def test_archive_name_matches_test_cache_pack(self) -> None:
        # tools/test_cache.py pack names a non-normal kind's archive with a -<kind> suffix.
        self.assertEqual(profile_cache.archive_path(Path('out'), 'linux-x64'),
                         Path('out') / 'gui_forms_test_cache-linux-x64-profile.tar.gz')

    def test_tests_command_hands_the_archive_to_the_profiler(self) -> None:
        command: list[str] = profile_cache.tests_command(Path('/t'), Path('/a.tar.gz'), Path('/o'), 4)
        self.assertEqual(command[2:], ['/t/tools/profile_hot_paths.py', 'run', '--archive', '/a.tar.gz',
                                       '--output', '/o', '--jobs', '4'])


if __name__ == '__main__':
    unittest.main()
