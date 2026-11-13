#!/usr/bin/env python3
"""Build an independent consumer and check installed CMake component failures."""
from __future__ import annotations
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

# The components tools/build_sdk.py --features all adds to the default SDK.
FEATURE_COMPONENTS: tuple[str, ...] = ("TextMasks", "AudioLoopTransport", "FreeType", "HarfBuzz")


def run(command: list[str], expected_success: bool = True) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(command, text=True, encoding='utf-8', stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if (result.returncode == 0) != expected_success:
        raise RuntimeError("Unexpected command result: " + " ".join(command) + "\n" + result.stdout)
    return result.stdout


def check_shared_stxlib_state(prefix: Path) -> None:
    """The SDK ships STXlib's state as one shared library (stx's cmake/Stxlib.cmake).

    lib/libstxlib_state.dylib or lib/libstxlib_state.so (lib64 where the
    platform installs there) on macOS and Linux,
    bin/stxlib_state.dll (with lib/libstxlib_state.dll.a) on Windows. A static
    lib/libstxlib_state.a would give the Application library and each
    consumer their own allocator and SIMD selection, so it fails here.
    """
    shared: list[Path] = sorted((prefix / "bin").glob("stxlib_state.dll"))
    library: Path
    for library in sorted(prefix.glob("lib*")):
        if not library.is_dir():
            continue
        shared += sorted(library.glob("libstxlib_state.dylib")) + sorted(library.glob("libstxlib_state.so*"))
        if (library / "libstxlib_state.a").exists():
            raise RuntimeError(f"the SDK has a static {library.name}/libstxlib_state.a; STXlib's state must be one "
                               "shared library")
    if not shared:
        raise RuntimeError("the SDK lacks STXlib's shared state (lib/libstxlib_state.dylib or .so, or bin/stxlib_state.dll)")
    print("Shared STXlib state: " + ", ".join(str(path.relative_to(prefix)) for path in shared))


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
    parser.add_argument("--upstream-text-stack", action="store_true",
                        help="Build and run tests/upstream_text_stack_consumer, which links the system's own HarfBuzz "
                             "and FreeType (pkg-config harfbuzz freetype2) beside Application (requires --native)")
    parser.add_argument("--hosted-client", action="store_true",
                        help="Build and run tests/hosted_client_consumer: a parent starting a hosted program "
                             "through Controls, and the program linking HostedClient (macOS and Linux)")
    args: argparse.Namespace = parser.parse_args()
    if args.feature_components and not args.native:
        parser.error("--feature-components requires --native: TextMasks lives in Application")
    if args.upstream_text_stack and not args.native:
        parser.error("--upstream-text-stack requires --native: the text engine lives in Application")
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
        check_shared_stxlib_state(prefix)
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
                 "-DGUI_FORMS_CONSUMER_MAIN=" + str(main_source),
                 "-DGUI_FORMS_CONSUMER_FACE=" + str(project / "assets" / "fonts" / "Arimo-Regular.ttf")]
                + package_arguments)
            run(["cmake", "--build", str(feature_build), "--parallel", "2"])
            print(run(["ctest", "--test-dir", str(feature_build), "--output-on-failure"]))
        if args.upstream_text_stack:
            upstream: Path = scratch_path / "upstream-text-consumer"
            shutil.copytree(project / "tests" / "upstream_text_stack_consumer", upstream)
            upstream_build: Path = scratch_path / "upstream-text-build"
            run(["cmake", "-S", str(upstream), "-B", str(upstream_build)] + package_arguments)
            run(["cmake", "--build", str(upstream_build), "--parallel", "2"])
            print(run(["ctest", "--test-dir", str(upstream_build), "--output-on-failure"]))
        if args.hosted_client:
            hosted: Path = scratch_path / "hosted-client-consumer"
            shutil.copytree(project / "tests" / "hosted_client_consumer", hosted)
            hosted_build: Path = scratch_path / "hosted-client-build"
            run(["cmake", "-S", str(hosted), "-B", str(hosted_build)] + package_arguments)
            run(["cmake", "--build", str(hosted_build), "--parallel", "2"])
            print(run(["ctest", "--test-dir", str(hosted_build), "--output-on-failure"]))
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
