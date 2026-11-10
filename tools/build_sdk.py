#!/usr/bin/env python3
"""Build and install the GUI.Forms SDK from this tree for one target.

The build entry point of the source SDK (SDK_MANIFEST.json's "build"):

  python3 tools/build_sdk.py --target <platform> --prefix <dir> [--features default|all]
                             [--jobs N] [--work <dir>]

It runs the target's own preparation (on macOS and Linux, fetching and
building the pinned CPU Skia; on Windows nothing more), configures the
toolkit without tests or the demonstration programs, builds it and installs
it into --prefix. The configure options are the ones tools/ci_build.py
configures CI with (tools/toolkit_configure.py, imported rather than
copied, and free of template_harness, which a source SDK does not carry), so
a consumer's SDK is the build CI tests;
test_build_sdk.py checks the two stay equal.

--features all adds every optional component a product may link on top of
that default (FEATURE_SETS): one install then serves every product, as
template_harness's lockstep build uses it.

In a source SDK archive the text sources and miniaudio are already in place
(SDK_MANIFEST.json lists them) and are not fetched; in a Git checkout they
are fetched as ci_build.py fetches them. --target names the platform to
build for; only the host's platform is admitted until per-target toolchains
exist.
"""
from __future__ import annotations

import argparse
import json
import os
import platform as host_platform
import subprocess
import sys
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import toolkit_configure  # noqa: E402  (after the path insert above)

TARGETS: tuple[str, ...] = ("macos-arm64", "windows-x64", "linux-x64", "linux-arm64")
# The ci_build.py host name of each target.
TARGET_HOSTS: dict[str, str] = {"macos-arm64": "macos", "windows-x64": "windows",
                                "linux-x64": "linux", "linux-arm64": "linux"}
MANIFEST_NAME: str = "SDK_MANIFEST.json"
# Configure options added to the default (CI's) set, by --features value.
# "all" turns on each optional component a product can link that the default
# leaves out; each installs as a CMake package component:
#   GUI_FORMS_BUILD_TEXT_MASKS            GUIForms::TextMasks (inside Application)
#   GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT  GUIForms::AudioLoopTransport (inside Audio)
#   GUI_FORMS_INSTALL_TEXT_STACK          GUIForms::FreeType and GUIForms::HarfBuzz, the
#                                         pinned static libraries and their headers
#   GUI_FORMS_INSTALL_GALLERY_MODEL       GUIForms::GalleryModel, the Gallery model the
#                                         programs repository builds the Gallery from
# The default already builds Audio with the stx Vorbis decoder, the
# HarfBuzz/FreeType text engine and the host's Application. Left out of
# "all": GUI_FORMS_BUILD_PREPARED_TEXT, a development interface that is not
# installable and changes Application's declarations for every consumer;
# the probes, benchmarks, fuzzers, diagnostics and the Windows transactional
# DIB experiment, which build programs or change behaviour rather than add
# a linkable component; and the Gallery, demoboard and tests.
FEATURE_SETS: dict[str, tuple[str, ...]] = {
    "default": (),
    "all": ("-DGUI_FORMS_BUILD_TEXT_MASKS=ON", "-DGUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT=ON",
            "-DGUI_FORMS_INSTALL_TEXT_STACK=ON", "-DGUI_FORMS_BUILD_GALLERY_MODEL=ON",
            "-DGUI_FORMS_INSTALL_GALLERY_MODEL=ON"),
}


def host_target() -> str:
    """The target platform of the machine this runs on."""
    systems: dict[str, str] = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}
    system: str = systems.get(host_platform.system(), host_platform.system().lower())
    if system.startswith(("mingw", "msys", "cygwin")):
        system = "windows"
    architecture: str = "x64"
    if host_platform.machine().lower() in ("arm64", "aarch64"):
        architecture = "arm64"
    return system + "-" + architecture


def check_target(target: str, host: str) -> None:
    """Raise ValueError unless target can be built on host (only the host's own platform today)."""
    if target not in TARGETS:
        raise ValueError(f"unknown target {target}; the targets are {', '.join(TARGETS)}")
    if target != host:
        raise ValueError(f"cannot build {target} on this {host} host: build_sdk.py builds only for the "
                         "host's own platform until per-target cross-compile toolchains exist")


