#!/usr/bin/env python3
"""Build an independent consumer and check installed CMake component failures."""
from __future__ import annotations
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

import sdk_consumer_link

# The components tools/build_sdk.py --features all adds to the default SDK.
FEATURE_COMPONENTS: tuple[str, ...] = ("TextMasks", "AudioLoopTransport")


def run(command: list[str], expected_success: bool = True) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(command, text=True, encoding='utf-8', stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if (result.returncode == 0) != expected_success:
        raise RuntimeError("Unexpected command result: " + " ".join(command) + "\n" + result.stdout)
    return result.stdout


# The shared libraries an SDK may hold (one module, owner decision 2026-11-15):
# the C ABI library, which holds its own engine for C and C# programs, and the
# localizable-literal compiler plugin, which Clang loads and no program ships.
# Every other GUI.Forms or STXlib library is a static archive.
ALLOWED_SHARED_STEMS: tuple[str, ...] = ("gui_forms_abi", "libgui_forms_abi", "gui_forms_localizable")
FIRST_PARTY_STEMS: tuple[str, ...] = ("gui_forms_", "libgui_forms_", "gui_drawing_", "libgui_drawing_",
                                      "stxlib_", "libstxlib_")


def shared_library_name(name: str) -> bool:
    """Whether a file name is a shared library's: .dll, .dylib, .so or .so.N."""
    lowered: str = name.lower()
    return lowered.endswith((".dll", ".dylib", ".so")) or ".so." in lowered


def check_one_module_sdk(prefix: Path) -> None:
    """The SDK holds no first-party shared library but the C ABI library.

    A C++ program links every GUI.Forms library and STXlib's state statically
    into its one module, so a shared Threading, Application, drawing or
    STXlib state library in bin/ or lib*/ fails here.
    """
    found: list[Path] = []
    directory: Path
    for directory in [prefix / "bin"] + sorted(prefix.glob("lib*")):
        if not directory.is_dir():
            continue
        path: Path
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or not shared_library_name(path.name):
                continue
            if not path.name.lower().startswith(FIRST_PARTY_STEMS):
                continue
            if path.name.lower().startswith(ALLOWED_SHARED_STEMS):
                continue
            found.append(path)
    if found:
        raise RuntimeError("the SDK has first-party shared libraries; a program is one module: "
                           + ", ".join(str(path.relative_to(prefix)) for path in found))
    print("One module: no first-party shared library besides the C ABI library")


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--native", action="store_true", help="Exercise the selected native Application component")
    parser.add_argument("--toolchain", type=Path, help="Target CMake toolchain for cross-build verification")
    parser.add_argument("--runner", help="CTest cross-compiling emulator command, such as wine")
    parser.add_argument("--relocate", action="store_true", help="Copy the installed SDK to a new prefix before verification")
    parser.add_argument("--feature-components", action="store_true",
                        help="The prefix holds build_sdk.py --features all's components: build and run a consumer "
                             "of Application, TextMasks, Audio and AudioLoopTransport (requires --native)")
    parser.add_argument("--hosted-client", action="store_true",
                        help="Build and run tests/hosted_client_consumer: a parent starting a hosted program "
                             "through Controls, and the program linking HostedClient (macOS and Linux)")
    parser.add_argument("--sdk-consumer", action="store_true",
                        help="Build and run tests/sdk_consumer, which links every public component (the controls, "
                             "a composite, an application font family, the drawing raster seam, TextMasks and the "
                             "C ABI) from the prefix's archives (requires --native)")
    parser.add_argument("--sdk-consumer-text-masks", default="ON",
                        help="Whether the prefix holds TextMasks for --sdk-consumer (ON or OFF)")
    parser.add_argument("--sdk-consumer-c-abi", default="ON",
                        help="Whether the prefix holds the C ABI library for --sdk-consumer (ON or OFF)")
    args: argparse.Namespace = parser.parse_args()
    if args.sdk_consumer and not args.native:
        parser.error("--sdk-consumer requires --native: the consumer links Application")
    if args.feature_components and not args.native:
        parser.error("--feature-components requires --native: TextMasks lives in Application")
    prefix: Path = args.prefix.resolve(strict=True)
    project: Path = Path(__file__).resolve().parent.parent
    scratch: str
    with tempfile.TemporaryDirectory(prefix="gui-forms-installed-consumer-") as scratch:
        scratch_path: Path = Path(scratch)
        if args.relocate:
            relocated: Path = scratch_path / "relocated-sdk"
            shutil.copytree(prefix, relocated, symlinks=True)
            prefix = relocated
        package_arguments: list[str] = ["-DCMAKE_PREFIX_PATH=" + str(prefix)]
        if args.toolchain:
            toolchain_path: Path = args.toolchain.resolve(strict=True)
            package_arguments += ["-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain_path),
                                  "-DCMAKE_FIND_ROOT_PATH=" + str(prefix)]
        if args.runner:
            emulator_arguments: list[str] = shlex.split(args.runner)
            emulator_value: str = ";".join(emulator_arguments)
            package_arguments.append("-DCMAKE_CROSSCOMPILING_EMULATOR=" + emulator_value)
        check_one_module_sdk(prefix)
        consumer: Path = scratch_path / "consumer"
        shutil.copytree(project / "examples" / "paint_contract", consumer)
        build: Path = scratch_path / "build"
        configure: list[str] = ["cmake", "-S", str(consumer), "-B", str(build)] + package_arguments
        if args.native:
            configure.append("-DGUI_FORMS_EXAMPLE_NATIVE=ON")
        run(configure)
        run(["cmake", "--build", str(build), "--parallel", "2"])
        test_output: str = run(["ctest", "--test-dir", str(build), "--output-on-failure"])
        print(test_output)
        if args.feature_components:
            features: Path = scratch_path / "feature-consumer"
            shutil.copytree(project / "tests" / "installed_feature_consumer", features)
            main_source: Path = project / "tests" / "application_text_consumer" / "main.cpp"
            feature_build: Path = scratch_path / "feature-build"
            run(["cmake", "-S", str(features), "-B", str(feature_build),
                 "-DGUI_FORMS_CONSUMER_MAIN=" + str(main_source)]
                + package_arguments)
            run(["cmake", "--build", str(feature_build), "--parallel", "2"])
            print(run(["ctest", "--test-dir", str(feature_build), "--output-on-failure"]))
        if args.hosted_client:
            hosted: Path = scratch_path / "hosted-client-consumer"
            shutil.copytree(project / "tests" / "hosted_client_consumer", hosted)
            hosted_build: Path = scratch_path / "hosted-client-build"
            run(["cmake", "-S", str(hosted), "-B", str(hosted_build)] + package_arguments)
            run(["cmake", "--build", str(hosted_build), "--parallel", "2"])
            print(run(["ctest", "--test-dir", str(hosted_build), "--output-on-failure"]))
        if args.sdk_consumer:
            # The SDK-consumer link test against this prefix (tools/sdk_consumer_link.py).
            consumer_arguments: list[str] = list(package_arguments[1:])
            print(sdk_consumer_link.build_consumer(
                prefix, scratch_path, sdk_consumer_link.DEFAULT_FACE,
                sdk_consumer_link.switch(args.sdk_consumer_text_masks),
                sdk_consumer_link.switch(args.sdk_consumer_c_abi), consumer_arguments))
            # A component the prefix lacks fails the consumer's configure; one
            # switched off here was not checked, and that is said, not hidden.
            unchecked: list[str] = sdk_consumer_link.unchecked_features(
                sdk_consumer_link.switch(args.sdk_consumer_text_masks),
                sdk_consumer_link.switch(args.sdk_consumer_c_abi))
            if unchecked:
                print("WARNING: the SDK consumer did not check " + ", ".join(unchecked)
                      + " (switched off); the link test is not proven for them")
        invalid: Path = scratch_path / "invalid"
        invalid.mkdir()
        components: list[str] = ["NotAComponent"]
        if not args.native:
            components.append("Application")
        if not args.feature_components:
            components.extend(FEATURE_COMPONENTS)
        component: str
        for component in components:
            invalid_configuration: Path = invalid / "CMakeLists.txt"
            invalid_configuration.write_text(
                "cmake_minimum_required(VERSION 3.25)\n"
                "project(InvalidGUIFormsConsumer LANGUAGES CXX)\n"
                "find_package(GUIForms CONFIG REQUIRED COMPONENTS " + component + ")\n", encoding="utf-8")
            output: str = run(["cmake", "-S", str(invalid), "-B", str(scratch_path / component)] +
                         package_arguments, expected_success=False)
            if "Requested GUIForms component '" + component + "' is not installed" not in output:
                raise RuntimeError("Failure was not the expected component diagnostic:\n" + output)
            print("Missing component rejected: " + component)
    print("Installed-package verification passed")


if __name__ == "__main__":
    main()
