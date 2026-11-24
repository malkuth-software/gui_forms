#!/usr/bin/env python3
"""Extract (and optionally install) each packaged program, launch it, and verify its close.

Reads packages-<platform>.json written by tools/package_programs.py. For every
packaged program the archive is extracted to a scratch directory and the
executable is launched with the extra smoke arguments, with GUI_FORMS_FONT_DIR unset so the bundled fonts are the ones
exercised. With --install the installer is also applied on this machine and
the installed copy is launched the same way (macOS `installer -pkg`, Windows
`setup.exe /S` then the quiet uninstaller, Linux `apt-get install ./x.deb`).

Every program is launched with --smoke-test (plus smoke.extra_arguments from
packaging/programs.json), as PlaySuite's smoke launches its games: the program
runs normally, a UI timer inside it requests the ordinary close once a frame
has been presented, the host prints its {"window":...} close receipt and the
program prints "GUI.Forms window smoke passed" before exiting 0. A launch
passes when the exit code is 0, the pass line was printed, the receipt reports
at least one presented frame and the renderer loaded its bundled fonts.

Every extracted program is also launched with --mcp (tools/check_mcp_launch.py):
it must answer initialize and tools/list, and exit once its input closes.

Results are written to smoke-<platform>.json beside the packages receipt and
the exit status is non-zero when any launch fails.
"""
from __future__ import annotations

import argparse
import json
import os
import platform as host_platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

import check_mcp_launch

ROOT: Path = Path(__file__).resolve().parents[1]
PLATFORMS: tuple[str, ...] = ("macos-arm64", "windows-x64", "linux-x64", "linux-arm64")
PASS_LINE: str = "GUI.Forms window smoke passed"


@dataclass
class LaunchResult:
    program: str
    source: str
    executable: str
    arguments: list[str]
    passed: bool
    graceful: bool
    exit_code: int | None
    receipt_found: bool
    renderer_name: str | None
    survived_seconds: float
    detail: str
    stdout_tail: list[str] = field(default_factory=list)
    stderr_tail: list[str] = field(default_factory=list)


def detect_platform() -> str:
    system = host_platform.system()
    arm = host_platform.machine().lower() in ("arm64", "aarch64")
    if system == "Darwin":
        return "macos-arm64"
    if system == "Windows":
        return "windows-x64"
    return "linux-arm64" if arm else "linux-x64"