def vendored_sources_present(source: Path) -> bool:
    """Whether source is a source SDK whose manifest's vendored sources are all in place."""
    manifest: Path = source / MANIFEST_NAME
    if not manifest.is_file():
        return False
    record: dict = json.loads(manifest.read_text(encoding="utf-8"))
    entry: dict
    for entry in record.get("third_party", []):
        if not (source / entry["path"]).is_dir():
            raise RuntimeError(f"{MANIFEST_NAME} lists {entry['path']}, which is missing")
    return True


def configure_command(target: str, source: Path, toolkit: Path, prefix: Path,
                      features: str = "default", stx_prefix: str = "", stx_package_dir: str = "") -> list[str]:
    """The configure of target's SDK: ci_build.py's options with tests and programs off, plus features' options.

    stx_prefix links an stx package built elsewhere and compiles nothing of
    stx; stx_package_dir is where the package is built and reused
    (toolkit_configure.stx_options).
    """
    host: str = TARGET_HOSTS[target]
    command: list[str] = ["cmake", "-S", str(source), "-B", str(toolkit), "-G", "Ninja",
                          *toolkit_configure.toolkit_options(source, prefix, False, False),
                          *toolkit_configure.host_options(host), *FEATURE_SETS[features],
                          *toolkit_configure.stx_options(stx_prefix, stx_package_dir)]
    # A source SDK carries stx at stx.lock's commit (package_sdk.py), so the
    # configure builds from it instead of fetching it.
    vendored_stx: Path = source / "stx"
    if (vendored_stx / "CMakeLists.txt").is_file():
        command.append("-DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX=" + str(vendored_stx))
    return command


def build(target: str, prefix: Path, jobs: int, work: Path, features: str = "default",
          stx_prefix: str = "", stx_package_dir: str = "") -> None:
    """Prepare, configure, build and install target's SDK with features into prefix, using work for the build trees."""
    work.mkdir(parents=True, exist_ok=True)
    toolkit: Path = work / "gui-forms"
    toolkit_configure.prepare_host(ROOT, not vendored_sources_present(ROOT))
    toolkit_configure.run(*configure_command(target, ROOT, toolkit, prefix, features, stx_prefix, stx_package_dir))
    toolkit_configure.run("cmake", "--build", toolkit, "--parallel", jobs)
    toolkit_configure.run("cmake", "--install", toolkit)
    print(f"GUI.Forms SDK for {target} installed in {prefix}", flush=True)


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", required=True, help="the platform to build for: " + ", ".join(TARGETS))
    parser.add_argument("--prefix", type=Path, required=True, help="where to install the SDK")
    parser.add_argument("--features", choices=tuple(FEATURE_SETS), default="default",
                        help="default: CI's components; all: also every optional component a product may "
                             "link (the text masks and the audio loop transport)")
    parser.add_argument("--stx-prefix", default=os.environ.get("GUI_FORMS_STX_PREFIX", ""),
                        help="an stx package built earlier (a configure's GUI_FORMS_STX_PACKAGE_DIR) to link "
                             "as is, compiling nothing of stx (default: $GUI_FORMS_STX_PREFIX)")
    parser.add_argument("--stx-package-dir", default=os.environ.get("GUI_FORMS_STX_PACKAGE_DIR", ""),
                        help="where the stx package is built and reused while its inputs are unchanged "
                             "(default: $GUI_FORMS_STX_PACKAGE_DIR, else the build tree)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--work", type=Path, default=None,
                        help="the build directory (default: .build/sdk-<target> in this tree)")
    args: argparse.Namespace = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        check_target(args.target, host_target())
    except ValueError as error:
        parser.error(str(error))
    work: Path = args.work if args.work is not None else ROOT / ".build" / ("sdk-" + args.target)
    try:
        build(args.target, args.prefix.resolve(), args.jobs, work.resolve(), args.features,
              args.stx_prefix, args.stx_package_dir)
    except subprocess.CalledProcessError as error:
        print(f"build_sdk.py: a step failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
