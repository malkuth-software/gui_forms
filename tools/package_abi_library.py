#!/usr/bin/env python3
"""Archive the C ABI library a deployment build made, for bindings that ship it (pyguiforms).

One archive per platform, gui-forms-abi-<version>-<platform>.zip:

  <library>                 the library under the name a loader asks for:
                            libgui_forms_abi.<major>.dylib (macOS),
                            libgui_forms_abi.so.<major> (Linux),
                            gui_forms_abi<major>.dll (Windows); the file
                            itself, never a symbolic link
  gf_abi.json               the audited ABI manifest it was built from
                            (manifests/abi/gf_abi.json)
  ABI_LIBRARY.json          schema, version, revision, platform, ABI major
                            and minor, the library's name and SHA-256
  licenses/                 the notices the program packages carry for this
                            platform (tools/package_notices.py copy_notices,
                            packaging/licenses.json: GUI.Forms' own licence,
                            THIRD_PARTY_NOTICES.md and each component's),
                            which redistributing the library needs

The library comes from the build directory of the release workflow's
programs build (cmake/CAbi.cmake writes it at the top of the build tree).
The archive is reproducible: entries in name order, one fixed time. --check
reads an existing archive and checks its shape and the library's digest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

ROOT: Path = Path(__file__).resolve().parents[1]
MANIFEST: Path = ROOT / "manifests" / "abi" / "gf_abi.json"
DESCRIPTION_NAME: str = "ABI_LIBRARY.json"
MANIFEST_NAME: str = "gf_abi.json"
LICENSES_DIRECTORY: str = "licenses/"
NOTICES_NAME: str = "THIRD_PARTY_NOTICES.md"
DESCRIPTION_SCHEMA: int = 1
PLATFORMS: tuple[str, ...] = ("macos-arm64", "windows-x64", "linux-x64", "linux-arm64")
# The time every entry carries, so two packagings of one build are equal.
FIXED_TIME: tuple[int, int, int, int, int, int] = (2026, 1, 1, 0, 0, 0)


class PackagingError(Exception):
    """A build directory, manifest or archive that cannot be packaged or does not check."""


def library_name(platform: str, major: int) -> str:
    """The file name a loader asks for on platform (cmake/CAbi.cmake's output name and SOVERSION)."""
    if platform.startswith("windows-"):
        return "gui_forms_abi" + str(major) + ".dll"
    if platform.startswith("macos-"):
        return "libgui_forms_abi." + str(major) + ".dylib"
    if platform.startswith("linux-"):
        return "libgui_forms_abi.so." + str(major)
    raise PackagingError("no C ABI library name for platform " + platform)


def archive_name(version: str, platform: str) -> str:
    return "gui-forms-abi-" + version + "-" + platform + ".zip"


def read_manifest(manifest: Path) -> dict[str, Any]:
    document: dict[str, Any] = json.loads(manifest.read_text(encoding="utf-8"))
    if document.get("format") != 1 or not isinstance(document.get("abi"), dict):
        raise PackagingError(str(manifest) + " is not a format 1 ABI manifest")
    return document


def find_library(build_dir: Path, platform: str, major: int) -> Path:
    """The library file in build_dir, its symbolic links followed to the file."""
    candidate: Path = build_dir / library_name(platform, major)
    if not candidate.exists():
        raise PackagingError("no " + candidate.name + " in " + str(build_dir) + " (was gui_forms_abi built?)")
    resolved: Path = candidate.resolve()
    if not resolved.is_file():
        raise PackagingError(str(candidate) + " is not a file")
    return resolved


def description(version: str, revision: str, platform: str, manifest: dict[str, Any], name: str,
                library: bytes) -> dict[str, Any]:
    described: dict[str, Any] = {
        "schema": DESCRIPTION_SCHEMA,
        "version": version,
        "revision": revision,
        "platform": platform,
        "abi_major": manifest["abi"]["major"],
        "abi_minor": manifest["abi"]["minor"],
        "library": name,
        "sha256": hashlib.sha256(library).hexdigest(),
    }
    return described


def write_entry(archive: zipfile.ZipFile, name: str, data: bytes, executable: bool) -> None:
    entry: zipfile.ZipInfo = zipfile.ZipInfo(name, FIXED_TIME)
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = (0o755 if executable else 0o644) << 16
    archive.writestr(entry, data)


def license_entries(licenses: Path) -> list[tuple[str, bytes, bool]]:
    """Every file under licenses (a notices directory), as archive entries."""
    if not (licenses / NOTICES_NAME).is_file():
        raise PackagingError(str(licenses) + " lacks " + NOTICES_NAME)
    entries: list[tuple[str, bytes, bool]] = []
    for path in sorted(licenses.rglob("*")):
        if path.is_file():
            entries.append((LICENSES_DIRECTORY + path.relative_to(licenses).as_posix(), path.read_bytes(), False))
    return entries


def copy_program_notices(platform: str, build_dir: Path, version: str, revision: str, runtime: Path | None,
                         runtime_dir: Path | None, destination: Path) -> None:
    """The third-party notices a program package carries for platform (tools/package_notices.py)."""
    import package_notices
    package_notices.copy_notices(platform, build_dir, destination, runtime, runtime_dir, log=print)


def package(build_dir: Path, platform: str, version: str, revision: str, out: Path, manifest_path: Path,
            licenses: Path) -> Path:
    """Writes the platform's archive into out and returns its path; licenses is a notices directory."""
    if platform not in PLATFORMS:
        raise PackagingError("unknown platform " + platform + "; one of " + ", ".join(PLATFORMS))
    manifest_bytes: bytes = manifest_path.read_bytes()
    manifest: dict[str, Any] = read_manifest(manifest_path)
    major: int = manifest["abi"]["major"]
    name: str = library_name(platform, major)
    library: bytes = find_library(build_dir, platform, major).read_bytes()
    described: dict[str, Any] = description(version, revision, platform, manifest, name, library)
    described_bytes: bytes = (json.dumps(described, indent=2, sort_keys=True) + "\n").encode("utf-8")
    out.mkdir(parents=True, exist_ok=True)
    target: Path = out / archive_name(version, platform)
    entries: list[tuple[str, bytes, bool]] = [(DESCRIPTION_NAME, described_bytes, False),
                                              (MANIFEST_NAME, manifest_bytes, False),
                                              (name, library, True)]
    entries.extend(license_entries(licenses))
    entries.sort()
    with zipfile.ZipFile(target, "w") as archive:
        for entry_name, data, executable in entries:
            write_entry(archive, entry_name, data, executable)
    return target


def check_archive(archive_path: Path) -> dict[str, Any]:
    """The archive's description, after checking its entries and the library's digest."""
    with zipfile.ZipFile(archive_path) as archive:
        names: list[str] = sorted(archive.namelist())
        if DESCRIPTION_NAME not in names or MANIFEST_NAME not in names:
            raise PackagingError(archive_path.name + " lacks " + DESCRIPTION_NAME + " or " + MANIFEST_NAME)
        described: dict[str, Any] = json.loads(archive.read(DESCRIPTION_NAME).decode("utf-8"))
        if described.get("schema") != DESCRIPTION_SCHEMA:
            raise PackagingError(archive_path.name + ": unknown description schema")
        expected: list[str] = [DESCRIPTION_NAME, MANIFEST_NAME, described["library"],
                               LICENSES_DIRECTORY + NOTICES_NAME]
        for expected_name in expected:
            if expected_name not in names:
                raise PackagingError(archive_path.name + " lacks " + expected_name)
        for name in names:
            if name not in expected and not name.startswith(LICENSES_DIRECTORY):
                raise PackagingError(archive_path.name + " holds an unexpected " + name)
        if described["library"] != library_name(described["platform"], described["abi_major"]):
            raise PackagingError(archive_path.name + ": the library is not named as a loader asks for it")
        digest: str = hashlib.sha256(archive.read(described["library"])).hexdigest()
        if digest != described["sha256"]:
            raise PackagingError(archive_path.name + ": the library's SHA-256 is not the recorded one")
        manifest: dict[str, Any] = json.loads(archive.read(MANIFEST_NAME).decode("utf-8"))
        if manifest.get("abi") != {"major": described["abi_major"], "minor": described["abi_minor"]}:
            raise PackagingError(archive_path.name + ": the manifest's ABI version is not the recorded one")
    if archive_path.name != archive_name(described["version"], described["platform"]):
        raise PackagingError(archive_path.name + " is not named for its version and platform")
    return described


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=PLATFORMS)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--revision", default="")
    parser.add_argument("--out", type=Path, default=Path("dist-abi"))
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--runtime", type=Path, default=None,
                        help="the macOS LLVM runtime prefix (its notices; tools/package_notices.py)")
    parser.add_argument("--runtime-dir", type=Path, default=None,
                        help="the Windows runtime directory (its notices; tools/package_notices.py)")
    parser.add_argument("--check", type=Path, default=None, help="check this existing archive; write nothing")
    args: argparse.Namespace = parser.parse_args(argv)
    try:
        if args.check is not None:
            described: dict[str, Any] = check_archive(args.check)
            print(args.check.name + ": " + described["library"] + " " + described["sha256"], flush=True)
            return 0
        if args.platform is None or args.build_dir is None or args.version is None:
            parser.error("--platform, --build-dir and --version are required to package")
        with tempfile.TemporaryDirectory() as work:
            licenses: Path = Path(work) / "licenses"
            copy_program_notices(args.platform, args.build_dir, args.version, args.revision, args.runtime,
                                 args.runtime_dir, licenses)
            target: Path = package(args.build_dir, args.platform, args.version, args.revision, args.out,
                                   args.manifest, licenses)
        checked: dict[str, Any] = check_archive(target)
        print(target.name + ": " + checked["library"] + " " + checked["sha256"], flush=True)
    except PackagingError as refused:
        print("package_abi_library: " + str(refused), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