def clean_environment(platform: str) -> dict[str, str]:
    environment = os.environ.copy()
    for variable in ("GUI_FORMS_FONT_DIR", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH"):
        environment.pop(variable, None)
    if platform.startswith("windows"):
        environment["PATH"] = environment.get("SystemRoot", "C:\\Windows") + "\\System32"
    return environment


def extract_archive(archive: Path, destination: Path, platform: str) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as tar:
            tar.extractall(destination, filter="data")
        return
    if platform.startswith("macos") and shutil.which("ditto"):
        # ditto preserves the bundle's extended attributes and code signature.
        subprocess.run(["ditto", "-x", "-k", str(archive), str(destination)], check=True)
        return
    with zipfile.ZipFile(archive) as compressed:
        compressed.extractall(destination)
        if not platform.startswith("windows"):
            for member in compressed.infolist():
                mode = (member.external_attr >> 16) & 0o777
                if mode:
                    (destination / member.filename).chmod(mode)


def receipt_from_output(lines: list[str]) -> dict[str, Any] | None:
    for line in reversed(lines):
        line = line.strip()
        if line.startswith("{") and '"window"' in line:
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return None


class Smoke:
    def __init__(self, platform: str, build_dir: Path | None, timeout: float, dry_run: bool, log=print) -> None:
        self.platform = platform
        self.build_dir = build_dir
        self.timeout = timeout
        self.dry_run = dry_run
        self.log = log

    def launch(self, record: dict[str, Any], executable: Path, source: str) -> LaunchResult:
        """Run the program with --smoke-test the way PlaySuite's smoke does.

        The program closes itself from a UI timer once it has presented a frame,
        prints the close receipt and the pass line, and exits 0. Nothing here
        asks the window system to quit it.
        """
        smoke = record["smoke"]
        arguments = ["--smoke-test", *smoke.get("extra_arguments", [])]
        command = [str(executable), *arguments]
        if self.dry_run:
            self.log("    would launch: " + " ".join(command))
            return LaunchResult(record["id"], source, str(executable), arguments, True, True, 0, False, None, 0.0, "dry run")
        if not executable.is_file():
            return LaunchResult(record["id"], source, str(executable), arguments, False, False, None, False, None, 0.0,
                                "executable missing after extraction/installation")
        environment = clean_environment(self.platform)
        started = time.monotonic()
        try:
            completed = subprocess.run(command, capture_output=True, text=True, env=environment,
                                       cwd=str(executable.parent), timeout=self.timeout)
            exit_code: int | None = completed.returncode
            stdout_lines = completed.stdout.splitlines()
            stderr_lines = completed.stderr.splitlines()
            detail = f"exited with code {exit_code} after {time.monotonic() - started:.1f}s"
        except subprocess.TimeoutExpired as expired:
            exit_code = None
            stdout_lines = (expired.stdout or "").splitlines() if isinstance(expired.stdout, str) else []
            stderr_lines = (expired.stderr or "").splitlines() if isinstance(expired.stderr, str) else []
            detail = f"did not exit within {self.timeout:.0f}s"
        pass_line = any(line.strip() == PASS_LINE for line in stdout_lines)
        receipt = receipt_from_output(stdout_lines)
        renderer = None
        presented: int | None = None
        if receipt and isinstance(receipt.get("window"), dict):
            renderer = receipt["window"].get("renderer_name")
            frames = receipt["window"].get("frames_presented")
            presented = frames if isinstance(frames, int) else None
        passed = exit_code == 0 and pass_line and receipt is not None
        if passed and (presented is None or presented < 1):
            passed = False
            detail += "; the close receipt reports no presented frame"
        elif passed and renderer and ("incomplete" in renderer or renderer == "fallback"):
            passed = False
            detail += f"; renderer {renderer!r} did not load its bundled fonts"
        elif not passed:
            if not pass_line:
                detail += "; no pass line"
            if receipt is None:
                detail += "; no close receipt"
        return LaunchResult(record["id"], source, str(executable), arguments, passed, passed, exit_code,
                            receipt is not None, renderer, round(time.monotonic() - started, 2), detail,
                            stdout_lines[-20:], stderr_lines[-20:])

    def serve_mcp(self, record: dict[str, Any], executable: Path, source: str) -> LaunchResult:
        """Launch the program with --mcp and check that it serves MCP (tools/check_mcp_launch.py)."""
        arguments = ["--mcp"]
        if self.dry_run:
            self.log(f"    would check: {executable} --mcp")
            return LaunchResult(record["id"], source, str(executable), arguments, True, True, 0, False, None, 0.0, "dry run")
        started = time.monotonic()
        # Every packaged program opts into the library allocator
        # (CMakeLists.txt); the check fails one whose opt-in is not live.
        passed, detail = check_mcp_launch.check(executable, self.timeout, clean_environment(self.platform),
                                                require_library_allocator=True)
        return LaunchResult(record["id"], source, str(executable), arguments, passed, passed, None, False, None,
                            round(time.monotonic() - started, 2), detail)

    # -- install paths ------------------------------------------------------------

    def install_and_locate(self, record: dict[str, Any], installer: Path) -> Path:
        install = record["install"]
        if self.platform.startswith("macos"):
            self.log(f"    sudo installer -pkg {installer.name} -target /")
            if not self.dry_run:
                subprocess.run(["sudo", "/usr/sbin/installer", "-pkg", str(installer), "-target", "/"], check=True)
            return Path("/Applications") / install["macos_app"] / record["executable"]
        if self.platform.startswith("windows"):
            self.log(f"    {installer.name} /S")
            if not self.dry_run:
                subprocess.run([str(installer), "/S"], check=True, timeout=600)
            root = os.path.expandvars(install["windows_dir"].replace("$LOCALAPPDATA", "%LOCALAPPDATA%"))
            return Path(root) / record["executable"]
        self.log(f"    sudo apt-get install -y ./{installer.name}")
        if not self.dry_run:
            # apt treats a bare relative path as a package name; give it an absolute path.
            subprocess.run(["sudo", "apt-get", "install", "-y", str(installer.resolve())], check=True)
        return Path(install["linux_dir"]) / record["executable"]

    def uninstall(self, record: dict[str, Any]) -> None:
        if not self.platform.startswith("windows"):
            return
        root = os.path.expandvars(record["install"]["windows_dir"].replace("$LOCALAPPDATA", "%LOCALAPPDATA%"))
        uninstaller = Path(root) / "Uninstall.exe"
        self.log(f"    {uninstaller} /S")
        if not self.dry_run and uninstaller.is_file():
            subprocess.run([str(uninstaller), "/S", f"_?={root}"], check=False, timeout=600)
            leftovers = [path for path in Path(root).rglob("*") if path.is_file() and path.name != "Uninstall.exe"] if Path(root).exists() else []
            if leftovers:
                raise RuntimeError("uninstaller left files behind: " + ", ".join(str(path) for path in leftovers[:5]))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=PLATFORMS, default=None)
    parser.add_argument("--dist", type=Path, required=True, help="directory holding the artifacts and packages-<platform>.json")
    parser.add_argument("--build-dir", type=Path, default=None, help="the build directory (kept for the command line; unused)")
    parser.add_argument("--install", action="store_true", help="also apply the installer and launch the installed copy")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds a program may take to present, capture and exit")
    parser.add_argument("--receipt", type=Path, default=None, help="default <dist>/smoke-<platform>.json")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    platform = args.platform or detect_platform()
    packages = json.loads((args.dist / f"packages-{platform}.json").read_text(encoding="utf-8"))
    if packages.get("platform") != platform:
        raise SystemExit(f"packages receipt is for {packages.get('platform')}, not {platform}")
    smoke = Smoke(platform, args.build_dir, args.timeout, args.dry_run)
    results: list[LaunchResult] = []
    print(f"GUI.Forms package smoke: platform {platform}, version {packages['version']}" + (" [dry run]" if args.dry_run else ""))
    with tempfile.TemporaryDirectory(prefix="gui-forms-packages-") as scratch:
        for record in packages["programs"]:
            if record["status"] != "packaged":
                print(f"- {record['display_name']}: skipped ({record['reason']})")
                continue
            print(f"- {record['display_name']}")
            archive = args.dist / record["artifacts"]["archive"]
            extracted = Path(scratch) / record["id"]
            print(f"    extract {archive.name} -> {extracted}")
            if not args.dry_run:
                extract_archive(archive, extracted, platform)
            folder = record["install"]["macos_app"] if platform.startswith("macos") else record["install"]["folder"]
            results.append(smoke.launch(record, extracted / folder / record["executable"], "archive"))
            print(f"    {'PASS' if results[-1].passed else 'FAIL'}: {results[-1].detail}")
            results.append(smoke.serve_mcp(record, extracted / folder / record["executable"], "archive --mcp"))
            print(f"    {'PASS' if results[-1].passed else 'FAIL'}: {results[-1].detail}")
            installer_name = record["artifacts"].get("installer")
            if args.install and installer_name:
                installed = smoke.install_and_locate(record, args.dist / installer_name)
                results.append(smoke.launch(record, installed, "installer"))
                print(f"    {'PASS' if results[-1].passed else 'FAIL'}: {results[-1].detail}")
                smoke.uninstall(record)
    receipt = {"schema": 1, "platform": platform, "version": packages["version"], "revision": packages.get("revision"),
               "passed": all(result.passed for result in results), "launches": [asdict(result) for result in results]}
    if not args.dry_run:
        receipt_path = args.receipt or (args.dist / f"smoke-{platform}.json")
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {receipt_path}")
    failures = [result for result in results if not result.passed]
    for failure in failures:
        print(f"FAILED {failure.program} ({failure.source}): {failure.detail}")
        for line in failure.stderr_tail:
            print("    stderr: " + line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
