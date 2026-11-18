#!/usr/bin/env python3
"""Install a build tree into a temporary prefix, then build and run tests/sdk_consumer against it.

The SDK-consumer link test (the gui_forms_sdk_consumer_link test, and
tools/verify_install.py --sdk-consumer for a prefix built elsewhere). The
consumer uses every public component, so a symbol the installed archives
lack fails its link: the one-module SDK once shipped an empty
GUIForms::TextMasks interface and an Application archive without the text
masks, which no in-tree test caught (b68c80ecc, 2d4bb797b).

  sdk_consumer_link.py --build-dir BUILD --scratch DIR [--text-masks ON|OFF]
                       [--c-abi ON|OFF] [--face TTF] [-- CMAKE_ARGUMENTS...]

The arguments after `--` configure the consumer the way the build tree was
configured (its toolchain file, compilers, build type, generator and flags),
so the consumer links the archives with the compiler that built them.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tests_checkout  # noqa: E402

ROOT: Path = Path(__file__).resolve().parent.parent
# The exit status of a run that passed without a feature it should check (a
# tree without the text masks or the C ABI library): CTest reports it as
# skipped (SKIP_RETURN_CODE), not passed, since the test proved less.
SKIPPED_FEATURES: int = 77
# The consumer project, in the gui_forms_tests checkout (tools/tests_checkout.py;
# gui_forms_tests sets GUI_FORMS_TESTS_ROOT for its tests).
TESTS_CHECKOUT: Path | None = tests_checkout.tests_root()
CONSUMER: Path = (TESTS_CHECKOUT or ROOT) / "tests" / "sdk_consumer"
DEFAULT_FACE: Path = ROOT / "assets" / "fonts" / "Arimo-Regular.ttf"


def run(command: list[str]) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False)
    if result.returncode != 0:
        raise RuntimeError("sdk_consumer_link: failed: " + " ".join(command) + "\n" + result.stdout)
    return result.stdout


def switch(value: str) -> bool:
    """A CMake truth value as the parent tree passes it (ON, OFF, TRUE, 1, empty)."""
    lowered: str = value.strip().lower()
    truth: bool = lowered in ("1", "on", "yes", "true", "y")
    return truth


def unchecked_features(text_masks: bool, c_abi: bool) -> list[str]:
    """The components a run did not check, by their CMake names."""
    missing: list[str] = []
    if not text_masks:
        missing.append("TextMasks")
    if not c_abi:
        missing.append("CAbi")
    return missing


def consumer_arguments(prefix: Path, face: Path, text_masks: bool, c_abi: bool,
                       cmake_arguments: list[str]) -> list[str]:
    """The consumer's configure arguments: the prefix, its options, then the parent tree's."""
    arguments: list[str] = [
        "-DCMAKE_PREFIX_PATH=" + str(prefix),
        "-DGUI_FORMS_SDK_CONSUMER_FACE=" + str(face),
        "-DGUI_FORMS_SDK_CONSUMER_TEXT_MASKS=" + ("ON" if text_masks else "OFF"),
        "-DGUI_FORMS_SDK_CONSUMER_C_ABI=" + ("ON" if c_abi else "OFF"),
    ]
    argument: str
    for argument in cmake_arguments:
        # An empty value from the parent (an unset toolchain file, say) is left out.
        if argument.startswith("-D") and argument.endswith("="):
            continue
        arguments.append(argument)
    return arguments


def build_consumer(prefix: Path, scratch: Path, face: Path, text_masks: bool, c_abi: bool,
                   cmake_arguments: list[str]) -> str:
    """Builds tests/sdk_consumer against `prefix` in scratch/ and runs its tests; returns their output."""
    source: Path = scratch / "sdk-consumer"
    build: Path = scratch / "sdk-consumer-build"
    if source.exists():
        shutil.rmtree(source)
    if build.exists():
        shutil.rmtree(build)
    shutil.copytree(CONSUMER, source)
    run(["cmake", "-S", str(source), "-B", str(build)]
        + consumer_arguments(prefix, face, text_masks, c_abi, cmake_arguments))
    run(["cmake", "--build", str(build), "--parallel", "2"])
    output: str = run(["ctest", "--test-dir", str(build), "--output-on-failure"])
    return output


def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__,
                                                              formatter_class=argparse.RawDescriptionHelpFormatter)
    source: argparse._MutuallyExclusiveGroup = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--build-dir", type=Path, help="the build tree to install")
    source.add_argument("--prefix", type=Path,
                        help="an SDK already installed (a test cache's prefix/), used as it is")
    parser.add_argument("--scratch", type=Path, required=True, help="a directory this run may empty and fill")
    parser.add_argument("--text-masks", default="ON", help="require TextMasks (the tree's GUI_FORMS_BUILD_TEXT_MASKS)")
    parser.add_argument("--c-abi", default="ON", help="require CAbi (the tree builds gui_forms_abi)")
    parser.add_argument("--face", type=Path, default=DEFAULT_FACE)
    parser.add_argument("--config", default="", help="the configuration to install (multi-config generators)")
    parser.add_argument("cmake_arguments", nargs=argparse.REMAINDER)
    args: argparse.Namespace = parser.parse_args()
    cmake_arguments: list[str] = list(args.cmake_arguments)
    if cmake_arguments and cmake_arguments[0] == "--":
        cmake_arguments = cmake_arguments[1:]
    scratch: Path = args.scratch.resolve()
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    prefix: Path = scratch / "prefix"
    if args.prefix is not None:
        prefix = args.prefix.resolve()
        print("sdk_consumer_link: the SDK installed at " + str(prefix), flush=True)
    else:
        install: list[str] = ["cmake", "--install", str(args.build_dir.resolve()), "--prefix", str(prefix)]
        if args.config:
            install += ["--config", args.config]
        run(install)
        print("sdk_consumer_link: installed into " + str(prefix), flush=True)
    output: str = build_consumer(prefix, scratch, args.face.resolve(strict=True), switch(args.text_masks),
                                 switch(args.c_abi), cmake_arguments)
    print(output, flush=True)
    # The prefix and the consumer's tree are large; a passing run leaves nothing.
    shutil.rmtree(scratch)
    missing: list[str] = unchecked_features(switch(args.text_masks), switch(args.c_abi))
    if missing:
        print("sdk_consumer_link: NOT PROVEN: this tree builds no " + " and no ".join(missing)
              + ", so the consumer could not check them; configure with GUI_FORMS_BUILD_TEXT_MASKS=ON "
              "(build_sdk.py --features all, tools/ci_build.py) for the whole test", file=sys.stderr, flush=True)
        return SKIPPED_FEATURES
    print("sdk_consumer_link: the installed SDK links and runs every component", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as failure:
        print(str(failure), file=sys.stderr, flush=True)
        sys.exit(1)
