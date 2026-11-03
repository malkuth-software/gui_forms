"""build_sdk.py configures exactly as ci_build.py's build_toolkit does, less tests and programs."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_sdk
import ci_build

# Options whose values are paths of the tree or build that ran the configure;
# only their presence and file names are compared.
PATH_OPTIONS: dict[str, str] = {"CMAKE_TOOLCHAIN_FILE": "cmake/llvm22.cmake"}
# Options the two set differently by design: CI also builds and runs the
# tests; each installs into its own prefix.
DIFFERENT_BY_DESIGN: dict[str, tuple[str, str]] = {"GUI_FORMS_BUILD_TESTS": ("ON", "OFF")}


class Captured(Exception):
    """Stops build_toolkit at its configure, carrying the command."""

    def __init__(self, command: list[str]) -> None:
        super().__init__("captured")
        self.command: list[str] = command


def capture_configure(toolkit: Path, configure: list[str]) -> None:
    raise Captured(configure)


def ignore_preparation(source: Path, fetch_text: bool) -> None:
    return None


def definitions(command: list[str]) -> dict[str, str]:
    """The -D options of a cmake configure command, by name."""
    result: dict[str, str] = {}
    argument: str
    for argument in command:
        if argument.startswith("-D"):
            parts: tuple[str, str, str] = argument[2:].partition("=")
            result[parts[0]] = parts[2]
    return result


def ci_configure(host: str, build: Path) -> list[str]:
    """The configure command ci_build.build_toolkit runs for host, without running anything."""
    with mock.patch.object(ci_build, "prepare_host", ignore_preparation), \
            mock.patch.object(ci_build, "configure_tree", capture_configure):
        try:
            ci_build.build_toolkit(host, build, build / "gui-forms-sdk", 1)
        except Captured as captured:
            return captured.command
    raise AssertionError("build_toolkit did not configure")


class ConfigureOptionTests(unittest.TestCase):
    def test_each_host_configures_as_ci_does(self) -> None:
        target: str
        for target in ("windows-x64", "macos-arm64", "linux-x64", "linux-arm64"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                build: Path = Path(directory)
                ci: dict[str, str] = definitions(ci_configure(build_sdk.TARGET_HOSTS[target], build))
                sdk: dict[str, str] = definitions(build_sdk.configure_command(
                    target, build_sdk.ROOT, build / "sdk-tree", build / "prefix"))
                self.assertEqual(sorted(ci), sorted(sdk))
                name: str
                for name in ci:
                    if name == "CMAKE_INSTALL_PREFIX":
                        continue
                    if name in PATH_OPTIONS:
                        self.assertTrue(Path(ci[name]).as_posix().endswith(PATH_OPTIONS[name]), ci[name])
                        self.assertTrue(Path(sdk[name]).as_posix().endswith(PATH_OPTIONS[name]), sdk[name])
                    elif name in DIFFERENT_BY_DESIGN:
                        self.assertEqual((ci[name], sdk[name]), DIFFERENT_BY_DESIGN[name])
                    else:
                        self.assertEqual(sdk[name], ci[name], name)
                self.assertEqual(sdk["GUI_FORMS_BUILD_GALLERY"], "OFF")
                self.assertEqual(sdk["CMAKE_INSTALL_PREFIX"], str(build / "prefix"))

    def test_all_features_add_only_the_optional_components(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build: Path = Path(directory)
            default: list[str] = build_sdk.configure_command(
                "windows-x64", build_sdk.ROOT, build / "tree", build / "prefix")
            every: list[str] = build_sdk.configure_command(
                "windows-x64", build_sdk.ROOT, build / "tree", build / "prefix", "all")
        self.assertEqual(every[:len(default)], default)
        self.assertEqual(definitions(every[len(default):]), {
            "GUI_FORMS_BUILD_TEXT_MASKS": "ON", "GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT": "ON",
            "GUI_FORMS_INSTALL_TEXT_STACK": "ON", "GUI_FORMS_BUILD_GALLERY_MODEL": "ON",
            "GUI_FORMS_INSTALL_GALLERY_MODEL": "ON"})
        self.assertNotIn("-DGUI_FORMS_BUILD_PREPARED_TEXT=ON", every)

    def test_the_default_features_add_nothing(self) -> None:
        self.assertEqual(build_sdk.FEATURE_SETS["default"], ())


class TargetTests(unittest.TestCase):
    def test_only_the_host_target_is_admitted(self) -> None:
        build_sdk.check_target("linux-x64", "linux-x64")
        with self.assertRaisesRegex(ValueError, "cross-compile"):
            build_sdk.check_target("windows-x64", "linux-x64")
        with self.assertRaisesRegex(ValueError, "unknown target"):
            build_sdk.check_target("windows-arm64", "windows-arm64")

    def test_the_host_target_is_a_known_platform_here(self) -> None:
        self.assertIn(build_sdk.host_target(), build_sdk.TARGETS + ("macos-x64", "windows-arm64"))

    def test_a_checkout_without_a_manifest_fetches_its_sources(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            self.assertFalse(build_sdk.vendored_sources_present(Path(directory)))


if __name__ == "__main__":
    unittest.main()
